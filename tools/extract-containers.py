#!/usr/bin/env python3
"""Copy/verify the original container GUI textures and embed software crops.

Offline only. When port/assets/inventory/{crafting,furnace,container}.png are
missing they are copied byte-for-byte from the local upstream checkout
(port/upstream/Minecraft.Client/Common/res/gui, gitignored media); afterwards
the committed copies are verified against the pinned hashes. Writes
jsapi/build/generated/ContainerAssets.hpp and port/assets/inventory/sources.json.
--check never writes.
"""
import argparse
import hashlib
import json
import re
import sys
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
REFERENCES = [ROOT / 'port/upstream', Path('/root/1234/Minecraft/port/upstream')]
GUI = 'Minecraft.Client/Common/res/gui/'
COMMIT = '09df8928eeb15328bca499d225e48ea95af6d119'
# name: (sha256, git blob sha1). Common/res is a subtree in the client tree
# manifest, so PNGs are pinned here by content, as extract-inventory.py does.
ASSETS = {
    'crafting.png': ('d6cf2562367d3bea1bb69410d5bfdc5501ee624ce2a5302718091e503684868f', 'da831189b8d327c7ed29c1a7b3922d435b872c35'),
    'furnace.png': ('0b067a1e068e90c1e6f3805cfcb8f8bf0fb75fd3d16e4c5cb0f1b3dfab0ba4bc', 'a5834e19516d760d1dc1b87db3e8abee756d9884'),
    'container.png': ('8840838eec59d15bf96a7b26f77e07dc865ca4b392a68c391e7c95ed45cb060a', 'bd1d383cef037c2084b1b8b9c41200537eca8a53'),
}
# Layout evidence: exact source expressions, located by line at generation time.
RULES = {
    'Minecraft.Client/CraftingScreen.cpp': [r'this->blit\(xo, yo, 0, 0, imageWidth, imageHeight\);'],
    'Minecraft.Client/FurnaceScreen.cpp': [r'this->blit\(xo \+ 56, yo \+ 36 \+ 12 - p, 176, 12 - p, 14, p \+ 2\);',
                                          r'this->blit\(xo \+ 79, yo \+ 34, 176, 14, p \+ 1, 16\);'],
    'Minecraft.Client/ContainerScreen.cpp': [r'int noRowHeight = defaultHeight - 6 \* 18;',
                                            r'this->blit\(xo, yo, 0, 0, imageWidth, containerRows \* 18 \+ 17\);',
                                            r'this->blit\(xo, yo \+ containerRows \* 18 \+ 17, 0, 222 - 96, imageWidth, 96\);'],
    'Minecraft.World/CraftingMenu.cpp': [r'addSlot\(new ResultSlot\( inventory->player, craftSlots, resultSlots, 0, 120 \+ 4, 31 \+ 4\)\);',
                                        r'addSlot\(new Slot\(craftSlots, x \+ y \* 3, 30 \+ x \* 18, 17 \+ y \* 18\)\);'],
    'Minecraft.World/InventoryMenu.cpp': [r'addSlot\(new ResultSlot\( inventory->player, craftSlots, resultSlots, 0, 144, 36\)\);',
                                         r'addSlot\(new Slot\(craftSlots, x \+ y \* 2, 88 \+ x \* 18, 26 \+ y \* 18\)\);'],
    'Minecraft.World/FurnaceMenu.cpp': [r'addSlot\(new Slot\(furnace, 0, 52 \+ 4, 13 \+ 4\)\);',
                                       r'addSlot\(new Slot\(furnace, 1, 52 \+ 4, 49 \+ 4\)\);',
                                       r'112 \+ 4, 31 \+ 4\)\);'],
    'Minecraft.World/ContainerMenu.cpp': [r'addSlot\(new Slot\(container, x \+ y \* 9, 8 \+ x \* 18, 18 \+ y \* 18\)\);',
                                         r'addSlot\(new Slot\(inventory, x \+ y \* 9 \+ 9, 8 \+ x \* 18, 103 \+ y \* 18 \+ yo\)\);',
                                         r'addSlot\(new Slot\(inventory, x, 8 \+ x \* 18, 161 \+ yo\)\);'],
}
# Crops: symbol, png, box (x, y, w, h). Chest = 3-row ContainerScreen: top
# 3*18+17=71 rows, then the 96-row player section from v=222-96.
CROPS = [
    ('Crafting', 'crafting.png', (0, 0, 176, 166)),
    ('Furnace', 'furnace.png', (0, 0, 176, 166)),
    ('FurnaceFlame', 'furnace.png', (176, 0, 14, 14)),
    ('FurnaceArrow', 'furnace.png', (176, 14, 24, 17)),
    ('ChestTop', 'container.png', (0, 0, 176, 71)),
    ('ChestBottom', 'container.png', (0, 126, 176, 96)),
]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def blob(data):
    return hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest()


