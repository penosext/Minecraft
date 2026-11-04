import { defineComponent } from 'vue';
import { MinecraftRenderer } from 'Minecraft';
import { terrainBlocks } from './terrain-blocks';
import { creativeItems } from './creative-items';
import { containerLayout, planTap, CONTAINER_SLOT_COUNT, ContainerKind, PanelHit, PanelPick } from './container-panel';
import { TextEditSession, isKeyboardAvailable } from '../../utils/keyboard';

export type indexOptions = {};

type MovementKey = 'forward' | 'backward' | 'left' | 'right' | 'jump';
// Native WorldRules report: value per rule plus the enforcement status of every
// rule, so a stored-only switch is never shown as if it changed the simulation.
type WorldRulesReport = {
  version?: number;
  rules: Record<string, boolean>;
  enforcement: Record<string, string>;
};
type WorldScene = {
  ticks: number; phase: number; daylight: number; mobs: number; killCount?: number;
  dayTicks?: number; rules?: WorldRulesReport;
};
type WorldBiome = { id: number; name: string };
type WorldTarget = { id: number; kind: string; health: number; maxHealth: number; distance: number };
type WorldAttack = WorldTarget & {
  hit: boolean; damaged: boolean; killed: boolean; damage: number; cooldownTicks: number; killCount: number;
};
type WorldStream = {
  radius: number; preloadRadius: number; displayTarget: number; displayLoaded: number;
  loaded: number; queued: number; target: number; totalGenerated: number;
};
type ChunkRangeKind = 'display' | 'preload';
type GameMode = 'creative' | 'survival';
type SettingsTab = 'chunks' | 'game' | 'hud';
// Debug-overlay (F3-style) items the player can switch on and off in settings.
type HudItem = 'fps' | 'renderMs' | 'touch' | 'xyz' | 'chunks' | 'time' | 'mobs' |
  'worldSize' | 'biome' | 'target' | 'kills' | 'mode';
type MiningInfo = {
  active: boolean; broken: boolean; progress: number; x: number; y: number; z: number;
  inventoryFull: boolean; unsupportedLoot?: boolean;
};
// busy is exclusive WorldJob ownership; loading is an ordinary, live background pump.
type ChunkLoadStatus = { busy: boolean; loading: boolean; error: string; stream: WorldStream };
type WorldJobState = 'idle' | 'running' | 'cancelling' | 'cancelled' | 'failed' | 'ready';
type WorldJobStatus = {
  state: WorldJobState; stage: string; progress: number; loadedChunks: number; totalChunks: number; error?: string;
};
type SurvivalInfo = {
  health: number; maxHealth: number; food: number; saturation: number;
  air: number; maxAir: number; dead: boolean; difficulty: number;
  armor?: number; eating?: boolean;
};
type ContainerInfo = {
  open: boolean; type?: ContainerKind; x?: number; y?: number; z?: number;
  slots: InventorySlot[]; burnRemaining?: number; burnTotal?: number; cookTicks?: number; cookTotal?: number;
  recipe?: { key: string; count: number; name?: string } | null;
};
type WorldInfo = {
  survival?: SurvivalInfo;
  stream?: WorldStream;
  player: { x: number; y: number; z: number };
  size: [number, number, number]; // Native order: width, height, depth.
  scene?: WorldScene;
  biome?: WorldBiome;
};
type WorldMetadata = WorldInfo & { path: string; seed: number };
type InventorySlot = {
  key: string; count: number; itemId?: number; aux?: number; maxStack?: number;
  damage: number; maxDamage: number; remaining: number;
  index?: number; name?: string; // Native index is a representative render state, not an item ordinal.
};
type LegacyUnresolvedSlot = { slot: number; key: string; count: number };
type InventoryInfo = {
  selected: number; slots: InventorySlot[]; armor?: unknown[]; emptyHand: boolean; gameMode: GameMode;
  legacyUnresolved?: LegacyUnresolvedSlot[];
};

// Keep compatibility local while the native API declarations are updated separately.
type WorldRenderer = {
  beginCreateWorld(seed: number, size: number, radius: number): boolean;
  beginOpenWorld(path: string, radius: number): boolean;
  worldJobStatus(): string;
  finishWorldJob(): string;
  cancelWorldJob(): boolean;
  setRenderRadius(radius: number): boolean;
  setChunkRanges(displayRadius: number, preloadRadius: number): boolean;
  chunkLoadStatus?(): string;
  saveWorld(): boolean;
  worldInfo(): string;
  survivalInfo?(): string;
  respawnPlayer?(): boolean;
  // Native survival use: plain food eats while held (32 ticks), armor equips on press.
  eatHeld?(held: boolean, pressed?: boolean): boolean;
  setDifficulty?(difficulty: number): string;
  containerInfo?(): string;
  closeContainer?(): boolean;
  // inventorySlot -1 = quick move containerSlot into the inventory. Returns {ok, ...containerInfo}.
  containerTransfer?(inventorySlot: number, containerSlot: number, count: number, toContainer: boolean): string;
  craftContainer?(): string;
  beginTouch?(): boolean;
  pollTouch?(): string;
  stopTouch?(): boolean;
  mineWorld?(yaw: number, pitch: number): boolean;
  setMiningInput?(held: boolean, yaw: number, pitch: number): boolean;
  miningInfo?(): string;
  setGameMode?(mode: GameMode): string;
  // Command-only bridges. Scene/World already implement the behaviour
  // (Scene::setDayTicks, Scene::setRule); these are the JS entry points that
  // main still has to register. Every one is optional and the command reports
  // "指令未接入" rather than pretending to have run when it is absent.
  setSceneTime?(dayTicks: number): string;
  setWorldRule?(rule: string, value: boolean): string;
  teleportPlayer?(x: number, y: number, z: number): string;
  attackWorld?(yaw: number, pitch: number): string;
  targetInfo?(yaw: number, pitch: number): string;
  inventoryInfo?(): string;
  selectInventory?(slot: number): string;
  setCreativeItem?(slot: number, itemIndex: number): string;
  inventoryTransfer?(from: number, to: number, count?: number): string;
  inventoryOpen?(open: boolean): boolean;
  placeWorld?(yaw: number, pitch: number): boolean;
  renderWorld(surfacePtr: number, width: number, height: number, yaw: number, pitch: number,
    forward: number, strafe: number, jump: boolean, dt: number,
    viewMode?: number, pixelStep?: number): boolean;
};
// Original mouse semantics: primary is the left button (attack a mob, otherwise mine),
// secondary is the right button (place).
type ControlRole = MovementKey | 'primary' | 'secondary';
type TouchRole = ControlRole | 'look' | 'ignore';
type WorldContact = { role: TouchRole; x: number; y: number };
// Chat lines are local only: there is no multiplayer, so nothing is ever sent.
// 'chat' is the player's own text, 'reply' a command result, 'notice' a refusal.
type ChatKind = 'chat' | 'reply' | 'notice';
type ChatEntry = { id: number; kind: ChatKind; text: string; expiresAt: number };
type CommandOutcome = { ok: boolean; text: string };
const worldRenderer = MinecraftRenderer as unknown as WorldRenderer;

const FRAME_INTERVAL_MS = 1000 / 30;
const FPS_WINDOW_MS = 500;
const WORLD_INFO_INTERVAL_MS = 500;
const WORLD_JOB_INTERVAL_MS = 100;
const RENDER_RADIUS_OPTIONS = [
  { radius: 1, label: '1 · 3×3 / 9 区块' },
  { radius: 2, label: '2 · 5×5 / 25 区块' },
  { radius: 3, label: '3 · 7×7 / 49 区块' },
];
const CHUNK_RANGE_MIN = 1;
const CHUNK_RANGE_MAX = 12;
// One tappable chip per radius. Plain click handlers are what every other
// working button on the device uses; the former drag sliders depended on
// touch coordinates that the device never delivered in a usable form.
const CHUNK_RANGE_STOPS = Array.from({ length: CHUNK_RANGE_MAX }, (_, i) => i + CHUNK_RANGE_MIN);
const SETTINGS_TABS: { key: SettingsTab; label: string }[] = [
  { key: 'chunks', label: '区块距离' },
  { key: 'game', label: '游戏' },
  { key: 'hud', label: '调试信息' },
];
// Order is the display order inside each HUD line.
const HUD_ITEMS: { key: HudItem; label: string }[] = [
  { key: 'fps', label: 'FPS' },
  { key: 'renderMs', label: '渲染耗时' },
  { key: 'touch', label: '触控方式' },
  { key: 'xyz', label: 'XYZ 坐标' },
  { key: 'chunks', label: '区块加载' },
  { key: 'time', label: '昼夜时刻' },
  { key: 'mobs', label: '生物数量' },
  { key: 'worldSize', label: '世界尺寸' },
  { key: 'biome', label: '群系' },
  { key: 'target', label: '瞄准目标' },
  { key: 'kills', label: '击杀数' },
  { key: 'mode', label: '游戏模式' },
];
// Matches .world-hud padding and .world-hud-text line height in index.less.
const HUD_LINE_HEIGHT = 12;
const HUD_PADDING = 8;
const CHAT_PANEL_HISTORY = 60;
// Lines that fit the chat screen's history box (see .chat-history in index.less).
const CHAT_PAGE_LINES = 9;
const QUICK_COMMANDS = ['/help', '/seed', '/time query'];
const WORLD_JOB_STAGES: Record<string, string> = {
  idle: '等待任务', bootstrap: '初始化世界', generate: '生成地形', spawn: '准备出生点',
  save: '分块保存', chunks: '加载附近区块', scene: '准备场景', inventory: '准备背包',
  ready: '世界已就绪', running: '处理中', cancelling: '正在取消', cancelled: '已取消', failed: '任务失败',
};
const MINE_INTERVAL_MS = 250; // Legacy bridge only; native held mining advances in renderWorld.
const MINING_INFO_INTERVAL_MS = 100;
const MINING_NOTICE_MS = 1800;
const ATTACK_INTERVAL_MS = 250;
const PLACE_INTERVAL_MS = 250;
const INVENTORY_INTERVAL_MS = 500;
// Mirror of native mcinventory::inventoryLayout (InventoryRenderer.cpp) so the
// transparent Vue hit cells land exactly on the slots native actually draws.
// Native owns every pixel; Vue never renders items, counts or the panel itself.
// Source GUI units are InventoryAssets.hpp; the scale is derived, never written
// down, so a different kMaxGuiScale only needs the constant below changed.
const GUI_SURFACE_WIDTH = 960;
const GUI_SURFACE_HEIGHT = 266;
// InventoryRenderer.hpp kMaxGuiScale, with the source 184×168 minimum fit.
const GUI_MAX_SCALE = 1.25;
const GUI_MIN_WIDTH = 184;
const GUI_MIN_HEIGHT = 168;
// InventoryAssets.hpp source units.
const GUI_HOTBAR_WIDTH = 182, GUI_HOTBAR_HEIGHT = 22, GUI_SLOT_PITCH = 20;
const GUI_SELECTION_WIDTH = 24, GUI_SELECTION_HEIGHT = 22;
const GUI_PANEL_WIDTH = 176, GUI_PANEL_HEIGHT = 166, GUI_ICON_SIZE = 16;
const GUI_GRID_X = 8, GUI_GRID_Y = 84, GUI_GRID_PITCH = 18, GUI_GRID_HOTBAR_Y = 142;
const GUI_ARMOR_X = 8, GUI_ARMOR_Y = 8, GUI_ARMOR_PITCH = 18;

type GuiRect = { x: number; y: number; width: number; height: number };
type GuiLayout = {
  scale: number; hotbar: GuiRect; panel: GuiRect;
  hotbarSlots: GuiRect[]; hotbarSelections: GuiRect[]; tileHitSlots: GuiRect[];
  tileSlots: GuiRect[]; armorSlots: GuiRect[];
};

// Native uses std::lround, which rounds halves away from zero on both signs.
function guiRound(value: number): number {
  return value < 0 ? -Math.round(-value) : Math.round(value);
}

// Native `scaled`: both edges are rounded, the size is their difference, so
// adjacent cells never overlap or leave a one-pixel seam.
function guiScaled(x: number, y: number, width: number, height: number,
  scale: number, originX: number, originY: number): GuiRect {
  const left = guiRound(x * scale);
  const top = guiRound(y * scale);
  return {
    x: originX + left, y: originY + top,
    width: guiRound((x + width) * scale) - left,
    height: guiRound((y + height) * scale) - top,
  };
}

const GUI_EMPTY_RECT: GuiRect = { x: 0, y: 0, width: 0, height: 0 };

function guiLayout(width: number, height: number): GuiLayout {
  // Native rejects the same degenerate viewports with an all-zero layout.
  if (width <= 0 || height <= 0 || width > 32768 || height > 32768) {
    return {
      scale: 0, hotbar: { ...GUI_EMPTY_RECT }, panel: { ...GUI_EMPTY_RECT },
      hotbarSlots: Array.from({ length: 9 }, () => ({ ...GUI_EMPTY_RECT })),
      hotbarSelections: Array.from({ length: 9 }, () => ({ ...GUI_EMPTY_RECT })),
      tileHitSlots: Array.from({ length: 36 }, () => ({ ...GUI_EMPTY_RECT })),
      tileSlots: Array.from({ length: 36 }, () => ({ ...GUI_EMPTY_RECT })),
      armorSlots: Array.from({ length: 4 }, () => ({ ...GUI_EMPTY_RECT })),
    };
  }
  // Whole quarter steps at or above the source minimum; a plain fit below it.
  const fit = Math.min(GUI_MAX_SCALE, width / GUI_MIN_WIDTH, height / GUI_MIN_HEIGHT);
  const scale = fit >= 1 ? Math.floor(fit * 4) / 4 : fit;
  const hotbar: GuiRect = {
    x: Math.trunc(width / 2) - guiRound(GUI_HOTBAR_WIDTH / 2 * scale),
    y: height - guiRound(GUI_HOTBAR_HEIGHT * scale),
    width: guiRound(GUI_HOTBAR_WIDTH * scale), height: guiRound(GUI_HOTBAR_HEIGHT * scale),
  };
  const panel: GuiRect = {
    x: Math.trunc((width - guiRound(GUI_PANEL_WIDTH * scale)) / 2),
    y: Math.trunc((height - guiRound(GUI_PANEL_HEIGHT * scale)) / 2),
    width: guiRound(GUI_PANEL_WIDTH * scale), height: guiRound(GUI_PANEL_HEIGHT * scale),
  };
  const hotbarSlots: GuiRect[] = [];
  const hotbarSelections: GuiRect[] = [];
  const tileSlots: GuiRect[] = [];
  const tileHitSlots: GuiRect[] = [];
  const armorSlots: GuiRect[] = [];
  for (let i = 0; i < 9; i += 1) {
    hotbarSlots[i] = guiScaled(1 + i * GUI_SLOT_PITCH, 0, GUI_SLOT_PITCH, GUI_HOTBAR_HEIGHT,
      scale, hotbar.x, hotbar.y);
    hotbarSelections[i] = guiScaled(-1 + i * GUI_SLOT_PITCH, -1, GUI_SELECTION_WIDTH, GUI_SELECTION_HEIGHT,
      scale, hotbar.x, hotbar.y);
    // Native indexes tileSlots by inventory id: 0..8 is the panel's hotbar row.
    tileSlots[i] = guiScaled(GUI_GRID_X + i * GUI_GRID_PITCH, GUI_GRID_HOTBAR_Y,
      GUI_ICON_SIZE, GUI_ICON_SIZE, scale, panel.x, panel.y);
    tileHitSlots[i] = guiScaled(GUI_GRID_X - 1 + i * GUI_GRID_PITCH, GUI_GRID_HOTBAR_Y - 1,
      GUI_ICON_SIZE + 2, GUI_ICON_SIZE + 2, scale, panel.x, panel.y);
  }
  for (let row = 0; row < 3; row += 1) for (let column = 0; column < 9; column += 1) {
    const i = 9 + row * 9 + column;
    tileSlots[i] = guiScaled(GUI_GRID_X + column * GUI_GRID_PITCH, GUI_GRID_Y + row * GUI_GRID_PITCH,
      GUI_ICON_SIZE, GUI_ICON_SIZE, scale, panel.x, panel.y);
    tileHitSlots[i] = guiScaled(GUI_GRID_X - 1 + column * GUI_GRID_PITCH, GUI_GRID_Y - 1 + row * GUI_GRID_PITCH,
      GUI_ICON_SIZE + 2, GUI_ICON_SIZE + 2, scale, panel.x, panel.y);
  }
  for (let i = 0; i < 4; i += 1) {
    armorSlots[i] = guiScaled(GUI_ARMOR_X, GUI_ARMOR_Y + i * GUI_ARMOR_PITCH,
      GUI_ICON_SIZE, GUI_ICON_SIZE, scale, panel.x, panel.y);
  }
  return { scale, hotbar, panel, hotbarSlots, hotbarSelections, tileHitSlots, tileSlots, armorSlots };
}

