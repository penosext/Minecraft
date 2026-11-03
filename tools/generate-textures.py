#!/usr/bin/env python3
import argparse
import json
from pathlib import Path

from PIL import Image


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('manifest', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text())
    root = args.manifest.parent
    names = ['dirt', 'grass_top', 'grass_side', 'grass_side_overlay', 'snow_side']
    atlas = Image.new('RGBA', (16 * len(names), 16))
    for index, name in enumerate(names):
        entry = manifest['textures'][name]
        source = Image.open(root / entry['file']).convert('RGBA')
        x, y, width, height = entry['rect']
        tile = source.crop((x, y, x + width, y + height))
        if tile.size != (16, 16):
            raise ValueError(f'{name}: expected original 16x16 tile, got {tile.size}')
        atlas.paste(tile, (index * 16, 0))
    pixels = [((a << 24) | (r << 16) | (g << 8) | b) for r, g, b, a in atlas.getdata()]
    lines = ['#pragma once', '#include <cstdint>', 'namespace legacy {',
             f'constexpr int kAtlasWidth = {atlas.width};',
             f'constexpr int kAtlasHeight = {atlas.height};',
             'constexpr uint32_t kAtlasPixels[] = {']
    for i in range(0, len(pixels), 8):
        lines.append('    ' + ', '.join(f'0x{v:08x}u' for v in pixels[i:i+8]) + ',')
    lines.extend(['};', '}'])
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text('\n'.join(lines) + '\n')
    print(f'Embedded original terrain pixels: {atlas.width}x{atlas.height}')


if __name__ == '__main__':
    main()
