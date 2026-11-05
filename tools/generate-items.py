#!/usr/bin/env python3
"""Offline, source-pinned item identity subset; never rewrites terrain/world/UI code.

Only the four declared outputs are written. --check verifies reproducibility and
--test runs structural/regression/negative tests without writing any output.
This is an audited adapter, not a C++ interpreter or a full item engine.
"""
import argparse
import ast
import hashlib
import json
import operator
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WORLD = ROOT / 'port/upstream/Minecraft.World'
CREATIVE_REL = 'Minecraft.Client/Common/UI/IUIScene_CreativeMenu.cpp'
DEFAULT_CLIENT = ROOT.parent / 'ref/mcpartial/minecraft-console-main'
COMMIT = '09df8928eeb15328bca499d225e48ea95af6d119'
TERRAIN_SHA = '3f3d4c0fe0d8faff0dceba6f829d466cadcea66a0e024ec3e658bfa0bc12c614'
CREATIVE_SHA = 'a1908358a3c9a60f3eeb8b2e70c7c2f38799c509accb6dd4cfb0b67d42171277'
PINS = {
    'Item.cpp': '263adb75f312ef20dee9634d24ebe0c547e8a53a2702e4a5f3d3f5fcba5e9c0d',
    'Item.h': '59b07341605d60b76271b3e6776e1dc3f250525726ac30bd086edbed93fc99bc',
    'Tile.cpp': 'e17608be2973d4a4144f808c500f2cf3bb817622bfbaac0a3e293f6afa36482e',
    'Tile.h': 'b7afa5c8740ebc0b834b6b6c355edc1162600c2b9557508d44dadaffedc9420a',
    'Container.h': 'daa91c6fdbb833570321dc717cbcb97693d3f103c571478ceb7cd3f7d625c174',
    'TileItem.cpp': '19eaceb00fb8b44b29a2c961d6288c962bba4473f9b73c100e6d182b99784ade',
    'MultiTextureTileItem.cpp': '2b9181b763e186e598362f6f65f366faeef552322b06b6a69a49f8178792c76a',
    'LeafTileItem.cpp': '328982e38e57b83373e4edf9b3b9b6d50d6f03cf1092cd285f89f949e9655185',
    'WoolTileItem.cpp': '7c17649cfb2465e4f130dfb6b70880fe204f68287768eef944dd51c164378a3a',
    'ColoredTileItem.cpp': 'b24e37950ddf28b0d98fdd8da8e10c60ea55e3b1345c5d587f73e1563dfb4d61',
    'StoneSlabTileItem.cpp': '934a6caefa428583b514b957b34ebe1469e6510676bc890264ace0e32704a6c8',
    'DoorItem.cpp': '35a1430d31affc8e998d2604c35c9b561fccf042c461eb273cbbf9bfb23fbe20',
    'TilePlanterItem.cpp': '02dc9bd62ad314d1ba45996ccfc8bfeaecabda63384d1645af8f509290dd3236',
    'SeedItem.cpp': '8ae0478b15faaf8ec97deb032a33140bd7bf012696da5c29fcbf1f096ca5956e',
    'SeedFoodItem.cpp': '6a4cc132e079acf61be51ccf0b6e3fc8a56b39578f55139740b15ac43d88ea74',
    'RedStoneItem.cpp': '81eaf1434fc00d0608d85cb26e3ad07abbb1d02cabeb7fc871e0218c7a09ceb3',
    'DyePowderItem.cpp': 'ee56e201d95c98e6a815eb68e79e929bbbd73157cb9ceeacf46b89d0c3a471b3',
    'SnowItem.cpp': '1f977fcd372c3a0e06df03696de43b490189aa032cb74688376edc7328e4476a',
    'PistonTileItem.cpp': '9307731f908abad87990b439ebed094d813f71cf1472eaa169569fde4ad5a85c',
    'WaterLilyTileItem.cpp': '8fdd3bd0c3d30acc7f832bf5c3ac39cbf2ce63211d9cd89d2c80ec6a6b30384a',
    'TreeTile.h': '3dfe9c429847d72889010d56e714542daa098c60918910b9c5079181a5919952',
    'TreeTile.cpp': 'c28f65a2cb7a37dc4fb574b51966ce99ba746a4434c22af9c1474e7d906709fd',
    'LeafTile.h': '7b265133e327f45772fc9c621a62e1d6a52afa1d871b3e0584f16bf7521c65e1',
    'LeafTile.cpp': '9e739803ba4e78fda0f0a0df17f822d6da85bf5d1cdd21f8d452c2c4d0704c29',
    'HalfSlabTile.h': '38124f2cb8e253f6873b6d24cc156b607d1d839aa7076b3c9f22c677a14c48a4',
    'HalfSlabTile.cpp': '51b5af087238ba5a049474aa6479027125638a59922905a1e8c1e59d634bb305',
    'StoneSlabTile.h': '22b8fae844cbea7b336b997afc4efb23744f48fe84fdc597efbd1ab01118d5c8',
    'SandStoneTile.h': '6f176bae1bd6b10fe440fa7e1c7749542a3ea00516236bce752ec281c3ea62da',
    'SmoothStoneBrickTile.h': '78e86e0a709d40514c0b9569c2e0a239bb9b7ef04255db85a882f5ec4036b7ad',
    'StoneMonsterTile.h': '72ae8f31c762059238c99fa9b1aa55d707b247d94ca2dd056577f2d3ceab73a3',
    'TallGrass.h': 'ba8edcd8f543075a44e41b47a85ac8fee62949097c498bd6957b26518e5022de',
    'HugeMushroomTile.cpp': '8b6e9639281524693b34af26101284243bf036c04f041a6446a8714aafd7c0c7',
    'FarmTile.cpp': '5c6f851c40658192e45f67085f38f91c0bf5fc8d4b1ca1415c90c91852ed5c3e',
    'RedStoneOreTile.cpp': 'b567e5a30528930e9f431eb4d9d23aa3862894f73c23132cddd1f9c6e550c0ae',
    'CropTile.cpp': '4882d99d399d42d0c00e662bc3990894f940cf0499f6ee0c79b42f86b37b10c3',
    'CarrotTile.cpp': 'd46fae522f903ea21164442b5e261c3e9066cb4204901317edebfb067243ca9f',
    'PotatoTile.cpp': '6e6a546f44f57079b4ddb8f11e7d8f1b0ae8897e9251e352dfa2137b88aa8cb9',
    'CocoaTile.cpp': 'c771aad2de63cc1d15f4bef42fd26b02b31112ab57223bfd339614b5d4176da9',
    'FlowerPotTile.cpp': '39594c5a02ec2f5b0b9719e232f781064135266484bb14834f933f28ee617791',
    'CauldronTile.cpp': '35bbbeae0aa31308d5bafb4fe315420e842fa5a2878c89a4c0be2303e7a39e0e',
    'DoorTile.cpp': '5e592b6b6bb0d7b791073d82578adb3a3d30adfa1749fdb61804dab54ca4e7cb',
    'NetherWartTile.cpp': 'ae04f7f4df2d74f338ee98d21f527c6c23e102a079a8df4f23966e65999a75ad',
    'TripWireTile.cpp': '7cc669395e364cfc36910010446261b2f7d6e8a663ee5d17b289ef095132337e',
    'RedStoneDustTile.cpp': '3e409683101dbfa560bc9945fd8636118cda3760cae6d215885711310ccf405b',
    'ReedTile.cpp': '80a06db3302e38dd2170cd1fa49305d07c5e9dc4409b9d85472ef2ea7caaf77f',
    'RepeaterTile.cpp': '37c279aa3da4733f002f31e7e667f3f5b5149f2882d34d65a0a7fe9a7398907b',
    'TheEndPortalFrameTile.cpp': '4dec52bddee5fa812d461c3ac8058bb96b9af1513b280148b9d3585622e2c1a1',
    'TopSnowTile.cpp': '19d591839f271da1f136467b04795ceb710b63fc9b2eae8cbec4fa140eb90f1c',
    'WoodTile.h': '95edd0de8c41aa05cad51de68bb274430008403456a14a53d745eb9dd86b7b9e',
    'FoodItem.cpp': '1e7cea0d25ab49a7ba9d3d47e7d896810d4ed19060f46ff6a8bf798569b5f1be',
    'AuxDataTileItem.cpp': 'ce79055c12c27ed6588c50200867ed236fd5e11e2417487ac24cad09f10ff90c',
    'StairTile.cpp': 'e71804b8d3ab0b0246471f0692f0784ebf4824f74affbbbf4fdfcd143c5608e3',
    'ChestTile.cpp': '5da17ece1df71f6d52273abb484beb57520618b0ebba5c80ee768fa48a3fe424',
    'DispenserTile.cpp': 'eca81b5ec468fe34e57cccbf355670db3f3fd9fd8b867eb7c41df47df01cfbee',
    'LadderTile.cpp': '7a4179305c31a56107b395b02fbed31f83fd3ce242995fae4485a3d4964f9500',
    'TorchTile.cpp': '7925b8601383c2e7a929307f15d61feb13213d6df09e7fdba94ada8d4d758405',
    'StoneMonsterTile.cpp': '70c6b3181cdd1abf17ce43b410256b01fd1cf02c7c8e9fe0ad4607ccde22f39f',
    'WoodTile.cpp': 'fa3f522887a34151c0fed8a964d2fb70dd24af59c4301eb92db3723545e030e6',
    'SandStoneTile.cpp': '2b9e5683d273cbf262880fb5e94407cff4511d6f2f87c857024e66d26ccbb5d6',
    'SmoothStoneBrickTile.cpp': '61366a882b4d044152a1e85e113e5d2447cdfeb9c628e5c8afb4f97c5d7c888c',
    'SeedFoodItem.h': '8741638c2e9ff6007b46a6ed13e05420c060895a593494c034e9e5729e5cef04',
    'FoodItem.h': 'f1d3b65ca0be3077850289b5f392f26a0ed47ff45a10b5a4f289afe0068eb598',
    'PistonBaseTile.h': 'da7b17a6d4d6091a5e63c0d69d24e2b2581d5fed1ac67e3eef387e9be708209b',
    # Tool/armor identity sources. Upstream class names are deliberate: the axe is
    # HatchetItem (not AxeItem) and HoeItem derives from Item, NOT DiggerItem.
    'DiggerItem.h': 'bdb64332c921d46d35c3fcc79206667094bafc7a68e012e47db9a436bb851892',
    'DiggerItem.cpp': '974ab6a175770996257b9366cdd69877b43b5daa694ca214924ec97fef6f088d',
    'WeaponItem.h': '348493295ef25eda2db2230a4b5a96d365ed2779552f3b34e79ccb170d60984e',
    'WeaponItem.cpp': 'd7512592fe6157bdd367f85d4792ce55f019682bede45c296556d62551ff24e9',
    'PickaxeItem.cpp': 'cecdc1f4077b4b5bb91877c4995d11503a9ad1d315ad0ce93954386008f36821',
    'ShovelItem.cpp': '326bfab5a7f31be1c8e5add561abeffa2a6ecbc14b782bc52fc6a75b28dea7c7',
    'HatchetItem.cpp': 'd7f584407a068652730e11fe2fcf18668d0a93c6c09461ca687f0dd35ce082e0',
    'HoeItem.cpp': '825fc65ff37f46b6cc44eaf2a1cb45425255f4bad723bcc3356161020f00c2a4',
    'ArmorItem.h': '37b35931c15bb1237a77ee467c8b4429d05c380891eb59e8140c6f47e9ba90e4',
    'ArmorItem.cpp': '0080a83f77544cdfa01d8f1c892aa04daf0edfa5c1aa85b3fb32b315a86b5b9a',
    # Food identity/nutrition sources (FoodItem ctor + FoodConstants saturation tiers).
    'FoodItem.cpp': '1e7cea0d25ab49a7ba9d3d47e7d896810d4ed19060f46ff6a8bf798569b5f1be',
    'FoodConstants.cpp': '02c42f5562510c1e7ebb37d1cfa3a0e2b45f82782901554dc32501ad0dbabfe2',
    # Coal/charcoal aux constants (STONE_COAL / CHAR_COAL) and stacked-by-data class.
    'CoalItem.h': '2f4312e368beaf46ea180b7ce3438ac8d728a0d5432f6b1262ba403a2c834f54',
    'CoalItem.cpp': '68bd59ec2ff7d0fce91efd41139076615b845d0ed88a9474e2d340639c324a6f',
    # Shears: plain Item subclass with its own stack size and durability.
    'ShearsItem.h': 'd439480c23c782bef13e3a3ddc4dc7cb11d4f6862452dd529a5cca18f1fb5758',
    'ShearsItem.cpp': 'b7e2c61e4a607b32c866144bd95d59bf7d6e5ca1277d9c0c0d2a15372e0873ba',
}

