#!/usr/bin/env python3
"""TurnHub signed firmware packages (.thfw).

Layout and rules: Documentation/engineering/SIGIL_OTA.md ("Firmware package")
and shared/include/firmware_package.h, which this tool must match.

  keygen   --out KEY.pem                 new ECDSA P-256 signing key (keep it secret)
  pubkey   --key KEY.pem --out HEADER.h  write shared/include/firmware_signing_key.h
  package  firmware.bin --out X.thfw     sign a build (key: --key or $TURNHUB_FIRMWARE_SIGNING_KEY)
  verify   X.thfw [--pubkey-header H]    check a package against the committed public key
  info     X.thfw                        print the header
  descriptor firmware.bin [--expect-product P]  check a build carries its descriptor
  feed     X.thfw... --release R --out turnhub-firmware.json

Needs the `cryptography` package; falls back to the copy bundled with
PlatformIO's esptool.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import struct
import sys
from pathlib import Path


def _import_crypto():
    try:
        import cryptography  # noqa: F401
    except ImportError:
        bundled = Path.home() / ".platformio/packages/tool-esptoolpy/_contrib"
        if bundled.is_dir():
            sys.path.insert(0, str(bundled))
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import ec, utils
    from cryptography.exceptions import InvalidSignature
    return hashes, serialization, ec, utils, InvalidSignature


PACKAGE_MAGIC = b"THFWPKG1"
DESCRIPTOR_MAGIC = b"THFWDSC1"
HEADER_FORMAT = 1
HEADER_BYTES = 128
SIGNED_BYTES = 64
PRODUCTS = {1: "atlas", 2: "sigil-eink", 3: "sigil-oled"}
PRODUCT_IDS = {name: pid for pid, name in PRODUCTS.items()}
# magic, format, product, major, minor, patch, radio, flags, size, hash, build, keyid
HEADER_STRUCT = struct.Struct("<8sBBBBBBHI32s8s4s")
assert HEADER_STRUCT.size == SIGNED_BYTES
ESP_IMAGE_MAGIC = 0xE9


class PackageError(Exception):
    pass


def find_descriptor(image: bytes) -> dict:
    hits = [m.start() for m in re.finditer(re.escape(DESCRIPTOR_MAGIC), image)]
    if len(hits) != 1:
        raise PackageError(f"expected exactly one firmware descriptor, found {len(hits)}")
    raw = image[hits[0]: hits[0] + 16]
    if len(raw) != 16:
        raise PackageError("firmware descriptor is cut off")
    product, major, minor, patch, radio = raw[8], raw[9], raw[10], raw[11], raw[12]
    if product not in PRODUCTS:
        raise PackageError(f"unknown product {product} in descriptor")
    return {"product": product, "version": (major, minor, patch), "radio": radio}


def load_private_key(path: str | None):
    _, serialization, ec, _, _ = _import_crypto()
    if path:
        pem = Path(path).read_bytes()
    else:
        env = os.environ.get("TURNHUB_FIRMWARE_SIGNING_KEY", "")
        if not env.strip():
            raise PackageError("no signing key: pass --key or set TURNHUB_FIRMWARE_SIGNING_KEY")
        pem = env.encode()
    key = serialization.load_pem_private_key(pem, password=None)
    if not isinstance(key, ec.EllipticCurvePrivateKey) or key.curve.name != "secp256r1":
        raise PackageError("signing key must be ECDSA P-256")
    return key


def public_bytes(public_key) -> bytes:
    _, serialization, _, _, _ = _import_crypto()
    return public_key.public_bytes(
        serialization.Encoding.X962, serialization.PublicFormat.UncompressedPoint)


def key_id(public: bytes) -> bytes:
    return hashlib.sha256(public).digest()[:4]


def build_package(image: bytes, private_key, build_id: str = "") -> bytes:
    hashes, _, ec, utils, _ = _import_crypto()
    if not image or image[0] != ESP_IMAGE_MAGIC:
        raise PackageError("not an ESP32 firmware image (first byte is not 0xE9)")
    desc = find_descriptor(image)
    public = public_bytes(private_key.public_key())
    build = bytes.fromhex(build_id[:16]) if build_id else b""
    build = build.ljust(8, b"\0")[:8]
    signed = HEADER_STRUCT.pack(
        PACKAGE_MAGIC, HEADER_FORMAT, desc["product"], *desc["version"], desc["radio"], 0,
        len(image), hashlib.sha256(image).digest(), build, key_id(public))
    der = private_key.sign(signed, ec.ECDSA(hashes.SHA256()))
    r, s = utils.decode_dss_signature(der)
    return signed + r.to_bytes(32, "big") + s.to_bytes(32, "big") + image


def parse_header(package: bytes) -> dict:
    if len(package) < HEADER_BYTES:
        raise PackageError("shorter than a package header")
    (magic, fmt, product, major, minor, patch, radio, flags, size, digest, build,
     kid) = HEADER_STRUCT.unpack(package[:SIGNED_BYTES])
    if magic != PACKAGE_MAGIC:
        raise PackageError("not a .thfw package")
    if fmt != HEADER_FORMAT or flags != 0:
        raise PackageError(f"unsupported header format {fmt} (flags {flags})")
    return {
        "product": product, "productName": PRODUCTS.get(product, f"unknown-{product}"),
        "version": (major, minor, patch), "radio": radio, "imageSize": size,
        "imageHash": digest, "buildId": build.rstrip(b"\0").hex(), "keyId": kid,
        "signature": package[SIGNED_BYTES:HEADER_BYTES],
    }


def verify_package(package: bytes, public: bytes) -> dict:
    hashes, _, ec, utils, InvalidSignature = _import_crypto()
    header = parse_header(package)
    if header["keyId"] != key_id(public):
        raise PackageError("signed by a different key")
    sig = header["signature"]
    der = utils.encode_dss_signature(int.from_bytes(sig[:32], "big"), int.from_bytes(sig[32:], "big"))
    key = ec.EllipticCurvePublicKey.from_encoded_point(ec.SECP256R1(), public)
    try:
        key.verify(der, package[:SIGNED_BYTES], ec.ECDSA(hashes.SHA256()))
    except InvalidSignature as error:
        raise PackageError("bad signature") from error
    image = package[HEADER_BYTES:]
    if len(image) != header["imageSize"]:
        raise PackageError(f"image is {len(image)} bytes, header says {header['imageSize']}")
    if hashlib.sha256(image).digest() != header["imageHash"]:
        raise PackageError("image hash mismatch")
    return header


def read_pubkey_header(path: Path) -> bytes:
    text = path.read_text()
    block = re.search(r"PUBLIC_KEY\[[^\]]*\]\s*=\s*\{([^}]*)\}", text)
    if not block:
        raise PackageError(f"no PUBLIC_KEY array in {path}")
    return bytes(int(v, 16) for v in re.findall(r"0x([0-9a-fA-F]{2})", block.group(1)))


def c_array(data: bytes, indent: str = "    ") -> str:
    rows = [", ".join(f"0x{b:02x}" for b in data[i:i + 12]) for i in range(0, len(data), 12)]
    return (",\n" + indent).join(rows)


def write_pubkey_header(public: bytes, out: Path) -> None:
    out.write_text(
        "#pragma once\n\n"
        "// Public half of the TurnHub firmware signing key (SIGIL_OTA.md, \"Keys and\n"
        "// signing\"). Generated by tools/firmware/thfw.py pubkey; the private half is\n"
        "// the TURNHUB_FIRMWARE_SIGNING_KEY secret and never enters the repository.\n"
        "// Changing it means a USB flash of every device.\n\n"
        "#include <stdint.h>\n\n"
        "#include \"firmware_package.h\"\n\n"
        "namespace TurnHubFirmwarePackage {\n\n"
        "constexpr uint8_t PUBLIC_KEY[PUBLIC_KEY_BYTES] = {\n"
        f"    {c_array(public)}}};\n\n"
        f"constexpr uint8_t KEY_ID[KEY_ID_BYTES] = {{{c_array(key_id(public), '')}}};\n\n"
        "}  // namespace TurnHubFirmwarePackage\n", newline="\n")


def version_text(v) -> str:
    return ".".join(str(x) for x in v)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("keygen")
    p.add_argument("--out", required=True)
    p = sub.add_parser("pubkey")
    p.add_argument("--key")
    p.add_argument("--out", required=True)
    p = sub.add_parser("package")
    p.add_argument("image")
    p.add_argument("--out", required=True)
    p.add_argument("--key")
    p.add_argument("--build-id", default="")
    p.add_argument("--expect-product", choices=sorted(PRODUCT_IDS))
    p = sub.add_parser("verify")
    p.add_argument("package")
    p.add_argument("--pubkey-header", default=str(Path(__file__).resolve().parents[2] / "shared/include/firmware_signing_key.h"))
    p = sub.add_parser("info")
    p.add_argument("package")
    p = sub.add_parser("descriptor")
    p.add_argument("image")
    p.add_argument("--expect-product", choices=sorted(PRODUCT_IDS))
    p = sub.add_parser("feed")
    p.add_argument("packages", nargs="+")
    p.add_argument("--release", required=True)
    p.add_argument("--out", required=True)

    args = parser.parse_args(argv)
    try:
        if args.command == "keygen":
            _, serialization, ec, _, _ = _import_crypto()
            out = Path(args.out)
            if out.exists():
                raise PackageError(f"{out} exists; refusing to overwrite a signing key")
            key = ec.generate_private_key(ec.SECP256R1())
            pem = key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                                    serialization.NoEncryption())
            out.parent.mkdir(parents=True, exist_ok=True)
            fd = os.open(out, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
            with os.fdopen(fd, "wb") as f:
                f.write(pem)
            print(f"wrote {out} (key ID {key_id(public_bytes(key.public_key())).hex()})")
        elif args.command == "pubkey":
            key = load_private_key(args.key)
            write_pubkey_header(public_bytes(key.public_key()), Path(args.out))
            print(f"wrote {args.out}")
        elif args.command == "package":
            image = Path(args.image).read_bytes()
            if args.expect_product and find_descriptor(image)["product"] != PRODUCT_IDS[args.expect_product]:
                raise PackageError(f"image is not a {args.expect_product} build")
            package = build_package(image, load_private_key(args.key), args.build_id)
            Path(args.out).write_bytes(package)
            h = parse_header(package)
            print(f"wrote {args.out}: {h['productName']} {version_text(h['version'])}, "
                  f"{h['imageSize']} bytes, key {h['keyId'].hex()}")
        elif args.command == "verify":
            h = verify_package(Path(args.package).read_bytes(), read_pubkey_header(Path(args.pubkey_header)))
            print(f"OK {h['productName']} {version_text(h['version'])} build {h['buildId'] or '-'}")
        elif args.command == "info":
            h = parse_header(Path(args.package).read_bytes())
            print(json.dumps({k: (v.hex() if isinstance(v, bytes) else v) for k, v in h.items()}, indent=2))
        elif args.command == "descriptor":
            desc = find_descriptor(Path(args.image).read_bytes())
            name = PRODUCTS[desc["product"]]
            if args.expect_product and name != args.expect_product:
                raise PackageError(f"image is a {name} build, not {args.expect_product}")
            print(f"{name} {version_text(desc['version'])} radio {desc['radio']}")
        elif args.command == "feed":
            packages = []
            for name in args.packages:
                data = Path(name).read_bytes()
                h = parse_header(data)
                packages.append({
                    "product": h["productName"], "version": version_text(h["version"]),
                    "radioProtocol": h["radio"], "file": Path(name).name, "size": len(data),
                    "sha256": hashlib.sha256(data).hexdigest(), "buildId": h["buildId"],
                })
            Path(args.out).write_text(json.dumps(
                {"schema": 1, "release": args.release, "packages": packages}, indent=2) + "\n")
            print(f"wrote {args.out} ({len(packages)} packages)")
    except PackageError as error:
        print(f"thfw: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
