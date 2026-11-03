#!/usr/bin/env python3
"""Independent pinned, source-byte audited scattered closure. Writes ScatteredOriginal* only."""
import argparse, hashlib, json, pathlib, re, runpy
ROOT = pathlib.Path(__file__).resolve().parent
UP = ROOT.parent.parent / 'upstream' / 'Minecraft.World'
OUT = ROOT / 'generated'
# Reuse the frozen Village extractor's balanced-function reader, never its produce().
HELPER_SHA = '57bd981113595b6716d07bb28433c54c615bb6b1ed774827a2842a850e57cdb9'
helper = (ROOT / 'extract.py').read_bytes()
def sha(b): return hashlib.sha256(b).hexdigest()
if sha(helper) != HELPER_SHA:
    raise RuntimeError('Frozen extraction helper changed')
e = runpy.run_path(str(ROOT / 'extract.py'))
PINNED = e['PINNED']
PINNED.update({
 'ScatteredFeaturePieces.h':'69da9b840ca64fb06e4a0331779d460a53f768abf4365a2166812d2b9b6dfba8',
 'ScatteredFeaturePieces.cpp':'6b7975215110027f383c44d952ae132125682e1aa5e0d248a7ef4760dafa663d',
 'RandomScatteredLargeFeature.cpp':'87c5b0d6f3a9137b630719b32341990f29818e55347fefdd64340d3f22c86efd',
 'StructureFeature.cpp':'f97ac1f7ec9161788ba64790432c10e7759cc3b1920c1066fec91c048d9276c1',
 'StoneSlabTile.h':'22b8fae844cbea7b336b997afc4efb23744f48fe84fdc597efbd1ab01118d5c8',
 'TripWireSourceTile.h':'0095423a3c96faddd01f4c29453981448effe7edeeac40f12e52e813cbc37760',
 'TripWireTile.h':'41a056911f36f897ff8403ab51bf9b49033c1f5c56112a13c698673e6f5fa1ca',
 'SmoothStoneBrickTile.h':'78e86e0a709d40514c0b9569c2e0a239bb9b7ef04255db85a882f5ec4036b7ad',
 'FlowerPotTile.h':'7350274666ae38f63d544a1749d48414d3f5872ddb89c9843d20f67323c3dfbe',
 'StairTile.h':'65afb3f4e50e811befcf3a4d92556768eb074b71aa80594672d461ef32885c49',
 'TreeTile.h':'3dfe9c429847d72889010d56e714542daa098c60918910b9c5079181a5919952',
 'LeverTile.cpp':'bc5631104be24a8eaaa75b0b753055e1e9aff1c7544b198a70464cd31b0a15bd',
 'WeighedTreasure.cpp':'d82f8e7488893b103751a22e1b0cf5c320418ad5d1f32b879bf24b12454da8d7',
 'EnchantedBookItem.cpp':'2bca38e736ce1f1993aec3ece2ea6fc4a5a39a7526cc7915700c7c91da450ff0',
 'Enchantment.cpp':'bc323a01bb747e84017dbbee0da0d47dbf1645db78a3a4aa6364380af76b0985',
 'Item.h':'59b07341605d60b76271b3e6776e1dc3f250525726ac30bd086edbed93fc99bc',
 'WeighedRandom.cpp':'799bfeb44192712a9f5b80b268c114695aa324ccafb3624f0a4db5534765e9bb',
})
PINNED.update({'ProtectionEnchantment.cpp': 'c58939d8b7e4294ca34fec502fc0c19b5e6b6460fb7609a455229819e591b1c7', 'OxygenEnchantment.cpp': '7d79955f6b676d57d33ee946e6241737708f24340948190c6052e86da7842b3e', 'WaterWorkerEnchantment.cpp': 'edbde8dcd4603bc265b767cd0b15291864f20c6afb64af895d65711bd342ccfc', 'ThornsEnchantment.cpp': '0c3f6cfca4df9136779e0eef936edaa03452f4d98f304590175872cda3eaef0f', 'DamageEnchantment.cpp': 'f27103f96ee2d06de3c63c3895ef2aba083a8e1896e9c5a9cf40d5a00192ea03', 'KnockbackEnchantment.cpp': 'b8a124f0df9d1132f9bc8fae5de25f5eac8b3672c53b163e079408121bd1ac34', 'FireAspectEnchantment.cpp': '6de14a251d729a3277d27ac7559d77bc15afc1b5b1cebde2ad3ed4bee589ef87', 'LootBonusEnchantment.cpp': '58e42ac88fe5dc4f13a71cb70d14f5bfc962c7d698b13c51f09533fa1add076a', 'DiggingEnchantment.cpp': 'c117576e3b39195f0d5783d71fd9a531dc226aec811b32d48b74ccd4aed70cab', 'UntouchingEnchantment.cpp': '38c2fe80e369a05501ab551c1a946d766cab853ff71b20904f448959b3a1d0b8', 'DigDurabilityEnchantment.cpp': 'c77d3ddfc50949b6d0d97f9a2c48354ccff62483f332a4cdd679fa1b1dfad324', 'ArrowDamageEnchantment.cpp': '26ead604234d3a3f06a7fcf810a46160d7f2b606a2db0c4b3d4c1618760d6eca', 'ArrowKnockbackEnchantment.cpp': '012608b26b28316c85565e8b308949f6f66553899fafd429854a7f7d0e73e55b', 'ArrowFireEnchantment.cpp': 'bf8f2b4cd454b45588209fd4ec097d5d1dce84e8d12f7c7f3fbaa9d38e388fd6', 'ArrowInfiniteEnchantment.cpp': '4ff44947c65da057af962513b2ecb697db57e5823d70e6acd5736437fd9f0869'})
manifest = e['manifest']
manifest.clear()
manifest.update(scope='RandomScatteredLargeFeature / all three ScatteredFeaturePieces postProcess bodies',
 sources={}, extractions=[], outputs={}, transformations=[], helperSha256=HELPER_SHA,
 limitations=[
 'Canonical whole-piece postProcess and immutable bounded 128-region LRU, like Village: not original chunk decoration RNG or first-chunk ground averaging.',
 'Original LargeFeature chunk seed and clear-nextInt flow retained; original Level::getRandomFor salt 14357617, spacing 32, separation 8.',
 'Biome center sample is original full-resolution biome, IDs 2/17 desert,21/22 jungle,6 swamp; no Village containsOnly check.',
 'Chest/dispenser placement retained, original weighted/count/slot and enchanted-book RNG consumed, but no inventories or tile entities exposed.',
 'Original Witch construction/moveTo/finalizeMobSpawn/addEntity call sites retained with explicit SpawnRequest alias, no entity or AI instantiated.',
 'No persistence, app forced placement, superflat options, neighbor physics, redstone, explosions, lighting or loot functionality.',
 'Reuse frozen Village detail Level/BoundingBox/Tile constants only; all additional classes isolated in scattered_detail, no global upstream class ODR.'])