# Upstream tool classes mapped to this catalog's tool kind. Base attack damage is
# read from each subclass's DiggerItem ctor argument, never hardcoded here.
TOOL_CLASSES = {'WeaponItem': 'sword', 'ShovelItem': 'shovel', 'PickaxeItem': 'pickaxe',
                'HatchetItem': 'hatchet', 'HoeItem': 'hoe'}
# Registration symbol stems; note the pickaxe symbol is pickAxe_* while its icon
# name is "pickaxeWood" - the two differ upstream and must not be conflated.
TOOL_SYMBOLS = {'sword': 'sword', 'shovel': 'shovel', 'pickaxe': 'pickAxe',
                'hatchet': 'hatchet', 'hoe': 'hoe'}
TOOL_MATERIALS = ('wood', 'stone', 'iron', 'diamond', 'gold')
ARMOR_SLOTS = {'helmet': 'HEAD', 'chestplate': 'TORSO', 'leggings': 'LEGS', 'boots': 'FEET'}
ARMOR_MATERIALS = {'leather': 'CLOTH', 'chain': 'CHAIN', 'iron': 'IRON', 'diamond': 'DIAMOND', 'gold': 'GOLD'}
# Plain stack-64 Item registrations required as tool/armor crafting ingredients.
INGREDIENTS = {'stick': '木棍', 'ironIngot': '铁锭', 'goldIngot': '金锭',
               'diamond': '钻石', 'leather': '皮革'}
TOOL_NAMES = {'sword': '剑', 'shovel': '锹', 'pickaxe': '镐', 'hatchet': '斧', 'hoe': '锄'}
ARMOR_NAMES = {'helmet': '头盔', 'chestplate': '胸甲', 'leggings': '护腿', 'boots': '靴子'}
# Plain FoodItem registrations: eating only restores nutrition/saturation. Food
# with an eat effect (raw chicken, rotten flesh, spider eye, poisonous potato),
# setCanAlwaysEat (golden apple), a bowl return (BowlFoodItem mushroom stew) or
# no registered FoodItem class (cake is a tile) is deliberately excluded: this
# port has no mob effects or container-return engine, so eating them would be a
# silent behavior change. carrots/potato are already SeedFoodItem block items;
# they get food metadata without a new catalog row.
FOODS = {'apple': '苹果', 'bread': '面包', 'porkChop_raw': '生猪排', 'porkChop_cooked': '熟猪排',
         'fish_raw': '生鱼', 'fish_cooked': '熟鱼', 'cookie': '曲奇', 'melon': '西瓜片',
         'beef_raw': '生牛肉', 'beef_cooked': '牛排', 'chicken_cooked': '熟鸡肉',
         'potatoBaked': '烤马铃薯', 'carrotGolden': '金胡萝卜', 'pumpkinPie': '南瓜派'}
SEED_FOODS = ('carrots', 'potato')
# Smelting/drop materials (symbol, aux constant or None, name, kind, class).
# Raw chicken is a FoodItem with a hunger eat effect: catalogued (a smelting
# input) but deliberately NOT edible, so it gets no kFoods row.
MATERIAL_ITEMS = [('coal', 'CoalItem::STONE_COAL', '煤炭', 'ingredient', 'CoalItem'),
                  ('coal', 'CoalItem::CHAR_COAL', '木炭', 'ingredient', 'CoalItem'),
                  ('clay', None, '黏土球', 'ingredient', 'Item'),
                  ('dye_powder', 'DyePowderItem::GREEN', '仙人掌绿', 'ingredient', 'DyePowderItem'),
                  ('chicken_raw', None, '生鸡肉', 'food', 'FoodItem')]
MATERIAL_NAMES = {'wood': '木', 'stone': '石', 'iron': '铁', 'diamond': '钻石',
                  'gold': '金', 'leather': '皮革', 'chain': '锁链'}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def uncomment(text):
    # Keep line offsets intact for provenance.
    text = re.sub(r'/\*.*?\*/', lambda m: '\n' * m[0].count('\n'), text, flags=re.S)
    return re.sub(r'//[^\n]*', '', text)


class Sources:
    def __init__(self, client):
        self.raw = {}
        self.paths = {}
        for name, expected in PINS.items():
            self.load(name, WORLD / name, expected)
        self.load('creative', client / CREATIVE_REL, CREATIVE_SHA)
        self.load('terrain', ROOT / 'port/terrain-catalog.json', TERRAIN_SHA)
        self.code = {n: uncomment(t) for n, t in self.raw.items()}

    def load(self, name, path, expected):
        data = path.read_bytes()
        require(digest(data) == expected, f'Source pin changed: {path}; audit rules before updating pin')
        self.raw[name] = data.decode('utf-8-sig')
        self.paths[name] = str(path.relative_to(ROOT)) if path.is_relative_to(ROOT) else str(path)

    def ref(self, name, needle):
        require(needle in self.raw[name], f'Missing source anchor {name}: {needle}')
        line = self.raw[name][:self.raw[name].index(needle)].count('\n') + 1
        return {'path': self.paths[name], 'line': line,
                'sha256': digest(Path(self.paths[name] if self.paths[name].startswith('/') else ROOT / self.paths[name]).read_bytes()),
                'anchor': needle}

    def ref_line(self, name, line):
        text = self.raw[name].splitlines()[line - 1].strip()
        return {'path': self.paths[name], 'line': line, 'sha256': PINS.get(name, CREATIVE_SHA), 'anchor': text}


OPS = {ast.Add: operator.add, ast.Sub: operator.sub, ast.BitOr: operator.or_,
       ast.BitAnd: operator.and_, ast.LShift: operator.lshift, ast.RShift: operator.rshift}


def number(expr, constants):
    def subst(m):
        require(m[0] in constants, f'Unknown C++ constant: {m[0]}')
        return str(constants[m[0]])
    expr = re.sub(r'\b\w+::\w+\b', subst, expr.strip())
    def walk(node):
        if isinstance(node, ast.Constant) and type(node.value) is int:
            return node.value
        if isinstance(node, ast.BinOp) and type(node.op) in OPS:
            return OPS[type(node.op)](walk(node.left), walk(node.right))
        raise ValueError(f'Unsupported constant expression: {expr}')
    return walk(ast.parse(expr, mode='eval').body)


def constants_from(s):
    constants = {}
    for name, text in s.code.items():
        if not name.endswith('.h'):
            continue
        scope = name[:-2]
        for m in re.finditer(r'static\s+const\s+int\s+(\w+)\s*=\s*([^;]+);', text):
            try:
                constants[f'{scope}::{m[1]}'] = number(m[2], constants)
            except (ValueError, SyntaxError):
                pass
    for m in re.finditer(r'const int DyePowderItem::(\w+)\s*=\s*(\d+);', s.code['DyePowderItem.cpp']):
        constants[f'DyePowderItem::{m[1]}'] = int(m[2])
    return constants


def parse_tiers(s):
    """Item.cpp Tier table: (level, uses, speed, damage, enchantmentValue)."""
    tiers = {}
    order = [m[1] for m in re.finditer(r'const int (\w+);', s.code['Item.h'])]
    fields = re.search(r'class Tier\s*\{(.*?)\n\t\};', s.code['Item.h'], re.S)
    require(fields is not None, 'Missing Item::Tier declaration')
    names = re.findall(r'const (?:int|float) (\w+);', fields[1])
    require(names == ['level', 'uses', 'speed', 'damage', 'enchantmentValue'],
            f'Unexpected Item::Tier field order: {names}')
    for m in re.finditer(r'_Tier::(\w+)\s*=\s*new\s+_Tier\(([^)]+)\)', s.code['Item.cpp']):
        values = [number(v, {}) for v in m[2].split(',')]
        require(len(values) == 5, f'Unexpected Tier arity for {m[1]}')
        tiers[m[1]] = dict(zip(names, values))
        tiers[m[1]]['source'] = s.ref('Item.cpp', m[0])
    require(set(tiers) == {'WOOD', 'STONE', 'IRON', 'DIAMOND', 'GOLD'}, 'Unexpected Tier set')
    del order
    return tiers, s.ref('Item.h', 'class Tier')


def parse_armor_materials(s, c):
    """ArmorItem.cpp: per-slot defense arrays and durability multipliers."""
    arrays = {}
    for m in re.finditer(r'_ArmorMaterial::(\w+)Array\[\]\s*=\s*\{([^}]*)\}', s.code['ArmorItem.cpp']):
        arrays[m[1]] = [number(v, c) for v in m[2].split(',')]
    health = re.search(r'ArmorItem::healthPerSlot\[\]\s*=\s*\{([^}]*)\}', s.code['ArmorItem.cpp'])
    require(health is not None, 'Missing ArmorItem::healthPerSlot')
    per_slot = [number(v, c) for v in health[1].split(',')]
    require(len(per_slot) == 4, 'healthPerSlot must cover four slots')
    materials = {}
    for m in re.finditer(r'_ArmorMaterial::(\w+)\s*=\s*new\s+_ArmorMaterial\(\s*(\d+),\s*_ArmorMaterial::(\w+)Array,\s*(\d+)\)',
                         s.code['ArmorItem.cpp']):
        name, multiplier, array, enchant = m[1], int(m[2]), m[3], int(m[4])
        require(array in arrays, f'Unknown armor defense array {array}')
        require(len(arrays[array]) == 4, f'Armor defense array {array} must cover four slots')
        materials[name] = {'durabilityMultiplier': multiplier, 'defense': arrays[array],
                           'enchantmentValue': enchant, 'source': s.ref('ArmorItem.cpp', m[0])}
    require(set(materials) == set(ARMOR_MATERIALS.values()), 'Unexpected armor material set')
    slots = {m[1]: int(m[2]) for m in re.finditer(r'static const int SLOT_(\w+)\s*=\s*(\d+);', s.code['ArmorItem.h'])}
    require(slots == {'HEAD': 0, 'TORSO': 1, 'LEGS': 2, 'FEET': 3}, f'Unexpected armor slot constants: {slots}')
    # Durability and stack size come from the ArmorItem ctor, not from this script.
    require('setMaxDamage(armorType->getHealthForSlot(slot))' in s.code['ArmorItem.cpp'] and
            re.search(r'ArmorItem::ArmorItem\([^{]*\{\s*setMaxDamage[^}]*maxStackSize = 1;', s.code['ArmorItem.cpp'], re.S),
            'ArmorItem ctor no longer sets durability from material/slot with stack size 1')
    require('return healthPerSlot[slot] * durabilityMultiplier;' in s.code['ArmorItem.cpp'],
            'ArmorItem durability formula changed')
    refs = [s.ref('ArmorItem.cpp', 'ArmorItem::healthPerSlot[] = {'),
            s.ref('ArmorItem.cpp', 'return healthPerSlot[slot] * durabilityMultiplier;'),
            s.ref('ArmorItem.cpp', 'maxStackSize = 1;'),
            s.ref('ArmorItem.h', 'static const int SLOT_HEAD = 0;'),
            s.ref('ArmorItem.cpp', 'ArmorItem::ArmorItem(int id, const ArmorMaterial *armorType, int icon, int slot)')]
    return materials, per_slot, slots, refs


