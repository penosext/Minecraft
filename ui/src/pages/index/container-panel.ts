// Container screens (2x2 inventory crafting, workbench, chest, furnace).
// Native draws everything; this module only mirrors mcinventory::containerLayout
// (InventoryRenderer.cpp) so transparent Vue hit cells land on native slots, and
// decides what a tap means. Native alone decides merges, swaps and stack limits.

export type ContainerKind = 'inventory' | 'workbench' | 'chest' | 'furnace';
export type PanelRect = { x: number; y: number; width: number; height: number };
// side 'inventory': player inventory id 0..35; 'container': panel slot; 'result': craft output.
export type PanelHit = PanelRect & { side: 'inventory' | 'container' | 'result'; slot: number; left: number; top: number };
export type PanelLayout = { kind: ContainerKind; scale: number; panel: PanelRect; hits: PanelHit[] };
export type PanelPick = { side: 'inventory' | 'container'; slot: number };
export type PanelTap =
  | { kind: 'select'; pick: PanelPick | null }
  | { kind: 'inventoryMove'; from: number; to: number }
  // into: inventory -> container. keep: leave the inventory slot selected (grid painting).
  | { kind: 'containerMove'; inventorySlot: number; containerSlot: number; count: number; toContainer: boolean; keep: boolean }
  | { kind: 'quickMove'; containerSlot: number }
  | { kind: 'craft' };

export const CONTAINER_SLOT_COUNT: Record<ContainerKind, number> = { inventory: 4, workbench: 9, chest: 27, furnace: 3 };
const MAX_SCALE = 1.25;

function round(value: number): number { return Math.round(value); }
function scaled(x: number, y: number, w: number, h: number, s: number, ox: number, oy: number): PanelRect {
  const left = round(x * s), top = round(y * s);
  return { x: ox + left, y: oy + top, width: round((x + w) * s) - left, height: round((y + h) * s) - top };
}
// Source 1x slot icon origins: InventoryMenu.cpp:41, CraftingMenu.cpp:35,
// ContainerMenu.cpp:23, FurnaceMenu.cpp:19-21.
function panelSlots(kind: ContainerKind): [number, number][] {
  if (kind === 'inventory') return [0, 1, 2, 3].map(i => [88 + (i % 2) * 18, 26 + Math.floor(i / 2) * 18]);
  if (kind === 'workbench') return Array.from({ length: 9 }, (_, i) => [30 + (i % 3) * 18, 17 + Math.floor(i / 3) * 18]);
  if (kind === 'chest') return Array.from({ length: 27 }, (_, i) => [8 + (i % 9) * 18, 18 + Math.floor(i / 9) * 18]);
  return [[56, 17], [56, 53], [116, 35]];
}

export function containerLayout(width: number, height: number, kind: ContainerKind): PanelLayout {
  const panelH = kind === 'chest' ? 168 : 166;
  const fit = Math.min(MAX_SCALE, width / 184, height / 168);
  const scale = fit >= 1 ? Math.floor(fit * 4) / 4 : fit;
  const panel: PanelRect = {
    x: Math.trunc((width - round(176 * scale)) / 2), y: Math.trunc((height - round(panelH * scale)) / 2),
    width: round(176 * scale), height: round(panelH * scale),
  };
  const hit = (side: PanelHit['side'], slot: number, x: number, y: number): PanelHit => {
    const r = scaled(x - 1, y - 1, 18, 18, scale, panel.x, panel.y);
    return { ...r, side, slot, left: r.x - panel.x, top: r.y - panel.y };
  };
  const hits: PanelHit[] = [];
  // Chest player rows are shifted by yo=(3-4)*18 (ContainerMenu.cpp:17,31,36).
  const rows = kind === 'chest' ? 85 : 84, hotbar = kind === 'chest' ? 143 : 142;
  for (let i = 0; i < 36; i += 1) {
    hits.push(hit('inventory', i, 8 + (i % 9) * 18, i < 9 ? hotbar : rows + Math.floor((i - 9) / 9) * 18));
  }
  panelSlots(kind).forEach(([x, y], i) => hits.push(hit('container', i, x, y)));
  if (kind === 'inventory') hits.push(hit('result', 0, 144, 36));   // InventoryMenu.cpp:35
  if (kind === 'workbench') hits.push(hit('result', 0, 124, 35));   // CraftingMenu.cpp:29
  return { kind, scale, panel, hits };
}

// Two-tap move model shared by every screen (no cursor stack on a touch pen):
//  * first tap selects a non-result slot; tapping it again deselects;
//  * inventory -> inventory uses inventoryTransfer;
//  * inventory -> crafting grid moves ONE item and keeps the source selected,
//    so a recipe is "painted" with successive taps; chest/furnace move the stack;
//  * container -> inventory moves the stack;
//  * tapping a selected container slot again quick-moves it (shift-click);
//  * the result slot crafts once (ResultSlot::onTake), whatever is selected.
export function planTap(kind: ContainerKind, selected: PanelPick | null, target: PanelHit, onePerTap: boolean): PanelTap {
  if (target.side === 'result') return { kind: 'craft' };
  const pick: PanelPick = { side: target.side, slot: target.slot };
  if (!selected) return { kind: 'select', pick };
  if (selected.side === pick.side && selected.slot === pick.slot) {
    return pick.side === 'container' ? { kind: 'quickMove', containerSlot: pick.slot } : { kind: 'select', pick: null };
  }
  if (selected.side === 'inventory' && pick.side === 'inventory') return { kind: 'inventoryMove', from: selected.slot, to: pick.slot };
  if (selected.side === 'inventory' && pick.side === 'container') {
    const grid = kind === 'inventory' || kind === 'workbench';
    const one = grid && onePerTap;
    return { kind: 'containerMove', inventorySlot: selected.slot, containerSlot: pick.slot, count: one ? 1 : 64, toContainer: true, keep: one };
  }
  if (selected.side === 'container' && pick.side === 'inventory') {
    return { kind: 'containerMove', inventorySlot: pick.slot, containerSlot: selected.slot, count: 64, toContainer: false, keep: false };
  }
  // Container -> container has no native single-step move; reselect instead.
  return { kind: 'select', pick };
}