source=e['source']; functions=e['functions']; header=e['header']; record=e['record']
def replacement(name, text, old, new, why):
    if old not in text: raise RuntimeError('Missing patch '+why)
    # Track ORIGINAL source line/byte spans, not shifted generated offsets.
    original=source(name); start=original.index(old); end=start+len(old)
    manifest['transformations'].append(dict(source=name, byteStart=len(original[:start].encode()),
        byteEndExclusive=len(original[:end].encode()), lineStart=original[:start].count('\n')+1,
        lineEnd=original[:end].count('\n')+1, sha256=sha(old.encode()), replacement=new, reason=why))
    return text.replace(old,new)
def patch(name):
 def apply(fn,s):
    changes=[]
    if fn.endswith('ScatteredFeaturePiece') and 'LevelGenerationOptions *' in s:
        a=s.index('\tLevelGenerationOptions *'); b=s.index('\n\tswitch (orientation)',a)
        s=replacement(name,s,s[a:b],'','Remove only app forced orientation; original nextInt(4) remains'); changes.append('Remove forced orientation block; exact ranges in transformations')
    if fn.endswith('isFeatureChunk'):
        a=s.index('\tLevelGenerationOptions *'); b=s.index('\n\n',a)
        s=replacement(name,s,s[a:b],'','Remove only app forced placement; forcePlacement stays false'); changes.append('Remove forced placement block; exact ranges in transformations')
    if fn.endswith('_init'):
        line='\tswamphutEnemies.push_back(new Biome::MobSpawnerData(eTYPE_WITCH, 1, 1, 1));'
        s=replacement(name,s,line,'','No ambient mob-spawner registry; not entity/AI emulation'); changes.append('Omit ambient mob-spawn registry')
    if fn.endswith('createChest'):
        a=s.index('\t\t\tshared_ptr<ChestTileEntity>'); b=s.index('\n\t\t\treturn true;',a)
        s=replacement(name,s,s[a:b],'\t\t\tconsumeTreasure(random, treasure, numRolls, 27);','Replace inventory allocation/writes with original RNG-only treasure loop'); changes.append('Inventory omitted, RNG-only extracted loop retained')
    if fn.endswith('createDispenser'):
        a=s.index('\t\t\tshared_ptr<DispenserTileEntity>'); b=s.index('\n\t\t\treturn true;',a)
        s=replacement(name,s,s[a:b],'\t\t\tconsumeTreasure(random, items, numRolls, 9);','Replace inventory allocation/writes with original RNG-only treasure loop'); changes.append('Inventory omitted, RNG-only extracted loop retained')
    return s,changes
 return apply

