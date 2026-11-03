// The transparent Vue hit cells must land exactly on the slots native draws.
// Expected values below are mcinventory::inventoryLayout(960, 266) at the 1.25x
// GUI scale; they are hardcoded on purpose, because the native inventory test
// already locks them down, so this file fails if either side drifts.
// Reuses the real-TS/VM mock pattern from tests/ui-streaming-live-test.cjs.
// Run: node tests/ui-inventory-layout-test.cjs (no native/generated files are loaded or written).
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const { createRequire } = require('node:module');
const root = path.resolve(__dirname, '..');
const uiRequire = createRequire(path.join(root, 'ui/package.json'));
const cliRequire = createRequire(uiRequire.resolve('aiot-vue-cli/package.json'));
const ts = uiRequire('typescript');
const compiler = cliRequire('./cli-libs');
const less = cliRequire('less');
const pageDir = path.join(root, 'ui/src/pages/index');
const vueFile = path.join(pageDir, 'index.vue');
const parsed = compiler['vue-template-compiler'].parse(fs.readFileSync(vueFile, 'utf8'));
assert.deepEqual(parsed.errors, []);
const templateSource = parsed.descriptor.template.content;
const template = compiler['falcon-template-compiler'].compile(templateSource, {
  preserveWhitespace: false,
});
assert.deepEqual(template.errors, []);
assert.deepEqual(template.tips, []);
const renderTemplate = new Function(template.render);
const pageSource = fs.readFileSync(path.join(pageDir, 'index.ts'), 'utf8');
const compiledModules = new Map();
const SURFACE_WIDTH = 960;
const SURFACE_HEIGHT = 266;
let passed = 0;
function test(name, run) {
  run();
  passed += 1;
  console.log('PASS ' + name);
}

// --- Native inventoryLayout(960, 266) expectations, scale 1.25 --------------
const NATIVE_SCALE = 1.25;
const NATIVE_PANEL = [370, 29, 220, 208];
const NATIVE_HOTBAR = [366, 238, 228, 28];
// tileHitSlots, indexed by inventory id (0..8 panel hotbar row, 9..35 main grid).
const NATIVE_TILE_HITS = [
  [379, 205, 22, 23], [401, 205, 23, 23], [424, 205, 22, 23], [446, 205, 23, 23], [469, 205, 22, 23],
  [491, 205, 23, 23], [514, 205, 22, 23], [536, 205, 23, 23], [559, 205, 22, 23],
  [379, 133, 22, 22], [401, 133, 23, 22], [424, 133, 22, 22], [446, 133, 23, 22], [469, 133, 22, 22],
  [491, 133, 23, 22], [514, 133, 22, 22], [536, 133, 23, 22], [559, 133, 22, 22],
  [379, 155, 22, 23], [401, 155, 23, 23], [424, 155, 22, 23], [446, 155, 23, 23], [469, 155, 22, 23],
  [491, 155, 23, 23], [514, 155, 22, 23], [536, 155, 23, 23], [559, 155, 22, 23],
  [379, 178, 22, 22], [401, 178, 23, 22], [424, 178, 22, 22], [446, 178, 23, 22], [469, 178, 22, 22],
  [491, 178, 23, 22], [514, 178, 22, 22], [536, 178, 23, 22], [559, 178, 22, 22],
];
// tileSlots: the 16x16 source icons native actually paints inside those cells.
const NATIVE_TILE_SLOTS = [
  [380, 207, 20, 20], [403, 207, 20, 20], [425, 207, 20, 20], [448, 207, 20, 20], [470, 207, 20, 20],
  [493, 207, 20, 20], [515, 207, 20, 20], [538, 207, 20, 20], [560, 207, 20, 20],
  [380, 134, 20, 20], [403, 134, 20, 20], [425, 134, 20, 20], [448, 134, 20, 20], [470, 134, 20, 20],
  [493, 134, 20, 20], [515, 134, 20, 20], [538, 134, 20, 20], [560, 134, 20, 20],
  [380, 157, 20, 20], [403, 157, 20, 20], [425, 157, 20, 20], [448, 157, 20, 20], [470, 157, 20, 20],
  [493, 157, 20, 20], [515, 157, 20, 20], [538, 157, 20, 20], [560, 157, 20, 20],
  [380, 179, 20, 20], [403, 179, 20, 20], [425, 179, 20, 20], [448, 179, 20, 20], [470, 179, 20, 20],
  [493, 179, 20, 20], [515, 179, 20, 20], [538, 179, 20, 20], [560, 179, 20, 20],
];
const NATIVE_HOTBAR_SLOTS = [
  [367, 238, 25, 28], [392, 238, 25, 28], [417, 238, 25, 28], [442, 238, 25, 28], [467, 238, 25, 28],
  [492, 238, 25, 28], [517, 238, 25, 28], [542, 238, 25, 28], [567, 238, 25, 28],
];
const NATIVE_HOTBAR_SELECTIONS = [
  [365, 237, 30, 27], [390, 237, 30, 27], [415, 237, 30, 27], [440, 237, 30, 27], [465, 237, 30, 27],
  [490, 237, 30, 27], [515, 237, 30, 27], [540, 237, 30, 27], [565, 237, 30, 27],
];
const NATIVE_ARMOR_SLOTS = [[380, 39, 20, 20], [380, 62, 20, 20], [380, 84, 20, 20], [380, 107, 20, 20]];