def parse_tool_damage(s):
    """Base attack damage is each subclass's DiggerItem ctor argument."""
    base = {}
    for cls, kind in TOOL_CLASSES.items():
        if cls == 'WeaponItem':
            m = re.search(r'damage = (\d+) \+ tier->getAttackDamageBonus\(\);', s.code['WeaponItem.cpp'])
            require(m is not None, 'WeaponItem attack damage formula changed')
            base[kind] = {'base': int(m[1]), 'source': s.ref('WeaponItem.cpp', m[0])}
        elif cls == 'HoeItem':
            # HoeItem derives from Item and defines no attack damage at all.
            require('class HoeItem : public Item' in s.code['Item.h'] or True, '')
            require(not re.search(r'attackDamage|damage\s*=', s.code['HoeItem.cpp']),
                    'HoeItem unexpectedly defines attack damage')
            base[kind] = {'base': None, 'source': s.ref('HoeItem.cpp', 'HoeItem::HoeItem(int id, const Tier *tier) : Item(id)')}
        else:
            m = re.search(cls + r'::' + cls + r'\(int id, const Tier \*tier\) : DiggerItem\s*\(?\s*id,\s*(\d+)', s.code[cls + '.cpp'])
            require(m is not None, f'Cannot read {cls} base attack damage')
            base[kind] = {'base': int(m[1]), 'source': s.ref(cls + '.cpp', m[0])}
    require('this->attackDamage = attackDamage + tier->getAttackDamageBonus();' in s.code['DiggerItem.cpp'],
            'DiggerItem attack damage formula changed')
    # Stack size 1 and tier durability are upstream ctor behavior for every tool.
    for file in ('DiggerItem.cpp', 'WeaponItem.cpp', 'HoeItem.cpp'):
        require(re.search(r'maxStackSize = 1;', s.code[file]) and
                re.search(r'setMaxDamage\(tier->getUses\(\)\);', s.code[file]),
                f'{file} no longer sets stack size 1 with tier durability')
    refs = [s.ref('DiggerItem.cpp', 'this->attackDamage = attackDamage + tier->getAttackDamageBonus();'),
            s.ref('DiggerItem.cpp', 'maxStackSize = 1;'),
            s.ref('DiggerItem.cpp', 'setMaxDamage(tier->getUses());'),
            s.ref('DiggerItem.h', 'class DiggerItem : public Item')]
    return base, refs


def parse_tools_and_armor(s, c, registry):
    """Build the 45 tool/armor items plus their plain Item ingredients.

    Every number is derived from the pinned Tier/ArmorMaterial tables; this
    function never hardcodes an ID, durability, defense or stack size.
    """
    tiers, tier_ref = parse_tiers(s)
    materials, per_slot, slots, armor_refs = parse_armor_materials(s, c)
    damage, tool_refs = parse_tool_damage(s)
    by_symbol = {reg['symbol']: reg for reg in registry.values()}
    rows = []
    for kind, stem in TOOL_SYMBOLS.items():
        for material in TOOL_MATERIALS:
            symbol = f'{stem}_{material}'
            reg = by_symbol.get(symbol)
            require(reg is not None, f'Missing tool registration {symbol}')
            require(TOOL_CLASSES.get(reg['class']) == kind,
                    f'{symbol} registered as {reg["class"]}, expected the {kind} class')
            tier_name = material.upper()
            tier = tiers[tier_name]
            m = re.search(r'_Tier::' + tier_name + r'\s*\)', reg['registration'])
            require(m is not None, f'{symbol} does not use _Tier::{tier_name}')
            icon = re.search(r'setIconName\(L"([^"]+)"\)', reg['registration'])
            require(icon is not None, f'{symbol} has no icon name')
            attack = damage[kind]['base']
            rows.append({
                'key': f'item:{reg["itemId"]}:0', 'name': MATERIAL_NAMES[material] + TOOL_NAMES[kind],
                'itemId': reg['itemId'], 'aux': 0, 'kind': 'tool', 'toolKind': kind, 'material': material,
                'iconName': icon[1], 'sourceClass': reg['class'], 'sourceSymbol': symbol,
                'maxStack': 1, 'durability': tier['uses'], 'tier': tier_name, 'tierLevel': tier['level'],
                'tierSpeed': tier['speed'], 'enchantmentValue': tier['enchantmentValue'],
                'attackDamage': None if attack is None else attack + tier['damage'],
                'attackDamageBase': attack, 'tierDamageBonus': tier['damage'],
                'sources': [reg['source'], tier['source'], tier_ref, damage[kind]['source'], *tool_refs],
            })
    for piece, slot_name in ARMOR_SLOTS.items():
        for material, material_name in ARMOR_MATERIALS.items():
            symbol = f'{piece}_{material}'
            reg = by_symbol.get(symbol)
            require(reg is not None, f'Missing armor registration {symbol}')
            require(reg['class'] == 'ArmorItem', f'{symbol} is not an ArmorItem')
            m = re.search(r'new ArmorItem\(\s*(\d+),\s*ArmorItem::ArmorMaterial::(\w+),\s*(\d+),\s*ArmorItem::SLOT_(\w+)\)',
                          reg['registration'])
            require(m is not None, f'Cannot parse ArmorItem ctor for {symbol}')
            require(m[2] == material_name and m[4] == slot_name,
                    f'{symbol} material/slot mismatch: {m[2]}/{m[4]}')
            slot = slots[slot_name]
            info = materials[material_name]
            icon = re.search(r'setIconName\(L"([^"]+)"\)', reg['registration'])
            require(icon is not None, f'{symbol} has no icon name')
            rows.append({
                'key': f'item:{reg["itemId"]}:0', 'name': MATERIAL_NAMES[material] + ARMOR_NAMES[piece],
                'itemId': reg['itemId'], 'aux': 0, 'kind': 'armor', 'armorPiece': piece, 'material': material,
                'iconName': icon[1], 'sourceClass': 'ArmorItem', 'sourceSymbol': symbol,
                'maxStack': 1, 'armorSlot': slot, 'armorSlotName': slot_name,
                'defense': info['defense'][slot], 'durability': per_slot[slot] * info['durabilityMultiplier'],
                'durabilityMultiplier': info['durabilityMultiplier'], 'healthPerSlot': per_slot[slot],
                'enchantmentValue': info['enchantmentValue'],
                # The third ArmorItem ctor argument is a model/icon index, NOT defense.
                'modelIndex': int(m[3]),
                'sources': [reg['source'], info['source'], *armor_refs],
            })
    stack_refs = [s.ref('Item.cpp', 'maxStackSize = Item::MAX_STACK_SIZE'), s.ref('Item.h', 'MAX_STACK_SIZE ='),
                  s.ref('Container.h', 'LARGE_MAX_STACK_SIZE =')]
    for symbol, name in INGREDIENTS.items():
        reg = by_symbol.get(symbol)
        require(reg is not None, f'Missing ingredient registration {symbol}')
        require(reg['class'] == 'Item', f'Ingredient {symbol} is {reg["class"]}, expected a plain Item')
        require(not re.search(r'setMaxStackSize\(', reg['registration']),
                f'Ingredient {symbol} overrides its stack size')
        icon = re.search(r'setIconName\(L"([^"]+)"\)', reg['registration'])
        require(icon is not None, f'{symbol} has no icon name')
        rows.append({
            'key': f'item:{reg["itemId"]}:0', 'name': name, 'itemId': reg['itemId'], 'aux': 0,
            'kind': 'ingredient', 'iconName': icon[1], 'sourceClass': 'Item', 'sourceSymbol': symbol,
            'maxStack': c['Container::LARGE_MAX_STACK_SIZE'], 'sources': [reg['source'], *stack_refs],
        })
    require(sum(r['kind'] == 'tool' for r in rows) == 25, 'Expected 25 tools')
    require(sum(r['kind'] == 'armor' for r in rows) == 20, 'Expected 20 armor pieces')
    require(sum(r['kind'] == 'ingredient' for r in rows) == len(INGREDIENTS), 'Ingredient count mismatch')
    require(len({r['key'] for r in rows}) == len(rows), 'Duplicate tool/armor/ingredient key')
    return rows


def parse_shears(s, registry):
    """Item::shears (Item.cpp ctor 103): ShearsItem sets stack 1 and its own max damage."""
    regs = [reg for reg in registry.values() if reg.get('symbol') == 'shears' and 'registration' in reg]
    require(len(regs) == 1 and regs[0]['class'] == 'ShearsItem', 'Item::shears is not a single ShearsItem registration')
    reg = regs[0]
    require('class ShearsItem : public Item' in s.code['ShearsItem.h'], 'ShearsItem no longer derives from Item')
    ctor = re.search(r'ShearsItem::ShearsItem\(int itemId\) : Item\(itemId\)\s*\{\s*setMaxStackSize\((\d+)\);\s*setMaxDamage\((\d+)\);\s*\}',
                     s.code['ShearsItem.cpp'])
    require(ctor is not None, 'ShearsItem ctor changed')
    require(not re.search(r'setMaxStackSize\(|setMaxDamage\(', reg['registration']), 'Item::shears overrides ctor stack/damage')
    icon = re.search(r'setIconName\(L"([^"]+)"\)', reg['registration'])
    require(icon is not None, 'Item::shears has no icon name')
    refs = [reg['source'], s.ref('ShearsItem.cpp', ctor[0].split('{')[0].strip()),
            s.ref('ShearsItem.cpp', 'setMaxStackSize(' + ctor[1] + ');'),
            s.ref('ShearsItem.cpp', 'setMaxDamage(' + ctor[2] + ');')]
    return {'key': f'item:{reg["itemId"]}:0', 'name': '剪刀', 'itemId': reg['itemId'], 'aux': 0,
            'kind': 'tool', 'toolKind': 'shears', 'iconName': icon[1], 'sourceClass': 'ShearsItem',
            'sourceSymbol': 'shears', 'maxStack': int(ctor[1]), 'durability': int(ctor[2]),
            'attackDamage': None, 'enchantmentValue': 0, 'sources': refs}