function guiBounds(rects: GuiRect[]): GuiRect {
  const left = Math.min(...rects.map(rect => rect.x));
  const top = Math.min(...rects.map(rect => rect.y));
  return {
    x: left, y: top,
    width: Math.max(...rects.map(rect => rect.x + rect.width)) - left,
    height: Math.max(...rects.map(rect => rect.y + rect.height)) - top,
  };
}

const GUI_LAYOUT = guiLayout(GUI_SURFACE_WIDTH, GUI_SURFACE_HEIGHT);
// Hit layers are positioned absolutely; children carry layer-relative offsets so
// the template never repeats a coordinate that only this layout may decide.
const HOTBAR_HIT_LAYER = guiBounds(GUI_LAYOUT.hotbarSlots);
const INVENTORY_HIT_LAYER = GUI_LAYOUT.panel;
const HOTBAR_HITS = GUI_LAYOUT.hotbarSlots.map((rect, slot) => ({
  slot, x: rect.x, y: rect.y, width: rect.width, height: rect.height,
  left: rect.x - HOTBAR_HIT_LAYER.x, top: rect.y - HOTBAR_HIT_LAYER.y,
}));
// Slot id is the array index: native tileHitSlots is already inventory-id order.
const INVENTORY_HITS = GUI_LAYOUT.tileHitSlots.map((rect, slot) => ({
  slot, x: rect.x, y: rect.y, width: rect.width, height: rect.height,
  left: rect.x - INVENTORY_HIT_LAYER.x, top: rect.y - INVENTORY_HIT_LAYER.y,
}));
// Native draws the hotbar plus its 24×22 selection frame; ignore all of it.
const HOTBAR_IGNORE = guiBounds([GUI_LAYOUT.hotbar, ...GUI_LAYOUT.hotbarSelections, ...GUI_LAYOUT.hotbarSlots]);
const BIOME_NAMES = [
  '海洋', '平原', '沙漠', '峭壁', '森林', '针叶林', '沼泽', '河流',
  '下界', '末地', '冻洋', '冻河', '冰原', '雪山', '蘑菇岛', '蘑菇岛岸',
  '沙滩', '沙漠丘陵', '森林丘陵', '针叶林丘陵', '峭壁边缘', '丛林', '丛林丘陵',
];
const MOB_NAMES: Record<string, string> = { Pig: '猪', Cow: '牛', Sheep: '羊', Chicken: '鸡', Zombie: '僵尸' };
const WORLD_PIXEL_STEP = 3;
const WORLD_DAY_TICKS = 24000;
const WORLD_SIZE_OPTIONS = [
  { label: '经典', size: 864 },
  { label: '小', size: 1024 },
  { label: '中', size: 3072 },
  { label: '大', size: 5120 },
];
// Chat/command surface. The log is local echo only; nothing leaves the device.
const CHAT_MAX_LENGTH = 100;
const CHAT_HISTORY_LIMIT = 5;
const CHAT_FADE_MS = 8000;
const CHAT_CURSOR_COLOR = '#FF683D';
const CHAT_PLACEHOLDER_COLOR = '#878A99';
const SEED_PLACEHOLDER = '留空使用当前时间';
const CHAT_PLACEHOLDER = '聊天或 / 指令';
// Canonical native rule names (mcworld::WorldRule). Matching is exact, like
// WorldRules::parse, with a case-insensitive lookup added only for typing
// convenience; the canonical spelling is always what gets sent and displayed.
const WORLD_RULE_NAMES = [
  'DoDaylightCycle', 'DoTileDrops', 'DoMobLoot', 'DoMobSpawning', 'KeepInventory',
  'NaturalRegeneration', 'FireSpreads', 'MobGriefing', 'TNT', 'CheatsEnabled',
];
// Native enforcement strings (WorldRules.cpp enforcementText), shown verbatim in
// Chinese so a stored-only switch is never presented as a working feature.
const RULE_ENFORCEMENT_TEXT: Record<string, string> = {
  'enforced': '已生效',
  'exported-query-caller-enforces': '已导出给玩法层',
  'stored-but-not-enforced': '仅保存未生效',
};
const COMMAND_HELP = [
  '/help 指令帮助 · /seed 查看种子',
  '/time set day|night|<ticks> · /time query',
  '/gamemode creative|survival',
  // [数量] is accepted and bounds-checked, but native creative give always
  // fills the slot to the stack limit, so the help says so rather than
  // promising a count the engine cannot honour.
  '/give <物品> [数量] 仅创造模式 · 实际为整组',
  '/tp <x> <y> <z> 需在世界范围内',
  '/gamerule <规则> [true|false]',
];
const MOVEMENT_KEYS: MovementKey[] = ['forward', 'backward', 'left', 'right', 'jump'];
const CONTROL_BOUNDS: { role: ControlRole; x: number; y: number; width: number; height: number }[] = [
  { role: 'forward', x: 74, y: 174, width: 56, height: 40 },
  { role: 'left', x: 12, y: 218, width: 56, height: 40 },
  { role: 'backward', x: 74, y: 218, width: 56, height: 40 },
  { role: 'right', x: 136, y: 218, width: 56, height: 40 },
  // The two mouse buttons share the former place/attack/mine strip.
  { role: 'primary', x: 600, y: 218, width: 120, height: 40 },
  { role: 'secondary', x: 730, y: 218, width: 120, height: 40 },
  { role: 'jump', x: 872, y: 218, width: 76, height: 40 },
];

function clockNow(): number {
  return typeof performance !== 'undefined' && typeof performance.now === 'function'
    ? performance.now() : Date.now();
}

function eventTouches(event: any): any[] {
  const result: any[] = [];
  if (!event) return result;
  // An explicitly empty changedTouches list is not a release of all active touches.
  const touches = event.changedTouches !== undefined ? event.changedTouches : event.touches;
  if (touches) {
    for (let i = 0; i < touches.length; i += 1) result.push(touches[i]);
  } else if (touchId(event) || ['screenX', 'pageX', 'clientX', 'x'].some(key =>
    typeof event[key] === 'number')) {
    result.push(event);
  }
  return result;
}

function touchId(touch: any): string {
  if (!touch) return '';
  for (const key of ['identifier', 'id', 'touchId', 'pointerId', 'existingID']) {
    const value = touch[key];
    // The same real ID stays the same across API aliases; never invent an ID for missing ones.
    if ((typeof value === 'number' && isFinite(value)) ||
      (typeof value === 'string' && value !== '')) return String(value);
  }
  return '';
}

function pointOf(touch: any, coordinates: string) {
  if (!touch) return null;
  const pairs = coordinates === 'screen'
    ? [['screenX', 'screenY']] : [['pageX', 'pageY'], ['clientX', 'clientY'], ['x', 'y']];
  for (const pair of pairs) {
    const x = touch[pair[0]];
    const y = touch[pair[1]];
    if (typeof x === 'number' && typeof y === 'number' && isFinite(x) && isFinite(y)) return { x, y };
  }
  return null;
}

function touchPoint(event: any, coordinates: string, identifier = '') {
  const touches = eventTouches(event);
  const touch = identifier ? touches.find(item => touchId(item) === identifier) : touches[0];
  return pointOf(touch, coordinates);
}

function inBox(x: number, y: number, box: GuiRect): boolean {
  return x >= box.x && x <= box.x + box.width && y >= box.y && y <= box.y + box.height;
}

// hudHeight is the rendered height of the debug HUD box (0 when every item is
// switched off), so hiding HUD lines also frees that area for look drags.
function nativeTouchRole(x: number, y: number, statusVisible: boolean, hudHeight = 56): TouchRole {
  for (const bounds of CONTROL_BOUNDS) {
    if (x >= bounds.x && x <= bounds.x + bounds.width &&
      y >= bounds.y && y <= bounds.y + bounds.height) return bounds.role;
  }
  // Match the HUD and status bounds in index.less exactly.
  // HUD: (12,6)-(442,6+hudHeight); settings: (540,6)-(620,46); bag: (640,6)-(720,46);
  // chat: (740,6)-(820,46).
  // Save/exit now lives inside the settings panel, so the former (844,6)-(948,46)
  // reservation is gone: an area with no button must never swallow a look drag.
  // The first-person/preview toggle no longer exists either.
  // The native hotbar box is derived from the real 1.25× layout, selection frame
  // included, instead of being written down at the old 1× coordinates.
  if ((hudHeight > 0 && x >= 12 && x <= 442 && y >= 6 && y <= 6 + hudHeight) ||
    (x >= 540 && x <= 620 && y >= 6 && y <= 46) ||
    (x >= 640 && x <= 720 && y >= 6 && y <= 46) ||
    (x >= 740 && x <= 820 && y >= 6 && y <= 46) ||
    inBox(x, y, HOTBAR_IGNORE) ||
    (statusVisible && x >= 12 && x <= 948 && y >= 68 && y <= 98) ||
    (x >= 12 && x <= 192 && y >= 174 && y <= 258)) return 'ignore';
  // The chat log is deliberately absent from this list: it is a read-only,
  // self-fading overlay, so a drag across it must still rotate the camera.
  return x >= 0 && x < GUI_SURFACE_WIDTH && y >= 0 && y < GUI_SURFACE_HEIGHT ? 'look' : 'ignore';
}

function errorMessage(error: any): string {
  return error && error.message ? error.message : String(error);
}

// --- Chat command parsing -------------------------------------------------
// Everything below only parses and validates. No command text is ever turned
// into a success message without the matching native bridge actually running.

function commandWords(body: string): string[] {
  return body.split(/\s+/).filter(word => word !== '');
}

// Strict decimal integer: Number() would accept '0x10', '1e3', ' 1 ' and ''.
function parseIntegerArg(text: string): number | null {
  if (!/^[+-]?\d+$/.test(text)) return null;
  const value = Number(text);
  return Number.isSafeInteger(value) ? value : null;
}

// Strict finite decimal, for /tp coordinates. Rejects hex/exponent/Infinity.
function parseNumberArg(text: string): number | null {
  if (!/^[+-]?(\d+(\.\d*)?|\.\d+)$/.test(text)) return null;
  const value = Number(text);
  return isFinite(value) ? value : null;
}

function parseBooleanArg(text: string): boolean | null {
  if (text === 'true') return true;
  if (text === 'false') return false;
  return null;
}

// Canonical rule name for a user-typed one. Exact match first, so the native
// spelling always wins; a case-insensitive match is only a typing convenience.
function canonicalRuleName(text: string): string {
  if (WORLD_RULE_NAMES.indexOf(text) !== -1) return text;
  const lower = text.toLowerCase();
  return WORLD_RULE_NAMES.find(name => name.toLowerCase() === lower) || '';
}

function ruleEnforcementText(status: string | undefined): string {
  return status ? RULE_ENFORCEMENT_TEXT[status] || status : '生效状态未知';
}

function readWorldRules(rules: any): WorldRulesReport | null {
  if (!rules || typeof rules !== 'object' || !rules.rules || typeof rules.rules !== 'object' ||
    !rules.enforcement || typeof rules.enforcement !== 'object') return null;
  // Every canonical rule must be present and boolean; a partial report is not
  // trustworthy enough to display a rule state from.
  if (!WORLD_RULE_NAMES.every(name => typeof rules.rules[name] === 'boolean' &&
    typeof rules.enforcement[name] === 'string')) return null;
  return { version: rules.version, rules: rules.rules, enforcement: rules.enforcement };
}

function readPlayer(info: any): WorldMetadata['player'] {
  const player = info && info.player;
  if (!player || !['x', 'y', 'z'].every(axis =>
    typeof player[axis] === 'number' && isFinite(player[axis]))) {
    throw new Error('世界坐标无效');
  }
  return player;
}

function validChunkRadius(radius: any): boolean {
  return Number.isInteger(radius) && radius >= CHUNK_RANGE_MIN && radius <= CHUNK_RANGE_MAX;
}

function readWorldStream(info: any): WorldStream {
  if (!info || !validChunkRadius(info.radius)) {
    throw new Error('区块加载信息无效');
  }
  // Old native/mock snapshots have one radius and one set of counts.
  const stream = {
    ...info,
    preloadRadius: info.preloadRadius === undefined ? info.radius : info.preloadRadius,
    displayTarget: info.displayTarget === undefined ? info.target : info.displayTarget,
    displayLoaded: info.displayLoaded === undefined ? info.loaded : info.displayLoaded,
  };
  if (!validChunkRadius(stream.preloadRadius) ||
    stream.preloadRadius < stream.radius ||
    !['loaded', 'queued', 'target', 'totalGenerated', 'displayTarget', 'displayLoaded'].every(key =>
      Number.isSafeInteger(stream[key]) && stream[key] >= 0)) {
    throw new Error('区块加载信息无效');
  }
  return stream;
}

function readWorldInfo(info: any): WorldInfo {
  readPlayer(info);
  if (!Array.isArray(info.size) || info.size.length !== 3 ||
    !info.size.every((size: any) => typeof size === 'number' && Number.isInteger(size) && size > 0)) {
    throw new Error('世界尺寸无效');
  }
  if (info.scene && Array.isArray(info.scene.mobs)) {
    info.scene = { ...info.scene, mobs: info.scene.mobCount };
  }
  const scene = info.scene;
  if (scene !== undefined && (!scene ||
    !['ticks', 'phase', 'daylight', 'mobs'].every(key =>
      typeof scene[key] === 'number' && isFinite(scene[key])) ||
    !Number.isSafeInteger(scene.ticks) || scene.ticks < 0 ||
    scene.phase < 0 || scene.phase > 1 || scene.daylight < 0 || scene.daylight > 1 ||
    !Number.isInteger(scene.mobs) || scene.mobs < 0)) {
    throw new Error('昼夜生物信息无效');
  }
  // scene.rules is optional and purely informational; an old or partial native
  // snapshot simply leaves the rule display unavailable instead of failing.
  if (scene && scene.rules !== undefined && !readWorldRules(scene.rules)) {
    throw new Error('世界规则信息无效');
  }
  if (info.biome !== undefined && (!info.biome || !Number.isInteger(info.biome.id) ||
    info.biome.id < 0 || typeof info.biome.name !== 'string')) {
    throw new Error('群系信息无效');
  }
  if (info.stream !== undefined) info.stream = readWorldStream(info.stream);
  if (info.survival !== undefined) info.survival = readSurvival(info.survival);
  return info;
}