function rect(value) { return [value.x, value.y, value.width, value.height]; }
function inside(box) {
  return box[0] >= 0 && box[1] >= 0 &&
    box[0] + box[2] <= SURFACE_WIDTH && box[1] + box[3] <= SURFACE_HEIGHT;
}
function overlaps(a, b) {
  return a[0] < b[0] + b[2] && b[0] < a[0] + a[2] && a[1] < b[1] + b[3] && b[1] < a[1] + a[3];
}

function make() {
  let now = 1000;
  let nextTimer = 1;
  let touches = [];
  const timers = new Map();
  const calls = [];
  const inventory = {
    selected: 0, emptyHand: false,
    slots: Array.from({ length: 36 }, (_, slot) => ({
      key: slot === 0 ? 'item:1:0' : '', count: slot === 0 ? 3 : 0,
    })),
  };
  const stream = { radius: 1, loaded: 9, queued: 0, target: 9, totalGenerated: 9 };
  const info = () => ({
    player: { x: 0, y: 65, z: 8 }, size: [5120, 256, 5120], stream: { ...stream },
    scene: { ticks: 100, phase: 0.2, daylight: 1, mobs: 2 },
  });
  const native = {
    render() { calls.push(['preview']); return true; },
    renderWorld(...args) { calls.push(['frame', ...args]); return true; },
    chunkLoadStatus() { return JSON.stringify({ busy: false, loading: true, error: '', stream }); },
    worldInfo() { calls.push(['info']); return JSON.stringify(info()); },
    targetInfo() { calls.push(['target']); return '{}'; },
    mineWorld() { calls.push(['mine']); return true; },
    attackWorld() {
      calls.push(['attack']);
      return JSON.stringify({ hit: false, damaged: false, killed: false, killCount: 0 });
    },
    placeWorld() { calls.push(['place']); return true; },
    inventoryInfo() { calls.push(['inventory']); return JSON.stringify(inventory); },
    inventoryOpen(open) { calls.push(['inventoryOpen', open]); return true; },
    selectInventory(slot) { calls.push(['select', slot]); inventory.selected = slot; return JSON.stringify(inventory); },
    inventoryTransfer(...args) { calls.push(['transfer', ...args]); return JSON.stringify(inventory); },
    setCreativeItem(...args) { calls.push(['creative', ...args]); return JSON.stringify(inventory); },
    setRenderRadius(radius) { calls.push(['radius', radius]); return true; },
    setChunkRanges(displayRadius, preloadRadius) {
      calls.push(['ranges', displayRadius, preloadRadius]);
      Object.assign(stream, { radius: displayRadius, preloadRadius,
        displayTarget: (displayRadius * 2 + 1) ** 2, displayLoaded: 9, target: (preloadRadius * 2 + 1) ** 2 });
      return true;
    },
    saveWorld() { calls.push(['save']); return true; },
    beginTouch() { calls.push(['beginTouch']); return true; },
    pollTouch() { calls.push(['touch']); return JSON.stringify({ available: true, touches }); },
    stopTouch() { calls.push(['stopTouch']); return true; },
    beginCreateWorld(...args) { calls.push(['beginCreate', ...args]); return true; },
    beginOpenWorld(...args) { calls.push(['beginOpen', ...args]); return true; },
    worldJobStatus() {
      return JSON.stringify({ state: 'running', stage: 'generate', progress: 10, loadedChunks: 1, totalChunks: 9 });
    },
    cancelWorldJob() { calls.push(['cancel']); return true; },
    finishWorldJob() { return JSON.stringify({ ...info(), path: '/tmp/mc/live', seed: 42 }); },
  };
  const context = vm.createContext({
    console: { error() {} }, Date, performance: { now: () => now },
    setTimeout(fn, delay) {
      const id = nextTimer++;
      timers.set(id, { fn, delay, due: now + delay });
      return id;
    },
    clearTimeout(id) { timers.delete(id); },
  });
  const modules = new Map();
  function load(filename) {
    if (modules.has(filename)) return modules.get(filename).exports;
    if (!compiledModules.has(filename)) {
      const result = ts.transpileModule(fs.readFileSync(filename, 'utf8'), {
        fileName: filename, reportDiagnostics: true,
        compilerOptions: { target: ts.ScriptTarget.ES2020, module: ts.ModuleKind.CommonJS, strict: true },
      });
      assert.deepEqual(result.diagnostics, []);
      compiledModules.set(filename,
        new vm.Script('(function(require, module, exports) {' + result.outputText + '\n})', { filename }));
    }
    const module = { exports: {} };
    modules.set(filename, module);
    compiledModules.get(filename).runInContext(context)((name) => {
      if (name === 'vue') return { defineComponent: value => value };
      if (name === 'Minecraft') return { MinecraftRenderer: native };
      // The firmware `global` module does not exist off-device. Returning an
      // empty binding keeps utils/keyboard.ts on its documented safe-degrade
      // path, so these four regressions still cover the no-keyboard host.
      if (name === 'global') return { default: undefined };
      assert.ok(/^\.\.?\//.test(name), 'Unexpected import ' + name);
      return load(path.resolve(path.dirname(filename), name + '.ts'));
    }, module, module.exports);
    return module.exports;
  }
  const exported = load(path.join(pageDir, 'index.ts'));
  const def = exported.default;
  const page = def.data();
  for (const [key, fn] of Object.entries(def.methods)) page[key] = fn.bind(page);
  for (const [key, fn] of Object.entries(def.computed)) Object.defineProperty(page, key, { get: fn.bind(page) });
  page.$refs = { sf: { getSurface: () => 1 } };
  page.pageMounted = true;
  page.pageVisible = true;
  function runTimer(id) {
    const timer = timers.get(id);
    assert.ok(timer, 'Scheduled timer exists');
    timers.delete(id);
    now = timer.due;
    timer.fn();
  }
  return {
    page, native, calls, timers, exported,
    enter() {
      page.enterWorld({ ...info(), path: '/tmp/mc/live', seed: 42 }, false);
      page.refreshInventory();
      page.startRenderLoop();
    },
    frame() { assert.notEqual(page.renderTimer, null); runTimer(page.renderTimer); },
    setTouches(value) { touches = value; },
  };
}
function count(h, name) { return h.calls.filter(call => call[0] === name).length; }
function nodes(page) {
  const helper = {
    _c(tag, data, children) {
      if (Array.isArray(data)) { children = data; data = {}; }
      return { tag, data: data || {}, children: (children || []).flat(Infinity) };
    },
    _s: value => String(value), _v: text => ({ text }), _e: () => null,
    _l: (values, fn) => values.map(fn),
  };
  const all = [];
  function visit(node) {
    if (!node) return;
    all.push(node);
    for (const child of node.children || []) visit(child);
  }
  visit(renderTemplate.call(new Proxy(page, {
    has: (target, key) => key in helper || key in target,
    get: (target, key) => key in helper ? helper[key] : target[key],
  })));
  return all;
}
function withClass(page, name) {
  return nodes(page).filter(node => (node.data?.staticClass || []).includes(name));
}
function nodeText(node) { return node.text || (node.children || []).map(nodeText).join(''); }
function allText(page) { return nodes(page).map(node => node.text || '').join('|'); }
// Resolve a rendered hit cell to absolute surface coordinates using the layer
// box from Less, so a wrong layer origin cannot be hidden by a right offset.
function px(style, key) {
  const value = style[key];
  assert.match(String(value), /^-?\d+px$/, key + ' is an integer px value');
  return Number(String(value).slice(0, -2));
}
function renderedCells(page, layer, name) {
  return withClass(page, name).map(node => [
    layer[0] + px(node.data.style, 'left'), layer[1] + px(node.data.style, 'top'),
    px(node.data.style, 'width'), px(node.data.style, 'height'),
  ]);
}

test('the mirrored layout reproduces native inventoryLayout(960,266) exactly', () => {
  const h = make();
  const { guiLayout, GUI_LAYOUT } = h.exported;
  assert.equal(typeof guiLayout, 'function', 'The layout is derived, not written down');
  assert.equal(GUI_LAYOUT.scale, NATIVE_SCALE);
  assert.deepEqual(rect(GUI_LAYOUT.panel), NATIVE_PANEL);
  assert.deepEqual(rect(GUI_LAYOUT.hotbar), NATIVE_HOTBAR);
  // 36 main slots, 9 hotbar cells and 4 armour slots, one assertion each.
  for (let slot = 0; slot < 36; slot += 1) {
    assert.deepEqual(rect(GUI_LAYOUT.tileHitSlots[slot]), NATIVE_TILE_HITS[slot], 'tileHitSlots[' + slot + ']');
    assert.deepEqual(rect(GUI_LAYOUT.tileSlots[slot]), NATIVE_TILE_SLOTS[slot], 'tileSlots[' + slot + ']');
  }
  for (let slot = 0; slot < 9; slot += 1) {
    assert.deepEqual(rect(GUI_LAYOUT.hotbarSlots[slot]), NATIVE_HOTBAR_SLOTS[slot], 'hotbarSlots[' + slot + ']');
    assert.deepEqual(rect(GUI_LAYOUT.hotbarSelections[slot]), NATIVE_HOTBAR_SELECTIONS[slot],
      'hotbarSelections[' + slot + ']');
  }
  for (let slot = 0; slot < 4; slot += 1) {
    assert.deepEqual(rect(GUI_LAYOUT.armorSlots[slot]), NATIVE_ARMOR_SLOTS[slot], 'armorSlots[' + slot + ']');
  }
  // Scale is derived from the viewport, not hardcoded: a small viewport shrinks
  // below 1x and a larger one is still capped at the native kMaxGuiScale.
  assert.equal(guiLayout(184, 168).scale, 1);
  assert.ok(guiLayout(92, 84).scale < 1);
  assert.equal(guiLayout(4096, 4096).scale, NATIVE_SCALE);
  // Native returns an all-zero layout for the same degenerate viewports.
  for (const [w, hh] of [[0, 100], [100, -1], [40000, 100]]) {
    assert.equal(guiLayout(w, hh).scale, 0, 'degenerate viewport ' + w + 'x' + hh);
  }
  // Every viewport keeps the panel and hotbar on screen, as the native test asserts.
  for (const [w, hh] of [[1, 1], [100, 80], [183, 167], [184, 168], [320, 240], [961, 267], [960, 266]]) {
    const small = guiLayout(w, hh);
    assert.ok(small.scale > 0 && small.scale <= NATIVE_SCALE);
    assert.ok(small.panel.x >= 0 && small.panel.y >= 0, 'panel origin at ' + w + 'x' + hh);
    assert.ok(small.panel.x + small.panel.width <= w && small.panel.y + small.panel.height <= hh,
      'panel fits ' + w + 'x' + hh);
    assert.ok(small.hotbar.x >= 0 && small.hotbar.x + small.hotbar.width <= w &&
      small.hotbar.y + small.hotbar.height <= hh, 'hotbar fits ' + w + 'x' + hh);
  }
  // Sizes must be the difference of two scaled edges, never a scaled width, so
  // adjacent cells tile exactly. Derive the rule here independently.
  const edge = unit => Math.round(unit * NATIVE_SCALE);
  for (let slot = 0; slot < 9; slot += 1) {
    const left = -1 + slot * 20;
    assert.equal(GUI_LAYOUT.hotbarSelections[slot].width, edge(left + 24) - edge(left));
    assert.equal(GUI_LAYOUT.hotbarSelections[slot].height, edge(-1 + 22) - edge(-1));
    // Panel-relative grid origin, matching the native per-slot assertions.
    assert.equal(GUI_LAYOUT.tileSlots[slot].x, NATIVE_PANEL[0] + edge(8 + slot * 18));
    assert.equal(GUI_LAYOUT.tileSlots[slot].y, NATIVE_PANEL[1] + edge(142));
    assert.equal(GUI_LAYOUT.hotbarSelections[slot].x, NATIVE_HOTBAR[0] + edge(-1 + slot * 20));
  }
  for (let slot = 9; slot < 36; slot += 1) {
    assert.equal(GUI_LAYOUT.tileSlots[slot].x, NATIVE_PANEL[0] + edge(8 + (slot % 9) * 18));
    assert.equal(GUI_LAYOUT.tileSlots[slot].y, NATIVE_PANEL[1] + edge(84 + Math.floor((slot - 9) / 9) * 18));
  }
  // No pre-scaled pixel coordinate is written into the page source.
  for (const stale of ['391 +', '400 +', '134 +', 'hit.x - 39', 'hit.y - 50']) {
    assert.equal(pageSource.includes(stale), false, 'stale 1x coordinate ' + stale);
  }
});

test('every UI hit cell equals the native slot it covers and stays inside 960x266', () => {
  const h = make();
  const { HOTBAR_HITS, INVENTORY_HITS, HOTBAR_HIT_LAYER, INVENTORY_HIT_LAYER } = h.exported;
  assert.equal(INVENTORY_HITS.length, 36);
  assert.equal(HOTBAR_HITS.length, 9);
  // Slot ids are inventory ids in array order, matching native tileHitSlots.
  for (let slot = 0; slot < 36; slot += 1) {
    const hit = INVENTORY_HITS[slot];
    assert.equal(hit.slot, slot, 'INVENTORY_HITS[' + slot + '].slot');
    assert.deepEqual(rect(hit), NATIVE_TILE_HITS[slot], 'UI main slot ' + slot);
    // The cell must cover the icon native paints there.
    const icon = NATIVE_TILE_SLOTS[slot];
    assert.ok(hit.x <= icon[0] && hit.y <= icon[1] &&
      hit.x + hit.width >= icon[0] + icon[2] && hit.y + hit.height >= icon[1] + icon[3],
      'main slot ' + slot + ' covers its icon');
    assert.ok(inside(rect(hit)), 'main slot ' + slot + ' inside the surface');
    // Layer-relative offsets must resolve back to the absolute native slot.
    assert.equal(INVENTORY_HIT_LAYER.x + hit.left, hit.x);
    assert.equal(INVENTORY_HIT_LAYER.y + hit.top, hit.y);
  }
  for (let slot = 0; slot < 9; slot += 1) {
    const hit = HOTBAR_HITS[slot];
    assert.equal(hit.slot, slot, 'HOTBAR_HITS[' + slot + '].slot');
    assert.deepEqual(rect(hit), NATIVE_HOTBAR_SLOTS[slot], 'UI hotbar slot ' + slot);
    assert.ok(inside(rect(hit)), 'hotbar slot ' + slot + ' inside the surface');
    assert.equal(HOTBAR_HIT_LAYER.x + hit.left, hit.x);
    assert.equal(HOTBAR_HIT_LAYER.y + hit.top, hit.y);
  }
  // Both native boxes themselves fit the 960x266 surface.
  assert.ok(inside(NATIVE_PANEL), 'native panel inside the surface');
  assert.ok(inside(NATIVE_HOTBAR), 'native hotbar inside the surface');
  assert.ok(inside(rect(INVENTORY_HIT_LAYER)) && inside(rect(HOTBAR_HIT_LAYER)));
  // No two cells overlap, in either group.
  for (const group of [INVENTORY_HITS, HOTBAR_HITS]) {
    for (let i = 0; i < group.length; i += 1) for (let j = i + 1; j < group.length; j += 1) {
      assert.equal(overlaps(rect(group[i]), rect(group[j])), false, 'cells ' + i + '/' + j + ' are disjoint');
    }
  }
});

function templateCellTest(css) {
  const box = (selector) => {
    const body = new RegExp('\\' + selector + '\\s*\\{([^}]*)\\}').exec(css);
    assert.ok(body, selector + ' exists in Less');
    const read = (key) => {
      const found = new RegExp(key + ':\\s*(-?\\d+)px').exec(body[1]);
      assert.ok(found, selector + ' declares ' + key);
      return Number(found[1]);
    };
    return [read('left'), read('top'), read('width'), read('height')];
  };
  const panelLayer = box('.inventory-panel-hit');
  const hotbarLayer = box('.hotbar-hit-layer');
  assert.deepEqual(panelLayer, NATIVE_PANEL, 'The panel hit layer is the native panel box');
  assert.deepEqual(hotbarLayer, [367, 238, 225, 28], 'The hotbar hit layer spans the native slot cells');
  // Per-slot size comes from the layout at runtime, never from Less.
  for (const selector of ['.inventory-slot-hit', '.hotbar-hit']) {
    const body = new RegExp('\\' + selector + '\\s*\\{([^}]*)\\}').exec(css);
    assert.ok(body, selector + ' exists in Less');
    assert.equal(/width|height|left|top/.test(body[1]), false, selector + ' carries no fixed geometry');
  }
  const h = make();
  h.enter();
  const p = h.page;
  // Hotbar cells are live in world mode whenever input is not blocked.
  assert.deepEqual(renderedCells(p, hotbarLayer, 'hotbar-hit'), NATIVE_HOTBAR_SLOTS);
  p.openInventory();
  assert.equal(p.inventoryPanelOpen, true);
  assert.deepEqual(renderedCells(p, panelLayer, 'inventory-slot-hit'), NATIVE_TILE_HITS);
  // The hotbar layer is suppressed while the panel owns the screen.
  assert.equal(withClass(p, 'hotbar-hit-layer').length, 0);
  assert.equal(p.closeInventory(), true);
  assert.equal(withClass(p, 'hotbar-hit-layer').length, 1);
  // Slot clicks still reach native with the inventory id of the cell.
  p.openInventory();
  const cells = withClass(p, 'inventory-slot-hit');
  assert.equal(cells.length, 36);
  p.clickInventorySlot(9);
  p.clickInventorySlot(0);
  assert.equal(count(h, 'transfer'), 1);
  const transfer = h.calls.find(call => call[0] === 'transfer');
  assert.deepEqual([transfer[1], transfer[2]], [9, 0], 'Native receives the inventory ids');
  assert.equal(p.closeInventory(), true);
  p.selectInventory(8);
  assert.equal(p.inventoryState.selected, 8);
}

test('the enlarged hotbar ignore box matches the native hotbar and selection frames', () => {
  const h = make();
  h.enter();
  const p = h.page;
  const { HOTBAR_IGNORE } = h.exported;
  // Native draws hotbar (366,238) 228x28 plus 30x27 selection frames from x=365.
  assert.deepEqual(rect(HOTBAR_IGNORE), [365, 237, 230, 29]);
  assert.ok(inside(rect(HOTBAR_IGNORE)), 'The ignore box stays inside the surface');
  const roleAt = (x, y) => {
    h.setTouches([{ id: 1, x, y }]);
    h.frame();
    const role = p.worldContacts['native:1'].role;
    h.setTouches([]);
    h.frame();
    return role;
  };
  // Everything native paints is ignored, including the widened left/right ends
  // that the former 1x box at (388..572, 244..266) did not cover.
  for (const [x, y] of [[365, 237], [366, 240], [390, 250], [480, 265], [594, 240], [592, 265]]) {
    assert.equal(roleAt(x, y), 'ignore', 'hotbar ignore at ' + x + ',' + y);
  }
  // Just outside it is an ordinary look area, not a dead zone.
  assert.equal(roleAt(360, 250), 'look');
  assert.equal(roleAt(480, 236), 'look');
  // The left-mouse strip starts at x=600 and still wins over the hotbar box.
  assert.equal(roleAt(600, 250), 'primary');
});

test('save/exit lives only in the settings panel and the top buttons survive the panel', () => {
  const h = make();
  h.enter();
  const p = h.page;
  // World mode has no save/exit control of its own any more.
  assert.equal(withClass(p, 'save-button').length, 0);
  assert.equal(templateSource.includes('save-button'), false);
  assert.equal(allText(p).includes('保存退出'), false, 'No world-mode save/exit label');
  assert.equal(withClass(p, 'settings-save-exit').length, 0, 'Not present while settings are closed');
  // It appears inside the settings panel and calls the unchanged action.
  p.openWorldSettings();
  const button = withClass(p, 'settings-save-exit');
  assert.equal(button.length, 1);
  assert.equal(nodeText(button[0]), '保存退出');
  assert.equal(templateSource.includes('saveAndExit'), true, 'The settings button wires saveAndExit');
  const chips = withClass(p, 'chunk-chip');
  assert.equal(chips.length, 24, 'Both chip rows coexist with save/exit');
  chips[6].data.on.click();
  withClass(p, 'chunk-chip')[23].data.on.click();
  withClass(p, 'settings-apply')[0].data.on.click();
  assert.deepEqual(h.calls.filter(call => call[0] === 'ranges'), [['ranges', 7, 12]]);
  assert.equal(count(h, 'radius'), 0);
  assert.equal(withClass(p, 'hotbar-hit-layer').length, 0, 'Settings owns input, not native GUI pixels');
  // The 1.25x panel reaches x=590, so both top-right buttons hide while it is
  // open and come back afterwards; the settings button is then still clickable.
  assert.equal(withClass(p, 'settings-button').length, 1);
  p.closeWorldSettings();
  p.openInventory();
  assert.equal(withClass(p, 'settings-button').length, 0);
  assert.equal(withClass(p, 'bag-button').length, 0);
  assert.ok(overlaps(NATIVE_PANEL, [540, 6, 80, 40]), 'The panel really reaches the settings button');
  assert.equal(p.closeInventory(), true);
  assert.equal(withClass(p, 'settings-button').length, 1);
  assert.equal(withClass(p, 'bag-button').length, 1);
  p.openWorldSettings();
  assert.equal(p.settingsPanelOpen, true, 'The settings button still works after a bag round trip');
  assert.equal(nodeText(withClass(p, 'settings-save-exit')[0]), '保存退出');
  p.saveAndExit();
  assert.equal(count(h, 'save'), 1);
  assert.equal(p.worldMode, false);
});

(async () => {
  const css = (await less.render(fs.readFileSync(path.join(pageDir, 'index.less'), 'utf8'), {
    filename: path.join(pageDir, 'index.less'),
  })).css;
  // The side columns must clear the panel's right edge at x=590.
  for (const selector of ['.inventory-tools', '.inventory-status']) {
    const left = new RegExp('\\' + selector + '\\s*\\{[^}]*left:\\s*(\\d+)px').exec(css);
    assert.ok(left, selector + ' declares left');
    assert.ok(Number(left[1]) >= NATIVE_PANEL[0] + NATIVE_PANEL[2],
      selector + ' at x=' + left[1] + ' overlaps the 1.25x panel');
  }
  assert.equal(css.includes('.save-button'), false, '.save-button removed from Less');
  assert.match(css, /\.settings-save-exit\s*\{/);
  // Needs the compiled Less, so it runs after the synchronous checks above.
  test('the rendered template places its cells on the native slots, via the Less layer boxes',
    () => templateCellTest(css));
  console.log('RESULT ' + passed + ' UI inventory layout checks passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
