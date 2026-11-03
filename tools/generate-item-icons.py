#!/usr/bin/env python3
"""Embed 63 audited canonical inventory sprites, not a general item engine.

No arguments regenerates items.png (byte copy if missing), items.source.json and
ItemIcons.hpp. --check validates pinned catalog/sources/PNG/anchors and exact
outputs without writing. Requires Pillow; all generation is offline.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / 'port/assets/items'
HEADER = ROOT / 'jsapi/build/generated/ItemIcons.hpp'
COMMIT = '09df8928eeb15328bca499d225e48ea95af6d119'
REPO = 'https://github.com/gstaaij/minecraft-console'
PNG_PATH = 'Minecraft.Client/Common/res/TitleUpdate/res/items.png'
PNG_SHA = '1562d583262f494526f9ae34047ecbe6af060a237d3e30c751940c239b371064'
PNG_BLOB = '5456083c02957afa9d7b136980e7513ad5520b2f'
RGBA_SHA = '31076214c9426183e99cceaedec141f27cdb2f4086731a5503ff728159864da6'
CATALOG_SHA = '179af06030844929f0f885d255473b663f19eee48ffaca633635ba36bfe5b7f6'
PINS = {
    'Minecraft.World/Item.cpp': '263adb75f312ef20dee9634d24ebe0c547e8a53a2702e4a5f3d3f5fcba5e9c0d',
    'Minecraft.World/DyePowderItem.cpp': 'ee56e201d95c98e6a815eb68e79e929bbbd73157cb9ceeacf46b89d0c3a471b3',
    'Minecraft.Client/PreStitchedTextureMap.cpp': '0bbef250317693276e94382d5760ae57d1254e5098f0a166face91a5c6c5a7e7',
}
# itemId, aux, source registration symbol, exact icon name, x, y (16x16).
# Coordinates are independently checked against the pinned ADD_ICON row/column.
MAPPING = [
    (324, 0, 'door_wood', 'doorWood', 176, 32),
    (330, 0, 'door_iron', 'doorIron', 192, 32),
    (295, 0, 'seeds_wheat', 'seeds', 144, 0),
    (287, 0, 'string', 'string', 128, 0),
    (331, 0, 'redStone', 'redstone', 128, 48),
    (338, 0, 'reeds', 'reeds', 176, 16),
    (351, 3, 'dye_powder', 'dyePowder_brown', 224, 112),
    (356, 0, 'repeater', 'diode', 96, 80),
    (372, 0, 'netherwart_seeds', 'netherStalkSeeds', 208, 112),
    (380, 0, 'cauldron', 'cauldron', 192, 144),
    (390, 0, 'flowerPot', 'flowerPot', 208, 176),
    (391, 0, 'carrots', 'carrots', 128, 112),
    (392, 0, 'potato', 'potato', 112, 112),
]
# Original Item.cpp registrations and item-atlas ADD_ICON(row, column, name).
# 25 tools, 20 armor pieces, and the five materials in the pinned creative catalog.
NEW_ITEMS = [
    (268, 'sword_wood', 'swordWood', 4, 0),
    (272, 'sword_stone', 'swordStone', 4, 1),
    (267, 'sword_iron', 'swordIron', 4, 2),
    (276, 'sword_diamond', 'swordDiamond', 4, 3),
    (283, 'sword_gold', 'swordGold', 4, 4),
    (269, 'shovel_wood', 'shovelWood', 5, 0),
    (273, 'shovel_stone', 'shovelStone', 5, 1),
    (256, 'shovel_iron', 'shovelIron', 5, 2),
    (277, 'shovel_diamond', 'shovelDiamond', 5, 3),
    (284, 'shovel_gold', 'shovelGold', 5, 4),
    (270, 'pickAxe_wood', 'pickaxeWood', 6, 0),
    (274, 'pickAxe_stone', 'pickaxeStone', 6, 1),
    (257, 'pickAxe_iron', 'pickaxeIron', 6, 2),
    (278, 'pickAxe_diamond', 'pickaxeDiamond', 6, 3),
    (285, 'pickAxe_gold', 'pickaxeGold', 6, 4),
    (271, 'hatchet_wood', 'hatchetWood', 7, 0),
    (275, 'hatchet_stone', 'hatchetStone', 7, 1),
    (258, 'hatchet_iron', 'hatchetIron', 7, 2),
    (279, 'hatchet_diamond', 'hatchetDiamond', 7, 3),
    (286, 'hatchet_gold', 'hatchetGold', 7, 4),
    (290, 'hoe_wood', 'hoeWood', 8, 0),
    (291, 'hoe_stone', 'hoeStone', 8, 1),
    (292, 'hoe_iron', 'hoeIron', 8, 2),
    (293, 'hoe_diamond', 'hoeDiamond', 8, 3),
    (294, 'hoe_gold', 'hoeGold', 8, 4),
    (298, 'helmet_leather', 'helmetCloth', 0, 0),
    (302, 'helmet_chain', 'helmetChain', 0, 1),
    (306, 'helmet_iron', 'helmetIron', 0, 2),
    (310, 'helmet_diamond', 'helmetDiamond', 0, 3),
    (314, 'helmet_gold', 'helmetGold', 0, 4),
    (299, 'chestplate_leather', 'chestplateCloth', 1, 0),
    (303, 'chestplate_chain', 'chestplateChain', 1, 1),
    (307, 'chestplate_iron', 'chestplateIron', 1, 2),
    (311, 'chestplate_diamond', 'chestplateDiamond', 1, 3),
    (315, 'chestplate_gold', 'chestplateGold', 1, 4),
    (300, 'leggings_leather', 'leggingsCloth', 2, 0),
    (304, 'leggings_chain', 'leggingsChain', 2, 1),
    (308, 'leggings_iron', 'leggingsIron', 2, 2),
    (312, 'leggings_diamond', 'leggingsDiamond', 2, 3),
    (316, 'leggings_gold', 'leggingsGold', 2, 4),
    (301, 'boots_leather', 'bootsCloth', 3, 0),
    (305, 'boots_chain', 'bootsChain', 3, 1),
    (309, 'boots_iron', 'bootsIron', 3, 2),
    (313, 'boots_diamond', 'bootsDiamond', 3, 3),
    (317, 'boots_gold', 'bootsGold', 3, 4),
    (280, 'stick', 'stick', 3, 5),
    (265, 'ironIngot', 'ingotIron', 1, 7),
    (266, 'goldIngot', 'ingotGold', 2, 7),
    (264, 'diamond', 'diamond', 3, 7),
    (334, 'leather', 'leather', 6, 7),
]
MAPPING += [(iid, 0, symbol, name, col * 16, row * 16)
            for iid, symbol, name, row, col in NEW_ITEMS]


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def build(png):
    require(sha(png) == PNG_SHA, 'items.png SHA-256 pin mismatch')
    require(hashlib.sha1(b'blob ' + str(len(png)).encode() + b'\0' + png).hexdigest() == PNG_BLOB,
            'items.png Git blob mismatch')
    catalog_bytes = (ROOT / 'port/item-catalog.json').read_bytes()
    require(sha(catalog_bytes) == CATALOG_SHA, 'Creative item catalog pin mismatch')
    catalog = json.loads(catalog_bytes)['items']
    creative = {entry['key'] for entry in catalog if entry['creative'] and entry['renderIndex'] == -1}
    new_keys = {f'item:{iid}:0' for iid, *_ in NEW_ITEMS}
    require(len(NEW_ITEMS) == len(new_keys) == 50, 'Expected 50 distinct new item keys')
    require(new_keys == creative, 'New sprites must exactly cover creative items without a block thumbnail')
    require(all(not entry['placeable'] for entry in catalog if entry['key'] in new_keys),
            'New item sprite must not be a placeable block')
    sources = {}
    for path, expected in PINS.items():
        data = (ROOT / 'port/upstream' / path).read_bytes()
        require(sha(data) == expected, f'Source pin mismatch: {path}')
        sources[path] = data.decode('utf-8-sig')

    def anchor(path, needle):
        text = sources[path]
        require(text.count(needle) == 1, f'Missing/ambiguous source anchor: {path}: {needle}')
        return {'path': path, 'sha256': PINS[path],
                'line': text[:text.index(needle)].count('\n') + 1, 'anchor': needle}

    item_path = 'Minecraft.World/Item.cpp'
    dye_path = 'Minecraft.World/DyePowderItem.cpp'
    uv_path = 'Minecraft.Client/PreStitchedTextureMap.cpp'
    shared = [anchor(item_path, 'Item::Item(int id) : id( 256 + id )'),
              anchor(item_path, 'icon = iconRegister->registerIcon(m_textureName);'),
              anchor(uv_path, '#define ADD_ICON(row, column, name) (texturesByName[name] =\tnew SimpleIcon(name,name,horizRatio*column,vertRatio*row,horizRatio*(column+1),vertRatio*(row+1)));'),
              anchor(uv_path, 'if(iconType != Icon::TYPE_TERRAIN)'),
              anchor(uv_path, 'if(iconType != Icon::TYPE_TERRAIN)\n\t{\n\t\tfloat horizRatio = 1.0f/16.0f;\n\t\tfloat vertRatio = 1.0f/16.0f;')]
    # Do not accidentally match terrain names (reeds occurs in both tables).
    item_uvs = sources[uv_path].split('if(iconType != Icon::TYPE_TERRAIN)', 1)[1].split('else', 1)[0]
    from io import BytesIO
    with Image.open(BytesIO(png)) as image:
        require(image.size == (256, 256) and image.mode == 'RGBA', 'Expected 256x256 RGBA items.png')
        require(sha(image.tobytes()) == RGBA_SHA, 'Decoded items RGBA pin mismatch')
        rows, crops = [], []
        require(len(MAPPING) == 63 and len({r[:2] for r in MAPPING}) == 63, 'Expected 63 unique canonical keys')
        for iid, aux, symbol, name, x, y in MAPPING:
            registrations = list(re.finditer(r'Item::' + re.escape(symbol) + r'\s*=\s*[^;]*?new\s+\w+\(\s*(\d+)[^;]*;', sources[item_path]))
            require(len(registrations) == 1, f'Ambiguous Item registration: {symbol}')
            registration = registrations[0]
            base_name = 'dyePowder' if iid == 351 else name
            require(int(registration[1]) + 256 == iid and f'setIconName(L"{base_name}")' in registration[0],
                    f'Item ID/icon registration mismatch: {symbol}')
            refs = [anchor(item_path, registration[0])]
            if iid == 351:
                require(aux == 3, 'Only brown dye is covered')
                refs += [anchor(dye_path, 'const int DyePowderItem::BROWN = 3;'),
                         anchor(dye_path, '{ L"black", L"red", L"green", L"brown",'),
                         anchor(dye_path, 'icons[i] = iconRegister->registerIcon(getIconName() + L"_" + COLOR_TEXTURES[i]);'),
                         anchor(dye_path, 'int colorValue = Mth::clamp(itemAuxValue, 0, 15);'),
                         anchor(dye_path, 'return icons[colorValue];')]
            else:
                require(aux == 0, 'Unexpected item aux')
            matches = list(re.finditer(r'ADD_ICON\(\s*(\d+),\s*(\d+),\s*L"' + re.escape(name) + r'"\)', item_uvs))
            require(len(matches) == 1, f'Missing/ambiguous item UV: {name}')
            uv = matches[0]
            require((int(uv[2]) * 16, int(uv[1]) * 16) == (x, y), f'Crop/source UV mismatch: {name}')
            # Include leading indentation to distinguish a repeated terrain macro.
            uv_line = next(line for line in sources[uv_path].splitlines() if uv[0] in line)
            refs.append(anchor(uv_path, uv_line))
            crop = image.crop((x, y, x + 16, y + 16)).tobytes()
            rows.append({'key': f'item:{iid}:{aux}', 'itemId': iid, 'aux': aux, 'icon': name,
                         'crop': [x, y, 16, 16], 'rgbaSha256': sha(crop), 'sources': refs})
            crops.append(crop)
    manifest = {
        'schema_version': 1, 'scope': '63 canonical inventory sprites: original 13 plus 25 tools, 20 armor pieces and 5 materials; no item engine or world-state icons.',
        'repository': REPO, 'commit': COMMIT,
        'texture': {'path': PNG_PATH, 'sha256': PNG_SHA, 'gitBlob': PNG_BLOB,
                    'sizeBytes': len(png), 'width': 256, 'height': 256, 'mode': 'RGBA', 'rgbaSha256': RGBA_SHA},
        'githubVerification': {
            'method': 'GET', 'url': f'https://api.github.com/repos/gstaaij/minecraft-console/contents/{PNG_PATH}?ref={COMMIT}',
            'httpStatus': 200, 'responsePath': PNG_PATH, 'responseBlob': PNG_BLOB,
            'responseSize': 39203, 'decodedContentSha256': PNG_SHA,
            'contentEqualsLocalPng': True,
            'note': 'Read-only unauthenticated API verified during initial audit. Offline generation does not repeat this network verification; no commit-signature verification is claimed.'},
        'pixelContract': '16x16 original RGBA crops, straight AARRGGBB integers; preserve RGB even at alpha 0; nearest-neighbor UI blit, no tint/mesh/world metadata.',
        'sharedSources': shared, 'items': rows,
        'limitations': ['Only these 63 keys have sprites; other canonical items retain representative mesh thumbnails.',
                        'Inventory/hotbar sprites only; first-person held items are not rendered.',
                        'Not a full upstream ItemRenderer, item/tool engine or world renderer.',
                        'Pinned local C++ sources are required for offline anchor verification.'],
    }
    lines = ['#pragma once', '// Generated by tools/generate-item-icons.py; do not edit.',
             '// Provenance: port/assets/items/items.source.json. 63 canonical keys.',
             '#include <cstdint>', '#include <cstring>', 'namespace itemicons {',
             'constexpr int kIconSize = 16;', 'constexpr int kIconCount = 63;',
             f'constexpr char kSourceSha256[] = "{PNG_SHA}";',
             'struct Icon { const char* key; const char* name; int itemId, aux, x, y; uint32_t pixels[256]; };',
             '// Straight AARRGGBB; never tinted or shaded as terrain.',
             'inline constexpr Icon kIcons[] = {']
    for row, crop in zip(rows, crops):
        lines.append('    {"%s", "%s", %d, %d, %d, %d, {' % (row['key'], row['icon'], row['itemId'], row['aux'], *row['crop'][:2]))
        pixels = [(a << 24) | (r << 16) | (g << 8) | b for r, g, b, a in zip(crop[0::4], crop[1::4], crop[2::4], crop[3::4])]
        for at in range(0, 256, 8):
            lines.append('        ' + ', '.join(f'0x{p:08x}u' for p in pixels[at:at+8]) + ',')
        lines.append('    }},')
    lines += ['};', 'inline const Icon* findIcon(const char* canonicalKey) {',
              '    if (canonicalKey) for (const auto& icon : kIcons)',
              '        if (std::strcmp(canonicalKey, icon.key) == 0) return &icon;',
              '    return nullptr;', '}', '} // namespace itemicons', '']
    return {ASSETS / 'items.source.json': (json.dumps(manifest, ensure_ascii=False, indent=2) + '\n').encode(),
            HEADER: '\n'.join(lines).encode()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true', help='Verify all pinned sources and byte-exact generated outputs; write nothing')
    args = parser.parse_args()
    target = ASSETS / 'items.png'
    reference = ROOT.parent / 'ref/mcpartial/minecraft-console-main' / PNG_PATH
    require(target.exists() or not args.check, f'Missing asset: {target}')
    png = (target if target.exists() else reference).read_bytes()
    outputs = {target: png, **build(png)}
    for path, content in outputs.items():
        if args.check:
            require(path.exists() and path.read_bytes() == content, f'Stale generated output: {path}')
        elif not path.exists() or path.read_bytes() != content:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(content)
    print('PASS: 63 canonical item sprites; catalog/source pins, PNG/blob/RGBA, mapping anchors and exact outputs' if args.check else 'Generated 63 canonical 16x16 item sprites and pinned provenance')


if __name__ == '__main__':
    main()