function readWorldJobStatus(json: string): WorldJobStatus {
  const status = JSON.parse(json);
  if (!status || !['idle', 'running', 'cancelling', 'cancelled', 'failed', 'ready'].includes(status.state) ||
    typeof status.stage !== 'string' || typeof status.progress !== 'number' ||
    !isFinite(status.progress) || status.progress < 0 || status.progress > 100 ||
    !['loadedChunks', 'totalChunks'].every(key => Number.isSafeInteger(status[key]) && status[key] >= 0) ||
    (status.error !== undefined && typeof status.error !== 'string')) {
    throw new Error('世界任务状态无效');
  }
  return status;
}

function readWorldTarget(info: any): WorldTarget | null {
  if (info && typeof info === 'object' && !Array.isArray(info) && Object.keys(info).length === 0) return null;
  if (!info || !Number.isInteger(info.id) || info.id <= 0 || typeof info.kind !== 'string' ||
    !['health', 'maxHealth', 'distance'].every(key => typeof info[key] === 'number' && isFinite(info[key])) ||
    info.health < 0 || info.maxHealth <= 0 || info.health > info.maxHealth || info.distance < 0) {
    throw new Error('生物目标信息无效');
  }
  return { id: info.id, kind: info.kind, health: info.health, maxHealth: info.maxHealth, distance: info.distance };
}

function readMiningInfo(json: string): MiningInfo {
  const info = JSON.parse(json);
  if (!info || !['active', 'broken', 'inventoryFull'].every(key => typeof info[key] === 'boolean') ||
    typeof info.progress !== 'number' || !isFinite(info.progress) || info.progress < 0 || info.progress > 1 ||
    !['x', 'y', 'z'].every(key => Number.isSafeInteger(info[key])) ||
    (info.unsupportedLoot !== undefined && typeof info.unsupportedLoot !== 'boolean')) {
    throw new Error('采掘信息无效');
  }
  return info;
}

function parseSlot(slot: any): InventorySlot {
  if (!slot || typeof slot.key !== 'string' || !Number.isInteger(slot.count) || slot.count < 0 ||
    !['itemId', 'aux', 'maxStack'].every(key => slot[key] === undefined || Number.isInteger(slot[key])) ||
    !['damage', 'maxDamage', 'remaining'].every(key => slot[key] === undefined ||
      (Number.isSafeInteger(slot[key]) && slot[key] >= 0))) throw new Error('背包物品信息无效');
  // Durability is independent of canonical key/aux. Missing old-save fields are zero, not repaired.
  return { ...slot, damage: slot.damage ?? 0, maxDamage: slot.maxDamage ?? 0, remaining: slot.remaining ?? 0 };
}

function readInventoryInfo(json: string): InventoryInfo {
  const info = JSON.parse(json);
  if (!info || !Number.isInteger(info.selected) || info.selected < 0 || info.selected > 8 ||
    !Array.isArray(info.slots) || info.slots.length !== 36 ||
    !info.slots.every((slot: any) => slot && typeof slot.key === 'string' &&
      Number.isInteger(slot.count) && slot.count >= 0 &&
      ['itemId', 'aux', 'maxStack'].every(key => slot[key] === undefined || Number.isInteger(slot[key]))) ||
    typeof info.emptyHand !== 'boolean' ||
    (info.legacyUnresolved !== undefined && (!Array.isArray(info.legacyUnresolved) ||
      !info.legacyUnresolved.every((slot: any) => slot && Number.isInteger(slot.slot) && slot.slot >= 0 && slot.slot < 40 &&
        typeof slot.key === 'string' && slot.key.length > 0 && Number.isInteger(slot.count) && slot.count > 0)))) {
    throw new Error('背包信息无效');
  }
  // Native owns identities, stack limits, counts and the protected legacy stash; never rewrite them here.
  return info;
}

function readSurvival(info: any): SurvivalInfo {
  if (!info || !['health', 'maxHealth', 'food', 'saturation', 'air', 'maxAir', 'difficulty'].every(key =>
    typeof info[key] === 'number' && isFinite(info[key])) || info.maxHealth !== 20 || info.maxAir !== 300 ||
    info.health < 0 || info.health > 20 || info.food < 0 || info.food > 20 || info.saturation < 0 ||
    info.air < -20 || info.air > 300 || typeof info.dead !== 'boolean' ||
    !Number.isInteger(info.difficulty) || info.difficulty < 0 || info.difficulty > 3) throw new Error('生存信息无效');
  return { ...info };
}

function readContainer(json: string): ContainerInfo {
  const info = JSON.parse(json);
  if (!info || typeof info.open !== 'boolean') throw new Error('容器信息无效');
  if (!info.open) return { open: false, slots: [] };
  const length = CONTAINER_SLOT_COUNT[info.type as ContainerKind] || 0;
  if (!length || !Array.isArray(info.slots) || info.slots.length !== length ||
    (info.type !== 'inventory' && !['x', 'y', 'z'].every(key => Number.isInteger(info[key]))) ||
    !['burnRemaining', 'burnTotal', 'cookTicks', 'cookTotal'].every(key => info[key] === undefined ||
      (typeof info[key] === 'number' && isFinite(info[key]) && info[key] >= 0))) throw new Error('容器槽位无效');
  if (info.recipe != null && (typeof info.recipe.key !== 'string' ||
    !Number.isInteger(info.recipe.count) || info.recipe.count < 0)) throw new Error('合成产出无效');
  return { ...info, slots: info.slots.map(parseSlot) };
}

function readWorldMetadata(json: string): WorldMetadata {
  const info = JSON.parse(json);
  if (!info || typeof info.path !== 'string' || !/^\/tmp\/mc\/.+/.test(info.path) ||
    info.path.split('/').some((part: string) => part === '..') ||
    typeof info.seed !== 'number' || !isFinite(info.seed)) {
    throw new Error('世界信息无效（需位于 /tmp/mc/ 下）');
  }
  readWorldInfo(info);
  return info;
}

// Exported for the layout regression only: it asserts this mirror against the
// native inventoryLayout values that the native tests already lock down.
export { guiLayout, GUI_LAYOUT, HOTBAR_HITS, INVENTORY_HITS, HOTBAR_IGNORE, HOTBAR_HIT_LAYER, INVENTORY_HIT_LAYER };