def span(name, pattern):
 s=source(name); hit=re.search(pattern,s,re.S)
 if not hit: raise RuntimeError('Missing source range '+name+' '+pattern)
 return record(name,hit.start(),hit.end(),hit[0],[])

def audit_projection(name, original, projected, reason):
 # Exact original line/byte spans for EVERY removed/replaced line in adapters.
 import difflib
 s=source(name); base=s.index(original); lines=original.splitlines(True)
 output=projected.splitlines(True)
 for op,a,b,c,d in difflib.SequenceMatcher(a=lines,b=output,autojunk=False).get_opcodes():
  if op=='equal': continue
  start=base+sum(map(len,lines[:a])); end=base+sum(map(len,lines[:b]))
  manifest['transformations'].append(dict(source=name,operation=op,
   byteStart=len(s[:start].encode()),byteEndExclusive=len(s[:end].encode()),
   lineStart=s[:start].count('\n')+1,lineEnd=s[:end].count('\n')+1,
   sha256=sha(s[start:end].encode()),replacement=''.join(output[c:d]),reason=reason))

COMPAT = r'''
using detail::BoundingBox; using detail::Facing; using detail::Math; using detail::Mth; using detail::BaseRailTile;
using detail::DirectionalTile; using std::list; using std::vector; using std::wstring;
using std::shared_ptr; using std::max;
struct Direction: detail::Direction { static const int STEP_X[4], STEP_Z[4]; };
struct Tile: detail::Tile {
    explicit Tile(int n):detail::Tile(n) {}
    static Tile* chest;
    static detail::Tile* tiles[256];
};
struct LeverTile { static int getLeverFacing(int); };
struct SpawnRequest {
    double x=0,y=0,z=0; float yaw=0,pitch=0; bool finalized=false;
    template<class L> explicit SpawnRequest(L*) {}
    void moveTo(double a,double b,double c,float d,float e) {x=a;y=b;z=c;yaw=d;pitch=e;}
    void finalizeMobSpawn(std::nullptr_t) {finalized=true;}
};
// Deliberately a request value, NOT a Witch entity or a stub claiming original AI.
using Witch = SpawnRequest;
struct Biome {
    int id; explicit Biome(int n):id(n) {}
    static Biome *desert,*desertHills,*jungle,*jungleHills,*swampland;
};
class BiomeSource {
    std::function<int(int,int)> biome_;
public:
    explicit BiomeSource(std::function<int(int,int)> f):biome_(f) {}
    Biome* getBiome(int,int);
};
class Level: public detail::Level {
    BiomeSource biomes_;
public:
    vector<SpawnRequest> spawnRequests;
    Level(int64_t seed,int size,std::function<int(int,int)> b,std::function<int(int,int)> g,
          std::function<uint8_t(int,int,int)> r):detail::Level(seed,size,b,g,r,{}),biomes_(b) {}
    BiomeSource* getBiomeSource() {return &biomes_;}
    Biome* getBiome(int x,int z) {return biomes_.getBiome(x,z);}
    void addEntity(shared_ptr<SpawnRequest> request) {spawnRequests.push_back(*request);}
};
struct WeighedTreasure {
    int minCount,maxCount,randomWeight;
    WeighedTreasure(int,int,int lo,int hi,int weight):minCount(lo),maxCount(hi),randomWeight(weight) {}
    static struct WeighedTreasureArray addToTreasure(struct WeighedTreasureArray,WeighedTreasure*);
};
struct WeighedTreasureArray {
    vector<WeighedTreasure*> values;
    shared_ptr<WeighedTreasure> extra;
    unsigned int length;
    WeighedTreasureArray(WeighedTreasure** p,int n):values(p,p+n),length(n) {}
    WeighedTreasure* operator[](size_t n) const {return values[n];}
};
struct WeighedRandom {
    static int getTotalWeight(WeighedTreasureArray);
    static WeighedTreasure* getRandomItem(Random*,WeighedTreasureArray,int);
    static WeighedTreasure* getRandomItem(Random*,WeighedTreasureArray);
};
void consumeTreasure(Random*,WeighedTreasureArray,int,int);
struct BookRandom { WeighedTreasure* createForRandomTreasure(Random*); };
'''

