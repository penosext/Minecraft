#!/usr/bin/env python3
"""Verify local original inventory sources/textures and embed software GUI assets.

Offline only. First invocation copies the three reference PNGs into the allowed
port/assets/inventory directory. Subsequent invocations verify immutable hashes.
--check never writes. Default generated header: jsapi/build/generated/InventoryAssets.hpp.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
REFERENCE = Path('/root/1234/ref/mcpartial/minecraft-console-main')
CLIENT = 'Minecraft.Client/'
WORLD = 'Minecraft.World/'
# Exact local reference bytes. Client tree records Common as a subtree (not a
# recursive PNG pin); keep both SHA256 and Git blob SHA1, never invent a tree pin.
ASSETS = {
    'gui.png': ('e223bea0b5004a3b5497607c6d529daf840396526a46eff75c299364cf9dc795', '81af329e601467a046f59bdff22f450d23b7c7c4'),
    'inventory.png': ('53dd5bde88588215b1700aee4a7dd70a3049e7e53f914982774ebdc26d6862b0', '0b5f2916dd4baf95803ef797b7cf2d7fca6c8c8a'),
    'icons.png': ('8b5a8cfd44b933559c36aecaa722165ad048f1e942f1eeae616554124d6dac43', '73fe9bbda3709ce97c94522210e477a899d75941'),
}
SOURCES = [CLIENT + name for name in ('Gui.cpp', 'GuiComponent.cpp', 'InventoryScreen.cpp', 'AbstractContainerScreen.cpp', 'ItemRenderer.cpp')]
SOURCES += [WORLD + name for name in ('Inventory.cpp', 'InventoryMenu.cpp', 'ItemInstance.cpp', 'Item.cpp', 'Item.h', 'TileItem.cpp', 'Container.h')]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def blob(data):
    return hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest()


def generate(root, reference, check):
    assets = root / 'port/assets/inventory'
    trees = {}
    for group in ('client', 'world'):
        tree = json.loads((root / f'port/original-{group}-tree.json').read_text())
        trees[group] = {entry['path']: entry for entry in tree['tree']}
    source_rows, source_text = [], {}
    for name in SOURCES:
        upstream = root / 'port/upstream' / name
        path = upstream if upstream.is_file() else reference / name
        raw = path.read_bytes()
        group = 'client' if name.startswith(CLIENT) else 'world'
        pin = trees[group][name.split('/', 1)[1]]['sha']
        if blob(raw) != pin:
            raise ValueError(f'source Git tree pin mismatch: {path}')
        source_rows.append(dict(source=name, local_file=str(path), git_blob_sha1=pin, sha256=sha(raw)))
        source_text[name] = raw.decode()
    # Evidence snippets carry exact original text/line/range hashes. Constants
    # below are platform C++ literals checked against these original expressions.
    rules = []
    patterns = {
        CLIENT + 'Gui.cpp': [r'int quickSelectWidth=182;', r'blit\([^\n]+0, 0, 182, 22\);', r'blit\([^\n]+selected \* 20[^\n]+0, 22, 24, 22\);', r'x = screenWidth / 2 - 9 \* 10 \+ i \* 20 \+ 2;', r'y = screenHeight - iSafezoneYHalf - iTooltipsYOffset - 16 - 3 \+ 22;'],
        CLIENT + 'GuiComponent.cpp': [r'float us = 1 / 256\.0f;', r'float vs = 1 / 256\.0f;'],
        CLIENT + 'InventoryScreen.cpp': [r'int xo = \(width - imageWidth\) / 2;', r'int yo = \(height - imageHeight\) / 2;', r'this->blit\(xo, yo, 0, 0, imageWidth, imageHeight\);'],
        CLIENT + 'AbstractContainerScreen.cpp': [r'imageWidth = 176;', r'imageHeight = 166;'],
        CLIENT + 'ItemRenderer.cpp': [r'font->drawShadow\(amount, x \+ 19 - 2 - font->width\(amount\), y \+ 6 \+ 3,[^\n]+'],
        WORLD + 'Inventory.cpp': [r'const int Inventory::MAX_INVENTORY_STACK_SIZE = 64;', r'const int Inventory::INVENTORY_SIZE = 4 \* 9;', r'const int Inventory::SELECTION_SIZE = 9;', r'armor = ItemInstanceArray\( 4 \);'],
        WORLD + 'InventoryMenu.cpp': [r'addSlot\(new ArmorSlot\(i, inventory, inventory->getContainerSize\(\) - 1 - i, 8, 8 \+ i \* 18\)\s*\);', r'addSlot\(new Slot\(inventory, x \+ \(y \+ 1\) \* 9, 8 \+ x \* 18, 84 \+ y \* 18\)\);', r'addSlot\(new Slot\(inventory, x, 8 \+ x \* 18, 142\)\);'],
        WORLD + 'ItemInstance.cpp': [r'return getItem\(\)->getMaxStackSize\(\);'],
        WORLD + 'Item.cpp': [r'maxStackSize = Item::MAX_STACK_SIZE;'],
        WORLD + 'Item.h': [r'static const int MAX_STACK_SIZE = Container::LARGE_MAX_STACK_SIZE;'],
        WORLD + 'Container.h': [r'static const int LARGE_MAX_STACK_SIZE = 64;'],
        WORLD + 'TileItem.cpp': [r'TileItem::TileItem\(int id\) : Item\(id\)'],
    }
    for name, expressions in patterns.items():
        text = source_text[name]
        for pattern in expressions:
            match = re.search(pattern, text)
            if not match:
                raise ValueError(f'original rule changed: {name}: {pattern}')
            rules.append(dict(source=name, line=text.count('\n', 0, match.start()) + 1,
                              text=match.group(), sha256=sha(match.group().encode())))
    images, asset_rows = {}, []
    for name, (sha256, gitsha) in ASSETS.items():
        local = assets / name
        original = reference / CLIENT / 'Common/res/gui' / name
        raw = local.read_bytes() if local.is_file() else original.read_bytes()
        if sha(raw) != sha256 or blob(raw) != gitsha:
            raise ValueError(f'original PNG mismatch: {name}')
        if not local.is_file() and not check:
            assets.mkdir(parents=True, exist_ok=True)
            local.write_bytes(raw)
        if check and not local.is_file():
            raise ValueError(f'missing local PNG: {local}')
        image = Image.open(local if local.is_file() else original).convert('RGBA')
        if image.size != (256, 256):
            raise ValueError(f'unexpected PNG dimensions: {name}')
        images[name] = image
        asset_rows.append(dict(source=CLIENT + 'Common/res/gui/' + name,
                               local_file='port/assets/inventory/' + name, sha256=sha256,
                               git_blob_sha1=gitsha, size=[256, 256],
                               tree_pin='Common subtree f6e2cbbb8fa59c258653e491b02f8e32b66454c0; PNG blob pinned locally'))
    lines = ['// Generated by tools/extract-inventory.py; do not edit.', '#pragma once', '#include <cstdint>',
             'namespace mcinventory::assets {',
             'inline constexpr int HotbarWidth=182, HotbarHeight=22, SlotPitch=20;',
             'inline constexpr int SelectionWidth=24, SelectionHeight=22;',
             'inline constexpr int PanelWidth=176, PanelHeight=166, IconSize=16;',
             'inline constexpr int GridX=8, GridY=84, GridPitch=18, GridHotbarY=142;',
             'inline constexpr int ArmorX=8, ArmorY=8, ArmorPitch=18;']
    for symbol, name, box in [('Hotbar', 'gui.png', (0, 0, 182, 22)),
                              ('Selection', 'gui.png', (0, 22, 24, 44)),
                              ('Panel', 'inventory.png', (0, 0, 176, 166))]:
        rgba = images[name].crop(box)
        values = [(a << 24) | (r << 16) | (g << 8) | b for r, g, b, a in rgba.getdata()]
        lines.append(f'inline constexpr uint32_t {symbol}Pixels[] = {{')
        for at in range(0, len(values), 8):
            lines.append('    ' + ','.join(f'0x{v:08x}u' for v in values[at:at + 8]) + ',')
        lines.append('};')
    lines.append('} // namespace mcinventory::assets')
    header = ('\n'.join(lines) + '\n').encode()
    manifest = dict(schema_version=1, generator='tools/extract-inventory.py',
                    source_commit='09df8928eeb15328bca499d225e48ea95af6d119',
                    generated_header='InventoryAssets.hpp', generated_sha256=sha(header),
                    assets=asset_rows, sources=source_rows, rules=rules,
                    layout=dict(hotbar=[182, 22], selection_uv=[0, 22, 24, 22], selection_offset=[-1, -1],
                                hotbar_icon_offset=[3, 3], hotbar_pitch=20, panel=[176, 166],
                                main_grid=[8, 84, 18, 18], panel_hotbar=[8, 142, 18],
                                armor=[8, 8, 18], count_anchor=[17, 9]),
                    boundaries=[
                        'Source Client uses gui/gui.png, not widgets.png; local original bytes are unmodified.',
                        'InventoryScreen::renderBg and AbstractContainerScreen::render are #if 0 (4J unused); reproduces their legacy texture/layout, not console XUI/Scaleform.',
                        '1x GUI units at normal viewport including 960x266 is an explicit platform choice, NOT evidence of original console GUI scaling.',
                        'Uniform nearest-neighbor shrink only below 184x168; no console safe-zone, split-screen or GUI scale settings.',
                        '36 main slots including hotbar 0..8; 4 armor state slots retained without equip/armor stats.',
                        'Existing terrain catalog blocks/state variants ONLY, not full Item registry, tools, original drops or crafting.',
                        'TileItem inherits Item max stack 64; these are block-state stacks, not original items for doors/liquids/crops.',
                        'Software terrain-mesh thumbnails replace huge original GUI item renderer closure.',
                        'Counts use platform 3x5 bitmap digits at original right-aligned (17,9) anchor; not original font glyphs.',
                        'Default inventory is EMPTY; creative catalog selection is an explicit platform action.',
                        'Crafting/character preview are inactive background artwork only; no original character/menu behavior.',
                        'Save identity pins parent/world at Inventory.open plus exact level.json bytes and validates public seed/dimensions/state source hashes; without World friend cannot attest the pre-open private directory identity.',
                        'FNV32 detects corruption, not hostile cryptographic forgery; save is main-thread only with last-byte checks, not a multiwriter lock.'
                    ])
    return header, (json.dumps(manifest, indent=2, ensure_ascii=False) + '\n').encode()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=ROOT)
    parser.add_argument('--reference-root', type=Path, default=REFERENCE)
    parser.add_argument('--output-dir', type=Path)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    output = args.output_dir or args.root / 'jsapi/build/generated'
    try:
        header, manifest = generate(args.root, args.reference_root, args.check)
        for path, data in [(output / 'InventoryAssets.hpp', header),
                           (args.root / 'port/assets/inventory/constants.manifest.json', manifest)]:
            if args.check:
                if not path.is_file() or path.read_bytes() != data:
                    raise ValueError(f'missing/stale generated output: {path}')
            else:
                path.parent.mkdir(parents=True, exist_ok=True)
                if not path.is_file() or path.read_bytes() != data:
                    path.write_bytes(data)
    except (ValueError, KeyError, OSError) as error:
        print(f'extract-inventory: {error}', file=sys.stderr)
        return 1
    print(f'{"Verified" if args.check else "Generated"} {output / "InventoryAssets.hpp"}; 3 original GUI textures, source-pinned constants')
    return 0


if __name__ == '__main__':
    sys.exit(main())