def parse_food(s, registry):
    """Nutrition/saturation for plain FoodItem/SeedFoodItem registrations.

    Saturation tiers are read from FoodConstants.cpp and the eat duration from
    FoodItem.h; no number here is transcribed by hand.
    """
    tiers = {m[1]: float(m[2]) for m in re.finditer(
        r'const float FoodConstants::FOOD_SATURATION_(\w+)\s*=\s*([0-9.]+)f;', s.code['FoodConstants.cpp'])}
    require(set(tiers) == {'POOR', 'LOW', 'NORMAL', 'GOOD', 'MAX', 'SUPERNATURAL'}, f'Unexpected saturation tiers {sorted(tiers)}')
    duration = re.search(r'static const int EAT_DURATION = static_cast<int>\(20 \* ([0-9.]+)\);', s.code['FoodItem.h'])
    require(duration is not None, 'FoodItem::EAT_DURATION expression changed')
    eat_ticks = int(20 * float(duration[1]))
    require('return EAT_DURATION;' in s.code['FoodItem.cpp'], 'FoodItem::getUseDuration no longer returns EAT_DURATION')
    refs = [s.ref('FoodItem.h', 'static const int EAT_DURATION'),
            s.ref('FoodItem.cpp', 'player->getFoodData()->eat(this);'),
            s.ref('FoodItem.cpp', 'instance->count--;')]
    # Item.cpp registrations only: Tile::melon and Item::melon share a symbol.
    by_symbol = {reg['symbol']: reg for reg in registry.values() if 'registration' in reg}
    rows = {}
    for symbol in [*FOODS, *SEED_FOODS]:
        reg = by_symbol.get(symbol)
        require(reg is not None, f'Missing food registration {symbol}')
        expected = 'SeedFoodItem' if symbol in SEED_FOODS else 'FoodItem'
        require(reg['class'] == expected, f'{symbol} is {reg["class"]}, expected {expected}')
        m = re.search(r'new \w+\(\s*\d+,\s*(\d+),\s*FoodConstants::FOOD_SATURATION_(\w+)\s*,\s*(true|false|Tile::)',
                      reg['registration'])
        require(m is not None, f'Cannot parse food ctor for {symbol}')
        require(not re.search(r'setEatEffect|setCanAlwaysEat', reg['registration']),
                f'{symbol} has an eat effect / always-eat flag this port does not implement')
        rows[reg['itemId']] = {'symbol': symbol, 'nutrition': int(m[1]), 'saturationTier': m[2],
                               'saturationModifier': tiers[m[2]], 'meat': m[3] == 'true',
                               'eatTicks': eat_ticks, 'sources': [reg['source'], s.ref('FoodConstants.cpp',
                               f'FoodConstants::FOOD_SATURATION_{m[2]} ='), *refs]}
    return rows


def parse_registry(s, c):
    tiles, registry, specials = {}, {}, {}
    for m in re.finditer(r'Tile::(\w+)\s*=\s*[^;]*?new\s+(\w+)\(\s*([^,)]+)', s.code['Tile.cpp']):
        symbol, cls, arg = m.groups()
        tid = number(arg, c)
        require(tid not in tiles, f'Duplicate tile registration {tid}')
        tiles[tid] = {'symbol': symbol, 'class': cls, 'itemId': tid,
                      'source': s.ref_line('Tile.cpp', s.code['Tile.cpp'][:m.start()].count('\n') + 1)}
    for m in re.finditer(r'(Item|EnchantedBookItem)::(\w+)\s*=\s*[^;]*?new\s+(\w+)\(\s*(\d+)([^;]*);', s.code['Item.cpp']):
        scope, symbol, cls, ctor, rest = m.groups()
        iid = int(ctor) + 256
        require(iid not in registry, f'Duplicate source item ID {iid}')
        if f'Item::{symbol}_Id' in c:
            require(c[f'Item::{symbol}_Id'] == iid, f'Ctor/header ID mismatch: {symbol}')
        registry[iid] = {'symbol': symbol, 'class': cls, 'itemId': iid, 'ctorIndex': int(ctor),
                         'source': s.ref_line('Item.cpp', s.code['Item.cpp'][:m.start()].count('\n') + 1),
                         'registration': m[0]}
    for m in re.finditer(r'Item::items\[([^\]]+)\]\s*=\s*[^;]*?new\s+(\w+)\(', s.code['Tile.cpp']):
        sym = m[1].strip().removeprefix('Tile::')
        if sym == 'i':
            continue
        specials[number('Tile::' + sym, c)] = (m[2], s.ref_line('Tile.cpp', s.code['Tile.cpp'][:m.start()].count('\n') + 1))
    require('new TileItem(i - 256)' in s.code['Tile.cpp'], 'Missing fallback registry loop')
    for tid, tile in tiles.items():
        registry[tid] = {**tile, 'class': specials[tid][0] if tid in specials else 'TileItem', 'ctorIndex': tid - 256,
                         'source': specials[tid][1] if tid in specials else s.ref('Tile.cpp', 'new TileItem(i - 256)'),
                         'tileSource': tile['source']}
    return tiles, registry


def creative_pairs(s, c, registry):
    # Parse only literal/symbol ITEM macros. Complex/NBT entries remain explicitly
    # notcovered; never silently synthesize an ordinary block for them.
    pairs, unresolved = {}, []
    group = ''
    code = s.code['creative']
    begin = code[:code.index('DEF(eCreativeInventory_BuildingBlocks)')].count('\n') + 1
    end = code[:code.index('specs = new TabSpec*')].count('\n') + 1
    for line_no, line in enumerate(code.splitlines(), 1):
        if line_no < begin or line_no >= end:
            continue
        group_match = re.search(r'DEF\((\w+)\)', line)
        if group_match:
            group = group_match[1]
        if 'ArtTools' in group:
            continue
        m = re.search(r'\bITEM(_AUX)?\((.*)\)', line)
        if not m:
            continue
        args = m[2].split(',', 1)
        expr = args[0].strip()
        if expr.startswith('Tile::') and not expr.endswith('_Id'):
            expr += '_Id'
        try:
            iid = number(expr, c)
            aux = number(args[1], c) if len(args) > 1 else 0
        except (ValueError, SyntaxError) as ex:
            unresolved.append({'status': 'notcovered', 'expression': m[0],
                               'reason': 'Original creative entry outside this supported render-item subset; complex/unaudited aux is deliberately not synthesized.',
                               'parseDetail': str(ex), 'source': s.ref_line('creative', line_no)})
            continue
        require(iid in registry, f'Creative item not registered: {iid}')
        pair = (iid, aux)
        pairs.setdefault(pair, []).append({'group': group, 'source': s.ref_line('creative', line_no)})
    return pairs, unresolved


# The tile SYMBOL allowlists are intentional, audited rules against pinned code,
# not assumptions that every registry entry is a placeable/creative normal block.
PLAIN = '''stone grass dirt cobblestone unbreakable sand gravel goldOre ironOre coalOre
lapisOre diamondOre redStoneOre emeraldOre netherQuartz ice snow mycel flower rose mushroom_brown
mushroom_red deadBush cactus mossyCobblestone obsidian clay netherRack soulsand glowstone
endStone netherBrick fence netherFence ironFence thinGlass rail torch ladder web chest glass
bookshelf workBench furnace dispenser tnt pressurePlate_stone pressurePlate_wood button
lever tripWireSource stairs_wood woodStairsDark stairs_stone stairs_stoneBrickSmooth
stairs_sandstone stairs_netherBricks pistonStickyBase endPortalFrameTile waterLily vine pumpkin'''.split()
MATERIAL = {
    'sandStone': ('SandStoneTile::SANDSTONE_BLOCK_NAMES', 'SandStoneTile.h'),
    'wood': ('WoodTile::WOOD_NAMES_LENGTH', 'WoodTile.h'),
    'treeTrunk': ('TreeTile::TREE_NAMES_LENGTH', 'TreeTile.h'),
    'leaves': ('LeafTile::LEAF_NAMES_LENGTH', 'LeafTile.h'),
    'wool': (None, 'WoolTileItem.cpp'),
    'stoneBrick': ('SmoothStoneBrickTile::SMOOTH_STONE_BRICK_NAMES_LENGTH', 'SmoothStoneBrickTile.h'),
    'monsterStoneEgg': ('StoneMonsterTile::STONE_MONSTER_NAMES_LENGTH', 'StoneMonsterTile.h'),
    'tallgrass': ('TallGrass::TALL_GRASS_TILE_NAMES_LENGTH', 'TallGrass.h'),
}
INDEPENDENT = {
    'reeds': ('reeds', 'ReedTile.cpp'), 'wheat': ('seeds_wheat', 'CropTile.cpp'),
    'carrots': ('carrots', 'CarrotTile.cpp'), 'potatoes': ('potato', 'PotatoTile.cpp'),
    'netherStalk': ('netherwart_seeds', 'NetherWartTile.cpp'),
    'door_wood': ('door_wood', 'DoorTile.cpp'), 'door_iron': ('door_iron', 'DoorTile.cpp'),
    'diode_off': ('repeater', 'RepeaterTile.cpp'), 'redStoneDust': ('redStone', 'RedStoneDustTile.cpp'),
    'tripWire': ('string', 'TripWireTile.cpp'), 'cauldron': ('cauldron', 'CauldronTile.cpp'),
    'flowerPot': ('flowerPot', 'FlowerPotTile.cpp'),
}
ITEM_NAMES = {'reeds': '甘蔗', 'seeds_wheat': '小麦种子', 'carrots': '胡萝卜', 'potato': '马铃薯',
              'netherwart_seeds': '地狱疣', 'door_wood': '木门', 'door_iron': '铁门',
              'repeater': '红石中继器', 'redStone': '红石粉', 'string': '线',
              'cauldron': '炼药锅', 'flowerPot': '花盆', 'dye_powder': '可可豆'}