const index = defineComponent({
  data() {
    return {
      $page: {} as FalconPage<indexOptions>,
      terrainBlocks,
      creativeItems,
      blockIndex: 0, // Preview-only render-state index; never an inventory selection.
      pickerOpen: false,
      inventoryPanelOpen: false,
      inventoryState: null as InventoryInfo | null,
      inventoryError: '',
      inventoryFrom: -1,
      lastInventoryAt: 0,
      hotbarHits: HOTBAR_HITS,
      inventoryHits: INVENTORY_HITS,
      createPanelOpen: false,
      renderRadiusOptions: RENDER_RADIUS_OPTIONS,
      selectedRenderRadius: 1,
      settingsPanelOpen: false,
      settingsRenderRadius: 1,
      settingsPreloadRadius: 1,
      settingsError: '',
      settingsTab: 'chunks' as SettingsTab,
      settingsTabs: SETTINGS_TABS,
      chunkRangeStops: CHUNK_RANGE_STOPS,
      gameSettingsError: '',
      hudItems: HUD_ITEMS,
      hudOptions: HUD_ITEMS.reduce((all, item) => { all[item.key] = true; return all; },
        {} as Record<HudItem, boolean>),
      chatPanelOpen: false,
      survival: null as SurvivalInfo | null,
      secondaryUsePressed: false,
      respawnError: '',
      containerPanelOpen: false,
      containerState: null as ContainerInfo | null,
      containerSelection: null as PanelPick | null,
      containerOnePerTap: true, // Crafting grids take one item per tap.
      containerError: '',
      difficultyLabels: ['和平', '简单', '普通', '困难'],
      lookSensitivity: 0.35,
      // Full scrollable history for the chat screen; chatLog stays the short fading overlay.
      chatHistory: [] as ChatEntry[],
      chatPageOffset: 0, // Lines scrolled up from the newest; 0 follows new output.
      quickCommands: QUICK_COMMANDS,
      worldStream: null as WorldStream | null,
      lastStreamInfoAt: 0,
      worldJob: null as WorldJobStatus | null,
      worldJobActive: false,
      chunkLoading: false, // Only native busy (exclusive WorldJob), never ordinary chunk loading.
      chunkLoadError: '',
      saveExitPending: false,
      worldJobCreated: false,
      worldJobCancelRequested: false,
      worldJobCancelPending: false,
      worldJobError: '',
      worldJobGeneration: 0,
      worldJobTimer: null as ReturnType<typeof setTimeout> | null,
      seedInput: String(Date.now() >>> 0),
      // System input method state. Sessions are created in mounted() and
      // released in beforeDestroy(); the UUID lives inside TextEditSession.
      seedEdit: null as TextEditSession | null,
      chatEdit: null as TextEditSession | null,
      keyboardAvailable: false,
      // True while the system keyboard owns the screen: movement, look and the
      // left/right buttons must all stay released until the edit finishes.
      textEditing: false,
      chatLog: [] as ChatEntry[],
      chatSequence: 0,
      chatHistoryLimit: CHAT_HISTORY_LIMIT,
      worldRules: null as WorldRulesReport | null,
      worldSizeOptions: WORLD_SIZE_OPTIONS,
      selectedWorldSize: 5120,
      worldSize: [] as number[],
      worldScene: null as WorldScene | null,
      worldBiome: null as WorldBiome | null,
      targetHP: null as WorldTarget | null,
      killCount: 0,
      firstAttackLogged: false,
      worldMode: false,
      worldDirty: false,
      worldBusy: false,
      lastWorldPath: '',
      worldPath: '',
      worldSeed: 0,
      playerX: 0,
      playerY: 0,
      playerZ: 0,
      lastWorldInfoAt: 0,
      worldInfoError: '',
      actionMessage: '',
      previewYaw: 45,
      previewPitch: 30,
      worldYaw: 0,
      worldPitch: -10,
      // First person only; the preview toggle button is gone, native still receives 0.
      viewMode: 0,
      nativeTouchStarted: false,
      nativeTouchAvailable: false,
      nativeMultiTouchLogged: false,
      worldContacts: {} as Record<string, WorldContact>,
      fallbackRoles: {} as Record<string, TouchRole>,
      movement: { forward: false, backward: false, left: false, right: false, jump: false },
      movementTouches: { forward: [] as string[], backward: [] as string[],
        left: [] as string[], right: [] as string[], jump: [] as string[] },
      primaryHeld: false,
      primaryTouches: [] as string[],
      nextPrimaryAt: 0,
      secondaryHeld: false,
      secondaryTouches: [] as string[],
      nextSecondaryAt: 0,
      emptyHandNotified: false,
      nativeResult: '',
      yaw: 45,
      pitch: 30,
      dragging: false,
      lookTouchId: '',
      touchCoordinates: 'screen',
      lastX: 0,
      lastY: 0,
      lastFrameAt: 0,
      fpsText: '--',
      renderMsText: '--',
      fpsFrames: 0,
      fpsWindowStart: 0,
      fpsReportPending: true,
      renderTimeTotal: 0,
      renderTimeSamples: 0,
      hudYaw: 45,
      hudPitch: 30,
      hudTouchMode: '回退',
      touchCount: 0,
      debugTouchCount: 0,
      renderTimer: null as ReturnType<typeof setTimeout> | null,
      loopRunning: false,
      pageMounted: false,
      pageVisible: true,
    };
  },

  computed: {
    worldInputBlocked(): boolean {
      // textEditing is included so the system keyboard never lets a stale
      // contact keep walking the player or rotating the camera behind it.
      return this.inventoryPanelOpen || this.containerPanelOpen || this.pickerOpen || this.settingsPanelOpen || this.chatPanelOpen ||
        this.worldBusy || this.chunkLoading || this.saveExitPending || this.textEditing || this.playerDead;
    },
    // Each line keeps only the items the player left switched on; empty lines vanish.
    hudLines(): string[] {
      const on = this.hudOptions;
      const join = (parts: (string | false)[]) => parts.filter(part => !!part).join(' · ');
      const scene = this.worldScene;
      const clock = scene ? (scene.dayTicks === undefined ? scene.ticks : scene.dayTicks) % WORLD_DAY_TICKS : 0;
      const period = scene ? (scene.phase < 0.25 || scene.phase >= 0.75 ? '昼' : '夜') : '';
      const mode = this.inventoryState ? (this.inventoryState.gameMode === 'creative' ? '创造' : '生存') : '--';
      return [
        join([on.fps && `FPS ${this.fpsText}`, on.renderMs && `渲染 ${this.renderMsText} ms`,
          on.touch && `双指:${this.hudTouchMode}`]),
        join([on.xyz && this.playerPosition, on.chunks && this.streamStatus]),
        join([on.time && (scene ? `${period} · 时刻 ${clock}` : '昼/夜 -- · 时刻 --'),
          on.mobs && `生物 ${scene ? scene.mobs : '--'}`, on.worldSize && `世界 ${this.worldDimensions}`]),
        join([on.biome && `群系 ${this.biomeStatus}`, on.target && this.targetStatus,
          on.kills && `击杀 ${this.killCount}`, on.mode && `模式 ${mode}`]),
      ].filter(line => line !== '');
    },
    chatPageLines(): ChatEntry[] {
      const end = this.chatHistory.length - this.chatPageOffset;
      return this.chatHistory.slice(Math.max(0, end - CHAT_PAGE_LINES), Math.max(0, end));
    },
    // Native draws hearts/hunger/armor/air; the UI only owns the death screen.
    playerDead(): boolean {
      return !!(this.worldMode && this.survival && this.survival.dead);
    },
    containerTitle(): string {
      const type = this.containerState && this.containerState.type;
      return type === 'chest' ? '箱子' : type === 'furnace' ? '熔炉' : '工作台';
    },
    // Native screen whose slots the open panel shows; null when none is open.
    panelKind(): ContainerKind | null {
      if (this.containerPanelOpen && this.containerState && this.containerState.type) return this.containerState.type;
      return this.inventoryPanelOpen ? 'inventory' : null;
    },
    panelLayout() {
      const kind = (this as any).panelKind as ContainerKind | null;
      return kind ? containerLayout(960, 266, kind) : null;
    },
    // Grid + result cells of the inventory screen (its 36 inventory cells are inventoryHits).
    inventoryGridHits(): PanelHit[] {
      return containerLayout(960, 266, 'inventory').hits.filter(hit => hit.side !== 'inventory');
    },
    containerHits(): PanelHit[] {
      const layout = (this as any).panelLayout;
      return this.containerPanelOpen && layout ? layout.hits : [];
    },
    containerPanelStyle(): Record<string, string> {
      const layout = (this as any).panelLayout;
      const p = layout ? layout.panel : { x: 0, y: 0, width: 0, height: 0 };
      return { left: p.x + 'px', top: p.y + 'px', width: p.width + 'px', height: p.height + 'px' };
    },
    craftRecipeName(): string {
      const recipe = this.containerState && this.containerState.recipe;
      return recipe ? (recipe.name || recipe.key) + ' ×' + recipe.count : '';
    },
    hudHeight(): number {
      return this.hudLines.length ? HUD_PADDING + this.hudLines.length * HUD_LINE_HEIGHT : 0;
    },
    gameModeLabel(): string {
      if (!this.inventoryState) return '状态未就绪';
      return this.inventoryState.gameMode === 'creative' ? '创造' : '生存';
    },
    keepInventoryOn(): boolean | null {
      return this.worldRules ? this.worldRules.rules.KeepInventory : null;
    },
    keepInventoryNote(): string {
      if (!this.worldRules) return '规则状态未就绪';
      return '已写入存档 · 关闭时死亡会清空背包与盔甲（本移植没有掉落物实体，物品不会留在地上）';
    },
    worldJobVisible(): boolean {
      return this.worldBusy && this.worldJobActive;
    },
    worldJobStage(): string {
      if (this.worldJobCancelRequested) return '正在取消，请稍候';
      const stage = this.worldJob ? this.worldJob.stage : 'bootstrap';
      return WORLD_JOB_STAGES[stage] || stage || '处理中';
    },
    settingsRanges() {
      return (['display', 'preload'] as ChunkRangeKind[]).map(kind => {
        const radius = kind === 'display' ? this.settingsRenderRadius : this.settingsPreloadRadius;
        const side = radius * 2 + 1;
        return {
          kind, radius,
          label: `${kind === 'display' ? '显示范围' : '预加载范围'} ${radius}（周围${side}×${side} / 总数${side * side}区块）`,
        };
      });
    },
    streamStatus(): string {
      const stream = this.worldStream;
      return stream ? `显示 ${stream.displayLoaded}/${stream.displayTarget} · 预载 ${stream.loaded}/${stream.target} · 队列 ${stream.queued}`
        : '显示 -- · 预载 -- · 队列 --';
    },
    streamDetails(): string {
      const stream = this.worldStream;
      return stream ? `显示已载 ${stream.displayLoaded}/${stream.displayTarget} · 预加载已载 ${stream.loaded}/${stream.target} · 队列 ${stream.queued} · 累计生成 ${stream.totalGenerated}`
        : this.streamStatus;
    },
    worldPositionStatus(): string {
      return `${this.playerPosition} · ${this.streamStatus}`;
    },
    selectedInventoryKey(): string {
      const inventory = this.inventoryState;
      const slot = inventory && inventory.slots[inventory.selected];
      return slot && slot.count > 0 ? slot.key : '';
    },
    heldItemName(): string {
      const inventory = this.inventoryState;
      if (!inventory) return '状态未就绪';
      return this.inventorySlotName(inventory.selected);
    },
    inventoryMigrationWarning(): string {
      const unresolved = this.inventoryState && this.inventoryState.legacyUnresolved;
      return unresolved && unresolved.length ? `旧状态物品已归一，${unresolved.length}项保留待处理` : '';
    },
    currentBlockName(): string {
      const block = this.terrainBlocks[this.blockIndex];
      return block ? block.name : '暂无方块';
    },
    blockPosition(): string {
      return this.terrainBlocks.length ? `${this.blockIndex + 1}/${this.terrainBlocks.length}` : '0/0';
    },
    playerPosition(): string {
      return `X ${this.playerX.toFixed(1)}  Y ${this.playerY.toFixed(1)}  Z ${this.playerZ.toFixed(1)}`;
    },
    worldDimensions(): string {
      return this.worldSize.length === 3 ? `${this.worldSize[0]}×${this.worldSize[2]}` : '--×--';
    },
    biomeStatus(): string {
      const biome = this.worldBiome;
      return biome ? BIOME_NAMES[biome.id] || biome.name || `未知(${biome.id})` : '--';
    },
    targetStatus(): string {
      const target = this.targetHP;
      return target ? `目标 ${MOB_NAMES[target.kind] || target.kind} ${target.health}/${target.maxHealth}` : '目标 --';
    },
    // Newest last, oldest trimmed. Fading is time based, so a line that has
    // outlived CHAT_FADE_MS simply stops being rendered.
    visibleChat(): ChatEntry[] {
      const now = clockNow();
      return this.chatLog.filter(entry => entry.expiresAt > now).slice(-this.chatHistoryLimit);
    },
    chatVisible(): boolean {
      return this.worldMode && !this.inventoryPanelOpen && !this.chatPanelOpen &&
        !this.settingsPanelOpen && this.visibleChat.length > 0;
    },
    sceneStatus(): string {
      if (!this.worldScene) return '昼/夜 -- · 时刻 -- · 生物 --';
      // Native scene alone advances time: 20 TPS, 24000 ticks = 20 real minutes.
      // phase is the original eased celestial angle, not ticks / 24000.
      const phase = this.worldScene.phase;
      const period = phase < 0.25 || phase >= 0.75 ? '昼' : '夜';
      // dayTicks is the celestial clock, which is what phase and the sky are
      // derived from and what /time reports. It only differs from ticks once
      // DoDaylightCycle has been switched off, and then the raw simulation
      // counter would contradict both the sky and the command.
      const clock = this.worldScene.dayTicks === undefined
        ? this.worldScene.ticks : this.worldScene.dayTicks;
      const ticks = clock % WORLD_DAY_TICKS;
      return `${period} · 时刻 ${ticks} · 生物 ${this.worldScene.mobs}`;
    },
  },

  mounted() {
    this.pageMounted = true;
    this.setupTextEdit();
    this.resumeWorldJobCancellation();
    this.startRenderLoop();
  },

  beforeUnmount() {
    this.pageMounted = false;
    this.releaseTextEdit();
    this.suspendPage();
  },

  // The CLI's bundled Vue 2 runtime uses beforeDestroy rather than beforeUnmount.
  beforeDestroy() {
    this.pageMounted = false;
    this.releaseTextEdit();
    this.suspendPage();
  },

  methods: {
    // BasePage forwards these Falcon page lifecycle callbacks to its Vue root.
    onShow() {
      this.pageVisible = true;
      this.resetMovement();
      this.lastWorldInfoAt = 0;
      this.lastInventoryAt = 0;
      this.resumeWorldJobCancellation();
      this.startRenderLoop();
    },

    onHide() {
      this.pageVisible = false;
      // A hidden page must not leave the system keyboard open, but the
      // listeners stay registered so onShow can start a new edit.
      this.cancelTextEdit();
      this.suspendPage();
    },

    onUnload() {
      this.pageVisible = false;
      this.pageMounted = false;
      this.releaseTextEdit();
      this.suspendPage();
    },

    suspendPage() {
      // Invalidate even an already-queued callback before requesting native cancellation.
      this.worldJobGeneration += 1;
      this.stopWorldJobPoll();
      if (this.worldJobActive && this.worldBusy) {
        this.worldJobCancelRequested = true;
        this.worldJobCancelPending = true;
        this.requestNativeWorldJobCancel();
      }
      this.stopRenderLoop();
      // saveWorld joins a running chunk pump natively; never read worldInfo/inventory here.
      if (!this.worldBusy && this.worldMode && this.worldDirty && this.saveCurrentWorld() && this.saveExitPending) {
        // saveWorld has joined the pump. Complete the queued exit without saving twice onShow.
        this.chunkLoading = false;
        if (this.closeInventory()) this.leaveWorldPreview();
      }
    },

    startRenderLoop() {
      if (!this.pageMounted || !this.pageVisible || this.worldBusy || this.loopRunning) return;
      this.resetMovement();
      if (this.worldMode) this.beginNativeTouch();
      this.loopRunning = true;
      this.lastFrameAt = clockNow();
      this.fpsFrames = 0;
      this.renderTimeTotal = 0;
      this.renderTimeSamples = 0;
      this.debugTouchCount = 0;
      this.fpsWindowStart = this.lastFrameAt;
      this.fpsReportPending = true;
      this.fpsText = '--';
      this.renderMsText = '--';
      this.renderTick();
    },

    stopRenderLoop() {
      this.loopRunning = false;
      if (this.renderTimer !== null) {
        clearTimeout(this.renderTimer);
        this.renderTimer = null;
      }
      this.stopNativeTouch();
      this.lastFrameAt = 0;
      this.fpsFrames = 0;
      this.fpsText = '--';
      this.targetHP = null;
    },

    renderTick() {
      this.renderTimer = null;
      if (!this.loopRunning || this.worldBusy || !this.pageMounted || !this.pageVisible) return;
      const started = clockNow();
      const dt = Math.max(0, Math.min(0.1, (started - this.lastFrameAt) / 1000));
      this.lastFrameAt = started;
      if (this.worldMode) {
        // Native makes every World access safe during the live pump. Only exclusive jobs block input.
        this.refreshChunkLoadStatus();
        this.pollNativeTouch();
        if (this.saveExitPending && !this.chunkLoading) this.saveAndExit();
        if (!this.chunkLoading) {
          this.tickPrimary(started);
          this.tickSecondary(started);
        }
      }
      if (!this.chunkLoading && this.nativeRender(dt)) this.fpsFrames += 1;
      // A submitted frame can advance the live chunk queue without making the world busy.
      if (this.worldMode && !this.chunkLoading) this.refreshChunkLoadStatus();
      const finished = clockNow();
      if (this.worldMode && !this.chunkLoading && finished - this.lastWorldInfoAt >= WORLD_INFO_INTERVAL_MS) {
        this.lastWorldInfoAt = finished;
        this.refreshWorldInfo();
        this.refreshTargetInfo();
      }
      if (this.worldMode && !this.chunkLoading && finished - this.lastInventoryAt >= INVENTORY_INTERVAL_MS) {
        this.refreshInventory();
      }
      const elapsed = finished - this.fpsWindowStart;
      if (elapsed >= FPS_WINDOW_MS) {
        // One HUD batch per window, never clear renderMsText between native frames.
        this.fpsText = (this.fpsFrames * 1000 / elapsed).toFixed(1);
        this.renderMsText = this.renderTimeSamples
          ? (this.renderTimeTotal / this.renderTimeSamples).toFixed(1) : '--';
        this.hudYaw = Math.round(this.yaw);
        this.hudPitch = Math.round(this.pitch);
        this.hudTouchMode = this.nativeTouchAvailable ? '原生' : '回退';
        if (this.fpsReportPending && this.fpsFrames > 0) {
          this.fpsReportPending = false;
          console.error('MINECRAFT_RENDER_STATS ' + JSON.stringify({
            frames: this.fpsFrames, elapsedMs: elapsed,
            submittedFps: this.fpsText, renderMs: this.renderMsText,
            yaw: this.yaw, pitch: this.pitch, blockIndex: this.blockIndex,
            worldMode: this.worldMode, touchCount: this.debugTouchCount,
            touchBackend: this.hudTouchMode,
            player: this.worldMode ? { x: this.playerX, y: this.playerY, z: this.playerZ } : undefined,
          }));
        }
        this.fpsFrames = 0;
        this.renderTimeTotal = 0;
        this.renderTimeSamples = 0;
        this.debugTouchCount = this.touchCount;
        this.fpsWindowStart = finished;
      }
      if (this.loopRunning) {
        const delay = Math.max(1, Math.ceil(FRAME_INTERVAL_MS - (finished - started)));
        this.renderTimer = setTimeout(() => this.renderTick(), delay);
      }
    },

    nativeRender(dt = 0): boolean {
      if (this.worldBusy || this.chunkLoading) return false;
      let renderStarted: number | null = null;
      try {
        const surface = (this.$refs as any).sf;
        if (!surface || typeof surface.getSurface !== 'function') {
          this.nativeResult = 'surface 元素不可用';
          return false;
        }
        const ptr = Number(surface.getSurface());
        if (!Number.isSafeInteger(ptr) || ptr <= 0) {
          this.nativeResult = 'surface 指针无效';
          return false;
        }
        renderStarted = clockNow();
        let ok: boolean;
        if (this.worldMode) {
          const forward = this.worldInputBlocked ? 0 : Number(this.movement.forward) - Number(this.movement.backward);
          const strafe = this.worldInputBlocked ? 0 : Number(this.movement.right) - Number(this.movement.left);
          // Native physics (including falling) can change position even without input.
          this.worldDirty = true;
          ok = worldRenderer.renderWorld(ptr, 960, 266, this.yaw, this.pitch,
            forward, strafe, !this.worldInputBlocked && this.movement.jump, Math.max(0, Math.min(0.1, dt)),
            this.viewMode, WORLD_PIXEL_STEP) === true;
        } else {
          ok = MinecraftRenderer.render(ptr, 960, 266, 0, this.yaw, this.pitch, this.blockIndex) === true;
        }
        this.nativeResult = ok ? '' : '渲染提交失败';
        return ok;
      } catch (e: any) {
        this.nativeResult = '渲染异常: ' + errorMessage(e);
        return false;
      } finally {
        if (renderStarted !== null) {
          this.renderTimeTotal += Math.max(0, clockNow() - renderStarted);
          this.renderTimeSamples += 1;
        }
      }
    },

    inventorySlotName(index: number): string {
      const slot = this.inventoryState && this.inventoryState.slots[index];
      if (!slot || slot.count <= 0 || !slot.key) return '空手';
      const item = this.creativeItems.find(item => item.key === slot.key);
      // Canonical key is the identity; neither a render index nor a terrain-state name is an item name.
      return item ? item.name : /^item:\d+:\d+$/.test(slot.key) ? slot.name || '未知物品' : '旧版物品';
    },

    applyInventory(json: string) {
      this.inventoryState = readInventoryInfo(json);
      this.inventoryError = '';
      this.lastInventoryAt = clockNow();
      if (!this.inventoryState.emptyHand) {
        this.emptyHandNotified = false;
        if (this.actionMessage === '空手') this.actionMessage = '';
      }
    },

    refreshInventory(): boolean {
      this.lastInventoryAt = clockNow();
      if (!this.worldMode || this.chunkLoading || this.worldBusy) return false;
      try {
        if (!worldRenderer.inventoryInfo) throw new Error('native inventoryInfo 未接入');
        this.applyInventory(worldRenderer.inventoryInfo());
        return true;
      } catch (e: any) {
        // Keep the last valid native snapshot, including protected legacy entries, on transient failures.
        this.inventoryError = '背包状态更新失败: ' + errorMessage(e);
        return false;
      }
    },

    selectInventory(slot: number) {
      if (!this.worldMode || this.chunkLoading || this.worldInputBlocked || !Number.isInteger(slot) || slot < 0 || slot > 8) return;
      try {
        if (!worldRenderer.selectInventory) throw new Error('native selectInventory 未接入');
        this.applyInventory(worldRenderer.selectInventory(slot));
        this.worldDirty = true;
        this.actionMessage = '';
      } catch (e: any) {
        this.actionMessage = '选择快捷栏失败: ' + errorMessage(e);
      }
    },

    quiesceWorldInput() {
      // Retain ignored IDs until release: a finger held while closing a panel must not resume movement/look.
      const contacts = this.worldContacts;
      const roles = this.fallbackRoles;
      this.resetMovement();
      for (const id of Object.keys(contacts)) contacts[id].role = 'ignore';
      for (const id of Object.keys(roles)) roles[id] = 'ignore';
      this.worldContacts = contacts;
      this.fallbackRoles = roles;
      this.touchCount = Math.max(Object.keys(contacts).length, Object.keys(roles).length);
    },

    openInventory() {
      if (!this.worldMode || this.chunkLoading || this.worldInputBlocked) return;
      this.quiesceWorldInput();
      try {
        if (!worldRenderer.inventoryOpen) throw new Error('native inventoryOpen 未接入');
        if (worldRenderer.inventoryOpen(true) !== true) throw new Error('native 返回失败');
        this.inventoryPanelOpen = true;
        this.inventoryFrom = -1;
        this.containerSelection = null;
        this.actionMessage = '';
        this.refreshInventory();
        this.refreshContainer();
      } catch (e: any) {
        this.actionMessage = '打开背包失败: ' + errorMessage(e);
      }
    },

    closeInventory(): boolean {
      if (!this.inventoryPanelOpen) return true;
      if (this.chunkLoading) return false;
      this.quiesceWorldInput();
      try {
        if (!worldRenderer.inventoryOpen) throw new Error('native inventoryOpen 未接入');
        if (worldRenderer.inventoryOpen(false) !== true) throw new Error('native 返回失败');
        this.inventoryPanelOpen = false;
        this.inventoryFrom = -1;
        this.containerSelection = null;
        this.containerState = null;
        this.refreshInventory();
        this.pickerOpen = false;
        this.actionMessage = '';
        return true;
      } catch (e: any) {
        this.actionMessage = '关闭背包失败: ' + errorMessage(e);
        return false;
      }
    },

    clickInventorySlot(slot: number) {
      if (!this.worldMode || this.chunkLoading || this.worldBusy || !this.inventoryPanelOpen || this.pickerOpen ||
        !Number.isInteger(slot) || slot < 0 || slot > 35) return;
      // A grid slot is selected: the inventory tap completes a grid move.
      if (this.containerSelection) { this.clickPanelSlot({ side: 'inventory', slot } as PanelHit); return; }
      if (this.inventoryFrom < 0) { this.inventoryFrom = slot; return; }
      if (this.inventoryFrom === slot) { this.inventoryFrom = -1; return; }
      try {
        if (!worldRenderer.inventoryTransfer) throw new Error('native inventoryTransfer 未接入');
        // No optimistic counts or swaps: native alone decides exchange, merge and stack limits.
        this.applyInventory(worldRenderer.inventoryTransfer(this.inventoryFrom, slot));
        this.inventoryFrom = -1;
        this.worldDirty = true;
        this.actionMessage = '';
      } catch (e: any) {
        this.actionMessage = '背包转移失败: ' + errorMessage(e);
      }
    },

    // --- Container panels (chest / workbench / furnace / 2x2 grid) -----------
    applyContainer(json: string): { ok: boolean; status: string } {
      const raw = JSON.parse(json);
      this.containerState = readContainer(json);
      if (raw && raw.inventory && Array.isArray(raw.inventory.slots)) this.applyInventory(JSON.stringify(raw.inventory));
      return { ok: raw && raw.ok !== false, status: raw && typeof raw.status === 'string' ? raw.status : '' };
    },
    refreshContainer(): boolean {
      if (!this.worldMode || this.worldBusy || !worldRenderer.containerInfo) return false;
      try {
        this.applyContainer(worldRenderer.containerInfo());
        this.containerError = '';
        return true;
      } catch (e: any) {
        this.containerError = '容器状态更新失败: ' + errorMessage(e);
        return false;
      }
    },
    // After a right click: open the panel if native Tile::use opened one.
    syncContainerPanel(): boolean {
      if (!this.refreshContainer() || !this.containerState || !this.containerState.open ||
        this.containerState.type === 'inventory') return false;
      this.setSecondary(false);
      this.quiesceWorldInput();
      this.containerPanelOpen = true;
      this.containerSelection = null;
      this.actionMessage = '';
      return true;
    },
    closeContainerPanel(): boolean {
      if (!this.containerPanelOpen) return true;
      this.quiesceWorldInput();
      try {
        if (!worldRenderer.closeContainer) throw new Error('native closeContainer 未接入');
        if (worldRenderer.closeContainer() !== true) throw new Error('native 返回失败');
      } catch (e: any) {
        this.actionMessage = '关闭容器失败: ' + errorMessage(e);
        return false;
      }
      this.containerPanelOpen = false;
      this.containerState = null;
      this.containerSelection = null;
      this.refreshInventory();
      this.worldDirty = true;
      return true;
    },
    toggleContainerOnePerTap() {
      this.containerOnePerTap = !this.containerOnePerTap;
    },
    // Every container/grid/result cell, plus inventory cells of a container panel.
    clickPanelSlot(hit: PanelHit) {
      const kind = this.panelKind;
      if (!kind || !this.worldMode || this.chunkLoading || this.worldBusy || this.pickerOpen) return;
      // In the plain inventory screen an inventory-side first selection lives in inventoryFrom.
      const selected: PanelPick | null = this.containerSelection ||
        (kind === 'inventory' && this.inventoryFrom >= 0 ? { side: 'inventory', slot: this.inventoryFrom } : null);
      const tap = planTap(kind, selected, hit, this.containerOnePerTap);
      const setSelection = (pick: PanelPick | null) => {
        if (kind === 'inventory' && pick && pick.side === 'inventory') {
          this.inventoryFrom = pick.slot; this.containerSelection = null;
        } else { this.containerSelection = pick; this.inventoryFrom = -1; }
      };
      try {
        if (tap.kind === 'select') { setSelection(tap.pick); return; }
        if (tap.kind === 'inventoryMove') {
          if (!worldRenderer.inventoryTransfer) throw new Error('native inventoryTransfer 未接入');
          this.applyInventory(worldRenderer.inventoryTransfer(tap.from, tap.to));
          setSelection(null);
        } else if (tap.kind === 'craft') {
          if (!worldRenderer.craftContainer) throw new Error('native craftContainer 未接入');
          const result = this.applyContainer(worldRenderer.craftContainer());
          if (!result.ok) {
            this.actionMessage = result.status === 'inventory-full' ? '背包已满，无法合成' : '没有匹配的配方';
            return;
          }
        } else {
          if (!worldRenderer.containerTransfer) throw new Error('native containerTransfer 未接入');
          const json = tap.kind === 'quickMove'
            ? worldRenderer.containerTransfer(-1, tap.containerSlot, 64, false)
            : worldRenderer.containerTransfer(tap.inventorySlot, tap.containerSlot, tap.count, tap.toContainer);
          const result = this.applyContainer(json);
          // A grid "paint" keeps its source selected while that stack lasts.
          const keep = tap.kind === 'containerMove' && tap.keep && result.ok && this.inventoryState &&
            this.inventoryState.slots[tap.inventorySlot] && this.inventoryState.slots[tap.inventorySlot].count > 0;
          setSelection(keep && tap.kind === 'containerMove' ? { side: 'inventory', slot: tap.inventorySlot } : null);
          if (!result.ok) { this.actionMessage = '无法放到这里'; return; }
        }
        this.worldDirty = true;
        this.actionMessage = '';
      } catch (e: any) {
        this.actionMessage = '容器操作失败: ' + errorMessage(e);
      }
    },

    refreshChunkLoadStatus() {
      if (!this.worldMode || this.worldBusy) return;
      if (!worldRenderer.chunkLoadStatus) { this.chunkLoading = false; return; }
      try {
        const status: ChunkLoadStatus = JSON.parse(worldRenderer.chunkLoadStatus());
        if (!status || typeof status.busy !== 'boolean' || typeof status.loading !== 'boolean' ||
          typeof status.error !== 'string') {
          throw new Error('区块任务状态无效');
        }
        status.stream = readWorldStream(status.stream);
        const now = clockNow();
        const changed = this.chunkLoading !== status.busy;
        this.chunkLoading = status.busy;
        // A pump failure is diagnostic only; it must not cover or interrupt the current world.
        const chunkError = status.error ? '区块补载暂不可用' : '';
        if (this.chunkLoadError !== chunkError) this.chunkLoadError = chunkError;
        // Poll exclusive ownership every frame; batch queue statistics at 500ms (or transitions).
        if (changed || !this.worldStream || now - this.lastStreamInfoAt >= WORLD_INFO_INTERVAL_MS) {
          this.worldStream = { ...status.stream };
          this.selectedRenderRadius = status.stream.radius;
          this.lastStreamInfoAt = now;
        }
      } catch (e: any) {
        // Keep the last known exclusive ownership; native guards World access even if polling fails.
        if (this.chunkLoadError !== '区块状态暂不可用') this.chunkLoadError = '区块状态暂不可用';
      }
    },

    refreshWorldInfo() {
      if (this.chunkLoading || this.worldBusy) return;
      try {
        this.applyWorldInfo(readWorldInfo(JSON.parse(worldRenderer.worldInfo())));
        this.worldInfoError = '';
      } catch (e: any) {
        this.worldInfoError = '世界状态更新失败: ' + errorMessage(e);
      }
    },

    refreshTargetInfo() {
      if (!this.worldMode || this.chunkLoading || this.worldBusy) return;
      try {
        const target = worldRenderer.targetInfo
          ? readWorldTarget(JSON.parse(worldRenderer.targetInfo(this.yaw, this.pitch))) : null;
        this.targetHP = target && target.health > 0 ? target : null;
      } catch (_) {
        // A missing/dead target or an unavailable bridge must not leave stale HP.
        this.targetHP = null;
      }
    },

    applyWorldInfo(info: WorldInfo) {
      this.playerX = info.player.x;
      this.playerY = info.player.y;
      this.playerZ = info.player.z;
      this.worldSize = info.size.slice();
      this.worldScene = info.scene ? { ...info.scene } : null;
      // Rules come with the scene summary; keep the last valid report so a
      // query never has to invent a value, and clear it when the scene goes.
      this.worldRules = info.scene && info.scene.rules
        ? readWorldRules(info.scene.rules) : info.scene ? this.worldRules : null;
      this.worldBiome = info.biome ? { ...info.biome } : null;
      this.survival = info.survival ? { ...info.survival } : null;
      this.worldStream = info.stream ? readWorldStream(info.stream) : null;
      this.lastStreamInfoAt = clockNow();
      if (info.stream) this.selectedRenderRadius = info.stream.radius;
      if (info.scene && Number.isSafeInteger(info.scene.killCount) && info.scene.killCount! >= 0) {
        this.killCount = info.scene.killCount!;
      }
    },

    selectRenderRadius(radius: number) {
      // Creation keeps its original single-radius contract and progress/cancel flow.
      if (!this.worldMode && !this.worldBusy && RENDER_RADIUS_OPTIONS.some(option => option.radius === radius)) {
        this.selectedRenderRadius = radius;
      }
    },

    restoreSettingsRanges() {
      const stream = this.worldStream;
      this.settingsRenderRadius = stream ? stream.radius : this.selectedRenderRadius;
      this.settingsPreloadRadius = stream ? stream.preloadRadius : this.selectedRenderRadius;
      this.settingsError = '';
      this.gameSettingsError = '';
    },

    openWorldSettings() {
      if (!this.worldMode || this.worldInputBlocked) return;
      this.quiesceWorldInput();
      this.refreshWorldInfo();
      this.refreshInventory();
      this.restoreSettingsRanges();
      this.settingsPanelOpen = true;
    },

    closeWorldSettings() {
      this.quiesceWorldInput();
      this.refreshWorldInfo();
      this.restoreSettingsRanges();
      this.settingsPanelOpen = false;
    },

    setSettingsRange(kind: ChunkRangeKind, radius: number) {
      if (!this.worldMode || !this.settingsPanelOpen || this.worldBusy || this.chunkLoading || this.saveExitPending ||
        !Number.isInteger(radius) || radius < 1 || radius > CHUNK_RANGE_MAX) return;
      if (kind === 'display') {
        this.settingsRenderRadius = radius;
        this.settingsPreloadRadius = Math.max(radius, this.settingsPreloadRadius);
      } else {
        this.settingsPreloadRadius = Math.max(radius, this.settingsRenderRadius);
      }
      this.settingsError = '';
    },

    // A chip tap picks one radius directly; preload is still kept >= display.
    selectSettingsRange(kind: ChunkRangeKind, radius: number) {
      this.setSettingsRange(kind, radius);
    },

    selectSettingsTab(tab: SettingsTab) {
      if (!SETTINGS_TABS.some(item => item.key === tab)) return;
      this.settingsTab = tab;
      this.settingsError = '';
      this.gameSettingsError = '';
    },

    // Same native bridge and validation as /gamemode; the mode shown is the one native returns.
    setGameModeFromSettings(mode: GameMode) {
      if (!this.worldMode || !this.settingsPanelOpen || this.worldBusy || this.chunkLoading || this.saveExitPending) return;
      if (this.inventoryState && this.inventoryState.gameMode === mode) return;
      const outcome = this.commandGameMode([mode]);
      this.gameSettingsError = outcome.ok ? '' : outcome.text;
      if (outcome.ok) this.pushChat('reply', outcome.text);
    },

    respawn() {
      if (!this.worldMode || this.worldBusy || this.chunkLoading) return;
      try {
        if (!worldRenderer.respawnPlayer) throw new Error('native respawnPlayer 未接入');
        if (worldRenderer.respawnPlayer() !== true) throw new Error('当前未死亡');
        this.respawnError = '';
        this.resetMovement();
        this.refreshWorldInfo();
        this.refreshInventory();
      } catch (e: any) {
        this.respawnError = '重生失败: ' + errorMessage(e);
      }
    },

    toggleKeepInventory() {
      if (!this.worldMode || !this.settingsPanelOpen || this.worldBusy || this.chunkLoading || this.saveExitPending) return;
      if (!this.worldRules) { this.gameSettingsError = '世界规则状态未就绪'; return; }
      const outcome = this.commandGameRule(['KeepInventory', String(!this.worldRules.rules.KeepInventory)]);
      this.gameSettingsError = outcome.ok ? '' : outcome.text;
    },

    toggleHudItem(key: HudItem) {
      if (!HUD_ITEMS.some(item => item.key === key)) return;
      this.hudOptions = { ...this.hudOptions, [key]: !this.hudOptions[key] };
    },

    setAllHudItems(value: boolean) {
      this.hudOptions = HUD_ITEMS.reduce((all, item) => { all[item.key] = value; return all; },
        {} as Record<HudItem, boolean>);
    },

    applyChunkRanges() {
      if (!this.worldMode || !this.settingsPanelOpen || this.worldBusy || this.chunkLoading || this.saveExitPending) return;
      this.quiesceWorldInput();
      try {
        if (worldRenderer.setChunkRanges(this.settingsRenderRadius, this.settingsPreloadRadius) !== true) {
          throw new Error('native 返回失败');
        }
        this.selectedRenderRadius = this.settingsRenderRadius;
        this.settingsError = '';
        this.refreshWorldInfo();
        this.restoreSettingsRanges();
      } catch (e: any) {
        this.settingsError = '设置失败: ' + errorMessage(e);
      }
    },

    selectWorldSize(size: number) {
      if (this.worldMode || this.worldBusy ||
        !WORLD_SIZE_OPTIONS.some(option => option.size === size)) return;
      this.selectedWorldSize = size;
    },

    openCreatePanel() {
      if (this.worldMode || this.worldBusy) return;
      this.stopLook();
      this.pickerOpen = false;
      this.seedInput = String(Date.now() >>> 0);
      this.actionMessage = '';
      this.createPanelOpen = true;
    },

    closeCreatePanel() {
      if (this.worldBusy) return;
      this.stopLook();
      this.createPanelOpen = false;
    },

    // Retained on purpose: a device with a real (physical/USB) keyboard still
    // delivers native input events, and the web preview has no system IME at
    // all. The system keyboard is an addition to this path, not a replacement.
    onSeedInput(event: any) {
      if (this.worldBusy) return;
      const value = event && event.detail && event.detail.value !== undefined
        ? event.detail.value : event && event.target && event.target.value !== undefined
          ? event.target.value : event && event.value;
      if (value !== undefined && value !== null) this.seedInput = String(value);
    },

    // --- System input method (KEYBOARD_INPUT.md) --------------------------

    // mounted(): one session per field, each with its own confirm handler.
    // Registering the listeners here (not on first tap) is what the document
    // requires; a session that cannot register simply stays unavailable.
    setupTextEdit() {
      if (!this.seedEdit) {
        this.seedEdit = new TextEditSession(
          (text: string) => this.applySeedText(text), () => this.endTextEditing());
      }
      if (!this.chatEdit) {
        this.chatEdit = new TextEditSession(
          (text: string) => this.applyChatText(text), () => this.endTextEditing());
      }
      // attach() is false on a host/mock runtime with no `global` module; the
      // page then keeps using the native input event and never shows a
      // keyboard-only affordance as if it worked.
      const seedOk = this.seedEdit.attach();
      const chatOk = this.chatEdit.attach();
      this.keyboardAvailable = isKeyboardAvailable() && seedOk && chatOk;
    },

    // Rule 4: close whatever is still open before the component goes away.
    releaseTextEdit() {
      this.textEditing = false;
      if (this.seedEdit) this.seedEdit.detach();
      if (this.chatEdit) this.chatEdit.detach();
      this.keyboardAvailable = false;
    },

    // Close the live session but keep the listeners, for onHide/panel changes.
    cancelTextEdit() {
      if (this.seedEdit) this.seedEdit.close();
      if (this.chatEdit) this.chatEdit.close();
      this.endTextEditing();
    },

    beginTextEditing() {
      this.textEditing = true;
      // worldInputBlocked is already true here, so every held contact becomes
      // 'ignore' and nothing resumes until the finger is physically released.
      if (this.worldMode) this.quiesceWorldInput();
      else this.stopLook();
    },

    endTextEditing() {
      if (!this.textEditing) return;
      this.textEditing = false;
      if (this.worldMode) this.quiesceWorldInput();
    },

    openSeedKeyboard() {
      if (this.worldMode || this.worldBusy || !this.createPanelOpen) return;
      const session = this.seedEdit;
      if (!session) return;
      this.beginTextEditing();
      const started = session.start({
        text: this.seedInput,
        placeholder: SEED_PLACEHOLDER,
        placeholderColor: CHAT_PLACEHOLDER_COLOR,
        autofocus: true,
        // 4294967295 is ten digits, the documented maximum seed.
        maxlength: 10,
        showCursor: true,
        cursorColor: CHAT_CURSOR_COLOR,
        cursorSize: 3,
        confirmButtonDisabledOnTextEmpty: false,
        // The seed is a number, so the numeric keypad is the correct layout.
        inputType: 'Number',
        multiLinesEditVisible: false,
        capsLockSwitchOn: false,
        enterButtonText: '确认',
      });
      // No keyboard means the existing native input event is still the way in.
      if (!started) this.endTextEditing();
    },

    // The documented editFinishedClicked for the seed field. Validation is the
    // same as the typed path: createNewWorld still rejects an out-of-range seed.
    applySeedText(text: string) {
      this.seedInput = text.trim();
      this.endTextEditing();
    },

    openChatKeyboard() {
      if (!this.worldMode || this.worldBusy || this.chunkLoading ||
        this.inventoryPanelOpen || this.pickerOpen || this.settingsPanelOpen ||
        this.saveExitPending || this.textEditing) return;
      const session = this.chatEdit;
      if (!session) return;
      this.beginTextEditing();
      const started = session.start({
        text: '',
        placeholder: CHAT_PLACEHOLDER,
        placeholderColor: CHAT_PLACEHOLDER_COLOR,
        autofocus: true,
        maxlength: CHAT_MAX_LENGTH,
        showCursor: true,
        cursorColor: CHAT_CURSOR_COLOR,
        cursorSize: 3,
        confirmButtonDisabledOnTextEmpty: true,
        // Commands are ASCII, so English-first is the right layout.
        inputType: 'EnUSPreferred',
        multiLinesEditVisible: false,
        capsLockSwitchOn: false,
        enterButtonText: '发送',
      });
      if (!started) {
        this.endTextEditing();
        this.pushChat('notice', '系统输入法不可用，无法输入聊天');
      }
    },

// Full-screen chat: history of chat lines and command output, plus input.
// Opening the panel must NOT raise the system keyboard; tapping the input
// row (.chat-input -> openChatKeyboard) is what starts an edit session.
    openChatPanel() {
      if (!this.worldMode || this.worldInputBlocked) return;
      this.quiesceWorldInput();
      this.chatPanelOpen = true;
    },

    closeChatPanel() {
      if (!this.chatPanelOpen) return;
      if (this.chatEdit) this.chatEdit.close();
      this.endTextEditing();
      this.quiesceWorldInput();
      this.chatPanelOpen = false;
    },

    pageChat(direction: number) {
      const maxOffset = Math.max(0, this.chatHistory.length - CHAT_PAGE_LINES);
      const step = CHAT_PAGE_LINES - 1;
      this.chatPageOffset = Math.max(0, Math.min(maxOffset, this.chatPageOffset + direction * step));
    },

    // Quick buttons for commands that need no arguments.
    runQuickCommand(text: string) {
      if (!this.chatPanelOpen || this.textEditing) return;
      this.submitChat(text);
    },

    applyChatText(text: string) {
      this.endTextEditing();
      this.submitChat(text);
    },

    // --- Chat log ---------------------------------------------------------

    pushChat(kind: ChatKind, text: string) {
      if (!text) return;
      this.chatSequence += 1;
      this.chatLog.push({
        id: this.chatSequence, kind,
        text: text.slice(0, CHAT_MAX_LENGTH * 2),
        expiresAt: clockNow() + CHAT_FADE_MS,
      });
      this.chatHistory.push(this.chatLog[this.chatLog.length - 1]);
      this.chatPageOffset = 0;
      if (this.chatHistory.length > CHAT_PANEL_HISTORY) {
        this.chatHistory = this.chatHistory.slice(-CHAT_PANEL_HISTORY);
      }
      // Bound the array itself, not only the rendered slice.
      if (this.chatLog.length > CHAT_HISTORY_LIMIT * 4) {
        this.chatLog = this.chatLog.slice(-CHAT_HISTORY_LIMIT * 2);
      }
    },

    // Entry point for both the keyboard and a physical-keyboard submit.
    submitChat(raw: string) {
      const text = (raw || '').replace(/\n/g, '').trim();
      if (!text) return;
      if (text.charAt(0) !== '/') {
        // No multiplayer exists, so this is a local echo and is labelled as
        // such; claiming it was sent anywhere would be a lie.
        this.pushChat('chat', '<我> ' + text + '（仅本地显示）');
        return;
      }
      this.pushChat('chat', text);
      const outcome = this.runCommand(text.slice(1));
      this.pushChat(outcome.ok ? 'reply' : 'notice', outcome.text);
    },

    // --- Commands ---------------------------------------------------------
    // Only commands that really reach a native bridge are implemented. An
    // unknown name is rejected, a bad argument gets its usage line, and a
    // missing bridge says so: nothing here ever reports a result it did not get.

    runCommand(body: string): CommandOutcome {
      const words = commandWords(body);
      if (!words.length) return { ok: false, text: '未知指令，输入 /help 查看可用指令' };
      const name = words[0].toLowerCase();
      const args = words.slice(1);
      if (name === 'help') return this.commandHelp();
      if (name === 'seed') return this.commandSeed(args);
      if (name === 'time') return this.commandTime(args);
      if (name === 'gamemode') return this.commandGameMode(args);
      if (name === 'give') return this.commandGive(args);
      if (name === 'tp') return this.commandTeleport(args);
      if (name === 'gamerule') return this.commandGameRule(args);
      return { ok: false, text: '未知指令: /' + words[0] + '，输入 /help 查看可用指令' };
    },

    commandHelp(): CommandOutcome {
      // One line per command, so the fading log shows them in order.
      for (const line of COMMAND_HELP.slice(0, -1)) this.pushChat('reply', line);
      return { ok: true, text: COMMAND_HELP[COMMAND_HELP.length - 1] };
    },

    commandSeed(args: string[]): CommandOutcome {
      if (args.length) return { ok: false, text: '用法: /seed' };
      if (!this.worldMode) return { ok: false, text: '需要先进入世界' };
      return { ok: true, text: '世界种子: ' + this.worldSeed };
    },

    commandTime(args: string[]): CommandOutcome {
      const usage = { ok: false, text: '用法: /time set day|night|<ticks> 或 /time query' };
      if (!args.length) return usage;
      const action = args[0].toLowerCase();
      if (action === 'query') {
        if (args.length !== 1) return usage;
        const scene = this.worldScene;
        if (!scene) return { ok: false, text: '场景状态未就绪' };
        // dayTicks is the celestial clock; it differs from ticks once
        // DoDaylightCycle has been switched off, so report the one that drives
        // the sky rather than the raw simulation counter.
        const ticks = scene.dayTicks === undefined ? scene.ticks : scene.dayTicks;
        return { ok: true, text: '当前时刻 ' + (ticks % WORLD_DAY_TICKS) + ' ticks（一天 ' + WORLD_DAY_TICKS + '）' };
      }
      if (action !== 'set' || args.length !== 2) return usage;
      const value = args[1].toLowerCase();
      // Original day/night tick marks.
      let ticks: number | null = value === 'day' ? 1000 : value === 'night' ? 13000 : null;
      if (ticks === null) {
        ticks = parseIntegerArg(args[1]);
        if (ticks === null || ticks < 0) {
          return { ok: false, text: 'ticks 须为 0 或正整数，或 day / night' };
        }
      }
      if (!this.worldMode || this.chunkLoading || this.worldBusy) {
        return { ok: false, text: '世界忙或未进入，无法设置时间' };
      }
      if (!worldRenderer.setSceneTime) return { ok: false, text: '指令未接入: native setSceneTime 不可用' };
      try {
        this.applySceneJson(worldRenderer.setSceneTime(ticks));
        this.worldDirty = true;
        return { ok: true, text: '时刻已设为 ' + (ticks % WORLD_DAY_TICKS) };
      } catch (e: any) {
        return { ok: false, text: '设置时间失败: ' + errorMessage(e) };
      }
    },

    commandGameMode(args: string[]): CommandOutcome {
      if (args.length !== 1) return { ok: false, text: '用法: /gamemode creative|survival' };
      const mode = args[0].toLowerCase();
      if (mode !== 'creative' && mode !== 'survival') {
        return { ok: false, text: '模式须为 creative 或 survival' };
      }
      if (!this.worldMode || this.chunkLoading || this.worldBusy) {
        return { ok: false, text: '世界忙或未进入，无法切换模式' };
      }
      if (!worldRenderer.setGameMode) return { ok: false, text: '指令未接入: native setGameMode 不可用' };
      try {
        // Native owns the mode and returns the authoritative inventory.
        this.applyInventory(worldRenderer.setGameMode(mode as GameMode));
        this.worldDirty = true;
        return { ok: true, text: '模式已切换为 ' + (mode === 'creative' ? '创造' : '生存') };
      } catch (e: any) {
        return { ok: false, text: '切换模式失败: ' + errorMessage(e) };
      }
    },

    // Creative give only: setCreativeItem is the creative-pick bridge and
    // native rejects it outside creative mode, so survival is refused up front
    // instead of calling it and reporting a confusing native error.
    commandGive(args: string[]): CommandOutcome {
      if (!args.length || args.length > 2) return { ok: false, text: '用法: /give <物品> [数量]' };
      if (!this.worldMode || this.chunkLoading || this.worldBusy) {
        return { ok: false, text: '世界忙或未进入，无法给予物品' };
      }
      let count = 1;
      if (args.length === 2) {
        const parsed = parseIntegerArg(args[1]);
        if (parsed === null || parsed < 1 || parsed > 64) {
          return { ok: false, text: '数量须为 1 至 64 的整数' };
        }
        count = parsed;
      }
      const item = this.findCreativeItem(args[0]);
      if (!item) return { ok: false, text: '未知物品: ' + args[0] + '（可用背包的创造物品列表查看）' };
      if (!this.refreshInventory() || !this.inventoryState) {
        return { ok: false, text: '背包状态未就绪' };
      }
      if (this.inventoryState.gameMode === 'survival') {
        return { ok: false, text: '生存模式不能使用 /give' };
      }
      if (!worldRenderer.setCreativeItem) {
        return { ok: false, text: '指令未接入: native setCreativeItem 不可用' };
      }
      if (count > item.maxStack) {
        return { ok: false, text: item.name + ' 单格最多 ' + item.maxStack + ' 个' };
      }
      try {
        // setCreativeItem fills the selected hotbar slot, which is exactly what
        // the creative item picker does; the count it places is native's.
        this.applyInventory(worldRenderer.setCreativeItem(this.inventoryState.selected, item.itemIndex));
        this.worldDirty = true;
        const placed = this.inventoryState.slots[this.inventoryState.selected];
        const got = placed ? placed.count : 0;
        // Report what native actually put there, never the requested count.
        // setCreativeItem is the creative-pick bridge and always fills the
        // slot to the item's stack limit, so a smaller request cannot be
        // honoured; say so instead of letting the number look accepted.
        const note = args.length === 2 && got !== count
          ? '（原生创造给予为整组，未按 ' + count + ' 计）' : '';
        return { ok: true, text: '已放入快捷栏 ' + (this.inventoryState.selected + 1) + ' 格: ' +
          item.name + ' ×' + got + note };
      } catch (e: any) {
        return { ok: false, text: '给予失败: ' + errorMessage(e) };
      }
    },

    // Accepts the canonical item key ('item:1:0'), the English catalog key
    // ('stone') or the Chinese display name, because all three are visible in
    // the UI and none of them is ambiguous.
    findCreativeItem(text: string) {
      const lower = text.toLowerCase();
      return this.creativeItems.find(item => item.key === text) ||
        this.creativeItems.find(item => item.renderCatalogKey !== '' &&
          item.renderCatalogKey.toLowerCase() === lower) ||
        this.creativeItems.find(item => item.name === text) || null;
    },

    commandTeleport(args: string[]): CommandOutcome {
      if (args.length !== 3) return { ok: false, text: '用法: /tp <x> <y> <z>' };
      const point = args.map(parseNumberArg);
      if (point.some(value => value === null)) {
        return { ok: false, text: '坐标须为十进制数字' };
      }
      const [x, y, z] = point as number[];
      if (!this.worldMode || this.chunkLoading || this.worldBusy) {
        return { ok: false, text: '世界忙或未进入，无法传送' };
      }
      // Native size order is width, height, depth, i.e. x, y, z.
      if (this.worldSize.length !== 3) return { ok: false, text: '世界尺寸未就绪' };
      const [width, height, depth] = this.worldSize;
      if (x < 0 || x >= width || y < 0 || y >= height || z < 0 || z >= depth) {
        return { ok: false, text: '坐标超出世界范围: x/z 0 至 ' + (width - 1) + '，y 0 至 ' + (height - 1) };
      }
      if (!worldRenderer.teleportPlayer) {
        return { ok: false, text: '指令未接入: native teleportPlayer 不可用' };
      }
      try {
        // Native decides the landing spot (ground, collision); the UI reports
        // the position it reads back, never the one it asked for.
        this.applyWorldInfo(readWorldInfo(JSON.parse(worldRenderer.teleportPlayer(x, y, z))));
        this.worldDirty = true;
        return { ok: true, text: '已传送至 ' + this.playerPosition };
      } catch (e: any) {
        return { ok: false, text: '传送失败: ' + errorMessage(e) };
      }
    },

    commandGameRule(args: string[]): CommandOutcome {
      if (!args.length || args.length > 2) {
        return { ok: false, text: '用法: /gamerule <规则> [true|false]，规则名见 ' + WORLD_RULE_NAMES.length + ' 项' };
      }
      const rule = canonicalRuleName(args[0]);
      if (!rule) {
        return { ok: false, text: '未知规则: ' + args[0] + '（可用: ' + WORLD_RULE_NAMES.join(' ') + '）' };
      }
      if (!this.worldMode || this.chunkLoading || this.worldBusy) {
        return { ok: false, text: '世界忙或未进入，无法读写规则' };
      }
      if (args.length === 1) {
        const rules = this.worldRules;
        if (!rules) return { ok: false, text: '世界规则状态未就绪' };
        // Always state the enforcement status: six of the ten rules are only
        // stored, and a query that hid that would be misleading.
        return { ok: true, text: rule + ' = ' + rules.rules[rule] +
          '（' + ruleEnforcementText(rules.enforcement[rule]) + '）' };
      }
      const value = parseBooleanArg(args[1].toLowerCase());
      if (value === null) return { ok: false, text: '值须为 true 或 false' };
      if (!worldRenderer.setWorldRule) {
        return { ok: false, text: '指令未接入: native setWorldRule 不可用' };
      }
      try {
        this.applySceneJson(worldRenderer.setWorldRule(rule, value));
        this.worldDirty = true;
        const status = this.worldRules ? ruleEnforcementText(this.worldRules.enforcement[rule]) : '生效状态未知';
        return { ok: true, text: rule + ' 已设为 ' + value + '（' + status + '）' };
      } catch (e: any) {
        return { ok: false, text: '设置规则失败: ' + errorMessage(e) };
      }
    },

    // Shared reader for the bridges that answer with a fresh scene summary.
    // It goes through the same validation as the polled snapshot, so a
    // malformed reply throws instead of being displayed.
    applySceneJson(json: string) {
      const info = readWorldInfo(JSON.parse(json));
      this.applyWorldInfo(info);
    },

    createNewWorld() {
      if (this.worldMode || this.worldBusy) return;
      const text = this.seedInput.trim();
      const seed = text === '' ? Date.now() >>> 0 : Number(text);
      if (!Number.isInteger(seed) || seed < 0 || seed > 0xffffffff) {
        this.actionMessage = '种子须为 0 至 4294967295 的整数';
        return;
      }
      this.beginWorldJob(true, seed);
    },

    continueWorld() {
      if (this.worldMode || this.worldBusy || !this.lastWorldPath) return;
      this.beginWorldJob(false);
    },

    beginWorldJob(created: boolean, seed = 0) {
      if (this.worldBusy || !this.pageMounted || !this.pageVisible) return;
      this.quiesceWorldInput();
      this.stopRenderLoop();
      this.stopWorldJobPoll();
      const generation = ++this.worldJobGeneration;
      this.worldBusy = true;
      this.worldJobCreated = created;
      this.worldJobCancelRequested = false;
      this.worldJobCancelPending = false;
      this.worldJobError = '';
      // No invented percentage: the bar appears only after the first native snapshot.
      this.worldJob = null;
      this.actionMessage = '';
      this.pickerOpen = false;
      this.settingsPanelOpen = false;
      this.worldJobActive = true;
      try {
        const ok = created
          ? worldRenderer.beginCreateWorld(seed, this.selectedWorldSize, this.selectedRenderRadius)
          : worldRenderer.beginOpenWorld(this.lastWorldPath, this.selectedRenderRadius);
        if (ok !== true) {
          this.completeWorldJob((created ? '创建失败: ' : '继续失败: ') + 'native 未启动任务');
          return;
        }
      } catch (e: any) {
        this.worldJobError = (created ? '创建失败: ' : '继续失败: ') + errorMessage(e);
        this.cancelWorldJob();
        return;
      }
      // Yield for the overlay before the first short snapshot; native work stays off-thread.
      this.scheduleWorldJobPoll(generation);
    },

    stopWorldJobPoll() {
      if (this.worldJobTimer !== null) clearTimeout(this.worldJobTimer);
      this.worldJobTimer = null;
    },

    scheduleWorldJobPoll(generation: number) {
      if (generation !== this.worldJobGeneration || !this.worldBusy ||
        !this.pageMounted || !this.pageVisible || this.worldJobTimer !== null) return;
      this.worldJobTimer = setTimeout(() => {
        // A stale callback must not clear the timer belonging to a newer lifecycle/job.
        if (generation !== this.worldJobGeneration) return;
        this.worldJobTimer = null;
        this.pollWorldJob(generation);
      }, WORLD_JOB_INTERVAL_MS);
    },

    requestNativeWorldJobCancel() {
      try {
        if (worldRenderer.cancelWorldJob() !== true) this.actionMessage = '取消请求未接受，正在确认任务状态';
      } catch (e: any) {
        this.actionMessage = '取消请求失败，正在确认任务状态: ' + errorMessage(e);
      }
    },

    cancelWorldJob() {
      if (!this.worldBusy || !this.worldJobActive) return;
      this.quiesceWorldInput();
      this.worldJobCancelRequested = true;
      this.worldJobCancelPending = true;
      this.requestNativeWorldJobCancel();
      this.scheduleWorldJobPoll(this.worldJobGeneration);
    },

    resumeWorldJobCancellation() {
      if (!this.worldJobCancelPending || !this.worldBusy || !this.pageMounted || !this.pageVisible) return;
      // Never finish a job which was cancelled by onHide, even if it became ready in the background.
      this.worldJobCancelRequested = true;
      this.requestNativeWorldJobCancel();
      this.scheduleWorldJobPoll(this.worldJobGeneration);
    },

    pollWorldJob(generation: number) {
      if (generation !== this.worldJobGeneration || !this.worldBusy || !this.pageMounted || !this.pageVisible) return;
      try {
        const status = readWorldJobStatus(worldRenderer.worldJobStatus());
        if (generation !== this.worldJobGeneration || !this.pageMounted || !this.pageVisible) return;
        this.worldJob = status;
        if (status.state === 'cancelled' || status.state === 'idle') {
          this.completeWorldJob(this.worldJobError || (status.state === 'cancelled' || this.worldJobCancelRequested
            ? '已取消世界任务' : '世界任务未启动'));
          return;
        }
        if (status.state === 'failed') {
          this.completeWorldJob(this.worldJobError || (this.worldJobCreated ? '创建失败: ' : '继续失败: ') +
            (status.error || '未知原生错误'));
          return;
        }
        if (this.worldJobCancelRequested) {
          // ready can race with cancel; discard through native cancellation, never finish/enter it.
          if (status.state !== 'cancelling') this.requestNativeWorldJobCancel();
        } else if (status.state === 'ready') {
          try {
            const info = readWorldMetadata(worldRenderer.finishWorldJob());
            if (generation !== this.worldJobGeneration || !this.pageMounted || !this.pageVisible) return;
            this.enterWorld(info, this.worldJobCreated);
            this.completeWorldJob('');
          } catch (e: any) {
            // finish is a one-shot take; never poll or finish a consumed job again.
            this.requestNativeWorldJobCancel();
            this.completeWorldJob('进入世界失败: ' + errorMessage(e));
          }
          return;
        }
      } catch (e: any) {
        this.worldJobError = '世界任务失败: ' + errorMessage(e);
        this.worldJobCancelRequested = true;
        this.worldJobCancelPending = true;
        this.requestNativeWorldJobCancel();
      }
      this.scheduleWorldJobPoll(generation);
    },

    completeWorldJob(message: string) {
      this.stopWorldJobPoll();
      this.worldJobGeneration += 1;
      this.worldJob = null;
      this.worldJobActive = false;
      this.worldJobCancelRequested = false;
      this.worldJobCancelPending = false;
      this.worldJobError = '';
      this.worldBusy = false;
      this.actionMessage = message;
      this.resetMovement();
      if (this.worldMode && !message) {
        this.chunkLoading = false;
        this.refreshInventory();
      }
      this.startRenderLoop();
    },

    enterWorld(info: WorldMetadata, created: boolean) {
      this.resetMovement();
      this.previewYaw = this.yaw;
      this.previewPitch = this.pitch;
      this.yaw = created ? 0 : this.worldYaw;
      this.pitch = created ? -10 : this.worldPitch;
      this.worldPath = info.path;
      this.lastWorldPath = info.path;
      this.worldSeed = info.seed;
      // A new world starts with an empty chat log; nothing carries over.
      this.chatLog = [];
      this.chatHistory = [];
      this.chatPageOffset = 0;
      this.chatPanelOpen = false;
      this.worldRules = null;
      this.targetHP = null;
      this.killCount = 0;
      this.firstAttackLogged = false;
      this.applyWorldInfo(info);
      this.createPanelOpen = false;
      this.pickerOpen = false;
      this.actionMessage = '';
      this.nativeResult = '';
      this.worldInfoError = '';
      this.worldDirty = created;
      this.lastFrameAt = clockNow();
      this.lastWorldInfoAt = this.lastFrameAt;
      this.worldMode = true;
      this.inventoryPanelOpen = false;
      this.inventoryState = null;
      this.inventoryError = '';
      this.inventoryFrom = -1;
      this.containerPanelOpen = false;
      this.containerState = null;
      this.containerSelection = null;
      this.emptyHandNotified = false;
      this.settingsPanelOpen = false;
      this.chunkLoading = false;
      this.chunkLoadError = '';
      this.saveExitPending = false;
      this.fpsReportPending = true;
      // completeWorldJob releases worldBusy, then starts native touch and rendering.
    },

    saveCurrentWorld(): boolean {
      if (!this.worldMode || !this.worldPath) return false;
      try {
        if (worldRenderer.saveWorld() !== true) throw new Error('native 返回失败');
        this.worldDirty = false;
        this.actionMessage = '';
        return true;
      } catch (e: any) {
        this.actionMessage = '保存失败，仍保留当前世界: ' + errorMessage(e);
        return false;
      }
    },

    saveAndExit() {
      if (!this.worldMode || this.worldBusy) return;
      this.quiesceWorldInput();
      this.closeContainerPanel();
      this.refreshChunkLoadStatus();
      if (this.chunkLoading) {
        this.saveExitPending = true;
        this.actionMessage = '等待世界任务结束后保存退出';
        return;
      }
      this.saveExitPending = false;
      this.worldBusy = true;
      try {
        if (!this.closeInventory() || !this.saveCurrentWorld()) return;
        this.leaveWorldPreview();
      } finally {
        this.worldBusy = false;
      }
    },

    leaveWorldPreview() {
      this.cancelTextEdit();
      this.chatLog = [];
      this.chatHistory = [];
      this.chatPanelOpen = false;
      this.worldRules = null;
      this.pickerOpen = false;
      this.settingsPanelOpen = false;
      this.chunkLoading = false;
      this.chunkLoadError = '';
      this.saveExitPending = false;
      this.stopNativeTouch();
      this.worldYaw = this.yaw;
      this.worldPitch = this.pitch;
      this.worldMode = false;
      this.fpsReportPending = true;
      this.yaw = this.previewYaw;
      this.pitch = this.previewPitch;
      this.nativeResult = '';
      this.worldInfoError = '';
      this.actionMessage = '已保存至 ' + this.lastWorldPath + '（重启后丢失）';
      this.lastFrameAt = clockNow();
    },

    beginNativeTouch() {
      if (!this.worldMode || this.worldBusy || !this.pageVisible || !this.pageMounted || this.nativeTouchStarted) return;
      this.resetMovement();
      try {
        if (typeof worldRenderer.beginTouch !== 'function' ||
          typeof worldRenderer.pollTouch !== 'function' || typeof worldRenderer.stopTouch !== 'function') return;
        this.nativeTouchStarted = worldRenderer.beginTouch() === true;
        if (this.nativeTouchStarted) this.pollNativeTouch();
      } catch (_) {
        this.stopNativeTouch();
      }
    },

    stopNativeTouch() {
      if (this.nativeTouchStarted) {
        try { if (worldRenderer.stopTouch) worldRenderer.stopTouch(); } catch (_) { /* Already gone. */ }
      }
      this.nativeTouchStarted = false;
      this.nativeTouchAvailable = false;
      this.resetMovement();
    },

    pollNativeTouch() {
      if (!this.worldMode || !this.nativeTouchStarted || !worldRenderer.pollTouch) return;
      try {
        const packet = JSON.parse(worldRenderer.pollTouch());
        if (!packet || typeof packet.available !== 'boolean' || !Array.isArray(packet.touches)) {
          throw new Error('Invalid touch snapshot');
        }
        if (!packet.available) {
          if (this.nativeTouchAvailable) this.resetMovement();
          this.nativeTouchAvailable = false;
          return;
        }
        const contacts: Record<string, { x: number; y: number }> = {};
        for (const touch of packet.touches) {
          if (!touch || typeof touch.id !== 'number' || !isFinite(touch.id) ||
            typeof touch.x !== 'number' || !isFinite(touch.x) ||
            typeof touch.y !== 'number' || !isFinite(touch.y)) continue;
          contacts['native:' + touch.id] = { x: touch.x, y: touch.y };
        }
        if (!this.nativeTouchAvailable) this.resetMovement();
        this.nativeTouchAvailable = true;
        for (const id of Object.keys(this.worldContacts)) {
          if (!contacts[id]) delete this.worldContacts[id];
        }
        for (const id of Object.keys(contacts)) {
          const point = contacts[id];
          const contact = this.worldContacts[id];
          if (!contact) {
            this.worldContacts[id] = { ...point, role: this.worldInputBlocked ? 'ignore' : nativeTouchRole(point.x, point.y,
              !!(this.actionMessage || this.nativeResult || this.worldInfoError || this.inventoryError), this.hudHeight) };
          } else {
            if (this.worldInputBlocked) contact.role = 'ignore';
            if (contact.role === 'look') this.applyLookDelta(point.x - contact.x, point.y - contact.y);
            contact.x = point.x;
            contact.y = point.y;
          }
        }
        // Roles belong to contacts until release; leaving a button never changes its role.
        for (const key of MOVEMENT_KEYS) {
          this.movement[key] = Object.keys(this.worldContacts).some(id => this.worldContacts[id].role === key);
        }
        this.setPrimary(Object.keys(this.worldContacts).some(id => this.worldContacts[id].role === 'primary'));
        this.setSecondary(Object.keys(this.worldContacts).some(id => this.worldContacts[id].role === 'secondary'));
        this.touchCount = Object.keys(this.worldContacts).length;
        this.debugTouchCount = Math.max(this.debugTouchCount, this.touchCount);
        if (!this.nativeMultiTouchLogged && this.touchCount >= 2) {
          this.nativeMultiTouchLogged = true;
          console.error('MINECRAFT_TOUCH_STATE ' + JSON.stringify({
            touchCount: this.touchCount, movement: this.movement, yaw: this.yaw, pitch: this.pitch,
            roles: Object.keys(this.worldContacts).map(id => ({ id, role: this.worldContacts[id].role })),
          }));
        }
      } catch (_) {
        // A failed backend must release its own controls before Falcon becomes the fallback.
        this.stopNativeTouch();
      }
    },

    setPrimary(held: boolean) {
      if (held === this.primaryHeld) return;
      this.primaryHeld = held;
      this.nextPrimaryAt = 0;
      if (held && !this.nativeTouchAvailable) this.tickPrimary(clockNow());
    },

    // Original left button: a mob under the crosshair is attacked, anything else is mined.
    tickPrimary(now: number) {
      if (!this.worldMode || this.chunkLoading || this.worldInputBlocked || !this.pageVisible ||
        !this.primaryHeld || now < this.nextPrimaryAt) return;
      if (this.targetHP) this.attackPrimary(now);
      else this.minePrimary(now);
    },

    minePrimary(now: number) {
      this.nextPrimaryAt = now + MINE_INTERVAL_MS;
      try {
        if (worldRenderer.mineWorld && worldRenderer.mineWorld(this.yaw, this.pitch) === true) {
          this.worldDirty = true;
        }
      } catch (_) { /* Creative mining misses/failures are intentionally silent. */ }
    },

    attackPrimary(now: number) {
      this.nextPrimaryAt = now + ATTACK_INTERVAL_MS;
      try {
        if (!worldRenderer.attackWorld) { this.targetHP = null; return; }
        // Native owns hit detection, damage, hurt-frame immunity and kill accounting.
        // Never subtract HP locally or reuse mining as an attack/animation action.
        const result: WorldAttack = JSON.parse(worldRenderer.attackWorld(this.yaw, this.pitch));
        if (!result || !['hit', 'damaged', 'killed'].every(key => typeof (result as any)[key] === 'boolean') ||
          !Number.isSafeInteger(result.killCount) || result.killCount < 0) throw new Error('攻击信息无效');
        const target = result.hit ? readWorldTarget(result) : null;
        this.targetHP = target && !result.killed && target.health > 0 ? target : null;
        this.killCount = result.killCount;
        if (result.damaged || result.killed) this.worldDirty = true;
        if (!this.firstAttackLogged && target) {
          this.firstAttackLogged = true;
          console.error('MINECRAFT_ATTACK_STATS ' + JSON.stringify({
            kind: target.kind, hp: target.health, maxHP: target.maxHealth, killed: result.killed,
          }));
        }
      } catch (_) {
        // Bridge exceptions remain silent and release stale feedback, not other contacts.
        this.targetHP = null;
      }
    },

    setSecondary(held: boolean) {
      if (held === this.secondaryHeld) return;
      this.secondaryHeld = held;
      this.nextSecondaryAt = 0;
      if (!held) {
        // Player::releaseUsingItem: letting go cancels an unfinished meal.
        this.secondaryUsePressed = false;
        try { if (worldRenderer.eatHeld) worldRenderer.eatHeld(false, false); } catch (_) { /* best effort */ }
      }
      if (held && !this.nativeTouchAvailable) this.tickSecondary(clockNow());
    },

    // Original right button. Native placeWorld decides what the click means: it
    // tries Tile::use first (a wooden door toggles, an iron door consumes the
    // click), and only places the held item when nothing handled the use. The UI
    // must never re-derive that decision, so it reports no placement outcome.
    tickSecondary(now: number) {
      if (!this.worldMode || this.chunkLoading || this.worldInputBlocked || !this.pageVisible ||
        !this.secondaryHeld || now < this.nextSecondaryAt) return;
      this.nextSecondaryAt = now + PLACE_INTERVAL_MS;
      try {
        if (!worldRenderer.placeWorld) throw new Error('native placeWorld 未接入');
        // true means native acted (interaction or placement); false only means it
        // did not. "Failed to place" would be wrong, since not placing is often
        // the correct original behaviour.
        let acted = worldRenderer.placeWorld(this.yaw, this.pitch);
        if (typeof acted !== 'boolean') throw new Error('native 交互信息无效');
        // Tile::use on a chest/workbench/furnace opened a native panel.
        if (acted && this.syncContainerPanel()) return;
        // Source order: useOn (place/interact) first, then Item::use (eat / equip).
        const pressed = !this.secondaryUsePressed;
        this.secondaryUsePressed = true;
        if (!acted && worldRenderer.eatHeld) acted = worldRenderer.eatHeld(true, pressed) === true;
        const refreshed = this.refreshInventory();
        if (acted) {
          this.worldDirty = true;
          this.emptyHandNotified = false;
          this.actionMessage = '';
        } else if (refreshed && this.inventoryState && this.inventoryState.emptyHand && !this.emptyHandNotified) {
          this.emptyHandNotified = true;
          this.actionMessage = '空手';
        }
      } catch (e: any) {
        // Only a bridge error is reported, never an interpretation of the result.
        this.actionMessage = '右键无效果: ' + errorMessage(e);
        this.setSecondary(false);
      }
    },

    resetMovement() {
      for (const key of MOVEMENT_KEYS) {
        this.movement[key] = false;
        this.movementTouches[key] = [];
      }
      this.worldContacts = {};
      this.fallbackRoles = {};
      this.primaryTouches = [];
      this.setPrimary(false);
      this.secondaryTouches = [];
      this.setSecondary(false);
      this.touchCount = 0;
      this.stopLook();
    },

    claimFallbackControl(role: ControlRole, event: any): string[] {
      const ids: string[] = [];
      for (const touch of eventTouches(event)) {
        const id = touchId(touch);
        if (!id) continue;
        const previous = this.fallbackRoles['touch:' + id];
        if (previous && previous !== role) continue;
        this.fallbackRoles['touch:' + id] = role;
        if (ids.indexOf(id) === -1) ids.push(id);
      }
      this.countFallbackTouches();
      if (event && typeof event.preventDefault === 'function') event.preventDefault();
      return ids;
    },

    countFallbackTouches() {
      this.touchCount = Object.keys(this.fallbackRoles).length;
      this.debugTouchCount = Math.max(this.debugTouchCount, this.touchCount);
    },

    movementStart(key: MovementKey, event: any) {
      if (!this.worldMode || this.worldInputBlocked || this.nativeTouchAvailable) return;
      const ids = this.claimFallbackControl(key, event);
      for (const id of ids) {
        if (this.movementTouches[key].indexOf(id) === -1) this.movementTouches[key].push(id);
      }
      // ID-less runtimes retain single-contact button fallback, not a fabricated shared ID.
      if (ids.length || eventTouches(event).some(touch => !touchId(touch))) this.movement[key] = true;
    },

    movementEnd(key: MovementKey, event: any) {
      if (this.worldMode && this.nativeTouchAvailable) return;
      const ids = eventTouches(event).map(touchId).filter(id => id !== '');
      this.movementTouches[key] = this.movementTouches[key].filter(id => ids.indexOf(id) === -1);
      for (const id of ids) {
        if (this.fallbackRoles['touch:' + id] === key) delete this.fallbackRoles['touch:' + id];
      }
      if (ids.length || eventTouches(event).some(touch => !touchId(touch))) {
        this.movement[key] = this.movementTouches[key].length > 0;
      }
      this.countFallbackTouches();
    },

    primaryStart(event: any) {
      if (!this.worldMode || this.worldInputBlocked || this.nativeTouchAvailable) return;
      const ids = this.claimFallbackControl('primary', event);
      for (const id of ids) {
        if (this.primaryTouches.indexOf(id) === -1) this.primaryTouches.push(id);
      }
      if (ids.length || eventTouches(event).some(touch => !touchId(touch))) this.setPrimary(true);
    },

    primaryEnd(event: any) {
      if (this.worldMode && this.nativeTouchAvailable) return;
      const ids = eventTouches(event).map(touchId).filter(id => id !== '');
      this.primaryTouches = this.primaryTouches.filter(id => ids.indexOf(id) === -1);
      for (const id of ids) {
        if (this.fallbackRoles['touch:' + id] === 'primary') delete this.fallbackRoles['touch:' + id];
      }
      if (ids.length || eventTouches(event).some(touch => !touchId(touch))) {
        this.setPrimary(this.primaryTouches.length > 0);
      }
      this.countFallbackTouches();
    },

    secondaryStart(event: any) {
      if (!this.worldMode || this.worldInputBlocked || this.nativeTouchAvailable) return;
      const ids = this.claimFallbackControl('secondary', event);
      for (const id of ids) {
        if (this.secondaryTouches.indexOf(id) === -1) this.secondaryTouches.push(id);
      }
      if (ids.length || eventTouches(event).some(touch => !touchId(touch))) this.setSecondary(true);
    },

    secondaryEnd(event: any) {
      if (this.worldMode && this.nativeTouchAvailable) return;
      const ids = eventTouches(event).map(touchId).filter(id => id !== '');
      this.secondaryTouches = this.secondaryTouches.filter(id => ids.indexOf(id) === -1);
      for (const id of ids) {
        if (this.fallbackRoles['touch:' + id] === 'secondary') delete this.fallbackRoles['touch:' + id];
      }
      if (ids.length || eventTouches(event).some(touch => !touchId(touch))) {
        this.setSecondary(this.secondaryTouches.length > 0);
      }
      this.countFallbackTouches();
    },

    onControlTouch(event: any) {
      // Read-only native input must not prevent Falcon's save/camera click delivery.
      if (this.worldMode && this.nativeTouchAvailable) return;
      if (this.worldMode) {
        for (const touch of eventTouches(event)) {
          const id = touchId(touch);
          if (id && !this.fallbackRoles['touch:' + id]) this.fallbackRoles['touch:' + id] = 'ignore';
        }
        this.countFallbackTouches();
      } else if (eventTouches(event).some(touch => touchId(touch) === this.lookTouchId)) {
        this.stopLook();
      }
    },

    releaseTouches(event: any) {
      if (this.worldMode && this.nativeTouchAvailable) {
        for (const touch of eventTouches(event)) delete this.fallbackRoles['touch:' + touchId(touch)];
        return;
      }
      // Bubble/root releases clear only their own IDs, never other fingers.
      const ids = eventTouches(event).map(touchId).filter(id => id !== '');
      for (const key of MOVEMENT_KEYS) {
        if (this.movementTouches[key].some(id => ids.indexOf(id) !== -1)) this.movementEnd(key, event);
      }
      if (this.primaryTouches.some(id => ids.indexOf(id) !== -1)) this.primaryEnd(event);
      if (this.secondaryTouches.some(id => ids.indexOf(id) !== -1)) this.secondaryEnd(event);
      if (ids.indexOf(this.lookTouchId) !== -1 ||
        (!this.lookTouchId && eventTouches(event).some(touch => !touchId(touch)))) this.stopLook();
      for (const id of ids) delete this.fallbackRoles['touch:' + id];
      this.countFallbackTouches();
    },

    onLookStart(event: any) {
      if ((this.worldMode && this.nativeTouchAvailable) || this.worldInputBlocked || this.createPanelOpen || this.dragging) return;
      const touch = eventTouches(event).find(item => {
        const id = touchId(item);
        return !id || !this.fallbackRoles['touch:' + id];
      });
      if (!touch) return;
      const identifier = touchId(touch);
      this.touchCoordinates = pointOf(touch, 'screen') ? 'screen' : 'page';
      const point = pointOf(touch, this.touchCoordinates);
      this.dragging = !!point;
      if (!point) return;
      this.lookTouchId = identifier;
      if (identifier) this.fallbackRoles['touch:' + identifier] = 'look';
      this.countFallbackTouches();
      this.lastX = point.x;
      this.lastY = point.y;
      if (event && typeof event.preventDefault === 'function') event.preventDefault();
    },

    applyLookDelta(deltaX: number, deltaY: number) {
      if (this.worldInputBlocked) return;
      const sensitivity = this.worldMode ? this.lookSensitivity : 0.6;
      const dx = deltaX * sensitivity;
      const dy = deltaY * sensitivity;
      // Native camera: yaw 0 = +Z, positive pitch = looking up.
      this.yaw = ((this.yaw + (this.worldMode ? dx : -dx)) % 360 + 360) % 360;
      this.pitch = Math.max(-80, Math.min(80, this.pitch + (this.worldMode ? -dy : dy)));
      if (this.worldMode && (dx !== 0 || dy !== 0)) this.worldDirty = true;
    },

    updateLook(event: any) {
      const point = touchPoint(event, this.touchCoordinates, this.lookTouchId);
      if (!this.dragging || !point) return;
      this.applyLookDelta(point.x - this.lastX, point.y - this.lastY);
      this.lastX = point.x;
      this.lastY = point.y;
    },

    onLookMove(event: any) {
      if ((this.worldMode && this.nativeTouchAvailable) || !this.dragging) return;
      this.updateLook(event);
      if (event && typeof event.preventDefault === 'function') event.preventDefault();
    },

    onLookEnd(event: any) {
      if ((this.worldMode && this.nativeTouchAvailable) || !this.dragging ||
        !eventTouches(event).some(touch => touchId(touch) === this.lookTouchId)) return;
      this.updateLook(event);
      this.stopLook();
    },

    onLookCancel(event: any) {
      if (this.worldMode && this.nativeTouchAvailable) return;
      if (eventTouches(event).some(touch => touchId(touch) === this.lookTouchId)) this.stopLook();
    },

    stopLook() {
      if (this.lookTouchId && this.fallbackRoles['touch:' + this.lookTouchId] === 'look') {
        delete this.fallbackRoles['touch:' + this.lookTouchId];
      }
      this.dragging = false;
      this.lookTouchId = '';
      this.countFallbackTouches();
    },

    previousBlock() {
      this.changeBlock(-1);
    },

    nextBlock() {
      this.changeBlock(1);
    },

    changeBlock(delta: number) {
      const count = this.terrainBlocks.length;
      if (this.worldMode || this.worldBusy || !count) return;
      this.stopLook();
      this.blockIndex = (this.blockIndex + delta + count) % count;
    },

    toggleBlockPicker() {
      const count = this.worldMode ? this.creativeItems.length : this.terrainBlocks.length;
      if (!count || this.worldBusy || this.settingsPanelOpen || this.chunkLoading || this.saveExitPending ||
        (this.worldMode && !this.inventoryPanelOpen)) return;
      if (this.worldMode) this.quiesceWorldInput();
      else this.stopLook();
      this.createPanelOpen = false;
      this.pickerOpen = !this.pickerOpen;
    },

    selectBlock(index: number) {
      const count = this.worldMode ? this.creativeItems.length : this.terrainBlocks.length;
      if (this.worldBusy || this.chunkLoading || this.settingsPanelOpen || this.saveExitPending ||
        !Number.isInteger(index) || index < 0 || index >= count) return;
      if (this.worldMode) {
        if (!this.inventoryPanelOpen) return;
        try {
          if (!this.refreshInventory() || !this.inventoryState) return;
          if (!worldRenderer.setCreativeItem) throw new Error('native setCreativeItem 未接入');
          // The visible list is filtered; native expects the full ItemCatalog ordinal, NOT index/renderIndex.
          const item = this.creativeItems[index];
          this.applyInventory(worldRenderer.setCreativeItem(this.inventoryState.selected, item.itemIndex));
          this.worldDirty = true;
          this.actionMessage = '';
        } catch (e: any) {
          this.actionMessage = '创造选取失败: ' + errorMessage(e);
          return;
        }
      } else this.blockIndex = index;
      this.closeBlockPicker();
    },

    closeBlockPicker() {
      if (this.worldMode) this.quiesceWorldInput();
      else this.stopLook();
      this.pickerOpen = false;
    },

    resetView() {
      if (this.worldMode || this.worldBusy) return;
      this.stopLook();
      this.yaw = 45;
      this.pitch = 30;
    },
  },
});

export default index;
