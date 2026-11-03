#!/usr/bin/env python3
"""Source-byte extraction; only structures/generated is written. No gameplay stubs."""
import argparse, hashlib, json, pathlib, re
ROOT = pathlib.Path(__file__).resolve().parent
UP = ROOT.parent.parent / 'upstream' / 'Minecraft.World'
OUT = ROOT / 'generated'
PINNED = {
    'BaseRailTile.h': '53734c1fbc41910bcf10f8488fca8c78e05da4d110c53ebf986ba369b25361bf',
    'BiomeSource.cpp': 'ed4e9325a8a181c90da869f130a04c971d10ab46054ddab39e2e02e17b98e134',
    'BoundingBox.cpp': 'ef978f6cb6959cc6d5b480a063fc297f0ca26ef65b88d4ff356ad6a2977e160e',
    'BoundingBox.h': '75af1d1f311c8a8c36350521e2c72c775089e1344276525fb2304fc106b0c40e',
    'Dimension.cpp': '2ab8358f69657284a12dbe7d1d4f5ae17d2941fd592df704a6603f412225c9f3',
    'Direction.cpp': 'b3ae610f9f8f68e9b1c28de2a06eea32f1255297d2e24672ff23ec9c1978d5e3',
    'DirectionalTile.h': 'c2c8d4b4f6fa994cc07130448a724a3a049ef6f23f43f5121076525f45081f0d',
    'DoorItem.cpp': '35a1430d31affc8e998d2604c35c9b561fccf042c461eb273cbbf9bfb23fbe20',
    'DyePowderItem.cpp': 'ee56e201d95c98e6a815eb68e79e929bbbd73157cb9ceeacf46b89d0c3a471b3',
    'Facing.cpp': 'c47361c60be17ab1568615a7df826a32f2f89aa59ce5b96da74786c97297e6a0',
    'LargeFeature.cpp': 'e6b7fe955cba91199f16a748d5c97f53dff4a81a3d471f3511fb6125f5745bb8',
    'Level.cpp': '1f0b5d54179e6dcd7defa4a61b39c16449203c2be5ed5d508e5506f6bf367543',
    'Material.cpp': '656f96a06266e6bebf17c0f07f2139cd78a14a8b9c0793aca15efedf08e2e6cb',
    'Mth.cpp': '3b86fd381c759a4e69c717530c9e83a3fd537bac7be37c4a085d71f3394fd911',
    'Random.cpp': 'd0a0952991ca9cace3200fa709a8fea8aeb8f7f3144dcac2a5869cbd97a38193',
    'SandStoneTile.h': '6f176bae1bd6b10fe440fa7e1c7749542a3ea00516236bce752ec281c3ea62da',
    'StructurePiece.cpp': '6328b2888d6c559cc4dd643f9af0c27f54bcac423072a6ae6b51ab674e017ab6',
    'StructurePiece.h': 'a777a0778312387aa5c0e3f17f135462b5f839b1136f8aeac7ff145187f73597',
    'StructureStart.cpp': 'af1b4aef19905cf171d7861a99af985c0793bb64d9579ad3601cc303c0660eda',
    'Tile.h': 'b7afa5c8740ebc0b834b6b6c355edc1162600c2b9557508d44dadaffedc9420a',
    'VillageFeature.cpp': 'a7b0f9cc420531a3657439ec0c43371df731adefb4035efee695a7c6ef797339',
    'VillagePieces.cpp': 'b749d3c46644d146df3a2d8e75c18bcdc1b7530df626a9319ad56562fab00237',
    'VillagePieces.h': '793b88999695fe1a9c33377c2e815c5c09ce9e237390e49cb56e78d9c5a4b30e'
}
manifest = {'scope': 'source-derived VillageFeature/VillageStart/all VillagePieces',
 'limitations': ['No CompoundTag persistence, entities/villagers, chest inventory/loot, neighbor physics or lighting',
 'No ScatteredFeature yet', 'Default size=0, spacing=32 (16 for worlds <128 chunks), no superflat/options/forced placement',
 'Whole-village postProcess over BASE ground, then immutable clipped emission; not vanilla chunk decoration RNG/height scheduling',
 'BiomeSource::containsOnly uses optional original quarter Layer callback; fallback full biome at q*4 is approximate'],
 'sources': {}, 'extractions': [], 'outputs': {}}
