#!/usr/bin/env python3
"""Tests for thfw.py: packaging, verification and every refusal. Run:
python tools/firmware/test_thfw.py
"""

import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import thfw  # noqa: E402

_, _, ec, _, _ = thfw._import_crypto()


def fake_image(product=3, version=(0, 9, 1), radio=2, descriptors=1, size=4000):
    body = bytearray((i * 13 + 5) & 0xFF for i in range(size))
    body[0] = thfw.ESP_IMAGE_MAGIC
    desc = thfw.DESCRIPTOR_MAGIC + bytes([product, *version, radio, 0, 0, 0])
    for n in range(descriptors):
        at = 1000 + n * 500
        body[at:at + 16] = desc
    return bytes(body)


class ThfwTest(unittest.TestCase):
    def setUp(self):
        self.key = ec.generate_private_key(ec.SECP256R1())
        self.public = thfw.public_bytes(self.key.public_key())

    def test_round_trip(self):
        image = fake_image()
        package = thfw.build_package(image, self.key, "1df68ca0deadbeef")
        self.assertEqual(len(package), 128 + len(image))
        header = thfw.verify_package(package, self.public)
        self.assertEqual(header["productName"], "sigil-oled")
        self.assertEqual(header["version"], (0, 9, 1))
        self.assertEqual(header["radio"], 2)
        self.assertEqual(header["buildId"], "1df68ca0deadbeef")
        self.assertEqual(package[128:], image)
        # Offsets shared with firmware_package.h.
        self.assertEqual(package[:8], b"THFWPKG1")
        self.assertEqual(struct.unpack_from("<I", package, 16)[0], len(image))
        self.assertEqual(package[60:64], thfw.key_id(self.public))

    def test_refusals(self):
        image = fake_image()
        package = bytearray(thfw.build_package(image, self.key))
        other = thfw.public_bytes(ec.generate_private_key(ec.SECP256R1()).public_key())
        with self.assertRaisesRegex(thfw.PackageError, "different key"):
            thfw.verify_package(bytes(package), other)
        for offset, reason in ((11, "bad signature"), (100, "bad signature"),
                               (128 + 50, "hash mismatch")):
            bad = bytearray(package)
            bad[offset] ^= 1
            with self.assertRaisesRegex(thfw.PackageError, reason):
                thfw.verify_package(bytes(bad), self.public)
        with self.assertRaisesRegex(thfw.PackageError, "header says"):
            thfw.verify_package(bytes(package[:-1]), self.public)
        with self.assertRaisesRegex(thfw.PackageError, "not a .thfw"):
            thfw.verify_package(image, self.public)

    def test_images_that_cannot_be_packaged(self):
        with self.assertRaisesRegex(thfw.PackageError, "found 0"):
            thfw.build_package(fake_image(descriptors=0), self.key)
        with self.assertRaisesRegex(thfw.PackageError, "found 2"):
            thfw.build_package(fake_image(descriptors=2), self.key)
        with self.assertRaisesRegex(thfw.PackageError, "unknown product"):
            thfw.build_package(fake_image(product=9), self.key)
        with self.assertRaisesRegex(thfw.PackageError, "0xE9"):
            thfw.build_package(b"\0" + fake_image()[1:], self.key)

    def test_cli_and_pubkey_header(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            key = tmp / "key.pem"
            self.assertEqual(thfw.main(["keygen", "--out", str(key)]), 0)
            self.assertEqual(thfw.main(["keygen", "--out", str(key)]), 1)  # No overwrite.
            header = tmp / "firmware_signing_key.h"
            self.assertEqual(thfw.main(["pubkey", "--key", str(key), "--out", str(header)]), 0)
            (tmp / "firmware.bin").write_bytes(fake_image(product=2))
            pkg = tmp / "sigil.thfw"
            self.assertEqual(thfw.main(["package", str(tmp / "firmware.bin"), "--key", str(key),
                                        "--out", str(pkg), "--expect-product", "sigil-eink"]), 0)
            self.assertEqual(thfw.main(["package", str(tmp / "firmware.bin"), "--key", str(key),
                                        "--out", str(pkg), "--expect-product", "atlas"]), 1)
            self.assertEqual(thfw.main(["verify", str(pkg), "--pubkey-header", str(header)]), 0)
            self.assertEqual(len(thfw.read_pubkey_header(header)), 65)
            feed = tmp / "feed.json"
            self.assertEqual(thfw.main(["feed", str(pkg), "--release", "0.9.0", "--out", str(feed)]), 0)
            self.assertIn('"product": "sigil-eink"', feed.read_text())


if __name__ == "__main__":
    unittest.main()
