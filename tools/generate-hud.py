#!/usr/bin/env python3
"""Embed the survival HUD sprites (hearts, armor, air, hunger) as a C++ header.

Offline only. The console runtime binds TN_GUI_ICONS ("gui/icons.png") through
Textures::loadTexture, which prefixes "1_2_2/" for every non-original image
(Textures.cpp:1428-1435, OriginalImages/IsOriginalImage at 1573-1620). The
runtime atlas is therefore Common/res/1_2_2/gui/icons.png, NOT the older
Common/res/gui/icons.png already used for inventory constants (that one has no
hunger row). The first invocation copies it from --reference-root into
port/assets/hud/icons.png; later invocations verify the pinned bytes.

UVs are the legacy Gui.cpp blits. That HUD is compiled out in this build
(Gui.cpp:35 `#define RENDER_HUD 0`; the console draws an Iggy/Flash HUD fed by
IUIScene_HUD.cpp), so the layout is a reconstruction of the legacy code, the
same honesty boundary as the inventory panel. --check never writes.
"""
import argparse
import hashlib
import json
import shutil
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
ATLAS_REL = 'Minecraft.Client/Common/res/1_2_2/gui/icons.png'
ATLAS_SHA = 'e0e7fe67507435af02b931d26aaaff293a4971484e54d4eafa95a19365395d90'
ATLAS_BLOB = 'e5d56d73aaf91a851c41eac83a43e2fc8416a986'
TARGET = ROOT / 'port/assets/hud/icons.png'
MANIFEST = ROOT / 'port/assets/hud/hud.source.json'
HEADER = ROOT / 'jsapi/build/generated/HudIcons.hpp'
# Crop of the atlas that holds every cell used below: x 16..88, y 0..36.
CROP = (16, 0, 88, 36)
# Exact source lines, verified verbatim (whitespace-normalized) in the pinned files.
ANCHORS = {
    'Minecraft.Client/Gui.cpp': [
        '#define RENDER_HUD 0',
        'yLine1 = screenHeight - 18 - iSafezoneYHalf - iTooltipsYOffset;',
        'yLine2 = yLine1 - 10;',
        'if (i * 2 + 1 < armor) blit(xo, yLine2, 16 + 2 * 9, 9, 9, 9);',
        'if (i * 2 + 1 == armor) blit(xo, yLine2, 16 + 1 * 9, 9, 9, 9);',
        'if (i * 2 + 1 > armor) blit(xo, yLine2, 16 + 0 * 9, 9, 9, 9);',
        'blit(xo, yo, 16 + bg * 9, 9 * y0, 9, 9);',
        'if (i * 2 + 1 < oldHealth) blit(xo, yo, healthTexBaseX + 6 * 9, 9 * y0, 9, 9);',
        'if (i * 2 + 1 == oldHealth) blit(xo, yo, healthTexBaseX + 7 * 9, 9 * y0, 9, 9);',
        'if (i * 2 + 1 < currentHealth) blit(xo, yo, healthTexBaseX + 4 * 9, 9 * y0, 9, 9);',
        'if (i * 2 + 1 == currentHealth) blit(xo, yo, healthTexBaseX + 5 * 9, 9 * y0, 9, 9);',
        'int xo = xRight - i * 8 - 9;',
        'blit(xo, yo, 16 + bg * 9, 9 * 3, 9, 9);',
        'if (i * 2 + 1 < food) blit(xo, yo, texBaseX + 4 * 9, 9 * 3, 9, 9);',
        'if (i * 2 + 1 == food) blit(xo, yo, texBaseX + 5 * 9, 9 * 3, 9, 9);',
        'if (i < count) blit(xRight - i * 8 - 9, yLine2, 16, 9 * 2, 9, 9);',
        'else blit(xRight - i * 8 - 9, yLine2, 16 + 9, 9 * 2, 9, 9);',
        'random->setSeed(tickCount * 312871);',
        'if (currentHealth <= 4)',
        'yo += random.nextInt(2);',
        'if ((tickCount % (food * 3 + 1)) == 0)',
        'yo += random->nextInt(3) - 1;',
    ],
    'Minecraft.Client/Common/UI/IUIScene_HUD.cpp': [
        'bool blink = pMinecraft->localplayers[iPad]->invulnerableTime / 3 % 2 == 1;',
        'if (pMinecraft->localplayers[iPad]->invulnerableTime < 10) blink = false;',
        'int currentHealth = static_cast<int>(ceil(pMinecraft->localplayers[iPad]->getHealth()));',
        'int count = static_cast<int>(ceil((pMinecraft->localplayers[iPad]->getAirSupply() - 2) * 10.0f / Player::TOTAL_AIR_SUPPLY));',
    ],
    'Minecraft.Client/Textures.cpp': [
        'img = skins->getDefault()->getImageResource(L"1_2_2/" + name, false, isTu, drive);',
    ],
    'Minecraft.Client/DeathScreen.cpp': [
        'fillGradient(0, 0, width, height, 0x60500000, 0xa0803030);',
    ],
}
# Cells as (name, atlas x, atlas y); all are 9x9.
CELLS = [
    ('HeartContainer', 16, 0), ('HeartContainerBlink', 25, 0),
    ('HeartFull', 52, 0), ('HeartHalf', 61, 0), ('HeartBlinkFull', 70, 0), ('HeartBlinkHalf', 79, 0),
    ('ArmorEmpty', 16, 9), ('ArmorHalf', 25, 9), ('ArmorFull', 34, 9),
    ('Bubble', 16, 18), ('BubblePop', 25, 18),
    ('FoodContainer', 16, 27), ('FoodFull', 52, 27), ('FoodHalf', 61, 27),
]


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def blob(data):
    return hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest()


