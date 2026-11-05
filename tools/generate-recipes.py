#!/usr/bin/env python3
"""Offline, source-pinned crafting recipe extraction for the shaped-recipe subset.

Reads the pinned upstream recipe tables, resolves every Tile::/Item:: symbol with
the SAME mechanism tools/generate-items.py uses (Sources + constants_from +
parse_registry), and emits exactly two outputs:

    port/recipes.json
    jsapi/build/generated/Recipes.hpp

Scope boundaries that this tool deliberately enforces instead of guessing:
  * Only ShapedRecipy data is extracted. Shapeless recipes, FurnaceRecipes
    (smelting), FoodRecipies and ClothDyeRecipes are NOT covered.
  * A recipe is emitted only when its product key AND every ingredient resolve
    to entries that really exist in port/item-catalog.json. Nothing is invented.
  * Any call site this restricted grammar cannot prove is recorded in
    notcovered with the source line and the reason, never silently dropped.

This is a recipe data adapter plus a matcher. It is NOT an item engine: no
durability, no mining speed, no harvest level, no enchanting, no armor damage
reduction, no sword bonus, no hoe tilling, no shears shearing, no smelting.
"""
import argparse
import hashlib
import importlib.util
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WORLD = ROOT / 'port/upstream/Minecraft.World'
TOOLS = Path(__file__).resolve().parent
CATALOG = ROOT / 'port/item-catalog.json'
JSON_OUT = ROOT / 'port/recipes.json'
HPP_OUT = ROOT / 'jsapi/build/generated/Recipes.hpp'