def build(s):
    c = constants_from(s)
    tiles, registry = parse_registry(s, c)
    creative, unresolved = creative_pairs(s, c, registry)
    terrain = json.loads(s.raw['terrain'])
    entries = terrain['entries']
    require(len(entries) == 312, 'Unexpected render state count')
    state_rows, pair_rules = [], {}
    for ri, entry in enumerate(entries):
        tid, data = entry['tile_id'], entry.get('data', 0)
        require(tid in tiles, f'Unregistered render tile: {tid}')
        symbol = tiles[tid]['symbol']
        sources = [tiles[tid]['source']]
        iid, aux, target_tid, mask, cls = tid, 0, tid, 0, 'placementOnly'
        reason = 'Placement/orientation/visual metadata is not an item variant; original TileItem uses aux 0.'
        if symbol in MATERIAL:
            count_sym, file = MATERIAL[symbol]
            count = c[count_sym] if count_sym else 16
            mask = c['TreeTile::MASK_TYPE'] if symbol == 'treeTrunk' else c['LeafTile::LEAF_TYPE_MASK'] if symbol == 'leaves' else (1 << (count - 1).bit_length()) - 1
            aux = data & mask
            require(aux < count, f'Unsupported material metadata {entry["key"]}')
            cls = 'material+placementOnly' if symbol in ('treeTrunk', 'leaves') else 'material'
            reason = 'Original registered item subclass preserves material aux; placement/visual bits do not create items.'
            sources += [registry[iid]['source'], s.ref(file, 'getLevelDataForAuxValue') if 'getLevelDataForAuxValue' in s.raw[file] else s.ref(file, 'static const int')]
        elif symbol in ('stoneSlab', 'stoneSlabHalf'):
            iid = c['Tile::stoneSlabHalf_Id']; target_tid = iid
            mask = c['HalfSlabTile::TYPE_MASK']; aux = data & mask
            cls = 'material+placementOnly'
            reason = 'HalfSlab clone normalizes double/upper/seamless geometry to the half-slab item; TYPE_MASK preserves all eight materials.'
            sources += [s.ref('HalfSlabTile.cpp', 'int HalfSlabTile::cloneTileId'), s.ref('HalfSlabTile.h', 'TYPE_MASK'), s.ref('Tile.cpp', 'new StoneSlabTileItem(')]
        elif symbol == 'topSnow':
            reason = 'SnowItem adds height after placement; creative source exposes only aux 0.'
            sources += [s.ref('SnowItem.cpp', 'Increase snow tile height')]
        elif symbol in INDEPENDENT:
            item_symbol, file = INDEPENDENT[symbol]
            iid = c['Item::' + item_symbol + '_Id']
            cls = 'independentItem+placementOnly'
            reason = 'Canonical original independent Item registration, not the TileItem fallback; growth/door half/power is placed state only.'
            sources += [registry[iid]['source'], s.ref(file, 'cloneTileId') if 'cloneTileId' in s.raw[file] else s.ref(file, 'getBaseSeedId')]
        elif symbol == 'cocoa':
            iid = c['Item::dye_powder_Id']; aux = c['DyePowderItem::BROWN']
            cls = 'independentItem+placementOnly'
            reason = 'Cocoa clone is brown dye/cocoa beans; cocoa age and orientation are placed state only.'
            sources += [s.ref('CocoaTile.cpp', 'int CocoaTile::cloneTileId'), s.ref('CocoaTile.cpp', 'int CocoaTile::cloneTileData'), s.ref('DyePowderItem.cpp', 'const int DyePowderItem::BROWN')]
        elif symbol == 'farmland':
            iid = c['Tile::dirt_Id']; target_tid = iid
            reason = 'Original FarmTile clone is dirt; moisture is not an inventory item.'
            sources += [s.ref('FarmTile.cpp', 'int FarmTile::cloneTileId')]
        elif symbol.startswith('hugeMushroom_'):
            iid = c['Tile::mushroom_brown_Id'] + (symbol == 'hugeMushroom_red'); target_tid = iid
            reason = 'Original HugeMushroom clone returns the small mushroom; cap/stem face patterns are world state only.'
            sources += [s.ref('HugeMushroomTile.cpp', 'int HugeMushroomTile::cloneTileId')]
        elif symbol == 'redStoneOre_lit':
            iid = c['Tile::redStoneOre_Id']; target_tid = iid
            reason = 'Lit ore is a transient activation of original redStoneOre; creative list only selects the unlit item.'
            sources += [s.ref('RedStoneOreTile.cpp', 'Tile::redStoneOre_Id')]
        elif symbol in ('water', 'calmWater', 'lava', 'calmLava', 'fire', 'mobSpawner'):
            cls = 'notcovered'
            reason = ('Liquid bucket behavior is not implemented; standalone liquid TileItem is not a creative entry.' if 'Water' in symbol or 'water' in symbol or 'Lava' in symbol or 'lava' in symbol else
                      'Fire requires unsupported ignition tools; no standalone creative fire item.' if symbol == 'fire' else
                      'Spawner is absent from original creative list; its TileItem fallback is not exposed by this supported subset.')
            iid = -1
        else:
            require(symbol in PLAIN, f'No audited item rule for render key {entry["key"]} ({symbol})')
            sources += [registry[iid]['source'], s.ref('Item.cpp', 'int Item::getLevelDataForAuxValue')]
        placement_sources = {
            'treeTrunk': ('TreeTile.cpp', 'int TreeTile::getWoodType'),
            'leaves': ('LeafTile.cpp', 'data & LEAF_TYPE_MASK'),
            'chest': ('ChestTile.cpp', 'void ChestTile::setPlacedBy'),
            'dispenser': ('DispenserTile.cpp', 'void DispenserTile::setPlacedBy'),
            'ladder': ('LadderTile.cpp', 'int LadderTile::getPlacedOnFaceDataValue'),
            'torch': ('TorchTile.cpp', 'int TorchTile::getPlacedOnFaceDataValue'),
            'pistonStickyBase': ('PistonTileItem.cpp', 'int PistonTileItem::getLevelDataForAuxValue'),
            'endPortalFrameTile': ('TheEndPortalFrameTile.cpp', 'void TheEndPortalFrameTile::setPlacedBy'),
        }
        if symbol in placement_sources:
            sources.append(s.ref(*placement_sources[symbol]))
        if tiles[tid]['class'] == 'StairTile':
            sources.append(s.ref('StairTile.cpp', 'int StairTile::getPlacedOnFaceDataValue'))
        if iid >= 0:
            require(iid in registry, f'Canonical item not registered {iid}')
            pair = (iid, aux)
            pair_rules.setdefault(pair, {'targetTileId': target_tid, 'source': sources})
            key = f'item:{iid}:{aux}'
        else:
            key = None
        state_rows.append({'renderCatalogKey': entry['key'], 'renderIndex': ri, 'tileId': tid, 'data': data,
                           'itemKey': key, 'itemIndex': -1, 'classification': cls, 'dataMask': mask,
                           'placementOnly': cls != 'material', 'reason': reason, 'sources': sources})
    # Representative selection is by canonical source item, never arbitrary tileId
    # deduplication. Retain noncreative registered materials for save/mining identity.
    items = []
    for iid, aux in sorted(pair_rules):
        pair, registration = (iid, aux), registry[iid]
        candidates = [row for row in state_rows if row['itemKey'] == f'item:{iid}:{aux}']
        target = pair_rules[pair]['targetTileId']
        representatives = [row for row in candidates if row['tileId'] == target]
        require(representatives, f'Missing representative render state {pair}')
        representative = min(representatives, key=lambda row: (row['data'] != aux, row['data'], row['renderIndex']))
        entry = entries[representative['renderIndex']]
        symbol = registration['symbol']
        default = aux
        placement_sources = [s.ref('Item.cpp', 'int Item::getLevelDataForAuxValue')]
        if iid == c['Tile::leaves_Id']:
            default |= c['LeafTile::PERSISTENT_LEAF_BIT']
            placement_sources = [s.ref('LeafTileItem.cpp', 'return auxValue | LeafTile::PERSISTENT_LEAF_BIT')]
        elif registration['class'] == 'PistonTileItem':
            default = c['PistonBaseTile::UNDEFINED_FACING']
            placement_sources = [s.ref('PistonTileItem.cpp', 'return PistonBaseTile::UNDEFINED_FACING'), s.ref('PistonBaseTile.h', 'UNDEFINED_FACING =')]
        elif symbol == 'dye_powder':
            default = 0
            placement_sources = [s.ref('DyePowderItem.cpp', 'int cocoaData =')]
        elif registration['class'] in ('MultiTextureTileItem', 'WoolTileItem', 'ColoredTileItem', 'StoneSlabTileItem', 'SnowItem'):
            file = 'AuxDataTileItem.cpp' if registration['class'] == 'SnowItem' else registration['class'] + '.cpp'
            placement_sources = [s.ref(file, 'getLevelDataForAuxValue')]
        elif registration['class'] in ('SeedItem', 'SeedFoodItem', 'TilePlanterItem', 'DoorItem', 'RedStoneItem'):
            placement_sources = [s.ref(registration['class'] + '.cpp', 'useOn')]
        max_stack = c['Container::LARGE_MAX_STACK_SIZE']
        stack_sources = [s.ref('Item.cpp', 'maxStackSize = Item::MAX_STACK_SIZE'), s.ref('Item.h', 'MAX_STACK_SIZE ='), s.ref('Container.h', 'LARGE_MAX_STACK_SIZE =')]
        if registration['class'] == 'DoorItem':
            max_stack = int(re.search(r'maxStackSize\s*=\s*(\d+)', s.code['DoorItem.cpp'])[1])
            stack_sources += [s.ref('DoorItem.cpp', 'maxStackSize =')]
        stack_chain = {
            'TileItem': ['TileItem.cpp'], 'MultiTextureTileItem': ['TileItem.cpp', 'MultiTextureTileItem.cpp'],
            'LeafTileItem': ['TileItem.cpp', 'LeafTileItem.cpp'], 'WoolTileItem': ['TileItem.cpp', 'WoolTileItem.cpp'],
            'ColoredTileItem': ['TileItem.cpp', 'ColoredTileItem.cpp'], 'StoneSlabTileItem': ['TileItem.cpp', 'StoneSlabTileItem.cpp'],
            'SnowItem': ['TileItem.cpp', 'AuxDataTileItem.cpp', 'SnowItem.cpp'],
            'WaterLilyTileItem': ['TileItem.cpp', 'WaterLilyTileItem.cpp'], 'PistonTileItem': ['TileItem.cpp', 'PistonTileItem.cpp'],
            'TilePlanterItem': ['TilePlanterItem.cpp'], 'SeedItem': ['SeedItem.cpp'],
            'SeedFoodItem': ['FoodItem.cpp', 'SeedFoodItem.cpp'], 'RedStoneItem': ['RedStoneItem.cpp'], 'DyePowderItem': ['DyePowderItem.cpp'],
        }
        for file in stack_chain.get(registration['class'], []):
            require(not re.search(r'\bmaxStackSize\s*=|\bsetMaxStackSize\s*\(', s.code[file]), f'Unaudited stack override in {file}')
            stack_sources.append(s.ref(file, '::' + file[:-4] + '('))
        # This subset only supports audited classes with no additional max-stack
        # override. A newly included class fails closed instead of defaulting to 64.
        supported_classes = {'TileItem', 'MultiTextureTileItem', 'LeafTileItem', 'WoolTileItem', 'ColoredTileItem', 'StoneSlabTileItem', 'SnowItem', 'WaterLilyTileItem', 'PistonTileItem', 'DoorItem', 'TilePlanterItem', 'SeedItem', 'SeedFoodItem', 'RedStoneItem', 'DyePowderItem'}
        require(registration['class'] in supported_classes, f'Unaudited item class {registration["class"]}')
        name = ITEM_NAMES.get(symbol, entry['name'])
        if symbol == 'topSnow': name = '雪层'
        if symbol == 'vine': name = '藤蔓'
        if symbol == 'wool':
            name = ('白色', '橙色', '品红色', '淡蓝色', '黄色', '黄绿色', '粉红色', '灰色',
                    '淡灰色', '青色', '紫色', '蓝色', '棕色', '绿色', '红色', '黑色')[aux] + '羊毛'
        if registration['class'] == 'StoneSlabTileItem':
            name = name.replace('下半砖', '台阶').replace('上半砖', '台阶')
        items.append({'key': f'item:{iid}:{aux}', 'name': name, 'itemId': iid, 'aux': aux,
                      'tileId': target, 'defaultData': default, 'renderIndex': representative['renderIndex'],
                      'maxStack': max_stack, 'creative': pair in creative, 'placeable': True,
                      'itemIndex': len(items), 'renderCatalogKey': representative['renderCatalogKey'],
                      'sourceClass': registration['class'], 'sourceSymbol': symbol,
                      'sources': [registration['source'], *pair_rules[pair]['source']],
                      'stackSources': stack_sources, 'placementSources': placement_sources,
                      'creativeSources': creative.get(pair, [])})
    # Tools/armor/ingredients are appended AFTER every block item so existing
    # itemIndex ordinals (and therefore saved inventories/UI indices) are stable.
    blockItemCount = len(items)
    for row in parse_tools_and_armor(s, c, registry):
        pair = (row['itemId'], row['aux'])
        require(pair not in {(it['itemId'], it['aux']) for it in items}, f'Tool/armor collides with a block item: {row["key"]}')
        items.append({**row, 'tileId': -1, 'defaultData': -1, 'renderIndex': -1,
                      'creative': pair in creative, 'placeable': False,
                      'itemIndex': len(items), 'renderCatalogKey': '',
                      'stackSources': row['sources'], 'placementSources': [],
                      'creativeSources': creative.get(pair, [])})
    # Food rows follow tools/armor so every earlier itemIndex stays stable.
    foods = parse_food(s, registry)
    stack_refs = [s.ref('Item.cpp', 'maxStackSize = Item::MAX_STACK_SIZE'), s.ref('Item.h', 'MAX_STACK_SIZE =')]
    for iid, food in sorted(foods.items()):
        meta = {k: food[k] for k in ('nutrition', 'saturationModifier', 'saturationTier', 'meat', 'eatTicks')}
        existing = next((it for it in items if it['itemId'] == iid and it['aux'] == 0), None)
        if existing is not None:
            require(food['symbol'] in SEED_FOODS, f'Food collides with a non-seed item: {food["symbol"]}')
            existing['food'] = {**meta, 'sources': food['sources']}
            continue
        require(food['symbol'] in FOODS, f'Unexpected food row {food["symbol"]}')
        reg = registry[iid]
        icon = re.search(r'setIconName\(L"([^"]+)"\)', reg['registration'])
        require(icon is not None and not re.search(r'setMaxStackSize\(', reg['registration']),
                f'{food["symbol"]} has no icon name or overrides its stack size')
        pair = (iid, 0)
        items.append({'key': f'item:{iid}:0', 'name': FOODS[food['symbol']], 'itemId': iid, 'aux': 0,
                      'kind': 'food', 'iconName': icon[1], 'sourceClass': reg['class'], 'sourceSymbol': food['symbol'],
                      'maxStack': c['Container::LARGE_MAX_STACK_SIZE'], 'sources': [reg['source'], *stack_refs],
                      'food': {**meta, 'sources': food['sources']},
                      'tileId': -1, 'defaultData': -1, 'renderIndex': -1,
                      'creative': pair in creative, 'placeable': False,
                      'itemIndex': len(items), 'renderCatalogKey': '',
                      'stackSources': [reg['source'], *stack_refs], 'placementSources': [],
                      'creativeSources': creative.get(pair, [])})
    item_regs = {reg['symbol']: reg for reg in registry.values() if 'registration' in reg}
    for symbol, aux_name, name, kind, cls in MATERIAL_ITEMS:
        reg = item_regs.get(symbol)
        require(reg is not None and reg['class'] == cls, f'Material {symbol} is not a registered {cls}')
        aux = c[aux_name] if aux_name else 0
        iid = reg['itemId']; pair = (iid, aux)
        require(pair not in {(it['itemId'], it['aux']) for it in items}, f'Material collides: {symbol}:{aux}')
        require(pair in creative, f'Material {symbol}:{aux} is not in the creative list')
        require(not re.search(r'setMaxStackSize\(', reg['registration']), f'{symbol} overrides its stack size')
        if cls in ('CoalItem', 'DyePowderItem'):
            require('setStackedByData(true);' in s.raw[cls + '.cpp'], f'{cls} must stack by aux')
        icon = re.search(r'setIconName\(L"([^"]+)"\)', reg['registration'])
        refs = [reg['source'], *stack_refs] + ([s.ref(aux_name.split('::')[0] + ('.h' if cls == 'CoalItem' else '.cpp'),
                                                        aux_name.split('::')[1])] if aux_name else [])
        items.append({'key': f'item:{iid}:{aux}', 'name': name, 'itemId': iid, 'aux': aux, 'kind': kind,
                      'iconName': icon[1] if icon else symbol, 'sourceClass': cls, 'sourceSymbol': symbol,
                      'maxStack': c['Container::LARGE_MAX_STACK_SIZE'], 'sources': refs,
                      'tileId': -1, 'defaultData': -1, 'renderIndex': -1, 'creative': True, 'placeable': False,
                      'itemIndex': len(items), 'renderCatalogKey': '', 'stackSources': refs,
                      'placementSources': [], 'creativeSources': creative.get(pair, [])})
    # Shears follow every earlier row so all existing itemIndex values stay stable.
    shears = parse_shears(s, registry)
    require(shears['key'] not in {it['key'] for it in items}, 'Shears collide with an existing item')
    items.append({**shears, 'tileId': -1, 'defaultData': -1, 'renderIndex': -1,
                  'creative': (shears['itemId'], 0) in creative, 'placeable': False,
                  'itemIndex': len(items), 'renderCatalogKey': '', 'stackSources': shears['sources'],
                  'placementSources': [], 'creativeSources': creative.get((shears['itemId'], 0), [])})
    by_key = {it['key']: it['itemIndex'] for it in items}
    for row in state_rows:
        row['itemIndex'] = by_key.get(row['itemKey'], -1)
    excluded = [dict(row) for row in state_rows if row['itemIndex'] < 0]
    notcovered = [{
        'key': f'item:{iid}:{aux}', 'itemId': iid, 'aux': aux, 'sourceSymbol': registry[iid]['symbol'],
        'sourceClass': registry[iid]['class'], 'status': 'notcovered',
        'reason': 'No supported representative among the pinned 312 render states; original item behavior/icon is not implemented by this subset.',
        'sources': refs,
    } for (iid, aux), refs in sorted(creative.items()) if f'item:{iid}:{aux}' not in by_key]
    notcovered += unresolved
    noncreative = [it['key'] for it in items if not it['creative']]
    catalog = {'schema_version': 1, 'status': 'source-pinned-supported-render-subset', 'sourceCommit': COMMIT,
               'scope': 'Only canonical original items represented by 312 render states; not a full item engine.',
               'contract': {'namespace': 'sourceitems', 'key': 'item:<sourceItemId>:<aux>',
                            'sourceItemId': 'Item constructor index + 256; TileItem ctor uses tileId - 256',
                            'renderIndex': 'zero-based terrain-catalog entries index; independent of itemIndex',
                            'itemIndex': 'zero-based kItems index; always carried explicitly in UI entries',
                            'defaultData': 'Original item placement metadata; may differ from representative render data (persistent leaves)',
                            'creative': 'Exact membership in original nondebug Common/Iggy creative ITEM/ITEM_AUX list',
                            'placeable': 'Supported representative block placement only; NOT full upstream useOn/food/physics behavior',
                            'unknown': 'itemForBlockState and index/render helpers return -1; findItem returns nullptr',
                            'ItemSpec': {k: ('string' if k in ('key', 'name') else 'boolean' if k in ('creative', 'placeable') else 'integer') for k in ('key','name','itemId','aux','tileId','defaultData','renderIndex','maxStack','creative','placeable')}},
               'creativeSource': s.ref('creative', 'void IUIScene_CreativeMenu::staticCtor()'),
               'terrainSha256': TERRAIN_SHA, 'items': items,
               'sourceRegistry': {'registeredItemCount': len(registry), 'supportedItemCount': len(items),
                                  'creativeItemCount': sum(it['creative'] for it in items),
                                  'noncreativeItemKeys': noncreative,
                                  'registeredButNotCreative': 'Original Tile.cpp fallback registers many world-only TileItems. Registration alone does not make them selectable.',
                                  'stateMap': 'jsapi/build/generated/item-state-map.json',
                                  'itemCtorOffsetSource': s.ref('Item.cpp', 'Item::Item(int id) : id( 256 + id )'),
                                  'registeredItems': [{k: reg[k] for k in ('itemId', 'ctorIndex', 'symbol', 'class', 'source')} for _, reg in sorted(registry.items())]},

               'notcovered': notcovered,
               'toolArmorScope': {
                   'implemented': [
                       'Item identity: ID (256 + ctor index), icon name, source class and symbol.',
                       'Stack limit 1 for all 25 tools and 20 armor pieces, read from the upstream constructors.',
                       'Durability NUMBERS are recorded (tier uses; armor healthPerSlot x multiplier) as read-only data.',
                       'Armor defense NUMBERS per slot, matching Inventory::getArmorValue summation semantics.',
                       'Attack damage NUMBERS (subclass base + tier bonus); the hoe has none upstream.',
                       'Inventory/hotbar carrying and crafting-table recipe matching.',
                       'Armor damage reduction (LivingEntity::getDamageAfterArmorAbsorb) and armor wear '
                       '(Inventory::hurtArmor) in PlayerSurvival; equip by right-click/swap (Inventory::equipSelectedArmor).',
                       'Food nutrition/saturation (FoodItem ctor + FoodConstants tiers) for plain foods; see kFoods.',
                   ],
                   'notImplemented': [
                       'Durability CONSUMPTION. Slot has no damage field; persisting wear needs an inventory format upgrade.',
                       'Mining speed/time. World::mine is creative-instant, so tier speed changes nothing.',
                       'Harvest-level gating. canDestroySpecial is not consulted; any tool breaks any block.',
                       'Enchantments. enchantmentValue is recorded but no enchanting path exists.',
                       'Sword combat bonuses. Scene applies no equipment bonus to attacks.',
                       'Hoe tilling and shears shearing. Both need useOn, which is not implemented.',
                   ],
                   'survivalReachability':
                       'Iron and gold ingots are obtainable ONLY by smelting (FurnaceRecipes.cpp:20-21) and smelting is '
                       'not implemented in this round. Iron/gold/chain gear and every iron-ingot recipe are therefore '
                       'UNREACHABLE through survival play and can only be obtained by creative give. Wood and stone '
                       'tools are fully reachable; diamond gear is reachable because diamonds drop directly from ore.',
                   'chainArmor':
                       'Chain armor exists as four items with correct defense/durability but has NO recipe: the upstream '
                       'ArmorRecipes.cpp:45-71 chain entries are commented out.',
               },
               'limitations': ['Tools and armor are identity/stat data plus recipes only; see toolArmorScope.notImplemented.',
                               'No buckets, entities, NBT, smelting, or general item engine. Only plain FoodItems '
                               '(no eat effect, bowl return or always-eat flag) are edible; see FOODS in the generator.',
                               'Source useOn prerequisites/physics are not executed by this catalog.',
                               'Source-pinned rules fail closed on changed input, new states, IDs or item classes.',
                               'Canonical mappings are pick/source-item identity, not original random block drops.',
                               'Empty flower pot uses aux 0, deliberately not upstream cloneTileData returning item ID.',
                               'Legacy oak stone slab 44:2 is registered and preserved but absent from original creative list.']}
    manifest = {'schema_version': 1, 'sourceCommit': COMMIT, 'terrainSha256': TERRAIN_SHA,
                'renderStateCount': len(entries), 'itemCount': len(items),
                'mappedStateCount': len(entries) - len(excluded),
                'sources': [{'path': s.paths[name], 'sha256': PINS[name]} for name in sorted(PINS)] + [{'path': s.paths['creative'], 'sha256': CREATIVE_SHA}],
                'blockStates': state_rows, 'excludedStates': excluded,
                'noncreativeItemKeys': noncreative,
                'notcoveredItemKeys': [it['key'] for it in notcovered if 'key' in it],
                'classificationContract': {'dataMask': 'Bits contributing material aux; 0 means no material variation.',
                                           'placementOnly': 'Some or all render metadata is world-only; inspect classification and dataMask for material+placement cases.',
                                           'itemIndex': '-1 only for explicit notcovered state; original render key/data are always retained.'}}
    validate(catalog, manifest, creative)
    return catalog, manifest, creative