def normalized(text):
    return ' '.join(text.split())


def anchors():
    rows = []
    for rel, needles in ANCHORS.items():
        path = ROOT / 'port/upstream' / rel
        raw = path.read_bytes()
        lines = raw.decode('utf-8-sig').splitlines()
        for needle in needles:
            hits = [n for n, line in enumerate(lines, 1) if normalized(needle) in normalized(line)]
            require(hits, f'Missing source anchor {rel}: {needle}')
            rows.append({'path': 'port/upstream/' + rel, 'line': hits[0], 'sha256': sha(raw), 'anchor': needle})
    return rows


def build(png):
    require(sha(png) == ATLAS_SHA and blob(png) == ATLAS_BLOB, 'HUD atlas pin mismatch')
    from io import BytesIO
    with Image.open(BytesIO(png)) as image:
        image = image.convert('RGBA')
        require(image.size == (256, 256), 'Expected a 256x256 HUD atlas')
        crop = image.crop(CROP)
    width, height = CROP[2] - CROP[0], CROP[3] - CROP[1]
    pixels = [(a << 24) | (r << 16) | (g << 8) | b for r, g, b, a in crop.getdata()]
    for name, x, y in CELLS:
        cell = image.crop((x, y, x + 9, y + 9))
        require(any(p[3] for p in cell.getdata()), f'Empty HUD cell {name}')
    lines = ['#pragma once', '// Generated by tools/generate-hud.py; do not edit.',
             '// Provenance: port/assets/hud/hud.source.json (runtime 1_2_2/gui/icons.png).',
             '#include <cstdint>', 'namespace hudicons {',
             f'inline constexpr int kAtlasX = {CROP[0]}, kAtlasY = {CROP[1]}, kWidth = {width}, kHeight = {height}, kCell = 9;',
             '// Source atlas coordinates (Gui.cpp blit u, v) of each 9x9 cell.',
             'struct Cell { int u, v; };']
    for name, x, y in CELLS:
        lines.append(f'inline constexpr Cell k{name} {{{x}, {y}}};')
    lines.append('// Straight AARRGGBB, row-major kWidth x kHeight, origin (kAtlasX, kAtlasY).')
    lines.append('inline constexpr uint32_t kPixels[] = {')
    for at in range(0, len(pixels), 8):
        lines.append('    ' + ','.join(f'0x{p:08x}u' for p in pixels[at:at + 8]) + ',')
    lines += ['};', '} // namespace hudicons', '']
    header = '\n'.join(lines).encode()
    manifest = {
        'schema_version': 1, 'generator': 'tools/generate-hud.py',
        'atlas': {'source': ATLAS_REL, 'local_file': str(TARGET.relative_to(ROOT)), 'sha256': ATLAS_SHA,
                  'git_blob_sha1': ATLAS_BLOB, 'size': [256, 256], 'crop': list(CROP)},
        'cells': [{'name': n, 'u': x, 'v': y, 'size': 9} for n, x, y in CELLS],
        'generated_header': 'HudIcons.hpp', 'generated_sha256': sha(header),
        'sources': anchors(),
        'boundaries': [
            'Legacy Gui.cpp HUD is compiled out (RENDER_HUD 0); console uses an Iggy/Flash HUD. Layout and UVs are '
            'the legacy blits, values (ceil health, blink, air count) match IUIScene_HUD.cpp.',
            'Runtime atlas is the 1_2_2 icons.png per Textures.cpp; the older gui/icons.png lacks the hunger row.',
            'No poison/wither/hunger-effect, absorption, hardcore, regeneration bob or mount-health variants: '
            'this port has no mob effects or riding.',
        ],
    }
    return header, (json.dumps(manifest, indent=2, ensure_ascii=False) + '\n').encode()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference-root', type=Path, default=ROOT / 'port/upstream',
                        help='Upstream checkout holding the gitignored client PNG (only read when the asset is missing)')
    parser.add_argument('--check', action='store_true', help='Verify pinned inputs and exact outputs; write nothing')
    args = parser.parse_args()
    try:
        if not TARGET.exists():
            require(not args.check, f'Missing asset: {TARGET}')
            source = args.reference_root / ATLAS_REL
            require(source.exists(), f'HUD atlas absent locally and in reference: {source}')
            require(sha(source.read_bytes()) == ATLAS_SHA, f'Reference atlas pin mismatch: {source}')
            TARGET.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, TARGET)
        header, manifest = build(TARGET.read_bytes())
        for path, data in ((HEADER, header), (MANIFEST, manifest)):
            if args.check:
                require(path.is_file() and path.read_bytes() == data, f'Missing/stale generated output: {path}')
            elif not path.is_file() or path.read_bytes() != data:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)
    except (ValueError, OSError) as error:
        print(f'generate-hud: {error}', file=sys.stderr)
        return 1
    print(f'{"Verified" if args.check else "Generated"} {HEADER.name}: {len(CELLS)} HUD cells from pinned 1_2_2 icons.png')
    return 0


if __name__ == '__main__':
    sys.exit(main())
