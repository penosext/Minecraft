#!/usr/bin/env python3
"""Byte-exact local feature extraction. No network; --check never writes."""
import hashlib
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parent
UP = ROOT.parent.parent / 'upstream' / 'Minecraft.World'
OUT = ROOT / 'generated'
FEATURES = ['Feature', 'TreeFeature', 'BirchFeature', 'PineFeature', 'SpruceFeature',
            'MegaTreeFeature', 'SwampTreeFeature', 'GroundBushFeature', 'BasicTree',
            'FlowerFeature', 'TallGrassFeature', 'ReedsFeature', 'CactusFeature']
MATERIALS = ['MaterialColor', 'Material', 'GasMaterial', 'LiquidMaterial',
             'DecorationMaterial', 'PortalMaterial', 'WebMaterial']
BIOMES = ['Biome', 'ForestBiome', 'TaigaBiome', 'JungleBiome', 'SwampBiome',
          'PlainsBiome', 'DesertBiome', 'BeachBiome', 'MushroomIslandBiome',
          'IceBiome', 'ExtremeHillsBiome', 'HellBiome', 'TheEndBiome', 'RainforestBiome']
NAMES = set(['BiomeDecorator.cpp', 'RandomLevelSource.cpp', 'Random.cpp', 'Random.h',
             'Tile.h', 'Tile.cpp', 'Level.h', 'Level.cpp', 'LevelChunk.cpp', 'Definitions.h',
             'Mth.cpp', 'Mth.h', 'Direction.cpp', 'Direction.h', 'TreeTile.h', 'LeafTile.h',
             'VineTile.h', 'TallGrass.h', 'Bush.cpp', 'ReedTile.cpp', 'CactusTile.cpp'])
for n in FEATURES + MATERIALS:
    NAMES.add(n + '.h')
    if (UP / (n + '.cpp')).exists(): NAMES.add(n + '.cpp')
NAMES.update(n + '.cpp' for n in BIOMES)
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
        if data[end] == ord('{'): depth += 1
        elif data[end] == ord('}'): depth -= 1
        end += 1
    return cut(n, a, end)

files = {}
headers = ['// Generated exact original declarations; do not edit.\n#pragma once\n']
for n in MATERIALS + FEATURES:
    if n == 'SwampTreeFeature':
        # The redundant global Random forward declaration would shadow the
        # linked original_core ::Random inside our private namespace.
        headers.append(cut(n + '.h', 'class SwampTreeFeature', len(inputs[n + '.h'])))
    else:
        headers.append(body(n + '.h'))
files['OriginalDeclarations.inc'] = '\n'.join(headers)
# Test-only second compilation of untouched feature bodies against a dense,
# independent Level container rather than the callback/overlay adapter.
files['ReferenceDeclarations.inc'] = '\n'.join(headers[len(MATERIALS) + 1:])
files['ReferenceBodies.inc'] = '\n'.join(body(n + '.cpp') for n in FEATURES[:9])
cpp = ['#include "Platform.h"\nnamespace original_decoration { namespace detail {\n']
for n in MATERIALS + FEATURES:
    if n + '.cpp' in inputs: cpp.append(body(n + '.cpp'))
cpp.append('const float Mth::sinScale = 65536.0f / (float) (PI * 2);\nfloat *Mth::_sin = nullptr;')
for signature in ['void Mth::init()', 'float Mth::sin(float i)', 'float Mth::cos(float i)',
                  'int Mth::floor(float v)', 'int Mth::floor(double v)']:
    cpp.append(function('Mth.cpp', signature))
for signature in ['bool Bush::mayPlaceOn(int tile)', 'bool Bush::canSurvive(Level *level',
                  'bool ReedTile::mayPlace(Level *level', 'bool ReedTile::canSurvive(Level *level',
                  'bool CactusTile::canSurvive(Level *level']:
    name = signature.split('::')[0].split()[-1]
    cpp.append(function(name + '.cpp', signature))
for n in ['Biome', 'ForestBiome', 'TaigaBiome', 'JungleBiome', 'SwampBiome', 'RainforestBiome']:
    cpp.append(function(n + '.cpp', 'Feature *' + n + '::getTreeFeature(Random *random)'))
for n in ['Biome', 'JungleBiome']:
    cpp.append(function(n + '.cpp', 'Feature *' + n + '::getGrassFeature(Random *random)'))
cpp.append('} }\n')
files['Original.cpp'] = '\n\n'.join(cpp)
# Literal original constants, without pulling rendering/entity dependencies.
constants = []
for cls, file, pattern in [
    ('Tile', 'Tile.h', r'static const int \w+_Id\s*=\s*\d+;'),
    ('TreeTile', 'TreeTile.h', r'static const int (?:DARK_TRUNK|BIRCH_TRUNK|JUNGLE_TRUNK|FACING_[XYZ])\s*=\s*[^;]+;'),
    ('LeafTile', 'LeafTile.h', r'static const int (?:NORMAL_LEAF|EVERGREEN_LEAF|BIRCH_LEAF|JUNGLE_LEAF)\s*=\s*[^;]+;'),
    ('TallGrass', 'TallGrass.h', r'static const int (?:TALL_GRASS|FERN)\s*=\s*[^;]+;'),
    ('VineTile', 'VineTile.h', r'static const int VINE_\w+\s*=\s*[^;]+;')]:
    declarations = []
    for m in re.finditer(pattern.encode(), inputs[file]):
        declarations.append(cut(file, m.start(), m.end()))
    files[cls + 'Constants.inc'] = '\n'.join(declarations) + '\n'