def _load_items_module():
    """Reuse generate-items.py verbatim; never re-implement symbol resolution."""
    spec = importlib.util.spec_from_file_location('generate_items', TOOLS / 'generate-items.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


gi = _load_items_module()
require = gi.require

# Recipe-table source pins. Separate from generate-items.py PINS so an upstream
# recipe edit fails this tool closed without touching the item catalog.
RECIPE_PINS = {
    'Recipes.cpp': 'e7382deaea5ea58c1e25db2f78d8fb0f7cd5fe56f40255844d2ef54cd44109f5',
    'Recipes.h': '10b80465feff0bcf34044db7c358fea9f2c2a5c098b47848834ea41d54ed313e',
    'Recipy.h': '97f00d725a1830a15aa4dc42e14ad5ae121c23b1b8e835c0281057dc2e9a4b27',
    'ShapedRecipy.cpp': '85ca32639210d7dd66bd11a1fabad222356a5ea2813f7038749212824eb18ab4',
    'ShapedRecipy.h': '57c840487573aedf412356da94ad2c697b540de98f311ba990c9b454902b9369',
    'ShapelessRecipy.cpp': 'c1b4a86b25b15a43eef2409af4cbdce0719ce220977bf65556c4157ba3e35d1f',
    'CraftingContainer.cpp': 'a8ea81935430898eb9beee4f713b646240aeb4fffc7e62b493f8c93bb39e1bee',
    'ToolRecipies.cpp': 'fbce8b5faaabd537a942b9ff0f0b01373a9eab72ff9eff931356ab24c0837c55',
    'ToolRecipies.h': 'fd6f424a3830f4bf90e5c22c11d5961176feb17835380865df6f8607581069ae',
    'WeaponRecipies.cpp': '07bf5502bfe575b402fe8e9caf5860d07d28f26797af85dc651da90c15b02526',
    'WeaponRecipies.h': '0a2f74c79222c90586335d43b98d57dddd3e701c4554f48e3d77bb8ff3341711',
    'ArmorRecipes.cpp': '49a21d0d287d076624b93704a99f67ed1365a168ee9866366eea55fd44f7a7a6',
    'ArmorRecipes.h': 'efa8ee3b58db1a5463803d0a28b7937473b4cd29a63c6bc91eb0ccb8f64c99bd',
    'StructureRecipies.cpp': '69a57902a9fd55720e9421de300a843fc976f78e9cf49213dc7189e78742e738',
    'OreRecipies.cpp': 'd6ce8e27276dd846850e86275e41783a68787fab9113e1f96c8784aca51a8a2a',
    'OreRecipies.h': '9dcb460bd726890826c770da32a0c55782c897491fd97a9f8ea626bb09d147cd',
    'FurnaceRecipes.cpp': '17a81b16479f14cd78bfe6e154aca6dac36a0dc9a04e65acfcc1adfdd9594356',
    # Aux-value headers referenced by recipe ingredients but not loaded by
    # generate-items.py. Pinned here so an unresolved aux is a real finding and
    # never a false "unknown constant" excuse for skipping a recipe.
    'WallTile.h': 'ee154a80d724db8d2d12b2b759cbf80e5ac5df3d448de46cc3e4463b44026362',
    'QuartzBlockTile.h': '7e50977f32fb27e3011f3dae393e52768df5d7395bb67836070bb32a5d20d530',
    'CoalItem.h': '2f4312e368beaf46ea180b7ce3438ac8d728a0d5432f6b1262ba403a2c834f54',
}
# Consulted only to COUNT the call sites this round does not cover. Their text is
# never transcribed into an output, so they are read by presence, not by digest;
# their live digest is recorded in the manifest for audit.
COUNT_ONLY = ('FoodRecipies.cpp', 'ClothDyeRecipes.cpp')

GROUPS = {'T': 'tool', 'A': 'armour', 'S': 'structure', 'V': 'transport',
          'M': 'mechanism', 'F': 'food', 'D': 'decoration'}
ANY_AUX = -1


def digest(data):
    return hashlib.sha256(data).hexdigest()


class Sources(gi.Sources):
    """generate-items.py Sources plus the pinned recipe tables."""

    def __init__(self, client):
        super().__init__(client)
        for name, expected in RECIPE_PINS.items():
            self.load(name, WORLD / name, expected)
        for name in COUNT_ONLY:
            path = WORLD / name
            require(path.exists(), f'Missing recipe source: {path}')
            self.raw[name] = path.read_text('utf-8-sig')
            self.paths[name] = str(path.relative_to(ROOT))
        self.code = {name: gi.uncomment(text) for name, text in self.raw.items()}

    def sha(self, name):
        return RECIPE_PINS.get(name) or gi.PINS.get(name) or digest((ROOT / self.paths[name]).read_bytes())

    def at(self, name, line):
        """Provenance reference carrying the RAW (commented) anchor line."""
        return {'path': self.paths[name], 'line': line, 'sha256': self.sha(name),
                'anchor': self.raw[name].splitlines()[line - 1].strip()}

    def line_of(self, name, offset):
        return self.code[name][:offset].count('\n') + 1

    def anchor(self, name, needle):
        """Assert a structural anchor exists in uncommented code; return its line."""
        require(needle in self.code[name], f'Missing source anchor in {name}: {needle}')
        return self.at(name, self.line_of(name, self.code[name].index(needle)))


class Skip(Exception):
    """A call site this audited grammar refuses to guess at."""


# ---------------------------------------------------------------- C++ literals

def split_args(text):
    """Split a C++ argument list on top-level commas, honoring literals."""
    args, depth, start, i = [], 0, 0, 0
    while i < len(text):
        ch = text[i]
        if ch in '"\'':
            i += 1
            while i < len(text) and text[i] != ch:
                i += 2 if text[i] == '\\' else 1
        elif ch in '([{':
            depth += 1
        elif ch in ')]}':
            depth -= 1
        elif ch == ',' and depth == 0:
            args.append(text[start:i])
            start = i + 1
        i += 1
    args.append(text[start:])
    return [a.strip() for a in args]


def call_body(text, open_paren):
    """Return (body, end) for the parenthesised call starting at open_paren."""
    depth, i = 0, open_paren
    while i < len(text):
        ch = text[i]
        if ch in '"\'':
            i += 1
            while i < len(text) and text[i] != ch:
                i += 2 if text[i] == '\\' else 1
        elif ch == '(':
            depth += 1
        elif ch == ')':
            depth -= 1
            if depth == 0:
                return text[open_paren + 1:i], i
        i += 1
    raise Skip('Unbalanced call')


def wide_string(expr):
    m = re.fullmatch(r'L"((?:[^"\\]|\\.)*)"', expr.strip())
    if not m:
        raise Skip(f'Not a wide string literal: {expr}')
    return m[1]


def wide_char(expr):
    m = re.fullmatch(r"L?'(.)'", expr.strip())
    if not m:
        raise Skip(f'Not a character literal: {expr}')
    return m[1]


# ------------------------------------------------------------ symbol resolution

class Symbols:
    """Tile::/Item:: symbol -> numeric id, from the generate-items.py registry."""

    def __init__(self, s):
        self.constants = gi.constants_from(s)
        tiles, registry = gi.parse_registry(s, self.constants)
        self.tiles = {tile['symbol']: tid for tid, tile in tiles.items()}
        self.items = {reg['symbol']: iid for iid, reg in registry.items() if 'registration' in reg}
        self.registry = registry

    def number(self, expr):
        try:
            return gi.number(expr, self.constants)
        except (ValueError, SyntaxError) as ex:
            raise Skip(f'Unresolvable constant expression {expr!r}: {ex}')

    def target(self, expr):
        """Resolve a Tile*/Item* expression (possibly static_cast wrapped)."""
        expr = expr.strip()
        cast = re.fullmatch(r'static_cast<\s*\w+\s*\*\s*>\s*\((.*)\)', expr, re.S)
        if cast:
            expr = cast[1].strip()
        expr = re.sub(r'^\(\s*\w+\s*\*\s*\)', '', expr).strip()
        m = re.fullmatch(r'(Tile|Item)::(\w+)', expr)
        if not m:
            raise Skip(f'Not a plain Tile::/Item:: symbol: {expr}')
        table = self.tiles if m[1] == 'Tile' else self.items
        if m[2] not in table:
            raise Skip(f'Unregistered symbol {expr}')
        return table[m[2]], m[1]

    def instance(self, expr):
        """Resolve `new ItemInstance(target[, count[, aux]])` -> (id, aux, count)."""
        expr = expr.strip()
        m = re.match(r'new\s+ItemInstance\s*\(', expr)
        if not m:
            raise Skip(f'Not an ItemInstance construction: {expr}')
        body, end = call_body(expr, m.end() - 1)
        require(not expr[end + 1:].strip(), f'Trailing text after ItemInstance: {expr}')
        parts = split_args(body)
        if not 1 <= len(parts) <= 3:
            raise Skip(f'Unsupported ItemInstance arity: {expr}')
        iid, _ = self.target(parts[0])
        count = self.number(parts[1]) if len(parts) > 1 else 1
        aux = self.number(parts[2]) if len(parts) > 2 else 0
        return iid, aux, count


# -------------------------------------------------------------- shape handling

def grid_from_rows(rows):
    """Validate a rectangular shape and return (width, height, cell chars)."""
    rows = [r for r in rows if r != '']
    if not rows:
        raise Skip('Empty shape')
    widths = {len(r) for r in rows}
    if len(widths) != 1:
        raise Skip(f'Ragged shape rows {rows}; upstream width/height would be wrong')
    width, height = widths.pop(), len(rows)
    if width > 3 or height > 3:
        raise Skip(f'Shape larger than the 3x3 grid: {rows}')
    return width, height, ''.join(rows)


def make_recipe(s, name, line, product, rows, mapping, group):
    width, height, flat = grid_from_rows(rows)
    cells = [mapping.get(ch) for ch in flat]
    require(len(cells) == width * height, 'Cell count disagrees with width*height')
    if not any(cells):
        raise Skip('Shape references no mapped ingredient')
    return {'product': {'itemId': product[0], 'aux': product[1], 'count': product[2]},
            'width': width, 'height': height, 'cells': cells,
            'group': GROUPS[group], 'origin': f'{name}:{line}', 'line': line,
            'source': s.at(name, line)}


# ------------------------------------------------------- inline vararg grammar

def call_sites(s, name):
    """Literal addShaped/ShapelessRecipy call sites, excluding the definitions.

    `Recipes::addShapedRecipy(ItemInstance *result, ...)` is the vararg
    implementation, not a recipe, so matching it would report the function
    signature as an unparsable recipe.
    """
    sites = []
    for m in re.finditer(r'(Recipes\s*::\s*)?addShape(d|less)Recipy\s*\(', s.code[name]):
        if m[1]:
            continue
        sites.append({'line': s.line_of(name, m.start()), 'paren': m.end() - 1,
                      'kind': 'shaped' if m[2] == 'd' else 'shapeless'})
    return sites


def parse_inline(s, sym, name, skip_lines=()):
    """Parse literal addShapedRecipy(...) call sites under a restricted grammar.

    Accepted type-string letters: s/a (wide string row), c (selector char),
    i (Item*), t (Tile*), z (ItemInstance*), g (group char). Anything else, or
    any argument that is not a literal/registered symbol, raises Skip so the
    call site lands in notcovered with its source line.

    skip_lines holds call sites already expanded by parse_matrix_registry; they
    are loop bodies, not independent recipes, and would otherwise double-report.
    """
    code, recipes, skipped = s.code[name], [], []
    for site in call_sites(s, name):
        line, kind = site['line'], site['kind']
        if line in skip_lines:
            continue
        try:
            if kind == 'shapeless':
                raise Skip('Shapeless recipes are out of scope this round')
            body, _ = call_body(code, site['paren'])
            args = split_args(body)
            product = sym.instance(args[0])
            letters = wide_string(args[1])
            rows, mapping, group, selector, pos = [], {}, None, None, 2
            for ch in letters:
                if ch in 'sa':
                    rows.append(wide_string(args[pos])); pos += 1
                elif ch == 'c':
                    selector = wide_char(args[pos]); pos += 1
                elif ch in 'it':
                    require(selector is not None, f'{name}:{line} ingredient without selector')
                    iid, got = sym.target(args[pos])
                    require(got == ('Item' if ch == 'i' else 'Tile'),
                            f'{name}:{line} type letter {ch} disagrees with {args[pos]}')
                    mapping[selector] = {'itemId': iid, 'aux': ANY_AUX}
                    pos += 1
                elif ch == 'z':
                    require(selector is not None, f'{name}:{line} ingredient without selector')
                    iid, aux, _ = sym.instance(args[pos])
                    mapping[selector] = {'itemId': iid, 'aux': aux}
                    pos += 1
                elif ch == 'g':
                    group = wide_char(args[pos]); pos += 1
                else:
                    raise Skip(f'Unsupported type letter {ch!r} in {letters!r}')
            require(pos == len(args), f'{name}:{line} type string {letters!r} does not consume every argument')
            require(group in GROUPS, f'{name}:{line} unknown group {group!r}')
            recipes.append(make_recipe(s, name, line, product, rows, mapping, group))
        except Skip as ex:
            skipped.append({'origin': f'{name}:{line}', 'kind': kind, 'reason': str(ex),
                            'source': s.at(name, line)})
    return recipes, skipped


# --------------------------------------------------- tabular sub-registry files

def table_map(s, name):
    """ADD_OBJECT(map[n], expr) rows, in source order, grouped by index."""
    rows = {}
    for m in re.finditer(r'ADD_OBJECT\(\s*map\[(\d+)\]\s*,\s*(.+?)\s*\)\s*;', s.code[name]):
        rows.setdefault(int(m[1]), []).append(m[2])
    return rows


def table_shapes(s, name, cls):
    """wstring Cls::shapes[][4] = { {L"..",..}, .. } in source order."""
    m = re.search(r'wstring\s+' + cls + r'::shapes\[\]\[4\]\s*=\s*\{(.*?)\n\};', s.code[name], re.S)
    require(m is not None, f'Missing {cls}::shapes table')
    shapes = []
    for group in re.finditer(r'\{(.*?)\}', m[1], re.S):
        rows = [lit[1] for lit in re.finditer(r'L"((?:[^"\\]|\\.)*)"', group[1])]
        require(rows, f'Empty shape group in {cls}::shapes')
        shapes.append([r for r in rows if r != ''])
    return shapes, s.at(name, s.line_of(name, m.start()))


def define_value(s, name, macro):
    m = re.search(r'#define\s+' + macro + r'\s+(\d+)', s.code[name])
    require(m is not None, f'Missing #define {macro}')
    return int(m[1])


def material_cell(sym, expr):
    iid, _ = sym.target(expr)
    return {'itemId': iid, 'aux': ANY_AUX}


def parse_matrix_registry(s, sym, cpp, header, cls, macro, stick, group):
    """Expand the shapes x materials loop of Tool/Weapon/Armor recipe tables.

    The shape-to-product pairing is `shapes[t]` against `map[t+1]`, asserted
    against the pinned loop text rather than assumed.
    """
    limit = define_value(s, header, macro)
    shapes, shapes_ref = table_shapes(s, cpp, cls)
    rows = table_map(s, cpp)
    require(len(shapes) == limit - 1, f'{cls}: {len(shapes)} shapes for {limit - 1} products')
    require(set(rows) == set(range(limit)), f'{cls}: material/product rows {sorted(rows)}')
    materials = rows[0]
    for index in range(1, limit):
        require(len(rows[index]) == len(materials), f'{cls}: row {index} length differs from materials')
    loop = s.anchor(cpp, f'for (int t=0; t<{macro}-1; t++)')
    target = s.anchor(cpp, 'map[t+1].at(m)')
    shape_use = s.anchor(cpp, 'shapes[t]')
    # The two call sites inside this loop (tile branch and item branch) are the
    # expansion of this table; the inline pass must not report them again.
    consumed = {site['line'] for site in call_sites(s, cpp)
                if f'shapes[t]' in call_body(s.code[cpp], site['paren'])[0]}
    require(len(consumed) == 2, f'{cls}: expected two loop call sites, found {sorted(consumed)}')
    refs = [shapes_ref, loop, target, shape_use,
            s.anchor(cpp, "L'X', pObjMaterial->tile"), s.anchor(cpp, "L'X', pObjMaterial->item")]
    if stick:
        refs.append(s.anchor(cpp, "L'#', Item::stick"))
        require("L'#', Item::stick" in s.code[cpp], f'{cls}: stick mapping missing')
    else:
        require("Item::stick" not in s.code[cpp], f'{cls}: unexpected stick ingredient')
    recipes = []
    for m, material in enumerate(materials):
        for t in range(limit - 1):
            mapping = {'X': material_cell(sym, material)}
            if stick:
                mapping['#'] = {'itemId': sym.items['stick'], 'aux': ANY_AUX}
            product_expr = rows[t + 1][m]
            iid, _ = sym.target(product_expr)
            line = s.line_of(cpp, s.code[cpp].index(f'ADD_OBJECT(map[{t + 1}],{product_expr})'))
            recipe = make_recipe(s, cpp, line, (iid, 0, 1), shapes[t], mapping, group)
            recipe['sources'] = refs
            recipe['expandedFrom'] = f'{cpp}:{loop["line"]} shapes[{t}] x map[0][{m}]'
            recipes.append(recipe)
    return recipes, refs, consumed


def parse_ore_registry(s, sym):
    """OreRecipies: block <-> nine-unit ingot/dust pairs."""
    cpp = 'OreRecipies.cpp'
    limit = define_value(s, 'OreRecipies.h', 'MAX_ORE_RECIPES')
    rows = table_map(s, cpp)
    require(set(rows) == set(range(limit)), f'Ore rows {sorted(rows)}')
    loop = s.anchor(cpp, 'for (int i = 0; i < MAX_ORE_RECIPES; i++)')
    refs = [loop, s.anchor(cpp, 'Tile *from'), s.anchor(cpp, 'ItemInstance *to')]
    recipes, skipped = [], []
    for index in range(limit):
        pair = rows[index]
        require(len(pair) == 2, f'Ore row {index} is not a (block, units) pair')
        line = s.line_of(cpp, s.code[cpp].index(f'ADD_OBJECT(map[{index}],{pair[0]})'))
        try:
            block, _ = sym.target(pair[0])
            unit_id, unit_aux, unit_count = sym.instance(pair[1])
            pack = make_recipe(s, cpp, line, (block, 0, 1), ['###', '###', '###'],
                               {'#': {'itemId': unit_id, 'aux': unit_aux}}, 'D')
            unpack = make_recipe(s, cpp, line, (unit_id, unit_aux, unit_count), ['#'],
                                 {'#': {'itemId': block, 'aux': ANY_AUX}}, 'D')
        except Skip as ex:
            skipped.append({'origin': f'{cpp}:{line}', 'kind': 'shaped', 'reason': str(ex),
                            'source': s.at(cpp, line)})
            continue
        for recipe in (pack, unpack):
            recipe['sources'] = refs
            recipe['expandedFrom'] = f'{cpp}:{loop["line"]} map[{index}]'
        recipes += [pack, unpack]
    return recipes, refs, skipped


def verify_chain_armor(s):
    """Chain armor must stay recipe-less: its table rows are commented out."""
    live = re.findall(r'ADD_OBJECT\(map\[\d+\],\s*Item::\w+_chain\s*\)', s.code['ArmorRecipes.cpp'])
    require(not live, 'Chain armor table rows are no longer commented out upstream')
    commented = re.findall(r'//\s*ADD_OBJECT\(map\[\d+\],\s*Item::(\w+)_chain\s*\);', s.raw['ArmorRecipes.cpp'])
    require(len(commented) == 4, f'Expected four commented chain rows, found {len(commented)}')
    first = s.raw['ArmorRecipes.cpp'][:s.raw['ArmorRecipes.cpp'].index('//\tADD_OBJECT(map[1],Item::helmet_chain);')]
    return {'pieces': sorted(commented), 'line': first.count('\n') + 1,
            'source': s.at('ArmorRecipes.cpp', first.count('\n') + 1)}


def verify_bow_arrow_commented(s):
    """Bow/arrow are commented out of WeaponRecipies; they live in Recipes.cpp."""
    code = s.code['WeaponRecipies.cpp']
    require('Item::bow' not in code and 'Item::arrow' not in code,
            'Bow/arrow are no longer commented out of WeaponRecipies.cpp')
    raw = s.raw['WeaponRecipies.cpp']
    require('Item::bow' in raw, 'Expected the commented bow block in WeaponRecipies.cpp')
    return s.at('WeaponRecipies.cpp', raw[:raw.index('Item::bow')].count('\n') + 1)


# ------------------------------------------------------------------ assembly

def build(s):
    sym = Symbols(s)
    catalog = json.loads(CATALOG.read_text())
    known = {(it['itemId'], it['aux']): it for it in catalog['items']}
    by_id = {}
    for it in catalog['items']:
        by_id.setdefault(it['itemId'], set()).add(it['aux'])

    # Matching semantics, asserted against the pinned matcher rather than assumed.
    offset_ref = s.anchor('ShapedRecipy.cpp', 'for (int xOffs = 0; xOffs <= (3 - width); xOffs++)')
    flip_ref = s.anchor('ShapedRecipy.cpp', 'if (xFlip) expected = recipeItems[(width - xs - 1) + ys * width];')
    aux_ref = s.anchor('ShapedRecipy.cpp', 'expected->getAuxValue() != Recipes::ANY_AUX_VALUE')
    # Read the sentinel from the header rather than trusting the local constant.
    sentinel = re.search(r'static\s+const\s+int\s+ANY_AUX_VALUE\s*=\s*(-?\d+)\s*;', s.code['Recipes.h'])
    require(sentinel is not None, 'Missing Recipes::ANY_AUX_VALUE declaration')
    require(int(sentinel[1]) == ANY_AUX, f'ANY_AUX_VALUE changed upstream to {sentinel[1]}')
    any_aux_ref = s.anchor('Recipes.h', sentinel[0])
    any_item_ref = s.anchor('Recipes.cpp', 'pItemInstance= new ItemInstance(pItem,1,ANY_AUX_VALUE)')
    any_tile_ref = s.anchor('Recipes.cpp', 'pItemInstance= new ItemInstance(pTile,1,ANY_AUX_VALUE)')

    candidates, skipped, tables = [], [], {}
    tools, tool_refs, tool_lines = parse_matrix_registry(
        s, sym, 'ToolRecipies.cpp', 'ToolRecipies.h', 'ToolRecipies', 'MAX_TOOL_RECIPES', True, 'T')
    weapons, weapon_refs, weapon_lines = parse_matrix_registry(
        s, sym, 'WeaponRecipies.cpp', 'WeaponRecipies.h', 'WeaponRecipies', 'MAX_WEAPON_RECIPES', True, 'T')
    armor, armor_refs, armor_lines = parse_matrix_registry(
        s, sym, 'ArmorRecipes.cpp', 'ArmorRecipes.h', 'ArmorRecipes', 'MAX_ARMOUR_RECIPES', False, 'A')
    ores, ore_refs, ore_skipped = parse_ore_registry(s, sym)
    require(len(tools) == 20, f'Expected 20 tool recipes, got {len(tools)}')
    require(len(weapons) == 5, f'Expected 5 sword recipes, got {len(weapons)}')
    require(len(armor) == 16, f'Expected 16 armor recipes, got {len(armor)}')
    # ToolRecipies shape order is pickaxe, shovel, axe, hoe; verify against map[t+1].
    order = table_map(s, 'ToolRecipies.cpp')
    expected_order = ['pickAxe', 'shovel', 'hatchet', 'hoe']
    for index, stem in enumerate(expected_order, start=1):
        require(all(re.fullmatch(r'Item::' + stem + r'_\w+', e) for e in order[index]),
                f'ToolRecipies map[{index}] is not the {stem} row; shape order assumption broken')
    tables['ToolRecipies.cpp'] = {'recipes': tools, 'refs': tool_refs, 'shapeOrder': expected_order}
    tables['WeaponRecipies.cpp'] = {'recipes': weapons, 'refs': weapon_refs,
                                    'bowArrowCommentedOut': verify_bow_arrow_commented(s)}
    tables['ArmorRecipes.cpp'] = {'recipes': armor, 'refs': armor_refs,
                                  'shapeOrder': ['helmet', 'chestplate', 'leggings', 'boots'],
                                  'chainArmor': verify_chain_armor(s)}
    tables['OreRecipies.cpp'] = {'recipes': ores, 'refs': ore_refs}
    skipped += ore_skipped
    for table in tables.values():
        candidates += table['recipes']

    # Inline literal call sites. ToolRecipies.cpp also holds the one hand-written
    # shears recipe outside its loop; the loop call sites themselves are skipped.
    expanded = {'ToolRecipies.cpp': tool_lines, 'WeaponRecipies.cpp': weapon_lines,
                'ArmorRecipes.cpp': armor_lines}
    for name in ('ToolRecipies.cpp', 'WeaponRecipies.cpp', 'ArmorRecipes.cpp',
                 'StructureRecipies.cpp', 'Recipes.cpp'):
        inline, inline_skipped = parse_inline(s, sym, name, expanded.get(name, ()))
        candidates += inline
        skipped += inline_skipped

    # Identity gate: both the product and every ingredient must already exist in
    # port/item-catalog.json. Missing entries are reported, never synthesized.
    recipes, notcovered = [], list(skipped)
    for recipe in candidates:
        product = (recipe['product']['itemId'], recipe['product']['aux'])
        missing = []
        if product not in known:
            missing.append(f'product item:{product[0]}:{product[1]}')
        for cell in recipe['cells']:
            if cell is None:
                continue
            if cell['aux'] == ANY_AUX:
                if cell['itemId'] not in by_id:
                    missing.append(f'ingredient item:{cell["itemId"]}:*')
            elif (cell['itemId'], cell['aux']) not in known:
                missing.append(f'ingredient item:{cell["itemId"]}:{cell["aux"]}')
        if missing:
            notcovered.append({'origin': recipe['origin'], 'kind': 'shaped',
                               'reason': 'Not in port/item-catalog.json: ' + ', '.join(sorted(set(missing))),
                               'source': recipe['source']})
            continue
        recipe['productKey'] = f'item:{product[0]}:{product[1]}'
        recipe['productName'] = known[product]['name']
        recipe['cellKeys'] = [None if c is None else
                              (f'item:{c["itemId"]}:*' if c['aux'] == ANY_AUX else f'item:{c["itemId"]}:{c["aux"]}')
                              for c in recipe['cells']]
        recipes.append(recipe)

    # Deterministic order: product id, aux, size, then source position.
    recipes.sort(key=lambda r: (r['product']['itemId'], r['product']['aux'],
                                r['width'] * r['height'], r['origin']))
    notcovered.sort(key=lambda r: (r['source']['path'], r['source']['line']))
    for index, recipe in enumerate(recipes):
        recipe['index'] = index

    counted = {}
    for name in ('Recipes.cpp', 'StructureRecipies.cpp', 'ToolRecipies.cpp', 'WeaponRecipies.cpp',
                 'ArmorRecipes.cpp', 'OreRecipies.cpp', *COUNT_ONLY):
        counted[name] = {'shapedCallSites': len(re.findall(r'addShapedRecipy\s*\(', s.code[name])),
                         'shapelessCallSites': len(re.findall(r'addShapelessRecipy\s*\(', s.code[name])),
                         'sha256': digest((ROOT / s.paths[name]).read_bytes())}

    data = {
        'schema_version': 1,
        'status': 'source-pinned-shaped-recipe-subset',
        'sourceCommit': gi.COMMIT,
        'itemCatalogSha256': digest(CATALOG.read_bytes()),
        'scope': ('Shaped crafting recipes whose product and ingredients all exist in '
                  'port/item-catalog.json. Not an item engine and not a crafting UI.'),
        'contract': {
            'productKey': 'item:<itemId>:<aux>, identical to port/item-catalog.json keys',
            'cells': 'row-major width*height list; null means the slot must be empty',
            'cellAux': f'{ANY_AUX} means ANY_AUX_VALUE: the ingredient matches any aux value',
            'matching': ('3x3 grid with x/y offset scanning and horizontal mirroring, '
                         'mirroring ShapedRecipy::matches exactly'),
            'grid': 'caller supplies nine item keys row-major; empty string is an empty slot',
            'ordering': 'recipes are sorted by product id/aux/size/origin, not by upstream insertion order',
        },
        'semantics': {'offsetScan': offset_ref, 'horizontalMirror': flip_ref,
                      'anyAuxComparison': aux_ref, 'anyAuxValue': any_aux_ref,
                      'itemIngredientAnyAux': any_item_ref, 'tileIngredientAnyAux': any_tile_ref},
        'recipeCount': len(recipes),
        'recipes': recipes,
        'tables': {name: {k: v for k, v in table.items() if k != 'recipes'} for name, table in tables.items()},
        'callSites': counted,
        'notcovered': notcovered,
        'sources': [{'path': s.paths[name], 'sha256': RECIPE_PINS[name]} for name in sorted(RECIPE_PINS)],
        'implemented': [
            'Shaped recipe data extraction from the pinned tabular sub-registries and the literal inline call sites.',
            'Shape matching with x/y offsets and horizontal mirroring, matching ShapedRecipy::matches.',
            'ANY_AUX_VALUE ingredients: an Item*/Tile* ingredient matches any aux value.',
            'Product identity as an item-catalog key plus the upstream output count.',
            'Reverse lookup: list every recipe producing a given product key.',
            'Rejection of surplus ingredients and of the empty grid.',
        ],
        'notImplemented': [
            'SMELTING IS NOT IMPLEMENTED THIS ROUND. FurnaceRecipes.cpp is a separate singleton and was skipped.',
            'Shapeless recipes (Recipes.cpp addShapelessRecipy, FoodRecipies, ClothDyeRecipes) are not extracted.',
            'Tool/weapon repair by combining two damaged copies (Recipes.cpp getItemFor) needs a damage field.',
            'Durability consumption, mining speed, harvest-level gating and enchanting.',
            'Armor damage reduction, armor equipping and sword combat bonuses.',
            'Hoe tilling and shears shearing; both need useOn, which does not exist here.',
            'Ingredient consumption and inventory mutation; Crafting.hpp is pure and never touches Inventory.',
            'Recipe group ordering/paging of the original crafting menu.',
        ],
        'survivalReachability': (
            'Iron and gold ingots come ONLY from smelting (FurnaceRecipes.cpp:20-21) and smelting is not '
            'implemented this round, so every iron/gold recipe here - including iron and gold tools, armor, '
            'shears and the iron door - is UNREACHABLE in survival play and can only be reached by creative '
            'give. Wood and stone recipes are fully reachable; diamond gear is reachable because diamonds '
            'drop directly from ore.'),
        'uiStatus': (
            'There is no crafting-table GUI yet. Recipe matching is a usable backend capability (grid in, '
            'product out), but a player cannot craft from the interface until a crafting screen is built.'),
        'chainArmor': (
            'Chain armor has NO recipe by design: the ArmorRecipes.cpp chain table rows are commented out '
            'upstream. The four chain items still exist in the item catalog.'),
    }
    validate(data, known, by_id)
    return data


def validate(data, known, by_id):
    recipes = data['recipes']
    require(len(recipes) == data['recipeCount'], 'recipeCount disagrees with the recipe list')
    seen = set()
    for index, recipe in enumerate(recipes):
        require(recipe['index'] == index, 'Recipe indices are not contiguous')
        width, height = recipe['width'], recipe['height']
        require(1 <= width <= 3 and 1 <= height <= 3, f'Recipe {index} is not within 3x3')
        require(len(recipe['cells']) == width * height, f'Recipe {index} cell count mismatch')
        require(len(recipe['cellKeys']) == len(recipe['cells']), f'Recipe {index} key/cell mismatch')
        require(any(c is not None for c in recipe['cells']), f'Recipe {index} has no ingredient')
        product = (recipe['product']['itemId'], recipe['product']['aux'])
        require(product in known, f'Recipe {index} product is not in the item catalog')
        require(recipe['productKey'] == f'item:{product[0]}:{product[1]}', f'Recipe {index} product key mismatch')
        require(recipe['product']['count'] >= 1, f'Recipe {index} produces a nonpositive count')
        require(recipe['group'] in GROUPS.values(), f'Recipe {index} has an unknown group')
        require(recipe['source']['line'] >= 1 and recipe['source']['anchor'], f'Recipe {index} lacks provenance')
        for cell in recipe['cells']:
            if cell is None:
                continue
            require(cell['itemId'] in by_id, f'Recipe {index} ingredient id is not in the catalog')
            require(cell['aux'] == ANY_AUX or cell['aux'] in by_id[cell['itemId']],
                    f'Recipe {index} ingredient aux is not in the catalog')
        # Identical shape and identical product must not be emitted twice.
        signature = (product, recipe['product']['count'], width, height,
                     tuple(None if c is None else (c['itemId'], c['aux']) for c in recipe['cells']))
        require(signature not in seen, f'Duplicate recipe shape for {recipe["productKey"]}')
        seen.add(signature)
    for row in data['notcovered']:
        require(row['reason'] and row['source']['line'] >= 1, 'Notcovered row lacks a reason or provenance')
    # The audited counts this round must stay exact.
    # The 20 matrix tools (5 materials x 4 types) plus the inline shears recipe.
    tool_rows = [r for r in recipes if r['origin'].startswith('ToolRecipies.cpp') and r['product']['itemId'] != 359]
    require(len(tool_rows) == 20, f'Expected 20 tool recipes (5 materials x 4 types), got {len(tool_rows)}')
    require(len({r['product']['itemId'] for r in tool_rows}) == 20, 'Tool recipes must have 20 distinct products')
    weapon_rows = [r for r in recipes if r['origin'].startswith('WeaponRecipies.cpp')]
    require(len(weapon_rows) == 5, f'Expected 5 sword recipes, got {len(weapon_rows)}')
    armor_rows = [r for r in recipes if r['origin'].startswith('ArmorRecipes.cpp')]
    require(len(armor_rows) == 16, f'Expected 16 armor recipes (4 pieces x 4 materials), got {len(armor_rows)}')
    require(not any(r['product']['itemId'] in (302, 303, 304, 305) for r in recipes),
            'Chain armor must have no recipe')
    # ToolRecipies.cpp:106: shears are two iron ingots on the diagonal (" #", "# ").
    shears = [r for r in recipes if r['product']['itemId'] == 359]
    require(len(shears) == 1 and shears[0]['origin'].startswith('ToolRecipies.cpp') and
            shears[0]['product']['count'] == 1 and (shears[0]['width'], shears[0]['height']) == (2, 2) and
            [None if c is None else (c['itemId'], c['aux']) for c in shears[0]['cells']] == [None, (265, ANY_AUX), (265, ANY_AUX), None],
            'Expected exactly one source shears recipe: 2x2 iron ingots on the anti-diagonal')
    require(not any('item:359' in row['reason'] for row in data['notcovered']),
            'Shears are a catalog item; their recipe must not be notcovered')
    # Workbench and furnace are the two structure recipes this round must carry.
    keys = {r['productKey'] for r in recipes}
    for key, label in (('item:58:0', 'workBench'), ('item:61:0', 'furnace')):
        require(key in keys, f'Missing the {label} recipe')


# ----------------------------------------------------------------- outputs

def cpp(data):
    q = lambda v: json.dumps(v, ensure_ascii=False)
    cells, offsets = [], []
    for recipe in data['recipes']:
        offsets.append(len(cells))
        for cell in recipe['cells']:
            cells.append((-1, 0) if cell is None else (cell['itemId'], cell['aux']))
    lines = [
        '#pragma once',
        '// Generated offline by tools/generate-recipes.py. Do not edit.',
        '//',
        '// Shaped crafting recipe DATA only. Matching lives in',
        '// jsapi/src/MinecraftRenderer/Crafting.{hpp,cpp} and is a pure function.',
        '//',
        '// NOT IMPLEMENTED: smelting (FurnaceRecipes.cpp was skipped this round),',
        '// shapeless recipes, durability consumption, mining speed, harvest level,',
        '// enchanting, armor damage reduction, sword bonuses, hoe tilling, shears',
        '// shearing, ingredient consumption, and any crafting GUI.',
        '//',
        '// Iron/gold ingots are smelting-only upstream, so every iron/gold recipe',
        '// below is unreachable in survival play until smelting exists.',
        '#include <cstring>',
        '#include <string>',
        'namespace sourcerecipes {',
        f'constexpr int kAnyAux = {ANY_AUX};',
        '// itemId == -1 marks a slot that must be EMPTY. aux == kAnyAux matches any aux.',
        'struct RecipeCell { int itemId, aux; };',
        '// cellOffset indexes kRecipeCells; the cell block is width*height, row major.',
        'struct RecipeSpec { const char* productKey; const char* productName; const char* group;',
        '    const char* origin; int productItemId, productAux, productCount, width, height, cellOffset; };',
        'constexpr RecipeCell kRecipeCells[] = {',
    ]
    for itemId, aux in cells:
        lines.append(f'    {{{itemId}, {aux}}},')
    lines += ['};', 'constexpr int kRecipeCellCount = int(sizeof(kRecipeCells) / sizeof(kRecipeCells[0]));',
              'constexpr RecipeSpec kRecipes[] = {']
    for recipe, offset in zip(data['recipes'], offsets):
        values = [q(recipe['productKey']), q(recipe['productName']), q(recipe['group']), q(recipe['origin']),
                  str(recipe['product']['itemId']), str(recipe['product']['aux']),
                  str(recipe['product']['count']), str(recipe['width']), str(recipe['height']), str(offset)]
        lines.append('    {' + ', '.join(values) + '},')
    lines += [
        '};',
        'constexpr int kRecipeCount = int(sizeof(kRecipes) / sizeof(kRecipes[0]));',
        'inline int recipeCount() { return kRecipeCount; }',
        'inline const RecipeSpec* recipes() { return kRecipes; }',
        'inline const RecipeSpec* recipeAt(int index) {',
        '    return index >= 0 && index < kRecipeCount ? &kRecipes[index] : nullptr;',
        '}',
        '// Cells of one recipe, row major, width*height entries.',
        'inline const RecipeCell* recipeCells(const RecipeSpec& spec) { return kRecipeCells + spec.cellOffset; }',
        '} // namespace sourcerecipes',
    ]
    return '\n'.join(lines) + '\n'


def outputs(data):
    return {JSON_OUT: json.dumps(data, ensure_ascii=False, indent=2, sort_keys=False) + '\n',
            HPP_OUT: cpp(data)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--client-root', type=Path, default=gi.DEFAULT_CLIENT,
                        help='Local reference checkout root (never fetched)')
    parser.add_argument('--check', action='store_true', help='Verify outputs reproduce without writing')
    args = parser.parse_args()
    # Same rule as generate-items.py: the reference checkout is a required
    # generation input, so when it is absent reuse the committed outputs rather
    # than failing the build. Generation is skipped, never fabricated.
    # Fall back to the tracked port/upstream checkout first, exactly like
    # generate-items.py; a fresh clone (CI) has no generated Recipes.hpp to reuse.
    upstream = gi.ROOT / 'port/upstream'
    if args.client_root == gi.DEFAULT_CLIENT and not (args.client_root / gi.CREATIVE_REL).exists() and \
            (upstream / gi.CREATIVE_REL).exists():
        args.client_root = upstream
    if not (args.client_root / gi.CREATIVE_REL).exists():
        missing = [path for path in (JSON_OUT, HPP_OUT) if not path.exists()]
        require(not missing, 'Reference checkout absent and generated outputs missing: '
                             + ', '.join(str(m) for m in missing))
        print('SKIP: reference checkout absent; reusing committed generated outputs')
        return
    data = build(Sources(args.client_root))
    if args.check:
        for path, content in outputs(data).items():
            require(path.exists() and path.read_text() == content, f'Stale generated output: {path}')
        print(f'PASS: both outputs reproduce exactly ({data["recipeCount"]} recipes, '
              f'{len(data["notcovered"])} notcovered)')
        return
    for path, content in outputs(data).items():
        if not path.exists() or path.read_text() != content:
            path.write_text(content)
    groups = {}
    for recipe in data['recipes']:
        groups[recipe['group']] = groups.get(recipe['group'], 0) + 1
    print(f'Generated {data["recipeCount"]} shaped recipes '
          f'({", ".join(f"{k} {v}" for k, v in sorted(groups.items()))}), '
          f'{len(data["notcovered"])} explicit notcovered records')


if __name__ == '__main__':
    main()