def reference(name):
    for base in REFERENCES:
        path = base / name
        if path.is_file():
            return path
    raise ValueError(f'missing upstream reference: {name}')


def generate(check):
    assets = ROOT / 'port/assets/inventory'
    images, rows = {}, []
    for name, (sha256, gitsha) in ASSETS.items():
        local = assets / name
        if local.is_file():
            raw = local.read_bytes()
        elif check:
            raise ValueError(f'missing local PNG: {local}')
        else:
            raw = reference(GUI + name).read_bytes()
        if sha(raw) != sha256 or blob(raw) != gitsha:
            raise ValueError(f'original PNG mismatch: {name}')
        if not local.is_file():
            local.write_bytes(raw)
        image = Image.open(local).convert('RGBA')
        if image.size != (256, 256):
            raise ValueError(f'unexpected PNG size: {name}')
        images[name] = image
        rows.append(dict(asset=name, local_file='port/assets/inventory/' + name,
                         upstream_asset_path=GUI + name, sha256=sha256, git_blob_sha1=gitsha,
                         size=[256, 256], mode='RGBA'))
    trees = {}
    for group in ('client', 'world'):
        tree = json.loads((ROOT / f'port/original-{group}-tree.json').read_text())
        trees[group] = {entry['path']: entry['sha'] for entry in tree['tree']}
    evidence = []
    for name, patterns in RULES.items():
        raw = reference(name).read_bytes()
        group = 'client' if name.startswith('Minecraft.Client/') else 'world'
        if blob(raw) != trees[group][name.split('/', 1)[1]]:
            raise ValueError(f'source Git tree pin mismatch: {name}')
        text = raw.decode('utf-8-sig')
        for pattern in patterns:
            match = re.search(pattern, text)
            if not match:
                raise ValueError(f'original rule changed: {name}: {pattern}')
            evidence.append(dict(source=name, line=text.count('\n', 0, match.start()) + 1,
                                 text=match.group(), git_blob_sha1=blob(raw)))
    lines = ['// Generated by tools/extract-containers.py; do not edit.',
             '// Provenance: port/assets/inventory/sources.json.', '#pragma once', '#include <cstdint>',
             'namespace mcinventory::containerassets {']
    for symbol, name, (x, y, w, h) in CROPS:
        rgba = images[name].crop((x, y, x + w, y + h))
        values = [(a << 24) | (r << 16) | (g << 8) | b for r, g, b, a in rgba.getdata()]
        lines.append(f'inline constexpr int {symbol}Width = {w}, {symbol}Height = {h};')
        lines.append(f'inline constexpr uint32_t {symbol}Pixels[] = {{')
        for at in range(0, len(values), 8):
            lines.append('    ' + ','.join(f'0x{v:08x}u' for v in values[at:at + 8]) + ',')
        lines.append('};')
    lines.append('} // namespace mcinventory::containerassets')
    header = ('\n'.join(lines) + '\n').encode()
    manifest = dict(schema_version=1, generator='tools/extract-containers.py',
                    upstream_repository='https://github.com/gstaaij/minecraft-console',
                    upstream_reference_commit=COMMIT,
                    copy_method='Byte-for-byte copy from the local upstream checkout Common/res/gui; no resize or re-encode.',
                    generated_header='ContainerAssets.hpp', generated_sha256=sha(header),
                    assets=rows, crops=[dict(symbol=s, asset=n, box=list(b)) for s, n, b in CROPS],
                    rules=evidence,
                    boundaries=['AbstractContainerScreen::render is #if 0 upstream; this reproduces the legacy texture/slot layout, not console XUI.',
                                'Titles (font->draw labels) are not drawn: the port has no original font renderer.',
                                'Chest is the single 3-row ContainerScreen; large (double) chests are not supported.'])
    return header, (json.dumps(manifest, indent=2, ensure_ascii=False) + '\n').encode()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    try:
        header, manifest = generate(args.check)
        for path, data in [(ROOT / 'jsapi/build/generated/ContainerAssets.hpp', header),
                           (ROOT / 'port/assets/inventory/sources.json', manifest)]:
            if args.check:
                if not path.is_file() or path.read_bytes() != data:
                    raise ValueError(f'missing/stale generated output: {path}')
            elif not path.is_file() or path.read_bytes() != data:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)
    except (ValueError, KeyError, OSError) as error:
        print(f'extract-containers: {error}', file=sys.stderr)
        return 1
    print(('Verified' if args.check else 'Generated') + ' ContainerAssets.hpp; 3 original container GUI textures')
    return 0


if __name__ == '__main__':
    sys.exit(main())
