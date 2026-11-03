#!/usr/bin/env python3
"""Offline, pinned upstream extraction. --check never writes any input/output."""
import argparse
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parent
UPSTREAM = ROOT.parent.parent / 'upstream' / 'Minecraft.World'
COMMIT = '09df8928eeb15328bca499d225e48ea95af6d119'
PINNED = {
    'RandomLevelSource.cpp': '16ffda342fc843167ee4114748e86717ae89aff12c18fb5cad4eb31b7215346c',
    'RandomLevelSource.h': '5b3f8ed463bd596cc1e54d5600170b57f58f745f688d70fc60248e4d883462ca',
    'Vec3.cpp': '6dbc5447726fcd4550576b87e16da29107046684cc0c2a0a0885ad7183a598a3',
    'Vec3.h': 'f53ec3276e70f1b41162b576cb3bf4e29f2bfd4e5b229fc4b9ea8da0e6550abb',
    'Tile.h': 'b7afa5c8740ebc0b834b6b6c355edc1162600c2b9557508d44dadaffedc9420a',
    'Level.h': '5c775affc30abbdb4e98e6458cc418810cef71f2abcecbc939f2b1354c42c078',
    'Biome.cpp': '4403d31956dfc20cabbc56516abab1f5c595de71c6535d06a682861800e55143',
    'ArrayWithLength.h': '8ae25b36b2811843c436e2a1b4f5270a30a58c30ef05c884900d6643419ba1bf',
    'Mth.cpp': '3b86fd381c759a4e69c717530c9e83a3fd537bac7be37c4a085d71f3394fd911',
    'Mth.h': '406b4b6168e78f0aa1489802106ed58ede7bd4ad12ad80afa00d68c409b1148c',
    'ChunkSource.h': '7fec3e37c928f0af7ba4cb72c6e6acc25bf380125ed7c23289109722475448d8',
}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def unique(data, marker):
    if data.count(marker) != 1:
        raise ValueError(f'Nonunique marker {marker!r}: {data.count(marker)}')
    return data.index(marker)