def digest(b): return hashlib.sha256(b).hexdigest()
def source(name):
    b = (UP/name).read_bytes()
    if name not in PINNED or digest(b)!=PINNED[name]:
        raise RuntimeError('Pinned upstream source hash changed: '+name)
    manifest['sources'][name] = {'bytes': len(b), 'sha256': digest(b)}
    return b.decode('utf-8-sig')
def record(name, start, end, text, changes):
    raw = (UP/name).read_bytes()
    # All selected sources are ASCII or UTF-8 without BOM in extracted ranges.
    s = source(name)
    a = len(s[:start].encode()); b = len(s[:end].encode())
    manifest['extractions'].append({'source': name, 'byteStart': a, 'byteEndExclusive': b,
        'sha256': digest(raw[a:b]), 'adaptations': changes})
    return text
def masked(s):
    return re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"',
        lambda m: ''.join('\n' if c=='\n' else ' ' for c in m[0]), s, flags=re.S)
def functions(name, exclude=(), include=None, patch=None):
    s = source(name); m = masked(s); out=[]
    # Match top-level definitions, including multi-line parameter lists/initializers.
    pattern = r'^([^\n#{};]*\b(?:[A-Za-z_]\w*::)+~?\w+\s*\([^;{}]*?\)[^;{}]*?)\s*\{'
    previous_end=0
    for hit in re.finditer(pattern, m, re.M):
        if hit.start()<previous_end: continue
        start=hit.start(); opening=m.index('{',hit.start()); depth=1; end=opening+1
        while depth:
            depth += (m[end]=='{')-(m[end]=='}'); end+=1
        previous_end=end
        signature=s[start:opening].strip()
        fname=re.search(r'((?:\w+::)+~?\w+)\s*\(', signature)[1]
        if any(re.search(x,fname) for x in exclude): continue
        if include is not None and not any(re.search(x,fname) for x in include): continue
        text=s[start:end]; changes=[]
        if patch: text,changes=patch(fname,text)
        out.append(record(name,start,end,text,changes))
    if not out: raise RuntimeError('No functions '+name)
    return '\n\n'.join(out)+'\n'
def header(name, exclude):
    s=source(name); lines=[]
    for line in s.splitlines(True):
        if line.startswith('#include') or line.startswith('#pragma'): continue
        if any(re.search(p,line) for p in exclude): continue
        lines.append(line)
    return record(name,0,len(s),''.join(lines),['Remove platform includes and unsupported declarations: '+str(exclude)])

def village_patch(fname,s):
    changes=[]
    old=s
    s=re.sub(r'^\s*spawnVillagers\([^;]*;','',s,flags=re.M)
    if s!=old: changes.append('Remove entity/villager call; entity support not provided')
    if fname=='VillagePieces::Smithy::postProcess':
        s=s.replace('createChest(level, chunkBB, random, 5, 1, 5, treasureItems, 3 + random->nextInt(6));',
            'random->nextInt(6); // original roll count draw; loot omitted\n\t\t\tplaceBlock(level, Tile::chest_Id, 0, 5, 1, 5, chunkBB);')
        changes.append('Chest block only; original roll-count draw retained, loot generation omitted')
    if fname=='VillagePieces::StartPiece::StartPiece' and 'this->pieceSet = pieceSet;' in s:
        s=s.replace('this->pieceSet = pieceSet;', 'this->pieceSet = pieceSet;\n        ownedPieceWeights.assign(pieceSet->begin(), pieceSet->end());')
        changes.append('Track ownership of all original weights, including exhausted weights removed from selection list')
    if fname=='VillagePieces::StartPiece::~StartPiece':
        s=s.replace('for(auto& it : *pieceSet)', 'for(auto& it : ownedPieceWeights)')
        changes.append('Free exhausted weights too; no RNG or selection change')
    return s,changes

def feature_patch(fname,s):
    changes=[]
    if fname=='VillageFeature::isFeatureChunk':
        a=s.index('\t\tLevelGenerationOptions *'); b=s.index('\n\n',a)
        s=s[:a]+s[b:]; changes.append('Remove app forced-placement options; forcePlacement remains false')
    if fname=='VillageFeature::createStructureStart':
        s=re.sub(r'^\s*app\.AddTerrainFeaturePosition[^;]*;','',s,flags=re.M)
        changes.append('Remove app notification only')
    # Signed negative left shifts are UB: exact two-complement multiply instead.
    if '<< 4' in s:
        s=s.replace('(chunkX << 4)', '(chunkX * 16)').replace('(chunkZ << 4)', '(chunkZ * 16)')
        changes.append('Negative chunk signed shift replaced by defined multiply by 16')
    return s,changes

