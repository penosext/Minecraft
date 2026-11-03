#!/usr/bin/env python3
"""Add explicit source metadata variants without changing existing palette keys."""
import copy
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    path = ROOT / 'port/terrain-catalog.json'
    catalog = json.loads(path.read_text())
    entries = catalog['entries']
    by_key = {entry['key']: entry for entry in entries}
    requests = [(entry['key'], range(8)) for entry in list(entries)
                if entry['shape'] == 'stairs' and '_data_' not in entry['key']]
    requests += [(key, range(8)) for key in ('door_wood_lower', 'door_iron_lower')]
    requests += [(key, range(8, 12)) for key in ('door_wood_upper', 'door_iron_upper')]
    requests += [(key, range(2, 6)) for key in ('ladder', 'chest', 'furnace', 'dispenser')]
    requests += [('torch', range(1, 6))]
    directions = {2: 'north', 3: 'south', 4: 'west', 5: 'east'}
    for base_key, metadata in requests:
        base = by_key[base_key]
        for value in metadata:
            key = f'{base_key}_data_{value}'
            if key in by_key:
                continue
            entry = copy.deepcopy(base)
            entry['key'] = key
            entry['data'] = value
            entry['name'] += f' · 状态{value}'
            entry['quality'] = entry['coverage_class'] = 'simplified'
            entry['notes'] = '保留原方块metadata；平台渲染按方向/上下半部生成外形，不包含全部连接、红石或实体行为。'
            if base_key in ('chest', 'furnace', 'dispenser'):
                texture = entry['textures']
                front = texture.pop('north')
                texture[directions[value]] = front
            entries.append(entry)
            by_key[key] = entry
    catalog['coverage']['catalog_entries'] = len(entries)
    catalog['coverage']['exact'] = [entry['key'] for entry in entries if entry.get('quality') == 'exact']
    catalog['coverage']['simplified'] = [entry['key'] for entry in entries if entry.get('quality') != 'exact']
    text = json.dumps(catalog, ensure_ascii=False, indent=2) + '\n'
    if path.read_text() != text:
        path.write_text(text)
    print(f'Structure metadata catalog: {len(entries)} entries, existing keys preserved')


if __name__ == '__main__':
    main()