# Defaults and subclass overrides are taken from the original constructors.
fields = ['treeCount', 'flowerCount', 'grassCount', 'reedsCount', 'cactusCount']
def assignments(n, prefix=''):
    result = []
    pattern = rb'(?:decorator->)?(?:' + b'|'.join(f.encode() for f in fields) + rb')\s*=\s*-?\d+;'
    for m in re.finditer(pattern, inputs[n]):
        text = cut(n, m.start(), m.end()).replace('decorator->', '')
        result.append('c.' + text)
    return '\n'.join(result)
schedule = ['Counts counts(int id) { Counts c;\n' + assignments('BiomeDecorator.cpp') + '\nswitch(id) {']
for ids, n in [([1], 'PlainsBiome'), ([2,17], 'DesertBiome'), ([4,18], 'ForestBiome'),
               ([5,19], 'TaigaBiome'), ([6], 'SwampBiome'), ([14,15], 'MushroomIslandBiome'),
               ([16], 'BeachBiome'), ([21,22], 'JungleBiome')]:
    schedule += [' '.join('case %d:' % i for i in ids), assignments(n + '.cpp'), 'break;']
schedule.append('} return c; }')
files['Counts.inc'] = '\n'.join(schedule) + '\n'
# Preserve the exact tree/flower/grass/reed/cactus loops and their random ordering.
files['Trees.inc'] = cut('BiomeDecorator.cpp', '\tint forests = treeCount;', '\tPIXEndNamedEvent();\n\n\tPIXBeginNamedEvent(0,"Decorate mushrooms')
files['FlowersGrass.inc'] = cut('BiomeDecorator.cpp', '\tfor (int i = 0; i < flowerCount;', '\tPIXEndNamedEvent();\n\tPIXBeginNamedEvent(0,"Decorate bush')
files['Reeds.inc'] = cut('BiomeDecorator.cpp', '\tfor (int i = 0; i < reedsCount;', '\tif (random->nextInt(32)')
files['Cactus.inc'] = cut('BiomeDecorator.cpp', '\tfor (int i = 0; i < cactusCount;', '\n\n\n\tPIXEndNamedEvent();')
files['ChunkSeed.inc'] = cut('RandomLevelSource.cpp', '\tpprandom->setSeed(level->getSeed());', '\n\n\tbool hasVillage')
manifest = {
    'schema': 1, 'equivalence': 'original-feature subset schedule; NOT full BiomeDecorator or postProcess',
    'inputs': pinned, 'segments': segments,
    'compiledFeatures': FEATURES, 'rng': 'original_core original Random.cpp, including nextLong; no substitute RNG',
    'coordinates': 'SOURCE [-worldSize/2,worldSize/2), chunk*16, biome at origin+16, tree offsets nextInt(16)+8',
    'heights': {'genDepth': 128, 'maxBuildHeight': 256, 'PineAndSwampCeiling': 128},
    'schedule': ['original postProcess reseed chain', 'original tree count/bonus/species/init/place loop',
                 'optional original flower/grass loops', 'optional original reedsCount and unconditional 10 reeds loops',
                 'optional original cactus loop'],
    'omittedRandomConsumers': ['structures', 'water/lava lakes', 'monster rooms', 'ores', 'sand/clay/gravel',
                               'huge mushrooms', 'dead bush/waterlily/mushrooms', 'pumpkin', 'springs',
                               'biome extra decorate (jungle vines, desert wells, emeralds)', 'mobs/ice/snow/schematics'],
    'limitations': ['omitted stages do not consume RNG: positions differ from complete postProcess',
                    'each origin plan sees immutable pre-decoration input (base + optional structures) plus its own writes, never other origin plans',
                    'pre-decoration callbacks must never call final chunks or include decoration/player edits',
                    'outer-edge biome origin+16 sample is clamped to the finite world edge',
                    'plans merge origin X then Z, later writes win except leaves/plants never replace trunks',
                    'no game-rule level-generation exclusions; app options always null',
                    'Overworld only: biome IDs 8 and 9 return empty',
                    'helper Tile registry covers terrain/emitted IDs; no full Level lighting/ticks/rendering',
                    'plant sunlight uses direct sky transmission, no propagated skylight engine',
                    'original BasicTree plain char is compiled with -fsigned-char (original assumes -1)',
                    'original Random::nextLong has signed-left-shift UB; source unchanged, UBSan shift excluded'],
    'generated': {n: sha(s.encode()) for n, s in files.items()}}
# Verify source-to-output bytes too, not just source and output hashes separately.
for segment in segments:
    original = inputs[segment['source']][segment['startByte']:segment['endByteExclusive']]
    matches = []
    for name, text in files.items():
        offset = text.encode().find(original)
        if offset >= 0:
            matches.append({'file': name, 'startByte': offset, 'endByteExclusive': offset + len(original)})
    segment['outputMatches'] = matches
    # Counts are explicitly transformed (decorator->field => c.field); all
    # compiled feature bodies and declarations must remain exact slices.
    if segment['source'].split('.')[0] in FEATURES + MATERIALS:
        assert matches, 'feature bytes changed during extraction: ' + segment['source']
files['source-manifest.json'] = json.dumps(manifest, indent=2, ensure_ascii=False) + '\n'
check = '--check' in sys.argv
if not check: OUT.mkdir(exist_ok=True)
for n, s in files.items():
    path = OUT / n
    if check:
        assert path.read_bytes() == s.encode(), 'generated source differs: ' + n
    else: path.write_bytes(s.encode())
print(('Audited' if check else 'Extracted') + ' %d files, %d exact source byte ranges; SUBSET schedule' % (len(files), len(segments)))