def produce():
    excluded=[r'CompoundTag',r'GetType\(',r'\*Create\(',r'WeighedTreasureArray',r'loadStatic\(',r'staticCtor\(',r'spawnVillagers\(',r'getVillagerProfession\(']
    headers=header('BoundingBox.h',[r'intArray',r'IntArrayTag'])
    headers+=header('StructurePiece.h',[r'CompoundTag',r'^class Random;',r'GetType\(',r'isInChunk\(',r'getLocatorPosition\(',r'edgesLiquid\(',r'createChest\(',r'createDispenser\('])
    headers+=header('VillagePieces.h',excluded).replace('list<PieceWeight *> *pieceSet;', 'list<PieceWeight *> *pieceSet;\n        vector<PieceWeight*> ownedPieceWeights; // adapter ownership of exhausted weights')
    manifest['extractions'][-1]['adaptations'].append('Add ownedPieceWeights vector for exhausted-weight lifetime; original fields and constants preserved')
    headers+='''
class StructureStart {
public:
    std::list<StructurePiece*> pieces;
    StructureStart(); ~StructureStart();
    BoundingBox* getBoundingBox(); std::list<StructurePiece*>* getPieces();
    void calculateBoundingBox();
protected:
    BoundingBox* boundingBox; int chunkX, chunkZ;
};
class VillageFeature {
public:
    Level* level; Random* random;
    int villageSizeModifier, townSpacing, minTownSeparation, m_iXZSize;
    static std::vector<Biome*> allowedBiomes;
    static void staticCtor(); void _init(int); VillageFeature(int);
    bool isFeatureChunk(int,int,bool=false);
    class VillageStart: public StructureStart {
        bool valid; int m_iXZSize;
    public:
        VillageStart(Level*,Random*,int,int,int,int); bool isValid();
    };
    StructureStart* createStructureStart(int,int);
};
'''
    body=functions('BoundingBox.cpp',exclude=[r'createTag$',r'toString$'],patch=None)
    # intArray constructor excluded separately: its signature is not a supported runtime API.
    body=re.sub(r'BoundingBox::BoundingBox\(intArray sourceData\).*?\n\}', '', body, flags=re.S)
    # Keep extraction manifest honest about the omitted serialization constructor.
    manifest['extractions']=[e for e in manifest['extractions'] if not (e['source']=='BoundingBox.cpp' and 'intArray' in (UP/e['source']).read_bytes()[e['byteStart']:e['byteEndExclusive']].decode())]
    body+=functions('StructurePiece.cpp',exclude=[r'createTag$',r'load$',r'isInChunk$',r'getLocatorPosition$',r'edgesLiquid$',r'createChest$',r'createDispenser$'])
    body+=functions('VillagePieces.cpp',exclude=[r'loadStatic$',r'staticCtor$',r'addAdditonalSaveData$',r'readAdditonalSaveData$',r'spawnVillagers$',r'getVillagerProfession$'],patch=village_patch)
    body+='std::vector<Biome*> VillageFeature::allowedBiomes;\n'
    body+=functions('VillageFeature.cpp',include=[r'staticCtor$',r'_init$',r'VillageFeature$',r'isFeatureChunk$',r'createStructureStart$',r'VillageStart$',r'isValid$'],patch=feature_patch)
    # Options constructor and reflection ctor are outside the deliberately narrow facade.
    body=re.sub(r'VillageFeature::VillageFeature\(unordered_map.*?\n\}', '', body, flags=re.S)
    body=re.sub(r'VillageFeature::VillageStart::VillageStart\(\).*?\n\}', '', body, flags=re.S)
    manifest['extractions']=[e for e in manifest['extractions'] if not (e['source']=='VillageFeature.cpp' and any(x in (UP/e['source']).read_bytes()[e['byteStart']:e['byteEndExclusive']].decode() for x in ['unordered_map','VillageStart::VillageStart()']))]
    body+=functions('StructureStart.cpp',include=[r'StructureStart$',r'~StructureStart$',r'getBoundingBox$',r'getPieces$',r'calculateBoundingBox$'])
    # Avoid unsupported unused (x,z) ctor overload in the narrowed declaration.
    body=re.sub(r'StructureStart::StructureStart\(int x, int z\).*?\n\}', '', body, flags=re.S)
    manifest['extractions']=[e for e in manifest['extractions'] if not (e['source']=='StructureStart.cpp' and 'StructureStart(int x' in (UP/e['source']).read_bytes()[e['byteStart']:e['byteEndExclusive']].decode())]
    body+=functions('DoorItem.cpp',include=[r'place$'])
    body+=functions('Mth.cpp',include=[r'nextInt$'])
    body+=functions('Level.cpp',include=[r'getRandomFor$'])
    # Extract numerical TileID constants, never app catalog IDs.
    tile=source('Tile.h'); constants=re.findall(r'static const int \w+_Id = \d+;',tile)
    for name in ['BiomeSource.cpp','Dimension.cpp','LargeFeature.cpp','Random.cpp','Direction.cpp','Facing.cpp','SandStoneTile.h','BaseRailTile.h','DyePowderItem.cpp','Material.cpp','DirectionalTile.h']:
        source(name)
    # Record exact constant lines and their byte spans.
    for hit in re.finditer(r'static const int \w+_Id = \d+;',tile):
        record('Tile.h',hit.start(),hit.end(),hit[0],[])
    tiles='struct Tile {\n virtual ~Tile() {}\n int id; explicit Tile(int n):id(n) {}\n static const int UPDATE_CLIENTS=2;\n'+'\n'.join(constants)+'\nstatic Tile *rail,*ladder,*button,*door_wood;\nstatic Tile* tiles[256];\n};\nstruct DirectionalTile: Tile { explicit DirectionalTile(int n):Tile(n) {} };\n'
    prefix='// Generated from port/upstream; see source-manifest.json. Do not hand edit.\n'
    products={'Original.h':prefix+'#pragma once\n#include "Platform.h"\nnamespace original_structures { namespace detail {\n'+tiles+headers+'\n}}\n',
              'Original.cpp':prefix+'#include "Original.h"\nnamespace original_structures { namespace detail {\n'+body+'\n}}\n'}
    manifest['adapterPolicy'] = {
        'plan': 'FULL original VillageStart and VillagePieces selection/queues; NOT a rewritten plan',
        'seed': 'LargeFeature original two signed Random halves nextLong value computed with uint64 shifts to avoid upstream signed-left-shift UB; addFeature clear-key nextInt retained',
        'worldSize': 'Public blocks; LevelData/Dimension getXZSize return blocks/16, original VillageStart bound parameter supplied in blocks',
        'height': 'BASE first-air-above-solid; one full-world chunkBB per village before clipping, not first-generated-chunk slice',
        'random': 'Whole-plan postProcess continues start RNG; NOT original per-chunk decoration RNG',
        'materials': 'Door hinge solidBlocking table for village IDs, conservative opaque fallback for unknown base IDs; only IDs8..11 treated liquid',
        'metadata': 'Original TileID and 4-bit data unchanged; WHITE=15; original orientation and DoorItem::place helpers',
        'lifetime': 'Exhausted piece weights owned by StartPiece adapter; no selection/RNG change',
        'coordinates': 'Source-absolute output; caller maps +worldSize/2 and applies baseline -> structures -> edits',
        'missing': ['CompoundTag persistence', 'loot/inventory', 'villagers/entities', 'neighbor updates/lighting/tile ticking', 'ScatteredFeature'],
    }
    manifest['adapterFiles'] = {n:digest((ROOT/n).read_bytes()) for n in ['Platform.h','Platform.cpp','Structures.h','Structures.cpp']}
    for name,s in products.items(): manifest['outputs'][name]=digest(s.encode())
    products['source-manifest.json']=json.dumps(manifest,indent=2,sort_keys=True)+'\n'
    return products
if __name__=='__main__':
    args=argparse.ArgumentParser(); args.add_argument('--check',action='store_true'); args=args.parse_args()
    products=produce()
    if args.check:
        bad=[n for n,s in products.items() if not (OUT/n).exists() or (OUT/n).read_bytes()!=s.encode()]
        if bad: raise SystemExit('Structures source audit failed: '+', '.join(bad))
    else:
        OUT.mkdir(parents=True,exist_ok=True)
        for n,s in products.items(): (OUT/n).write_bytes(s.encode())
    print('Structures source-byte audit OK' if args.check else 'Extracted original village closure')