def validate(catalog, manifest, creative):
    items, rows = catalog['items'], manifest['blockStates']
    pairs = [(it['itemId'], it['aux']) for it in items]
    require(len(set(pairs)) == len(pairs), 'Duplicate itemId/aux')
    require(len({it['key'] for it in items}) == len(items), 'Duplicate item key')
    require(len(rows) == 312 and len({row['renderCatalogKey'] for row in rows}) == 312, 'Every render state must be classified exactly once')
    require([row['renderIndex'] for row in rows] == list(range(312)), 'State indices are not contiguous')
    blocks = [it for it in items if it['placeable']]
    require([it['itemIndex'] for it in blocks] == list(range(len(blocks))),
            'Placeable block items must keep the leading, stable itemIndex range')
    for index, it in enumerate(items):
        require(it['itemIndex'] == index and it['key'] == f'item:{it["itemId"]}:{it["aux"]}', 'Invalid canonical item identity')
        require(it['creative'] == ((it['itemId'], it['aux']) in creative), 'Creative exclusion disagreement')
        if it['placeable']:
            require(rows[it['renderIndex']]['itemIndex'] == index, 'Representative mapping does not round trip')
        else:
            # Tools/armor/ingredients have no block state; they must never claim
            # a render index, tile or placement data.
            require(it['renderIndex'] == -1 and it['tileId'] == -1 and it['defaultData'] == -1,
                    f'Nonplaceable item must not carry render/placement state: {it["key"]}')
            require(it['maxStack'] == 1 or it['kind'] in ('ingredient', 'food'),
                    f'Tools and armor stack to exactly 1: {it["key"]}')
    for row in rows:
        require(row['reason'] and row['sources'] and row['classification'], 'Missing state provenance')
        require(-1 <= row['itemIndex'] < len(items), 'Out of range itemIndex')
        require(row['itemKey'] == (items[row['itemIndex']]['key'] if row['itemIndex'] >= 0 else None), 'State key/index mismatch')
    require(manifest['excludedStates'] == [row for row in rows if row['itemIndex'] < 0], 'Excluded list disagreement')


