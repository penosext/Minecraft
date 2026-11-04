#!/usr/bin/env python3
"""Byte-exact local carver/ore extraction. No network; --check never writes.

Sources: LargeFeature / LargeCaveFeature / CanyonFeature (RandomLevelSource::
getChunk carvers) and OreFeature + BiomeDecorator::decorateOres (postProcess
ore stage). Every compiled function body is an unmodified byte slice of the
pinned upstream file; only include/pragma lines are dropped.
"""
import hashlib
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parent
UP = ROOT.parent.parent / 'upstream' / 'Minecraft.World'
OUT = ROOT / 'generated'
FEATURES = ['Feature', 'LargeFeature', 'LargeCaveFeature', 'CanyonFeature', 'OreFeature']
NAMES = set(['ArrayWithLength.h', 'BiomeDecorator.cpp', 'BiomeDecorator.h', 'RandomLevelSource.cpp', 'Mth.cpp',
             'Tile.h', 'Definitions.h', 'Level.h', 'Biome.h'])
for n in FEATURES:
    NAMES.add(n + '.h')
    NAMES.add(n + '.cpp')
sha = lambda b: hashlib.sha256(b).hexdigest()
inputs = {n: (UP / n).read_bytes() for n in sorted(NAMES)}
pinned = json.loads((ROOT / 'upstream-sha256.json').read_text())
assert {n: sha(b) for n, b in inputs.items()} == pinned, 'pinned upstream source changed'
segments = []


def cut(n, a, b):
    data = inputs[n]
    start = data.index(a.encode()) if isinstance(a, str) else a
    end = data.index(b.encode(), start) if isinstance(b, str) else b
    raw = data[start:end]
    segments.append({'source': n, 'startByte': start, 'endByteExclusive': end,
                     'sha256': sha(raw), 'bytes': len(raw)})
    return raw.decode('utf-8')


def body(n):
    data = inputs[n]
    # Only preprocessor include/pragma lines and leading whitespace are removed.
    match = re.search(rb'(?m)^(?![ \t]*(?:#|$|\r))[A-Za-z/]', data)
    return cut(n, match.start(), len(data))


def function(n, signature):
    data = inputs[n]
    a = data.index(signature.encode())
    opening = data.index(b'{', a)
    depth = 1
    end = opening + 1
    while depth:
        if data[end] == ord('{'):
            depth += 1
        elif data[end] == ord('}'):
            depth -= 1
        end += 1
    return cut(n, a, end)


files = {}
files['OriginalDeclarations.inc'] = ('// Generated exact original declarations; do not edit.\n#pragma once\n'
                                     + '\n'.join(body(n + '.h') for n in FEATURES))
cpp = ['#include "Platform.h"\nnamespace original_carvers { namespace detail {\n']
for n in FEATURES:
    cpp.append(body(n + '.cpp'))
cpp.append(cut('Mth.cpp', 'float *Mth::_sin = nullptr;', '\n\n// 4J - added - was in static constructor'))
for signature in ['void Mth::init()', 'float Mth::sin(float i)', 'float Mth::cos(float i)',
                  'int Mth::floor(float v)', 'int Mth::floor(double v)']:
    cpp.append(function('Mth.cpp', signature))
for signature in ['void BiomeDecorator::decorateDepthSpan(int count',
                  'void BiomeDecorator::decorateDepthAverage(int count',
                  'void BiomeDecorator::decorateOres()']:
    cpp.append(function('BiomeDecorator.cpp', signature))
cpp.append('} }\n')
files['Original.cpp'] = '\n\n'.join(cpp)
# Literal original constants, without rendering/entity dependencies.
ids = [cut('Tile.h', m.start(), m.end())
       for m in re.finditer(rb'static const int \w+_Id\s*=\s*\d+;', inputs['Tile.h'])]