def produce():
 # Verify all contextual sources too, including RNG/seed-flow evidence.
 for n in PINNED: source(n)
 h=COMPAT
 for cls,name,fields in [
  ('SandStoneTile','SandStoneTile.h',['TYPE_DEFAULT','TYPE_HEIROGLYPHS','TYPE_SMOOTHSIDE']),
  ('StoneSlabTile','StoneSlabTile.h',['SAND_SLAB']),
  ('TripWireSourceTile','TripWireSourceTile.h',['MASK_ATTACHED']),
  ('TripWireTile','TripWireTile.h',['MASK_ATTACHED']),
  ('SmoothStoneBrickTile','SmoothStoneBrickTile.h',['TYPE_DETAIL']),
  ('FlowerPotTile','FlowerPotTile.h',['TYPE_MUSHROOM_RED']),
  ('StairTile','StairTile.h',['DIR_NORTH','DIR_WEST','DIR_EAST','DIR_SOUTH']),
  ('TreeTile','TreeTile.h',['DARK_TRUNK'])]:
  h+='struct '+cls+' {\n'+''.join(span(name,r'static const int '+field+r'\s*=\s*[^;]+;')+'\n' for field in fields)+'};\n'
 h+='struct DyePowderItem { static const int ORANGE,BLUE; };\n'
 h+='struct Item { static BookRandom* enchantedBook;\n'
 for field in ['diamond','ironIngot','goldIngot','emerald','bone','rotten_flesh','saddle','horseArmorMetal','horseArmorGold','horseArmorDiamond','arrow']:
  h+=span('Item.h',r'static const int '+field+r'_Id\s*=\s*\d+;')+'\n'
 h+='};\n'
 h+=header('StructurePiece.h',[r'CompoundTag',r'^class Random;',r'GetType\(',r'isInChunk\(',r'getLocatorPosition\(',r'edgesLiquid\(',r'createDoor\('])
 h+=header('ScatteredFeaturePieces.h',[r'CompoundTag',r'GetType\(',r'\*Create\(',r'loadStatic\('])
 h+='''
class StructureStart {
public:
 list<StructurePiece*> pieces;
 StructureStart(); StructureStart(int,int); ~StructureStart();
 BoundingBox* getBoundingBox(); void calculateBoundingBox();
protected: BoundingBox* boundingBox; int chunkX,chunkZ;
};
class RandomScatteredLargeFeature {
public:
 Level* level; Random* random; int spacing,minSeparation;
 static vector<Biome*> allowedBiomes;
 static void staticCtor(); void _init(); RandomScatteredLargeFeature();
 bool isFeatureChunk(int,int,bool=false);
 StructureStart* createStructureStart(int,int);
 class ScatteredFeatureStart: public StructureStart {
 public: ScatteredFeatureStart(Level*,Random*,int,int);
 };
};
'''
 body=functions('StructurePiece.cpp',exclude=[r'createTag$',r'load$',r'isInChunk$',r'getLocatorPosition$',r'edgesLiquid$',r'createDoor$'],patch=patch('StructurePiece.cpp'))
 body+=functions('StructureStart.cpp',include=[r'StructureStart$',r'~StructureStart$',r'getBoundingBox$',r'calculateBoundingBox$'])
 body+=functions('ScatteredFeaturePieces.cpp',exclude=[r'loadStatic$',r'addAdditonalSaveData$',r'readAdditonalSaveData$'],patch=patch('ScatteredFeaturePieces.cpp'))
 # Original static treasure tables and selector: no synthetic geometry or probabilities.
 for pat in [r'WeighedTreasure \*ScatteredFeaturePieces::DesertPyramidPiece::treasureItems.*?\n\};',
             r'WeighedTreasure \*ScatteredFeaturePieces::JunglePyramidPiece::treasureItems.*?\n\};',
             r'WeighedTreasure \*ScatteredFeaturePieces::JunglePyramidPiece::dispenserItems.*?\n\};',
             r'ScatteredFeaturePieces::JunglePyramidPiece::MossStoneSelector ScatteredFeaturePieces::JunglePyramidPiece::stoneSelector;']:
  body+=span('ScatteredFeaturePieces.cpp',pat)+'\n'
 body+='vector<Biome*> RandomScatteredLargeFeature::allowedBiomes;\n'
 feature=functions('RandomScatteredLargeFeature.cpp',include=[r'staticCtor$',r'_init$',r'RandomScatteredLargeFeature$',r'isFeatureChunk$',r'createStructureStart$',r'ScatteredFeatureStart$'],patch=patch('RandomScatteredLargeFeature.cpp'))
 # Deliberately not extracting app options/reflection ctor overloads.
 for pattern in [r'RandomScatteredLargeFeature::RandomScatteredLargeFeature\(unordered_map.*?\n\}',r'RandomScatteredLargeFeature::ScatteredFeatureStart::ScatteredFeatureStart\(\).*?\n\}']:
  feature=re.sub(pattern,'',feature,flags=re.S)
 manifest['extractions'][:]=[x for x in manifest['extractions'] if not (x['source']=='RandomScatteredLargeFeature.cpp' and any(t in (UP/x['source']).read_bytes()[x['byteStart']:x['byteEndExclusive']].decode() for t in ['unordered_map','ScatteredFeatureStart::ScatteredFeatureStart()']))]
 body+=feature
 body+=functions('LeverTile.cpp',include=[r'getLeverFacing$'])
 for field in ['ORANGE','BLUE']:
  body+=span('DyePowderItem.cpp',r'const int DyePowderItem::'+field+r'\s*=\s*[^;]+;')+'\n'
 for field in ['X','Z']:
  body+=span('Direction.cpp',r'const int Direction::STEP_'+field+r'\[\]\s*=\s*\{.*?\};')+'\n'
 # RNG-only weighted treasure closure, mechanically projected from the original.
 weights=functions('WeighedRandom.cpp',include=[r'getTotalWeight$',r'getRandomItem$'])
 # Keep only array overloads (vector overloads are not part of this closure).
 weights=re.sub(r'(?:int\s+|WeighedRandomItem\s*\*\s*)WeighedRandom::[^{}]*vector<.*?\n\}', '',weights,flags=re.S)
 original_weights=source('WeighedRandom.cpp'); original_weights=original_weights[original_weights.index('int WeighedRandom::getTotalWeight(WeighedRandomItemArray'):]
 weights=weights.replace('WeighedRandomItemArray','WeighedTreasureArray').replace('WeighedRandomItem *','WeighedTreasure *').replace('DEBUG_BREAK();','assert(false);')
 manifest['extractions'][:]=[x for x in manifest['extractions'] if not (x['source']=='WeighedRandom.cpp' and b'vector<' in (UP/x['source']).read_bytes()[x['byteStart']:x['byteEndExclusive']])]
 manifest['transformations'].append(dict(source='WeighedRandom.cpp',reason='Array-only overloads; WeighedRandomItemArray/Item replaced by same-layout treasure adapter; DEBUG_BREAK replaced assert(false). No RNG changes.'))
 audit_projection('WeighedRandom.cpp',original_weights,weights.strip(),'RNG-only weighted array type substitution; original draws unchanged')
 body+=weights
 loot=functions('WeighedTreasure.cpp',include=[r'addChestItems$'])
 original_loot=loot.strip()
 loot=loot.replace('void WeighedTreasure::addChestItems(Random *random, WeighedTreasureArray items, shared_ptr<Container> dest, int numRolls)','void consumeTreasure(Random *random, WeighedTreasureArray items, int numRolls, int slots)')
 loot=loot.replace('*((WeighedRandomItemArray *)&items)','items').replace('treasure->item->getMaxStackSize()','64')
 loot=re.sub(r'^\s*shared_ptr<ItemInstance> copy =[^;]*;|^\s*copy->count =[^;]*;','',loot,flags=re.M)
 loot=loot.replace('dest->setItem(random->nextInt(dest->getContainerSize()), copy);','random->nextInt(slots);')
 manifest['transformations'].append(dict(source='WeighedTreasure.cpp',reason='addChestItems projected to consumeTreasure: remove ItemInstance copies/count writes; retain original weighted/count/slot draws. All source treasure counts <=7 and all nonstackable entries count=1, so original stack-size branch is always true. Slots=27 chest,9 dispenser.'))
 audit_projection('WeighedTreasure.cpp',original_loot,loot.strip(),'Inventory-only projection; exact removed/replaced source lines')
 body+=loot
 # Preserve book RNG draws; only selection-dependent max level affects the RNG.
 enc=source('Enchantment.cpp'); levels=[]
 for match in re.finditer(r'\w+ = new (\w+Enchantment)\((\d+),',enc):
  cls,ident=match[1],int(match[2]); file=cls+'.cpp'
  # Derived level bodies pinned separately below.
  text=source(file); level=re.search(r'::getMaxLevel\(\)\s*\{\s*return (\d+);',text)
  levels.append((ident,int(level[1]) if level else 1))
 levels.sort(); body+='static const int enchantmentMaxLevels[]={'+','.join(str(x[1]) for x in levels)+'};\n'
 book=functions('EnchantedBookItem.cpp',include=[r'createForRandomTreasure$'])
 # Keep only three-argument implementation; one-arg forwarding supplied by adapter.
 start=book.index('WeighedTreasure *EnchantedBookItem::createForRandomTreasure(Random *random, int')
 book=book[start:]
 original_book=book.strip()
 book=book.replace('EnchantedBookItem::createForRandomTreasure(Random *random, int minCount, int maxCount, int weight)','BookRandom::createForRandomTreasure(Random *random)')
 book=book.replace('Enchantment *enchantment = Enchantment::validEnchantments[random->nextInt(Enchantment::validEnchantments.size())];','int enchantment = enchantmentMaxLevels[random->nextInt(sizeof(enchantmentMaxLevels)/sizeof(*enchantmentMaxLevels))];')
 book=re.sub(r'^\s*shared_ptr<ItemInstance> book =[^;]*;','',book,flags=re.M)
 book=book.replace('enchantment->getMinLevel(), enchantment->getMaxLevel()','1, enchantment')
 book=re.sub(r'^\s*addEnchantment\([^;]*;','',book,flags=re.M)
 book=book.replace('new WeighedTreasure(book, minCount, maxCount, weight)','new WeighedTreasure(0, 0, 1, 1, 1)')
 manifest['transformations'].append(dict(source='EnchantedBookItem.cpp',reason='Keep enchantment index and Mth inclusive level draws with exact sorted original enchantment max levels; remove ItemInstance/tag allocation, return RNG-only treasure weight=1/count=1.'))
 audit_projection('EnchantedBookItem.cpp',original_book,book.strip(),'RNG-only book adapter; exact removed/replaced source lines')
 body+=book
 body+='''
WeighedTreasureArray WeighedTreasure::addToTreasure(WeighedTreasureArray a,WeighedTreasure* extra) {
 a.extra.reset(extra); a.values.push_back(extra); ++a.length; return a;
}
namespace {
Biome biomeTable[]={Biome(2),Biome(17),Biome(21),Biome(22),Biome(6)};
Biome otherBiome(-1);
Tile chestStorage(54); BookRandom bookStorage;
DirectionalTile repeaterStorage(93);
}
Biome* Biome::desert=&biomeTable[0]; Biome* Biome::desertHills=&biomeTable[1];
Biome* Biome::jungle=&biomeTable[2]; Biome* Biome::jungleHills=&biomeTable[3]; Biome* Biome::swampland=&biomeTable[4];
Biome* BiomeSource::getBiome(int x,int z) {int id=biome_(x,z);for(auto& b:biomeTable) if(b.id==id) return &b;return &otherBiome;}
Tile* Tile::chest=&chestStorage;
detail::Tile* Tile::tiles[256]={};
BookRandom* Item::enchantedBook=&bookStorage;
struct InitializeScattered {
 InitializeScattered() {Tile::tiles[93]=&repeaterStorage;RandomScatteredLargeFeature::staticCtor();}
} initializeScattered;
'''
 prefix='// Generated by extract-scattered.py; see ScatteredOriginal-manifest.json. Do not edit.\n'
 products={'ScatteredOriginal.h':prefix+'#pragma once\n#include "Original.h"\n#include <memory>\nnamespace original_structures { namespace scattered_detail {\n'+h+'\n}}\n',
           'ScatteredOriginal.cpp':prefix+'#include "ScatteredOriginal.h"\nnamespace original_structures { namespace scattered_detail {\n'+body+'\n}}\n'}
 for n,s in products.items(): manifest['outputs'][n]=sha(s.encode())
 products['ScatteredOriginal-manifest.json']=json.dumps(manifest,indent=2,sort_keys=True)+'\n'
 return products

if __name__=='__main__':
 args=argparse.ArgumentParser(); args.add_argument('--check',action='store_true'); args=args.parse_args()
 products=produce()
 if args.check:
  bad=[n for n,s in products.items() if not (OUT/n).exists() or (OUT/n).read_bytes()!=s.encode()]
  if bad: raise SystemExit('Scattered source-byte audit failed: '+', '.join(bad))
 else:
  for n,s in products.items(): (OUT/n).write_bytes(s.encode())
 print('Scattered source-byte audit OK' if args.check else 'Extracted original scattered closure')