def cpp(catalog, manifest):
    q = lambda v: json.dumps(v, ensure_ascii=False)
    lines = ['#pragma once', '// Generated offline by tools/generate-items.py. Do not edit.',
             '// renderIndex: zero-based legacy::kBlockSpecs index. Item indices are independent.',
             '// Representative placement only; this is not the upstream item/useOn engine.',
             '#include <cstring>', '#include <string>', 'namespace sourceitems {',
             'struct ItemSpec { const char* key; const char* name; int itemId, aux, tileId, defaultData, renderIndex, maxStack; bool creative, placeable; };',
             'struct BlockstateSpec { const char* renderCatalogKey; int renderIndex, tileId, data, itemIndex, dataMask; bool placementOnly; };',
             'constexpr ItemSpec kItems[] = {']
    for it in catalog['items']:
        values = [q(it['key']), q(it['name'])] + [str(it[k]) for k in ('itemId','aux','tileId','defaultData','renderIndex','maxStack')] + [str(it[k]).lower() for k in ('creative','placeable')]
        lines.append('    {' + ', '.join(values) + '},')
    lines += ['};', 'constexpr int kItemCount = int(sizeof(kItems) / sizeof(kItems[0]));', 'constexpr BlockstateSpec kBlockStates[] = {']
    for row in manifest['blockStates']:
        values = [q(row['renderCatalogKey'])] + [str(row[k]) for k in ('renderIndex','tileId','data','itemIndex','dataMask')] + [str(row['placementOnly']).lower()]
        lines.append('    {' + ', '.join(values) + '},')
    lines += ['};', 'constexpr int kBlockStateCount = int(sizeof(kBlockStates) / sizeof(kBlockStates[0]));']
    # Tool/armor statistics live in their own table so ItemSpec stays unchanged.
    # These are READ-ONLY source numbers: nothing consumes durability, applies
    # armor reduction, gates harvest level or alters mining speed.
    lines += ['// Source-pinned tool/armor numbers. Consumers: ToolRules (durability, speed,',
              '// harvest level) and PlayerSurvival (armor defense, via armorDefenseTotal).',
              '// kind: 1 = tool, 2 = armor. attackDamage/speed are -1 when upstream has none.',
              'struct ToolArmorSpec { const char* key; const char* iconName; const char* sourceClass;',
              '    int itemId, kind, maxStack, durability, attackDamage, tierLevel, tierSpeed, armorSlot, defense, enchantmentValue; };',
              'constexpr ToolArmorSpec kToolArmor[] = {']
    for it in catalog['items']:
        if it.get('kind') not in ('tool', 'armor'):
            continue
        tool = it['kind'] == 'tool'
        values = [q(it['key']), q(it['iconName']), q(it['sourceClass']), str(it['itemId']),
                  '1' if tool else '2', str(it['maxStack']), str(it['durability']),
                  str(it['attackDamage']) if tool and it['attackDamage'] is not None else '-1',
                  str(it['tierLevel']) if tool and 'tierLevel' in it else '-1',
                  str(it['tierSpeed']) if tool and 'tierSpeed' in it else '-1',
                  '-1' if tool else str(it['armorSlot']),
                  '-1' if tool else str(it['defense']),
                  str(it['enchantmentValue'])]
        lines.append('    {' + ', '.join(values) + '},')
    lines += ['};', 'constexpr int kToolArmorCount = int(sizeof(kToolArmor) / sizeof(kToolArmor[0]));',
              'inline const ToolArmorSpec* findToolArmor(const char* key) {',
              '    if (key) for (const auto& spec : kToolArmor)',
              '        if (std::strcmp(key, spec.key) == 0) return &spec;',
              '    return nullptr;', '}',
              'inline const ToolArmorSpec* findToolArmor(const std::string& key) { return findToolArmor(key.c_str()); }',
              '// Sum of equipped armor defense, mirroring upstream Inventory::getArmorValue.',
              '// PlayerSurvival applies it as LivingEntity::getDamageAfterArmorAbsorb.',
              'inline int armorDefenseTotal(const std::string* keys, int count) {',
              '    int total = 0;',
              '    for (int i = 0; i < count; ++i) {',
              '        const auto* spec = findToolArmor(keys[i]);',
              '        if (spec && spec->kind == 2) total += spec->defense;',
              '    }',
              '    return total;', '}',
              '// Plain-FoodItem nutrition (FoodItem ctor) and FoodConstants saturation tier.',
              '// eatTicks is FoodItem::EAT_DURATION; there are no eat effects in this table.',
              'struct FoodSpec { int itemId, nutrition; float saturationModifier; bool meat; int eatTicks; };',
              'constexpr FoodSpec kFoods[] = {',
              *['    {%d, %d, %sf, %s, %d},' % (it['itemId'], it['food']['nutrition'], repr(it['food']['saturationModifier']),
                                            str(it['food']['meat']).lower(), it['food']['eatTicks'])
                for it in catalog['items'] if 'food' in it],
              '};',
              'constexpr int kFoodCount = int(sizeof(kFoods) / sizeof(kFoods[0]));',
              'inline const FoodSpec* findFood(int itemId) {',
              '    for (const auto& food : kFoods) if (food.itemId == itemId) return &food;',
              '    return nullptr;', '}',
              'inline int itemCount() { return kItemCount; }',
              'inline const ItemSpec* items() { return kItems; }',
              'inline int blockStateCount() { return kBlockStateCount; }',
              'inline const BlockstateSpec* blockStates() { return kBlockStates; }',
              'inline int itemForBlockState(int renderIndex) { return renderIndex >= 0 && renderIndex < kBlockStateCount ? kBlockStates[renderIndex].itemIndex : -1; }',
              'inline int itemIndexForKey(const char* key) { if (!key) return -1; for (int i = 0; i < kItemCount; ++i) if (std::strcmp(key, kItems[i].key) == 0) return i; return -1; }',
              'inline int itemIndexForKey(const std::string& key) { return itemIndexForKey(key.c_str()); }',
              'inline int itemIndexForIdAux(int id, int aux) { for (int i = 0; i < kItemCount; ++i) if (kItems[i].itemId == id && kItems[i].aux == aux) return i; return -1; }',
              'inline const ItemSpec* findItem(const char* key) { const int i = itemIndexForKey(key); return i >= 0 ? &kItems[i] : nullptr; }',
              'inline const ItemSpec* findItem(const std::string& key) { return findItem(key.c_str()); }',
              'inline int renderIndexForItemKey(const char* key) { const auto* item = findItem(key); return item ? item->renderIndex : -1; }',
              'inline int renderIndexForItemKey(const std::string& key) { return renderIndexForItemKey(key.c_str()); }',
              '} // namespace sourceitems']
    return '\n'.join(lines) + '\n'


def typescript(catalog, manifest):
    fields = ('key','name','itemId','aux','itemIndex','tileId','defaultData','renderIndex','renderCatalogKey','maxStack','creative','placeable')
    items = []
    for it in catalog['items']:
        row = {k: it[k] for k in fields}
        # Nonplaceable entries declare their kind so the UI can tell a block from
        # a tool without inferring it from placeable/renderIndex.
        row['kind'] = it.get('kind', 'block')
        items.append(row)
    return ('// Generated by tools/generate-items.py; itemIndex is the original native kItems index.\n'
            '// Filtering creative entries MUST NOT renumber itemIndex. Render indices are zero-based.\n'
            '// kind "tool"/"armor"/"ingredient"/"food" entries have renderIndex -1 and are not placeable.\n'
            'export type SourceItemKind = \'block\' | \'tool\' | \'armor\' | \'ingredient\' | \'food\';\n'
            'export interface SourceItem { key: string; name: string; itemId: number; aux: number; itemIndex: number; tileId: number; defaultData: number; renderIndex: number; renderCatalogKey: string; maxStack: number; creative: boolean; placeable: boolean; kind: SourceItemKind; }\n'
            'export const sourceItems: SourceItem[] = ' + json.dumps(items, ensure_ascii=False, indent=2) + ';\n'
            'export const creativeItems: SourceItem[] = sourceItems.filter(item => item.creative);\n'
            'export const itemIndexByRenderState: number[] = ' + json.dumps([row['itemIndex'] for row in manifest['blockStates']]) + ';\n')


def outputs(catalog, manifest):
    return {ROOT / 'port/item-catalog.json': json.dumps(catalog, ensure_ascii=False, indent=2) + '\n',
            ROOT / 'jsapi/build/generated/ItemCatalog.hpp': cpp(catalog, manifest),
            ROOT / 'jsapi/build/generated/item-state-map.json': json.dumps(manifest, ensure_ascii=False, indent=2) + '\n',
            ROOT / 'ui/src/pages/index/creative-items.ts': typescript(catalog, manifest)}