files['TileConstants.inc'] = '\n'.join(ids) + '\n'
files['TileUpdateFlags.inc'] = cut('Tile.h', '\tstatic const int UPDATE_NEIGHBORS', '\nprivate:') + '\n'
files['LevelDepth.inc'] = cut('Level.h', '\tstatic const int genDepthBits = 7;', '\n\tstatic const int constSeaLevel') + '\n'
files['ByteArray.inc'] = cut('ArrayWithLength.h', 'typedef arrayWithLength<byte> byteArray;', '\ntypedef arrayWithLength<char>') + '\n'
files['Pi.inc'] = cut('Definitions.h', '#define PI (3.141592654f)', '\n#define HALF_PI') + '\n'
# Exact original ore feature construction (BiomeDecorator::_init, ore lines only).
files['OreFeatures.inc'] = cut('BiomeDecorator.cpp', '\tdirtOreFeature = new OreFeature(Tile::dirt_Id, 32);',
                               '\n\tyellowFlowerFeature') + '\n'
# Exact original postProcess reseed chain; consumers before decorate() are omitted.
files['ChunkSeed.inc'] = cut('RandomLevelSource.cpp', '\tpprandom->setSeed(level->getSeed());', '\n\n\tbool hasVillage') + '\n'
# Exact original getChunk carver order (cave then canyon), before structures.
files['CarverOrder.inc'] = cut('RandomLevelSource.cpp', '\tcaveFeature->apply(this, level, xOffs, zOffs, blocks);',
                               '\n\tif (generateStructures)') + '\n'
manifest = {
    'schema': 1,
    'equivalence': ('original LargeCaveFeature/CanyonFeature carvers in getChunk order; original OreFeature '
                    'and BiomeDecorator::decorateOres; NOT full postProcess'),
    'inputs': pinned, 'segments': segments,
    'compiledFeatures': FEATURES,
    'rng': 'original_core original Random.cpp, including nextLong; no substitute RNG',
    'coordinates': 'SOURCE chunk coordinates [-worldSize/32, worldSize/32); ore origins xt*16, offsets nextInt(16)+8',
    'heights': {'genDepth': 128, 'seaLevel': 63, 'maxBuildHeight': 256},
    'schedule': ['getChunk: prepareHeights, buildSurfaces (terrain module), caveFeature->apply, canyonFeature->apply',
                 'postProcess: original pprandom reseed chain, then decorateOres() exactly'],
    'omittedRandomConsumers': ['structure postProcess', 'water/lava lakes', 'monster rooms',
                               'sand/clay/gravel surface features and everything after decorateOres'],
    'limitations': [
        'ore stream starts after the original reseed chain without the lake/lava/monster-room draws: '
        'vein counts, sizes and depth bands are original, positions differ from complete postProcess',
        'decoration (trees/plants) keeps its own reseeded stream and never sees ore writes',
        'each ore origin plan sees immutable pre-decoration input (carved base + structures) plus its own writes',
        'ore plans merge origin X then Z; a later origin never replaces an earlier ore cell',
        'ore getHeightmap is the first air above the topmost non-air pre-decoration tile',
        'no game-rule level-generation exclusions; app options always null',
        'carved biome topMaterial comes from the zoomed per-block biome of the chunk being carved',
        'original Random::nextLong has signed-left-shift UB; source unchanged'],
    'generated': {n: sha(s.encode()) for n, s in files.items()}}
# Verify source-to-output bytes too, not just source and output hashes separately.
for segment in segments:
    original = inputs[segment['source']][segment['startByte']:segment['endByteExclusive']]
    matches = []
    for name, text in files.items():
        offset = text.encode().find(original)
        if offset >= 0:
            matches.append({'file': name, 'startByte': offset, 'endByteExclusive': offset + len(original)})
    assert matches, 'source bytes changed during extraction: ' + segment['source']
    segment['outputMatches'] = matches
files['source-manifest.json'] = json.dumps(manifest, indent=2, ensure_ascii=False) + '\n'
check = '--check' in sys.argv
if not check:
    OUT.mkdir(exist_ok=True)
for n, s in files.items():
    path = OUT / n
    if check:
        assert path.read_bytes() == s.encode(), 'generated source differs: ' + n
    else:
        path.write_bytes(s.encode())
print(('Audited' if check else 'Extracted') + ' %d files, %d exact source byte ranges; carvers + ores' % (len(files), len(segments)))
