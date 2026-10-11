#!/usr/bin/env python3
"""Capture actual SDL rendering; validate dimensions, palette and nonblank output.
Standard library only. Default X11 needs DISPLAY; --driver dummy is headless.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[2]

def decode_bmp(path):
    data = path.read_bytes()
    if data[:2] != b'BM':
        raise ValueError('Not a BMP')
    offset = struct.unpack_from('<I', data, 10)[0]
    width, height, planes, bpp = struct.unpack_from('<iiHH', data, 18)
    if planes != 1 or bpp != 32 or width <= 0:
        raise ValueError('Expected SDL 32-bit BMP')
    rows = [data[offset + i * width * 4:offset + (i + 1) * width * 4] for i in range(abs(height))]
    if height > 0:
        rows.reverse()
    # SDL_SaveBMP's 32-bit ARGB surface writes BGRA little-endian pixels.
    rgba = b''.join(bytes((row[i + 2], row[i + 1], row[i], 255))
                    for row in rows for i in range(0, len(row), 4))
    if len(rgba) != width * abs(height) * 4:
        raise ValueError('Truncated BMP')
    return width, abs(height), rgba

def write_png(path, width, height, rgba):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    scanlines = b''.join(b'\0' + rgba[y * width * 4:(y + 1) * width * 4] for y in range(height))
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0))
                     + chunk(b'IDAT', zlib.compress(scanlines, 9)) + chunk(b'IEND', b''))

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--driver', choices=['x11', 'dummy', 'windows'], default='x11')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, SDL_VIDEODRIVER=args.driver, SDL_AUDIODRIVER='dummy')
    tokens = json.loads((ROOT / 'design/tokens.json').read_text())['themes']
    cases = [(device, theme, 'game', 0) for device in ['atlas', 'sigil'] for theme in tokens]
    cases += [(device, 'graphite', screen, scenario) for device in ['atlas', 'sigil']
              for screen, scenario in [('pairing', 0), ('game', 2), ('game', 3)]]
    records = []
    hashes = set()
    with tempfile.TemporaryDirectory(prefix='turnhub-captures-') as scratch:
        for device, theme, screen, scenario in cases:
            name = f'{device}-{theme}-{screen}-{scenario}'
            bmp = Path(scratch) / (name + '.bmp')
            result = subprocess.run([str(args.executable.resolve()), '--device', device, '--theme', theme,
                                     '--screen', screen, '--scenario', str(scenario), '--capture', str(bmp)],
                                    env=env, text=True, capture_output=True, timeout=20)
            if result.returncode:
                raise RuntimeError(result.stdout + result.stderr)
            width, height, rgba = decode_bmp(bmp)
            expected = (480, 320) if device == 'atlas' else (240, 320)
            if (width, height) != expected:
                raise ValueError(f'{name}: wrong dimensions')
            background = bytes.fromhex(tokens[theme]['color']['bg'][1:])
            if rgba[:3] != background:
                raise ValueError(f'{name}: palette background mismatch')
            colors = {rgba[i:i+3] for i in range(0, len(rgba), 4)}
            if len(colors) < 32:
                raise ValueError(f'{name}: blank or unexpectedly flat rendering')
            digest = hashlib.sha256(rgba).hexdigest()
            if digest in hashes:
                raise ValueError(f'{name}: duplicate scene/theme rendering')
            hashes.add(digest)
            png = args.output / (name + '.png')
            write_png(png, width, height, rgba)
            records.append({'file': png.name, 'width': width, 'height': height, 'rgba_sha256': digest})
    provenance = {'lvgl': '9.3.0', 'sdl': '2.32.4', 'driver': args.driver, 'captures': records}
    (args.output / 'captures.json').write_text(json.dumps(provenance, indent=2) + '\n')
    print(f'PASS: {len(records)} SDL captures: exact dimensions, theme backgrounds, nonblank and distinct scenes')

if __name__ == '__main__':
    main()