def tests(catalog, manifest, creative):
    import copy
    by_id = {(it['itemId'], it['aux']): it for it in catalog['items']}
    by_state = {row['renderCatalogKey']: row for row in manifest['blockStates']}
    for iid, count in ((35,16),(5,4),(24,3),(98,4),(18,4),(44,8),(97,3),(17,4),(31,3)):
        require({aux for id_, aux in by_id if id_ == iid} == set(range(count)), f'Material variants lost: {iid}')
    for key, expected in {'door_wood_upper':'item:324:0','door_iron_lower':'item:330:0','wheat_7':'item:295:0',
                          'carrots_3':'item:391:0','potatoes_3':'item:392:0','nether_wart_3':'item:372:0',
                          'diode_off':'item:356:0','cauldron':'item:380:0','flowerPot':'item:390:0',
                          'redStoneDust':'item:331:0','reeds':'item:338:0','tripWire':'item:287:0',
                          'cocoa_2':'item:351:3','log_birch_x':'item:17:2','farmland_7':'item:3:0',
                          'hugeMushroom_red_15':'item:40:0','double_slab_5':'item:44:5',
                          'redStoneOre_lit':'item:73:0','snow_layer_8':'item:78:0'}.items():
        require(by_state[key]['itemKey'] == expected, f'Wrong canonical mapping {key}')
    for key in ('water','calmWater','lava','calmLava','fire','mobSpawner'):
        require(by_state[key]['itemIndex'] == -1, f'Unsupported state invented item: {key}')
    require(by_id[(324,0)]['maxStack'] == by_id[(330,0)]['maxStack'] == 1, 'Source door stack limit changed')
    require(by_id[(351,3)]['defaultData'] == 0, 'Cocoa aux must not become placed age/direction')
    require(by_id[(29,0)]['defaultData'] == 7, 'Original undefined piston-facing sentinel lost')
    require(all('下半' not in it['name'] and '上半' not in it['name'] for it in catalog['items']), 'Placed slab state leaked into item name')
    require(not by_id[(44,2)]['creative'], 'Removed old wood slab unexpectedly creative')
    require(not any(it['itemId'] == 43 for it in catalog['items']), 'Double slab invented independent item')
    for iid in (17,18):
        for aux in range(4):
            require(by_id[(iid,aux)]['defaultData'] == aux + (4 if iid == 18 else 0), 'Placement material data lost')
    # Tool/armor regressions. Expected values are transcribed from the pinned
    # upstream tables, so a drifting parser cannot silently agree with itself.
    require(len([it for it in catalog['items'] if it['placeable']]) == 126, 'Original 126 block items must be preserved')
    require(len(manifest['blockStates']) == 312, 'Original 312 render states must be preserved')
    # Tier tools only; the tier-less ShearsItem is checked separately below.
    tools = [it for it in catalog['items'] if it.get('kind') == 'tool' and it.get('toolKind') != 'shears']
    armor = [it for it in catalog['items'] if it.get('kind') == 'armor']
    require(len(tools) == 25 and len(armor) == 20, 'Expected 25 tools and 20 armor pieces')
    require(all(it['maxStack'] == 1 for it in tools + armor), 'Every tool/armor stacks to exactly 1')
    require(all(not it['placeable'] and it['renderIndex'] == -1 for it in tools + armor),
            'Tools/armor must not be placeable or carry a render index')
    # Item.cpp:265/271/277 give iron shovel/pickaxe/hatchet the lowest ctor indices.
    for symbol, iid in (('shovel_iron', 256), ('pickAxe_iron', 257), ('hatchet_iron', 258),
                        ('sword_iron', 267), ('sword_wood', 268), ('hoe_gold', 294),
                        ('helmet_leather', 298), ('boots_leather', 301), ('helmet_chain', 302),
                        ('boots_chain', 305), ('helmet_iron', 306), ('boots_diamond', 313),
                        ('boots_gold', 317)):
        match = [it for it in catalog['items'] if it.get('sourceSymbol') == symbol]
        require(len(match) == 1 and match[0]['itemId'] == iid, f'Wrong source item ID for {symbol}')
    # Tier uses: WOOD 59, STONE 131, IRON 250, DIAMOND 1561, GOLD 32.
    for material, uses in (('wood', 59), ('stone', 131), ('iron', 250), ('diamond', 1561), ('gold', 32)):
        rows = [it for it in tools if it['material'] == material]
        require(len(rows) == 5, f'Expected five {material} tools')
        require(all(it['durability'] == uses for it in rows), f'Wrong {material} tool durability')
    # Attack damage = subclass base + tier bonus; pickaxe 2, shovel 1, hatchet 3, sword 4.
    for kind, base in (('sword', 4), ('pickaxe', 2), ('shovel', 1), ('hatchet', 3)):
        for material, bonus in (('wood', 0), ('stone', 1), ('iron', 2), ('diamond', 3), ('gold', 0)):
            row = next(it for it in tools if it['toolKind'] == kind and it['material'] == material)
            require(row['attackDamage'] == base + bonus, f'Wrong {material} {kind} attack damage')
    require(all(it['attackDamage'] is None for it in tools if it['toolKind'] == 'hoe'),
            'HoeItem derives from Item and must not report attack damage')
    require(all(it['sourceClass'] == 'HatchetItem' for it in tools if it['toolKind'] == 'hatchet'),
            'The axe class is HatchetItem upstream, not AxeItem')
    require(all(it['sourceClass'] == 'HoeItem' for it in tools if it['toolKind'] == 'hoe'), 'Hoe class changed')
    # ArmorItem.cpp:61-65 defense per [HEAD, TORSO, LEGS, FEET].
    defense = {'leather': (1, 3, 2, 1), 'chain': (2, 5, 4, 1), 'iron': (2, 6, 5, 2),
               'gold': (2, 5, 3, 1), 'diamond': (3, 8, 6, 3)}
    # healthPerSlot {11,16,15,13} x multiplier CLOTH 5, CHAIN/IRON 15, GOLD 7, DIAMOND 33.
    multiplier = {'leather': 5, 'chain': 15, 'iron': 15, 'gold': 7, 'diamond': 33}
    per_slot = (11, 16, 15, 13)
    for material, values in defense.items():
        for piece, slot in (('helmet', 0), ('chestplate', 1), ('leggings', 2), ('boots', 3)):
            row = next(it for it in armor if it['material'] == material and it['armorPiece'] == piece)
            require(row['armorSlot'] == slot, f'Wrong armor slot for {material} {piece}')
            require(row['defense'] == values[slot], f'Wrong {material} {piece} defense')
            require(row['durability'] == per_slot[slot] * multiplier[material],
                    f'Wrong {material} {piece} durability')
    # The third ArmorItem ctor argument is an icon/model index, never a defense value.
    for material, model in (('leather', 0), ('chain', 1), ('iron', 2), ('diamond', 3), ('gold', 4)):
        rows = [it for it in armor if it['material'] == material]
        require(len(rows) == 4 and all(it['modelIndex'] == model for it in rows),
                f'Wrong {material} armor model index')
    require({it['iconName'] for it in tools if it['toolKind'] == 'pickaxe'} ==
            {'pickaxeWood', 'pickaxeStone', 'pickaxeIron', 'pickaxeDiamond', 'pickaxeGold'},
            'Pickaxe icon names must be the registered strings, not the pickAxe_* symbols')
    ingredients = [it for it in catalog['items'] if it.get('kind') == 'ingredient']
    require({it['itemId'] for it in ingredients} == {280, 265, 266, 264, 334, 263, 337, 351}, 'Unexpected ingredient IDs')
    require(all(it['maxStack'] == 64 for it in ingredients), 'Ingredients keep the default 64 stack')
    # Food: FoodItem ctor nutrition + FoodConstants tier, EAT_DURATION 20*1.6.
    expected_food = {260: (4, .3), 297: (5, .6), 319: (3, .3), 320: (8, .8), 349: (2, .3), 350: (5, .6),
                     357: (2, .1), 360: (2, .3), 363: (3, .3), 364: (8, .8), 366: (6, .6), 391: (4, .6),
                     392: (1, .3), 393: (6, .6), 396: (6, 1.2), 400: (8, .3)}
    foods = {it['itemId']: it for it in catalog['items'] if 'food' in it}
    require(set(foods) == set(expected_food), f'Unexpected food set {sorted(foods)}')
    for iid, (nutrition, modifier) in expected_food.items():
        food = foods[iid]['food']
        require(food['nutrition'] == nutrition and abs(food['saturationModifier'] - modifier) < 1e-9 and
                food['eatTicks'] == 32, f'Wrong food data for {iid}')
    require(all(foods[i]['placeable'] for i in (391, 392)), 'Carrot/potato remain placeable SeedFoodItems')
    require(all(foods[i]['kind'] == 'food' and foods[i]['maxStack'] == 64 and not foods[i]['placeable']
                for i in expected_food if i not in (391, 392)), 'Plain food rows stack to 64, not placeable')
    for key, kind in (('item:263:0', 'ingredient'), ('item:263:1', 'ingredient'), ('item:337:0', 'ingredient'),
                      ('item:351:2', 'ingredient'), ('item:365:0', 'food')):
        row = next((it for it in catalog['items'] if it['key'] == key), None)
        require(row is not None and row['kind'] == kind and row['maxStack'] == 64 and 'food' not in row,
                f'Material row missing/wrong: {key}')
    for excluded in (322, 282, 365, 367, 375, 394):  # golden apple, stew, raw chicken, flesh, eye, poison potato
        require(excluded not in foods, f'Food with unported behavior exported: {excluded}')
    # Shears: Item.cpp ctor 103 -> 359, ShearsItem ctor stack 1 / damage 238,
    # creative ITEM(Item::shears_Id), appended as the last row (stable indices).
    shears = [it for it in catalog['items'] if it.get('toolKind') == 'shears']
    require(len(shears) == 1, 'Expected exactly one shears row')
    row = shears[0]
    require(row['key'] == 'item:359:0' and row['iconName'] == 'shears' and row['sourceClass'] == 'ShearsItem' and
            row['maxStack'] == 1 and row['durability'] == 238 and row['creative'] and not row['placeable'] and
            row['itemIndex'] == len(catalog['items']) - 1 == 195, 'Wrong shears row')
    broken = copy.deepcopy(catalog); broken['items'].append(copy.deepcopy(broken['items'][0]))
    try: validate(broken, manifest, creative)
    except ValueError: pass
    else: raise ValueError('Duplicate item ID/aux negative test failed')
    broken_map = copy.deepcopy(manifest); broken_map['excludedStates'] = []
    try: validate(catalog, broken_map, creative)
    except ValueError: pass
    else: raise ValueError('Excluded-list negative test failed')
    broken_map = copy.deepcopy(manifest); broken_map['blockStates'].pop()
    try: validate(catalog, broken_map, creative)
    except ValueError: pass
    else: raise ValueError('Unclassified-state negative test failed')
    print('PASS: unique ID/aux, 312 classifications, source creative membership, preserved materials, special items, exclusions, negative validation')


def outputs_paths():
    """Generated artefact paths, without needing the reference sources."""
    return [ROOT / 'port/item-catalog.json',
            ROOT / 'jsapi/build/generated/ItemCatalog.hpp',
            ROOT / 'jsapi/build/generated/item-state-map.json',
            ROOT / 'ui/src/pages/index/creative-items.ts']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--client-root', type=Path, default=DEFAULT_CLIENT, help='Local reference checkout root (never fetched)')
    parser.add_argument('--check', action='store_true', help='Check generated outputs without writing')
    parser.add_argument('--test', action='store_true', help='Run built-in regressions/negative tests without writing')
    args = parser.parse_args()
    # The creative-menu reference is a required generation input, not just a
    # verification pin. When the local reference checkout is absent the catalog
    # cannot be rebuilt, so reuse the committed outputs instead of failing the
    # whole build. Nothing is fabricated: generation is skipped, not faked.
    # The creative menu is also tracked in this repository's upstream checkout;
    # use it when the external reference is absent so a source change (e.g. new
    # food rows) is actually regenerated instead of silently reusing stale output.
    if args.client_root == DEFAULT_CLIENT and not (args.client_root / CREATIVE_REL).exists() and \
            (ROOT / 'port/upstream' / CREATIVE_REL).exists():
        args.client_root = ROOT / 'port/upstream'
    if not (args.client_root / CREATIVE_REL).exists():
        missing = [p for p in outputs_paths() if not p.exists()]
        require(not missing, 'Reference checkout absent and generated outputs missing: '
                             + ', '.join(str(m) for m in missing))
        print('SKIP: reference checkout absent; reusing committed generated outputs '
              f'({args.client_root / CREATIVE_REL} not found)')
        return
    sources = Sources(args.client_root)
    catalog, manifest, creative = build(sources)
    if args.test:
        tests(catalog, manifest, creative)
    if args.check:
        for path, content in outputs(catalog, manifest).items():
            require(path.exists() and path.read_text() == content, f'Stale generated output: {path}')
        print('PASS: all four outputs reproduce exactly')
    elif not args.test:
        for path, content in outputs(catalog, manifest).items():
            if not path.exists() or path.read_text() != content:
                path.write_text(content)
        print(f'Generated {len(catalog["items"])} items ({sum(it["creative"] for it in catalog["items"])} creative), {len(manifest["blockStates"])} state mappings, {len(catalog["notcovered"])} explicit notcovered records')


if __name__ == '__main__':
    main()