def generate():
    source = {}
    for name, pinned in PINNED.items():
        data = (UPSTREAM / name).read_bytes()
        if sha(data) != pinned:
            raise ValueError(f'Pinned upstream SHA-256 mismatch: {name}')
        source[name] = data
    output, records = {}, []

    def add(name, data):
        output[name] = output.get(name, b'') + data

    def copy(out, src, begin, end, purpose):
        data = source[src]
        start = len(output.get(out, b''))
        add(out, data[begin:end])
        records.append({
            'source': src, 'source_byte_range': [begin, end],
            'source_lines_inclusive': [data[:begin].count(b'\n') + 1,
                                       data[:end-1].count(b'\n') + 1],
            'output': out, 'output_byte_range': [start, start + end-begin],
            'sha256': sha(data[begin:end]), 'purpose': purpose,
        })

    def function(out, src, signature):
        data = source[src]
        begin = unique(data, signature)
        close = re.search(rb'^\}', data[begin:], re.MULTILINE)
        if not close:
            raise ValueError(f'Missing function end {signature!r}')
        copy(out, src, begin, begin + close.end(), signature.decode())
        add(out, b'\n\n')

    def line(out, src, marker):
        data = source[src]
        begin = unique(data, marker)
        end = data.index(b'\n', begin) + 1
        copy(out, src, begin, end, marker.decode())

    cpp = 'TerrainOriginal.cpp'
    add(cpp, b'#include "Adapter.h"\n\nnamespace original_terrain_detail {\n\n')
    # The entire original conditional is preserved, including the non-selected
    # small-world implementation. _LARGE_WORLDS is fixed by CMake, not rewritten.
    data = source['RandomLevelSource.cpp']
    edge = unique(data, b'int RandomLevelSource::getMinDistanceToEdge')
    begin = data.rfind(b'#ifdef _LARGE_WORLDS', 0, edge)
    end = unique(data, b'#endif // _LARGE_WORLDS') + len(b'#endif // _LARGE_WORLDS')
    copy(cpp, 'RandomLevelSource.cpp', begin, end, 'Both original edge branches; select _LARGE_WORLDS=1')
    add(cpp, b'\n\n')
    for sig in (b'void RandomLevelSource::prepareHeights(',
                b'void RandomLevelSource::buildSurfaces(',
                b'doubleArray RandomLevelSource::getHeights('):
        function(cpp, 'RandomLevelSource.cpp', sig)
    for marker in (b'int g_numPrepareHeightCalls = 0;',
                   b'LARGE_INTEGER g_totalPrepareHeightsTime = {0,0};',
                   b'LARGE_INTEGER g_averagePrepareHeightsTime = {0, 0};'):
        # Definitions need to precede prepareHeights; use a separate include.
        line('TerrainCounters.inc', 'RandomLevelSource.cpp', marker)
    output[cpp] = output[cpp].replace(b'namespace original_terrain_detail {\n\n',
        b'namespace original_terrain_detail {\n\n#include "TerrainCounters.inc"\n\n', 1)
    # Account for the include insertion in output offsets.
    for record in records:
        if record['output'] == cpp:
            record['output_byte_range'] = [n + len(b'#include "TerrainCounters.inc"\n\n')
                                            for n in record['output_byte_range']]
    function(cpp, 'Biome.cpp', b'float Biome::getTemperature()')
    for sig in (b'Vec3::ThreadStorage::ThreadStorage()',
                b'Vec3::ThreadStorage::~ThreadStorage()',
                b'Vec3 *Vec3::newTemp(', b'Vec3 *Vec3::set(',
                b'double Vec3::dot(', b'double Vec3::length()',
                b'Vec3* Vec3::closestPointOnLine(', b'double Vec3::distanceFromLine('):
        function(cpp, 'Vec3.cpp', sig)
    for sig in (b'float Mth::sqrt(float x)', b'float Mth::sqrt(double x)'):
        function(cpp, 'Mth.cpp', sig)
    add(cpp, b'} // namespace original_terrain_detail\n')

    data = source['RandomLevelSource.cpp']
    begin = unique(data, b'\tm_XZSize = level->getLevelData()->getXZSize();')
    end = data.index(b'\tcaveFeature = ', begin)
    copy('TerrainConstructor.inc', 'RandomLevelSource.cpp', begin, end,
         'Terrain-only ctor segment: world size and original moat conditional')
    begin = data.index(b'\tthis->level = level;', unique(data, b'RandomLevelSource::RandomLevelSource('))
    end = unique(data, b'\tforestNoise = new PerlinNoise(random, 8);') + len(b'\tforestNoise = new PerlinNoise(random, 8);')
    copy('TerrainConstructor.inc', 'RandomLevelSource.cpp', begin, end,
         'Terrain-only ctor segment: level/random/pprandom and every original noise in original order')
    add('TerrainConstructor.inc', b'\n')
    line('TerrainChunkSeed.inc', 'RandomLevelSource.cpp', b'random->setSeed(xOffs * 341873128712l + zOffs * 132897987541l);')

    for marker in (b'typedef arrayWithLength<byte> byteArray;',
                   b'typedef arrayWithLength<float> floatArray;',
                   b'typedef arrayWithLength<Biome *> BiomeArray;'):
        line('TerrainArrays.inc', 'ArrayWithLength.h', marker)
    data = source['Level.h']
    begin = unique(data, b'\tstatic const int genDepthBits = 7;')
    end = data.index(b'\n', unique(data, b'\tstatic const int constSeaLevel = genDepth / 2 - 1;')) + 1
    copy('TerrainLevelConstants.inc', 'Level.h', begin, end, 'Original generation depth and sea-level constants')
    hdr = 'TerrainExtracted.h'
    add(hdr, b'#pragma once\n')
    for marker in (b'#define LEVEL_WIDTH_CLASSIC 54', b'#define LEVEL_WIDTH_SMALL 64',
                   b'#define LEVEL_WIDTH_MEDIUM (3*64)'):
        line(hdr, 'ChunkSource.h', marker)
    add(hdr, b'\nnamespace original_terrain_detail {\nclass Mth : public ::Mth {\npublic:\n')
    for marker in (b'static float sqrt(float x);', b'static float sqrt(double x);'):
        line(hdr, 'Mth.h', marker)
    add(hdr, b'};\nclass Tile {\npublic:\n')
    for name in ('stone', 'grass', 'dirt', 'unbreakable', 'calmWater', 'sand', 'sandStone', 'ice'):
        line(hdr, 'Tile.h', f'static const int {name}_Id = '.encode())
    add(hdr, b'};\n} // namespace original_terrain_detail\n')

    # Verify every emitted byte range, not merely hashes of synthesized output.
    for record in records:
        a, b = record['source_byte_range']
        c, d = record['output_byte_range']
        if source[record['source']][a:b] != output[record['output']][c:d]:
            raise ValueError(f'Extraction byte mismatch: {record}')
    # Individually enumerate the selected complete definitions as well as the
    # enclosing unmodified conditional block, so constructor segments cannot be
    # mistaken for a complete original constructor.
    functions = []
    data = source['RandomLevelSource.cpp']
    signatures = [b'int RandomLevelSource::getMinDistanceToEdge(',
                  b'float RandomLevelSource::getHeightFalloff(',
                  b'void RandomLevelSource::prepareHeights(',
                  b'void RandomLevelSource::buildSurfaces(',
                  b'doubleArray RandomLevelSource::getHeights(']
    for signature in signatures:
        begin = data.index(signature) # first falloff is the selected large-world branch
        close = re.search(rb'^\}', data[begin:], re.MULTILINE)
        end = begin + close.end()
        parent = next(r for r in records if r['source'] == 'RandomLevelSource.cpp'
                      and r['output'] == cpp and r['source_byte_range'][0] <= begin
                      and r['source_byte_range'][1] >= end)
        offset = parent['output_byte_range'][0] + begin - parent['source_byte_range'][0]
        if output[cpp][offset:offset+end-begin] != data[begin:end]:
            raise ValueError(f'Complete function byte mismatch: {signature!r}')
        functions.append({'signature': data[begin:].splitlines()[0].decode(),
                          'source_byte_range': [begin, end],
                          'output_byte_range': [offset, offset+end-begin],
                          'sha256': sha(data[begin:end])})
    manifest = {
        'schema': 1, 'commit': COMMIT,
        'selected_complete_terrain_functions': functions,
        'range_convention': 'zero-based half-open bytes; one-based inclusive lines',
        'sources': {name: {'sha256': PINNED[name], 'bytes': len(data)} for name, data in sorted(source.items())},
        'generated': {name: {'sha256': sha(data), 'bytes': len(data)} for name, data in sorted(output.items())},
        'exact_segments': records,
        'local_adapters': {name: sha((ROOT / name).read_bytes())
                           for name in ('Generator.h', 'Adapter.h', 'Adapter.cpp', 'CMakeLists.txt')},
        'fixed_build': ['_LARGE_WORLDS=1', 'FLOATING_ISLANDS=false (upstream)',
                        'not __PS3__; no SPU simulation', 'C++11', '-fwrapv',
                        '-ffp-contract=off', '-fno-fast-math', '-UNDEBUG'],
        'scope': 'Original base density and surface generation only, before caves/canyons/structures/postProcess',
        'constructor': 'NOT the complete original constructor: only exact source segments (size/moats and level/random/pprandom/all noise). Original empty pows initialized lazily by exact getHeights.',
        'adaptations': ['namespace original_terrain_detail isolates all game shells',
                        'global Random, PerlinNoise, arrayWithLength and Mth::lfloor come from original_core',
                        'local Mth sqrt overloads are exact extractions; no handwritten math replacement',
                        'Vec3 geometry/newTemp and pool ctor/dtor exact; TLS lookup uses C++ thread_local storage',
                        'LARGE_INTEGER timing union and chrono-counter boundary, not terrain logic',
                        'BiomeSource callback ownership bridge, no biome algorithm implementation or default',
                        'app boundary returns nullptr: original default settings, no biome overrides',
                        'getChunk is NOT ported; wrapper uses exact seed line then prepareHeights -> zoomed biomes -> buildSurfaces',
                        'original buildSurfaces biome/block transposition retained, no seam correction'],
        'not_ported': ['complete original constructor/destructor', 'Level/LevelChunk runtime',
                       'caves', 'canyons', 'structures', 'postprocessing/decorations/ores',
                       'physics', 'lighting', 'save', 'game options', 'app'],
        'claim': 'Source-byte identity and native deterministic build, NOT binary equivalence to an unavailable console executable',
    }
    output['source-manifest.json'] = (json.dumps(manifest, indent=2, sort_keys=True) + '\n').encode()
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    path = args.output.resolve()
    if path == ROOT or not path.is_relative_to(ROOT):
        parser.error('--output must be below original-core/terrain')
    files = generate()
    if not args.check:
        path.mkdir(parents=True, exist_ok=True)
    for name, data in files.items():
        target = path / name
        if args.check:
            if not target.is_file() or target.read_bytes() != data:
                raise ValueError(f'Generated byte audit failed: {target}')
        elif not target.is_file() or target.read_bytes() != data:
            target.write_bytes(data)
    print(f'{"Audited" if args.check else "Generated"} {len(files)} terrain files; pinned SHA-256 and all original byte ranges verified')


if __name__ == '__main__':
    main()
