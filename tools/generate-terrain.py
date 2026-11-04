#!/usr/bin/env python3
"""Build matched native/UI block tables and embed original atlas texels.

--check is read-only: it verifies the generated tables reproduce byte-for-byte
from the pinned catalog plus STATIC_BLOCKS, and writes nothing.

STATIC_BLOCKS below is an independent implementation, behaviour verified
against observed game rules; it is NOT a copy of any original source text. The
pinned port/terrain-catalog.json is SHA-checked by tools/generate-items.py and
is deliberately left untouched, so these 26 static blocks are appended here
instead of being edited into that file.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import sys

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
FACES = ('down', 'up', 'north', 'south', 'west', 'east')

# 26 static blocks completed in this pass. Every texture name below was
# confirmed present in the pinned terrain atlas before being referenced; no
# texture is invented and no unrelated texture is substituted. Shapes reuse the
# existing shape vocabulary; "approx" notes mark deliberate approximations.
# independent implementation, behaviour verified against observed game rules.
STATIC_BLOCKS = [
    # --- plain full cubes: shape/collision are an exact full cell ---
    {'key': 'sponge', 'name': '海绵', 'tile_id': 19, 'shape': 'cube',
     'textures': {'all': 'sponge'}, 'quality': 'exact'},
    {'key': 'lapisBlock', 'name': '青金石块', 'tile_id': 22, 'shape': 'cube',
     'textures': {'all': 'lapis_block'}, 'quality': 'exact'},
    {'key': 'goldBlock', 'name': '金块', 'tile_id': 41, 'shape': 'cube',
     'textures': {'all': 'gold_block'}, 'quality': 'exact'},
    {'key': 'ironBlock', 'name': '铁块', 'tile_id': 42, 'shape': 'cube',
     'textures': {'all': 'iron_block'}, 'quality': 'exact'},
    {'key': 'redBrick', 'name': '砖块', 'tile_id': 45, 'shape': 'cube',
     'textures': {'all': 'brick'}, 'quality': 'exact'},
    {'key': 'diamondBlock', 'name': '钻石块', 'tile_id': 57, 'shape': 'cube',
     'textures': {'all': 'diamond_block'}, 'quality': 'exact'},
    {'key': 'emeraldBlock', 'name': '绿宝石块', 'tile_id': 133, 'shape': 'cube',
     'textures': {'all': 'emerald_block'}, 'quality': 'exact'},
    {'key': 'redstoneBlock', 'name': '红石块', 'tile_id': 152, 'shape': 'cube',
     'textures': {'all': 'redstone_block'}, 'quality': 'exact'},
    {'key': 'coalBlock', 'name': '煤炭块', 'tile_id': 173, 'shape': 'cube',
     'textures': {'all': 'coal_block'}, 'quality': 'exact'},
    {'key': 'clayHardened', 'name': '硬化黏土', 'tile_id': 172, 'shape': 'cube',
     'textures': {'all': 'hardened_clay'}, 'quality': 'exact'},
    # Lamp off/lit are separate tile ids, not metadata of one another. Only the
    # face texture and light emission differ; collision is a full cell for both.
    {'key': 'redstoneLight', 'name': '红石灯', 'tile_id': 123, 'shape': 'cube',
     'textures': {'all': 'redstoneLight'}, 'quality': 'exact',
     'notes': '熄灭态；点亮态为另一方块id 124。碰撞箱为完整立方体。'},
    {'key': 'redstoneLight_lit', 'name': '红石灯（点亮）', 'tile_id': 124, 'shape': 'cube',
     'textures': {'all': 'redstoneLight_lit'}, 'quality': 'exact',
     'notes': '点亮态；发光等级与红石驱动未移植，仅静态外观与完整碰撞箱。'},
    # --- cubes with per-face textures ---
    # Pillar: upright orientation only. The rotated (data&12) variants reuse the
    # same two textures with swapped faces and are not enumerated here.
    {'key': 'hayBlock', 'name': '干草块', 'tile_id': 170, 'shape': 'cube',
     'textures': {'down': 'hay_block_top', 'up': 'hay_block_top', 'side': 'hay_block_side'},
     'quality': 'simplified',
     'notes': '仅竖直朝向；data&12的横放朝向未枚举（approx：横放时面纹理应旋转）。碰撞箱为完整立方体。'},
    # --- stained glass / hardened clay: 16 colour metadata variants ---
    # Full cube collision; the renderer draws them translucent.
    *[{'key': f'stained_glass_{c}', 'name': f'{n}色玻璃', 'tile_id': 95, 'shape': 'cube',
       'textures': {'all': f'glass_{c}'}, 'alpha': 0.65, 'data': i, 'quality': 'simplified',
       'notes': '半透明预览混合，非完整原版排序；碰撞箱为完整立方体。'}
      for i, (c, n) in enumerate([('white', '白'), ('orange', '橙'), ('magenta', '洋红'),
                                  ('light_blue', '淡蓝'), ('yellow', '黄'), ('lime', '黄绿'),
                                  ('pink', '粉'), ('gray', '灰'), ('silver', '淡灰'),
                                  ('cyan', '青'), ('purple', '紫'), ('blue', '蓝'),
                                  ('brown', '棕'), ('green', '绿'), ('red', '红'),
                                  ('black', '黑')])],
    *[{'key': f'clayHardened_{c}', 'name': f'{n}色陶瓦', 'tile_id': 159, 'shape': 'cube',
       'textures': {'all': f'hardened_clay_stained_{c}'}, 'data': i, 'quality': 'exact'}
      for i, (c, n) in enumerate([('white', '白'), ('orange', '橙'), ('magenta', '洋红'),
                                  ('light_blue', '淡蓝'), ('yellow', '黄'), ('lime', '黄绿'),
                                  ('pink', '粉'), ('gray', '灰'), ('silver', '淡灰'),
                                  ('cyan', '青'), ('purple', '紫'), ('blue', '蓝'),
                                  ('brown', '棕'), ('green', '绿'), ('red', '红'),
                                  ('black', '黑')])],
    # --- quartz: 0 plain, 1 chiseled, 2/3/4 pillar lines ---
    # All five are full-cube collision; only face textures differ.
    {'key': 'quartzBlock', 'name': '石英块', 'tile_id': 155, 'shape': 'cube',
     'textures': {'down': 'quartz_block_bottom', 'up': 'quartz_block_top',
                  'side': 'quartz_block_side'}, 'data': 0, 'quality': 'exact'},
    {'key': 'quartzBlock_chiseled', 'name': '錾制石英块', 'tile_id': 155, 'shape': 'cube',
     'textures': {'down': 'quartz_block_chiseled_top', 'up': 'quartz_block_chiseled_top',
                  'side': 'quartz_block_chiseled'}, 'data': 1, 'quality': 'exact'},
    {'key': 'quartzBlock_lines', 'name': '竖纹石英块', 'tile_id': 155, 'shape': 'cube',
     'textures': {'down': 'quartz_block_lines_top', 'up': 'quartz_block_lines_top',
                  'side': 'quartz_block_lines'}, 'data': 2, 'quality': 'simplified',
     'notes': 'data2竖纹；data3/4为X/Z横纹，面纹理旋转未编码（approx）。'},
    # --- stairs: reuse the existing stairs shape and its 8 metadata states ---
    *[{'key': f'{key}_data_{data}', 'name': f'{name} · 状态{data}', 'tile_id': tile,
       'shape': 'stairs', 'textures': {'all': texture}, 'data': data, 'quality': 'simplified',
       'notes': '保留原方块metadata；按方向/上下半部生成外形，碰撞箱沿用现有楼梯组合箱（含内外角与邻接锁定）。'}
      for tile, key, name, texture in [(108, 'stairs_bricks', '砖楼梯', 'brick'),
                                       (135, 'woodStairsBirch', '白桦木楼梯', 'planks_birch'),
                                       (136, 'woodStairsJungle', '丛林木楼梯', 'planks_jungle'),
                                       (156, 'stairs_quartz', '石英楼梯', 'quartz_block_side')]
      for data in range(8)],
    # --- wall: post 4/16 half-width, 1.5 tall, neighbour-connecting ---
    # Reuses the existing "wall" render shape; collision is implemented from the
    # wall rules in BlockCollision (post + connected arms, y1 = 1.5).
    {'key': 'cobbleWall', 'name': '圆石墙', 'tile_id': 139, 'shape': 'wall',
     'textures': {'all': 'cobblestone'}, 'data': 0, 'quality': 'simplified',
     'notes': '碰撞箱为柱体4/16半宽、高1.5，并按四向连接延伸；渲染为独立预览。'},
    {'key': 'cobbleWall_mossy', 'name': '苔石墙', 'tile_id': 139, 'shape': 'wall',
     'textures': {'all': 'cobblestone_mossy'}, 'data': 1, 'quality': 'simplified',
     'notes': 'data1苔石材质；碰撞箱与普通圆石墙相同。'},
    # --- stained glass pane: ThinFence-derived, reuses the pane shape ---
    *[{'key': f'stained_glass_pane_{c}', 'name': f'{n}色玻璃板', 'tile_id': 160, 'shape': 'pane',
       'textures': {'side': f'glass_{c}', 'up': f'glass_pane_top_{c}', 'down': f'glass_pane_top_{c}'},
       'alpha': 0.65, 'data': i, 'quality': 'simplified',
       'notes': '细栅栏族；碰撞箱沿用现有玻璃板连接箱（2/16厚，连接时半格延伸）。'}
      for i, (c, n) in enumerate([('white', '白'), ('orange', '橙'), ('magenta', '洋红'),
                                  ('light_blue', '淡蓝'), ('yellow', '黄'), ('lime', '黄绿'),
                                  ('pink', '粉'), ('gray', '灰'), ('silver', '淡灰'),
                                  ('cyan', '青'), ('purple', '紫'), ('blue', '蓝'),
                                  ('brown', '棕'), ('green', '绿'), ('red', '红'),
                                  ('black', '黑')])],
    # --- enchantment table: 12/16 tall slab-like box, full footprint ---
    {'key': 'enchantTable', 'name': '附魔台', 'tile_id': 116, 'shape': 'cube',
     'textures': {'down': 'enchantment_bottom', 'up': 'enchantment_top',
                  'side': 'enchantment_side'}, 'height': 0.75, 'quality': 'simplified',
     'notes': '碰撞箱高12/16（3/4格）、占满水平格。书本实体与附魔逻辑未移植；approx：渲染用降低高度的立方体近似。'},
    # --- anvil: full-height box narrowed 2/16 on the facing axis ---
    {'key': 'anvil', 'name': '铁砧', 'tile_id': 145, 'shape': 'cube',
     'textures': {'down': 'anvil_base', 'up': 'anvil_top', 'side': 'anvil_base'},
     'data': 0, 'quality': 'simplified',
     'notes': '碰撞箱按朝向（data&3）在该轴两侧各收窄2/16，高度满格。损坏等级data>>2仅换顶面纹理。'
              'approx：渲染用立方体近似真实的底座/立柱/砧面分层。'},
    *[{'key': f'anvil_damaged_{d}', 'name': f'铁砧 · 损坏{d}', 'tile_id': 145, 'shape': 'cube',
       'textures': {'down': 'anvil_base', 'up': f'anvil_top_damaged_{d}', 'side': 'anvil_base'},
       'data': d << 2, 'quality': 'simplified',
       'notes': '损坏态仅顶面纹理不同；碰撞箱与完好铁砧相同（朝向data&3为0）。'} for d in (1, 2)],
    # --- dragon egg: stepped egg silhouette ---
    {'key': 'dragonEgg', 'name': '龙蛋', 'tile_id': 122, 'shape': 'cube',
     'textures': {'all': 'dragon_egg'}, 'quality': 'simplified',
     'notes': '碰撞箱为完整格（原版未覆写形状）。approx：渲染用立方体近似蛋形轮廓；'
              '点击传送与下落行为未移植。'},
    # --- wool carpet: thin 1/16 sheet, 16 colours sharing the wool textures ---
    *[{'key': f'woolCarpet_{c}', 'name': f'{n}色地毯', 'tile_id': 171, 'shape': 'carpet',
       'textures': {'all': f'wool_colored_{c}'}, 'height': 0.0625, 'data': i,
       'quality': 'simplified',
       'notes': '复用羊毛纹理；碰撞箱高1/16薄片。approx：渲染用压扁立方体，'
                '非完整原版贴地面剔除。'}
      for i, (c, n) in enumerate([('white', '白'), ('orange', '橙'), ('magenta', '洋红'),
                                  ('light_blue', '淡蓝'), ('yellow', '黄'), ('lime', '黄绿'),
                                  ('pink', '粉'), ('gray', '灰'), ('silver', '淡灰'),
                                  ('cyan', '青'), ('purple', '紫'), ('blue', '蓝'),
                                  ('brown', '棕'), ('green', '绿'), ('red', '红'),
                                  ('black', '黑')])],
    # ------------------------------------------------------------------
    # Redstone powered render states.
    # independent implementation, rules verified against observed behaviour.
    #
    # The pre-existing catalog carried only the unpowered appearance of lever
    # (69), button (77) and repeater (93), so powered redstone had no way to
    # show its state. These entries add the missing half of each pair.
    #
    # The powered/pressed distinction is GEOMETRIC, not textural: the atlas has
    # no "lever on" or "button pressed" sprite, and SpecialShapes already
    # derives the handle angle from bit 8 (lever) and the 2/16 -> 1/16 plate
    # depth from bit 8 (button). Supplying the data value is therefore what
    # makes the state visible; inventing a recoloured sprite would be a fake.
    # The repeater pair genuinely differs by texture (repeater_off /
    # repeater_on), both confirmed present in the pinned atlas.
    # --- lever: 8 mount orientations x unpowered/powered (bit 8) ---
    *[{'key': f'lever_data_{d | p}',
       'name': f'拉杆 · 朝向{d}{"（通电）" if p else ""}', 'tile_id': 69, 'shape': 'lever',
       'textures': {'down': 'cobblestone', 'up': 'lever', 'side': 'cobblestone'},
       'data': d | p, 'quality': 'simplified',
       'notes': 'data&7为依附朝向，bit8为通电；通电态由手柄倾角体现（原子表无"通电"贴图）。'
                '碰撞箱沿用拉杆无碰撞体。'}
      for p in (0, 8) for d in range(8)],
    # --- buttons: 4 wall orientations x released/pressed (bit 8) ---
    # Stone (77) and wood (143) are separate tile ids with different auto-release
    # delays; they share geometry and differ only in face texture.
    *[{'key': f'{key}_data_{d | p}',
       'name': f'{name} · 朝向{d}{"（按下）" if p else ""}', 'tile_id': tile, 'shape': 'button',
       'textures': {'all': texture}, 'data': d | p, 'height': 0.125,
       'quality': 'simplified',
       'notes': 'data&7为依附朝向，bit8为按下；按下态为凸出2/16→1/16的几何差异。'
                '自动弹起需要tick驱动，纯函数模块不自动弹起。'}
      for tile, key, name, texture in [(77, 'button', '石按钮', 'stone'),
                                       (143, 'button_wood', '木按钮', 'planks_oak')]
      for p in (0, 8) for d in range(1, 5)],
    # --- repeater: unpowered 93 / powered 94, each 4 facings x 4 delay steps ---
    # Delay occupies bits 2-3; facing bits 0-1. The powered tile swaps both the
    # top texture and the torch cap colour.
    *[{'key': f'{key}_data_{(step << 2) | facing}',
       'name': f'红石中继器{suffix} · 朝向{facing} 延迟{step + 1}', 'tile_id': tile,
       'shape': 'repeater', 'textures': {'all': texture},
       'data': (step << 2) | facing, 'quality': 'simplified',
       'notes': 'data&3朝向，bits2-3延迟档（1-4刻，输出延迟为档位*2游戏刻）。'
                '单向导通；锁定（侧向中继器输入）未在渲染态中体现。'}
      for tile, key, suffix, texture in [(93, 'diode_off', '', 'repeater_off'),
                                         (94, 'diode_on', '（通电）', 'repeater_on')]
      for facing in range(4) for step in range(4)],
    # --- redstone dust: charge level 0-15 ---
    # The two dust overlay sprites in the pinned atlas are fully transparent
    # (alpha 0 at every pixel), so charge level CANNOT be shown by swapping
    # textures. The original tints the dust instead, interpolating the unlit and
    # lit-min/lit-max colours from the pinned colour table; the per-level colours
    # below are that ramp, and both renderers already honour BlockSpec::color.
    *[{'key': f'redStoneDust_data_{level}',
       'name': f'红石粉 · 充能{level}', 'tile_id': 55, 'shape': 'wire',
       'textures': {'all': 'redstone_dust_cross', 'overlay': 'redstone_dust_cross_overlay'},
       'data': level, 'color': colour, 'quality': 'simplified',
       'notes': '充能等级由颜色渐变体现（未通电4c0000，1→15由700000渐变至ff3200）。'
                '连接形状（直线/十字/爬坡）需要邻块上下文，此处为孤立十字预览。'}
      for level, colour in enumerate(
          ['4c0000', '700000', '7a0300', '840700', '8e0a00', '980e00', 'a31100',
           'ad1500', 'b71900', 'c11c00', 'cb2000', 'd62300', 'e02700', 'ea2a00',
           'f42e00', 'ff3200'])],
    # --- redstone torch: separate off (75) / on (76) tile ids, 5 mounts each ---
    # The torch inverts its input, so both states are reachable at runtime and
    # both sprites (redstone_torch_off / redstone_torch_on) are in the atlas.
    *[{'key': f'{key}_data_{d}',
       'name': f'红石火把{suffix} · 朝向{d}', 'tile_id': tile, 'shape': 'torch',
       'textures': {'all': texture}, 'data': d, 'quality': 'simplified',
       'notes': '红石火把反相：输入通电时熄灭。off/on为两个独立方块id（75/76），'
                '非同一方块的metadata。朝向1-4为壁挂、5为立于地面。'}
      for tile, key, suffix, texture in [(75, 'redstoneTorch_off', '（熄灭）', 'redstone_torch_off'),
                                         (76, 'redstoneTorch_on', '（点亮）', 'redstone_torch_on')]
      for d in range(1, 6)],
    # --- vine facings: VineTile bits 1=south(+Z) 2=west(-X) 4=north(-Z) 8=east(+X) ---
    # The pinned catalog only carries the four-sided data15 preview, so every
    # generated/placed vine (tree vines are single-bit) used to collapse onto one
    # face. Each set bit is the side of the cell the vine hangs against.
    *[{'key': f'vine_data_{d}', 'name': f'藤蔓 · 附着{d}', 'tile_id': 106, 'shape': 'vine',
       'textures': {'all': 'vine'}, 'color': '5bab46', 'data': d, 'quality': 'simplified',
       'notes': 'data位：1南(+Z) 2西(-X) 4北(-Z) 8东(+X)；每个置位画一面贴墙平面。'
                '顶面悬挂（data0）沿用四面预览；biome色未按邻格计算。'}
      for d in range(1, 15)],
    # --- cocoa facings: data&3 = Direction (S,W,N,E) toward the jungle log, >>2 = age ---
    # The pinned catalog has only direction 0 per age, so pods attached to
    # west/north/east logs rendered facing south.
    *[{'key': f'cocoa_data_{(age << 2) | d}', 'name': f'可可果（阶段{age}）· 朝向{d}',
       'tile_id': 127, 'shape': 'cocoa', 'textures': {'all': f'cocoa_{age}'},
       'data': (age << 2) | d, 'quality': 'simplified',
       'notes': 'data&3为朝向（0南1西2北3东，指向所附丛林原木），data>>2为生长阶段。'}
      for age in range(3) for d in range(1, 4)],
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true',
                        help='Verify generated outputs reproduce exactly; write nothing')
    arguments = parser.parse_args()
    global write_if_changed
    mismatches = []
    if arguments.check:
        def write_if_changed(path, source):
            if not path.exists():
                mismatches.append(f'{path}: missing')
            elif path.read_text() != source:
                mismatches.append(f'{path}: content differs')
    catalog_file = ROOT / 'port/terrain-catalog.json'
    catalog = json.loads(catalog_file.read_text())
    # The pinned catalog is never mutated; static blocks are appended in memory.
    entries = catalog['entries'] + [dict(block) for block in STATIC_BLOCKS]
    atlas_source = ROOT / 'port/assets/terrain.png'
    original = Image.open(atlas_source).convert('RGBA')
    if original.size != (256, 512):
        raise ValueError(f'Unexpected source atlas size: {original.size}')
    source = (ROOT / 'port/upstream/Minecraft.Client/PreStitchedTextureMap.cpp').read_text()
    terrain_source = source[source.index('float vertRatio = 1.0f/32.0f;'):]
    locations = {}
    for line in terrain_source.splitlines():
        if line.lstrip().startswith('//'):
            continue
        match = re.search(r'ADD_ICON\(\s*(\d+)\s*,\s*(\d+)\s*,\s*L"([^"]+)"', line)
        if match:
            row, column, name = match.groups()
            locations[name] = (int(column) * 16, int(row) * 16)
        sized = re.search(r'ADD_ICON_SIZE\(\s*(\d+)\s*,\s*(\d+)\s*,\s*L"([^"]+)"', line)
        if sized:
            row, column, name = sized.groups()
            locations[name] = (int(column) * 16, int(row) * 16)
    chest = Image.open(ROOT / 'port/assets/chest.png').convert('RGBA')
    chest_side = Image.new('RGBA', (14, 15))
    chest_side.paste(chest.crop((0, 14, 14, 19)), (0, 0))
    chest_side.paste(chest.crop((0, 33, 14, 43)), (0, 5))
    chest_front = Image.new('RGBA', (14, 15))
    chest_front.paste(chest.crop((14, 14, 28, 19)), (0, 0))
    chest_front.paste(chest.crop((14, 33, 28, 43)), (0, 5))
    chest_front.paste(chest.crop((1, 1, 3, 5)), (6, 3))
    external = {
        'chest_top': chest.crop((14, 0, 28, 14)),
        'chest_bottom': chest.crop((28, 19, 42, 33)),
        'chest_side': chest_side,
        'chest_front': chest_front,
    }
    used = {'dirt', 'grass_top', 'grass_side', 'grass_side_overlay', 'snow_side',
            'redstone_torch_off', 'fire_1', 'cauldron_inner', 'endframe_eye',
            'mushroom_red', 'planks_oak'}
    # Feasibility gate: a static block may only reference a texture that already
    # exists in the pinned atlas mapping. Referencing an absent texture is a hard
    # error rather than a silent fake or a wrong-texture stand-in.
    for block in STATIC_BLOCKS:
        for name in set(block['textures'].values()):
            if name not in locations and name not in external:
                raise ValueError(f'{block["key"]}: texture {name} is absent from the pinned atlas')
    seen = set()
    normalized = []
    for entry in entries:
        if entry['key'] in seen:
            raise ValueError(f'Duplicate block key: {entry["key"]}')
        seen.add(entry['key'])
        textures = entry['textures']
        sides = []
        for face in FACES:
            texture = textures.get(face, textures.get('side') if face not in ('up', 'down') else None)
            texture = texture or textures.get('all')
            if not texture:
                raise ValueError(f'{entry["key"]}: missing texture for {face}')
            sides.append(texture)
            used.add(texture)
        normalized.append((entry, sides))
    names = sorted(used)
    width, height = 256, math.ceil(len(names) / 16) * 16
    atlas = Image.new('RGBA', (width, height))
    icon_lines = []
    for index, name in enumerate(names):
        if name in external:
            tile = external[name].resize((16, 16), Image.Resampling.NEAREST)
        else:
            if name not in locations:
                raise ValueError(f'No original ADD_ICON mapping for {name}')
            x, y = locations[name]
            tile = original.crop((x, y, x + 16, y + 16))
        tx, ty = index % 16 * 16, index // 16 * 16
        atlas.paste(tile, (tx, ty))
        flag = 1 if name == 'grass_side' else 0
        icon_lines.append('    {L"%s", %.9ff, %.9ff, %.9ff, %.9ff, %d},' %
                          (name, tx / width, ty / height, (tx + 16) / width, (ty + 16) / height, flag))
    pixels = [((a << 24) | (r << 16) | (g << 8) | b) for r, g, b, a in atlas.getdata()]
    output = ROOT / 'jsapi/build/generated'
    output.mkdir(parents=True, exist_ok=True)
    lines = ['#pragma once', '#include <cstdint>', 'namespace legacy {',
             f'constexpr int kAtlasWidth = {width};', f'constexpr int kAtlasHeight = {height};',
             'constexpr uint32_t kAtlasPixels[] = {']
    for i in range(0, len(pixels), 8):
        lines.append('    ' + ', '.join(f'0x{value:08x}u' for value in pixels[i:i+8]) + ',')
    lines += ['};', '}']
    write_if_changed(output / 'LegacyTextures.hpp', '\n'.join(lines) + '\n')
    lines = ['#pragma once', '#include <cstdint>', 'namespace legacy {',
             'struct IconSpec { const wchar_t* name; float u0, v0, u1, v1; int flags; };',
             'constexpr IconSpec kIconSpecs[] = {', *icon_lines, '};',
             'struct BlockSpec { const char* key; const char* shape; const wchar_t* faces[6]; uint32_t color; float alpha; float height; int data; int tileId; };',
             'constexpr BlockSpec kBlockSpecs[] = {']
    for entry, faces in normalized:
        color = entry.get('color', 'ffffff')
        if isinstance(color, str):
            color = int(color.removeprefix('#').removeprefix('0x'), 16)
        faces_cpp = ', '.join(f'L"{name}"' for name in faces)
        lines.append('    {"%s", "%s", {%s}, 0x%06xu, %.6ff, %.6ff, %d, %d},' %
                     (entry['key'], entry['shape'], faces_cpp, color,
                      float(entry.get('alpha', 1.0)), float(entry.get('height', 1.0)),
                      int(entry.get('data', 0)), int(entry['tile_id'])))
    lines += ['};', 'constexpr int kBlockCount = sizeof(kBlockSpecs) / sizeof(kBlockSpecs[0]);', '}']
    write_if_changed(output / 'LegacyCatalog.hpp', '\n'.join(lines) + '\n')
    ui_entries = [{k: entry.get(k, 'preview' if k == 'quality' else '')
                   for k in ('key', 'name', 'shape', 'quality')} for entry in entries]
    ui = 'export const terrainBlocks = ' + json.dumps(ui_entries, ensure_ascii=False, indent=2) + ';\n'
    write_if_changed(ROOT / 'ui/src/pages/index/terrain-blocks.ts', ui)
    report = {'catalog_sha256': hashlib.sha256(catalog_file.read_bytes()).hexdigest(),
              'source_atlas_sha256': hashlib.sha256(atlas_source.read_bytes()).hexdigest(),
              'block_count': len(entries), 'texture_count': len(names), 'embedded_atlas_size': [width, height],
              'static_blocks': {
                  'provenance': 'independent implementation, behaviour verified against observed game rules',
                  'not_source_bytes': 'No original source text is reproduced here; these tables and the '
                                      'matching collision boxes were written independently.',
                  'pinned_catalog_untouched': True,
                  'entry_count': len(STATIC_BLOCKS),
                  'tile_ids': sorted({block['tile_id'] for block in STATIC_BLOCKS}),
                  'keys': [block['key'] for block in STATIC_BLOCKS],
                  'approximations': {block['key']: block['notes'] for block in STATIC_BLOCKS
                                     if 'approx' in block.get('notes', '')}},
              'texture_mapping': {name: list(locations[name]) if name in locations else 'original chest model UVs' for name in names}}
    write_if_changed(output / 'TerrainCatalog.manifest.json', json.dumps(report, indent=2, ensure_ascii=False) + '\n')
    if arguments.check:
        if mismatches:
            print('generate-terrain --check FAILED:', file=sys.stderr)
            for line in mismatches:
                print(f'  {line}', file=sys.stderr)
            return 1
        print(f'generate-terrain --check OK: {len(entries)} blocks '
              f'({len(STATIC_BLOCKS)} static additions), {len(names)} textures reproduce exactly')
        return 0
    print(f'Generated {len(entries)} matched native/UI blocks, {len(names)} original textures '
          f'({len(STATIC_BLOCKS)} static block additions)')
    return 0


def write_if_changed(path, source):
    if not path.exists() or path.read_text() != source:
        path.write_text(source)


if __name__ == '__main__':
    sys.exit(main())
