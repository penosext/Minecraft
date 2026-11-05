// Container panels (2x2 grid, workbench, chest, furnace): the mirrored layout
// must equal native mcinventory::containerLayout(960, 266, kind), and taps must
// reach the native container API with the right slots. Values below are
// hardcoded from native on purpose, like ui-inventory-layout-test.cjs.
// (Original header follows.)
//
// Expected values below are mcinventory::inventoryLayout(960, 266) at the 1.5x
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

// --- Native inventoryLayout(960, 266) expectations, scale 1.5 ---------------
const NATIVE_SCALE = 1.5;
const NATIVE_PANEL = [348, 8, 264, 249];
const NATIVE_HOTBAR = [343, 233, 273, 33];
// tileHitSlots, indexed by inventory id (0..8 panel hotbar row, 9..35 main grid).
const NATIVE_TILE_HITS = [
  [359, 220, 27, 27], [386, 220, 27, 27], [413, 220, 27, 27], [440, 220, 27, 27], [467, 220, 27, 27],
  [494, 220, 27, 27], [521, 220, 27, 27], [548, 220, 27, 27], [575, 220, 27, 27],
  [359, 133, 27, 27], [386, 133, 27, 27], [413, 133, 27, 27], [440, 133, 27, 27], [467, 133, 27, 27],
  [494, 133, 27, 27], [521, 133, 27, 27], [548, 133, 27, 27], [575, 133, 27, 27],
  [359, 160, 27, 27], [386, 160, 27, 27], [413, 160, 27, 27], [440, 160, 27, 27], [467, 160, 27, 27],
  [494, 160, 27, 27], [521, 160, 27, 27], [548, 160, 27, 27], [575, 160, 27, 27],
  [359, 187, 27, 27], [386, 187, 27, 27], [413, 187, 27, 27], [440, 187, 27, 27], [467, 187, 27, 27],
  [494, 187, 27, 27], [521, 187, 27, 27], [548, 187, 27, 27], [575, 187, 27, 27],
];
// tileSlots: the 16x16 source icons native actually paints inside those cells.
const NATIVE_TILE_SLOTS = [
  [360, 221, 24, 24], [387, 221, 24, 24], [414, 221, 24, 24], [441, 221, 24, 24], [468, 221, 24, 24],
  [495, 221, 24, 24], [522, 221, 24, 24], [549, 221, 24, 24], [576, 221, 24, 24],
  [360, 134, 24, 24], [387, 134, 24, 24], [414, 134, 24, 24], [441, 134, 24, 24], [468, 134, 24, 24],
  [495, 134, 24, 24], [522, 134, 24, 24], [549, 134, 24, 24], [576, 134, 24, 24],
  [360, 161, 24, 24], [387, 161, 24, 24], [414, 161, 24, 24], [441, 161, 24, 24], [468, 161, 24, 24],
  [495, 161, 24, 24], [522, 161, 24, 24], [549, 161, 24, 24], [576, 161, 24, 24],
  [360, 188, 24, 24], [387, 188, 24, 24], [414, 188, 24, 24], [441, 188, 24, 24], [468, 188, 24, 24],
  [495, 188, 24, 24], [522, 188, 24, 24], [549, 188, 24, 24], [576, 188, 24, 24],
];
const NATIVE_HOTBAR_SLOTS = [
  [345, 233, 30, 33], [375, 233, 30, 33], [405, 233, 30, 33], [435, 233, 30, 33], [465, 233, 30, 33],
  [495, 233, 30, 33], [525, 233, 30, 33], [555, 233, 30, 33], [585, 233, 30, 33],
];
const NATIVE_HOTBAR_SELECTIONS = [
  [341, 231, 37, 34], [372, 231, 36, 34], [402, 231, 36, 34], [432, 231, 36, 34], [462, 231, 36, 34],
  [492, 231, 36, 34], [522, 231, 36, 34], [552, 231, 36, 34], [582, 231, 36, 34],
];
const NATIVE_ARMOR_SLOTS = [[360, 20, 24, 24], [360, 47, 24, 24], [360, 74, 24, 24], [360, 101, 24, 24]];

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
  // Native container mock: type '' is closed; onUse is what a right click opens.
  const panel = { type: '', onUse: '', recipe: null };
  const SIZES = { inventory: 4, workbench: 9, chest: 27, furnace: 3 };
  const panelInfo = () => {
    if (!panel.type) return { open: false };
    return { open: true, type: panel.type, x: 1, y: 2, z: 3, recipe: panel.recipe, inventory,
      slots: Array.from({ length: SIZES[panel.type] }, () => ({ key: '', count: 0, damage: 0 })) };
  };
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
    placeWorld() { calls.push(['place']); if (panel.onUse) panel.type = panel.onUse; return true; },
    inventoryInfo() { calls.push(['inventory']); return JSON.stringify(inventory); },
    inventoryOpen(open) { calls.push(['inventoryOpen', open]); panel.type = open ? 'inventory' : ''; return true; },
    selectInventory(slot) { calls.push(['select', slot]); inventory.selected = slot; return JSON.stringify(inventory); },
    inventoryTransfer(...args) { calls.push(['transfer', ...args]); return JSON.stringify(inventory); },
    containerInfo() { calls.push(['containerInfo']); return JSON.stringify(panelInfo()); },
    closeContainer() { calls.push(['closeContainer']); panel.type = ''; return true; },
    containerTransfer(...args) { calls.push(['containerTransfer', ...args]); return JSON.stringify({ ok: true, ...panelInfo() }); },
    craftContainer() { calls.push(['craft']); return JSON.stringify({ ok: !!panel.recipe, status: panel.recipe ? 'ok' : 'no-recipe', ...panelInfo() }); },
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
    page, native, calls, timers, exported, panel, load: name => load(path.join(pageDir, name + '.ts')),
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


// --- Native containerLayout(960, 266, kind) at scale 1.5 --------------------
const NATIVE = {
inventory: { panel: [348, 8, 264, 249],
  slots: [[479, 46, 27, 27], [506, 46, 27, 27], [479, 73, 27, 27], [506, 73, 27, 27]],
  result: [563, 61, 27, 27],
  inventory: [[359, 220, 27, 27], [386, 220, 27, 27], [413, 220, 27, 27], [440, 220, 27, 27], [467, 220, 27, 27], [494, 220, 27, 27], [521, 220, 27, 27], [548, 220, 27, 27], [575, 220, 27, 27], [359, 133, 27, 27], [386, 133, 27, 27], [413, 133, 27, 27], [440, 133, 27, 27], [467, 133, 27, 27], [494, 133, 27, 27], [521, 133, 27, 27], [548, 133, 27, 27], [575, 133, 27, 27], [359, 160, 27, 27], [386, 160, 27, 27], [413, 160, 27, 27], [440, 160, 27, 27], [467, 160, 27, 27], [494, 160, 27, 27], [521, 160, 27, 27], [548, 160, 27, 27], [575, 160, 27, 27], [359, 187, 27, 27], [386, 187, 27, 27], [413, 187, 27, 27], [440, 187, 27, 27], [467, 187, 27, 27], [494, 187, 27, 27], [521, 187, 27, 27], [548, 187, 27, 27], [575, 187, 27, 27]] },
workbench: { panel: [348, 8, 264, 249],
  slots: [[392, 32, 27, 27], [419, 32, 27, 27], [446, 32, 27, 27], [392, 59, 27, 27], [419, 59, 27, 27], [446, 59, 27, 27], [392, 86, 27, 27], [419, 86, 27, 27], [446, 86, 27, 27]],
  result: [533, 59, 27, 27],
  inventory: [[359, 220, 27, 27], [386, 220, 27, 27], [413, 220, 27, 27], [440, 220, 27, 27], [467, 220, 27, 27], [494, 220, 27, 27], [521, 220, 27, 27], [548, 220, 27, 27], [575, 220, 27, 27], [359, 133, 27, 27], [386, 133, 27, 27], [413, 133, 27, 27], [440, 133, 27, 27], [467, 133, 27, 27], [494, 133, 27, 27], [521, 133, 27, 27], [548, 133, 27, 27], [575, 133, 27, 27], [359, 160, 27, 27], [386, 160, 27, 27], [413, 160, 27, 27], [440, 160, 27, 27], [467, 160, 27, 27], [494, 160, 27, 27], [521, 160, 27, 27], [548, 160, 27, 27], [575, 160, 27, 27], [359, 187, 27, 27], [386, 187, 27, 27], [413, 187, 27, 27], [440, 187, 27, 27], [467, 187, 27, 27], [494, 187, 27, 27], [521, 187, 27, 27], [548, 187, 27, 27], [575, 187, 27, 27]] },
chest: { panel: [348, 7, 264, 252],
  slots: [[359, 33, 27, 27], [386, 33, 27, 27], [413, 33, 27, 27], [440, 33, 27, 27], [467, 33, 27, 27], [494, 33, 27, 27], [521, 33, 27, 27], [548, 33, 27, 27], [575, 33, 27, 27], [359, 60, 27, 27], [386, 60, 27, 27], [413, 60, 27, 27], [440, 60, 27, 27], [467, 60, 27, 27], [494, 60, 27, 27], [521, 60, 27, 27], [548, 60, 27, 27], [575, 60, 27, 27], [359, 87, 27, 27], [386, 87, 27, 27], [413, 87, 27, 27], [440, 87, 27, 27], [467, 87, 27, 27], [494, 87, 27, 27], [521, 87, 27, 27], [548, 87, 27, 27], [575, 87, 27, 27]],
  result: null,
  inventory: [[359, 220, 27, 27], [386, 220, 27, 27], [413, 220, 27, 27], [440, 220, 27, 27], [467, 220, 27, 27], [494, 220, 27, 27], [521, 220, 27, 27], [548, 220, 27, 27], [575, 220, 27, 27], [359, 133, 27, 27], [386, 133, 27, 27], [413, 133, 27, 27], [440, 133, 27, 27], [467, 133, 27, 27], [494, 133, 27, 27], [521, 133, 27, 27], [548, 133, 27, 27], [575, 133, 27, 27], [359, 160, 27, 27], [386, 160, 27, 27], [413, 160, 27, 27], [440, 160, 27, 27], [467, 160, 27, 27], [494, 160, 27, 27], [521, 160, 27, 27], [548, 160, 27, 27], [575, 160, 27, 27], [359, 187, 27, 27], [386, 187, 27, 27], [413, 187, 27, 27], [440, 187, 27, 27], [467, 187, 27, 27], [494, 187, 27, 27], [521, 187, 27, 27], [548, 187, 27, 27], [575, 187, 27, 27]] },
furnace: { panel: [348, 8, 264, 249],
  slots: [[431, 32, 27, 27], [431, 86, 27, 27], [521, 59, 27, 27]],
  result: null,
  inventory: [[359, 220, 27, 27], [386, 220, 27, 27], [413, 220, 27, 27], [440, 220, 27, 27], [467, 220, 27, 27], [494, 220, 27, 27], [521, 220, 27, 27], [548, 220, 27, 27], [575, 220, 27, 27], [359, 133, 27, 27], [386, 133, 27, 27], [413, 133, 27, 27], [440, 133, 27, 27], [467, 133, 27, 27], [494, 133, 27, 27], [521, 133, 27, 27], [548, 133, 27, 27], [575, 133, 27, 27], [359, 160, 27, 27], [386, 160, 27, 27], [413, 160, 27, 27], [440, 160, 27, 27], [467, 160, 27, 27], [494, 160, 27, 27], [521, 160, 27, 27], [548, 160, 27, 27], [575, 160, 27, 27], [359, 187, 27, 27], [386, 187, 27, 27], [413, 187, 27, 27], [440, 187, 27, 27], [467, 187, 27, 27], [494, 187, 27, 27], [521, 187, 27, 27], [548, 187, 27, 27], [575, 187, 27, 27]] },
};

test('container-panel mirrors native containerLayout for every screen', () => {
  const h = make();
  const { containerLayout } = h.load('container-panel');
  for (const kind of Object.keys(NATIVE)) {
    const expected = NATIVE[kind];
    const layout = containerLayout(SURFACE_WIDTH, SURFACE_HEIGHT, kind);
    assert.equal(layout.scale, NATIVE_SCALE);
    assert.deepEqual(rect(layout.panel), expected.panel, kind + ' panel');
    // Values come from the page's vm realm; normalize before deepStrictEqual.
    const of = side => JSON.parse(JSON.stringify(layout.hits.filter(hit => hit.side === side).map(rect)));
    assert.deepEqual(of('inventory'), expected.inventory, kind + ' inventory rows');
    assert.deepEqual(of('container'), expected.slots, kind + ' panel slots');
    assert.deepEqual(of('result'), expected.result ? [expected.result] : [], kind + ' result');
    for (const hit of layout.hits) {
      assert.ok(inside(rect(hit)), kind + ' cell inside the surface');
      assert.equal(layout.panel.x + hit.left, hit.x);
      assert.equal(layout.panel.y + hit.top, hit.y);
      const r = rect(hit), p = expected.panel;
      assert.ok(r[0] >= p[0] && r[1] >= p[1] && r[0] + r[2] <= p[0] + p[2] && r[1] + r[3] <= p[1] + p[3],
        kind + ' cell inside its panel');
    }
    for (let i = 0; i < layout.hits.length; i += 1) for (let j = i + 1; j < layout.hits.length; j += 1) {
      assert.equal(overlaps(rect(layout.hits[i]), rect(layout.hits[j])), false, kind + ' cells ' + i + '/' + j);
    }
    // The 2x2 screen keeps exactly the plain inventory panel and rows.
    if (kind === 'inventory') {
      assert.deepEqual(expected.panel, NATIVE_PANEL);
      assert.deepEqual(expected.inventory, NATIVE_TILE_HITS);
    }
  }
});

test('planTap: select, grid painting, quick move, craft', () => {
  const h = make();
  const raw = h.load('container-panel').planTap;
  // Results come from the page's vm realm; normalize before deepStrictEqual.
  const planTap = (...args) => JSON.parse(JSON.stringify(raw(...args)));
  const inv = slot => ({ side: 'inventory', slot });
  const box = slot => ({ side: 'container', slot });
  assert.deepEqual(planTap('chest', null, inv(3), true), { kind: 'select', pick: inv(3) });
  assert.deepEqual(planTap('chest', inv(3), inv(3), true), { kind: 'select', pick: null });
  assert.deepEqual(planTap('chest', inv(3), inv(4), true), { kind: 'inventoryMove', from: 3, to: 4 });
  assert.deepEqual(planTap('chest', inv(3), box(5), true),
    { kind: 'containerMove', inventorySlot: 3, containerSlot: 5, count: 64, toContainer: true, keep: false });
  assert.deepEqual(planTap('workbench', inv(3), box(5), true),
    { kind: 'containerMove', inventorySlot: 3, containerSlot: 5, count: 1, toContainer: true, keep: true });
  assert.equal(planTap('workbench', inv(3), box(5), false).count, 64);
  assert.deepEqual(planTap('furnace', box(2), inv(7), true),
    { kind: 'containerMove', inventorySlot: 7, containerSlot: 2, count: 64, toContainer: false, keep: false });
  assert.deepEqual(planTap('furnace', box(2), box(2), true), { kind: 'quickMove', containerSlot: 2 });
  assert.deepEqual(planTap('chest', box(1), box(2), true), { kind: 'select', pick: box(2) });
  assert.deepEqual(planTap('inventory', inv(1), { side: 'result', slot: 0 }, true), { kind: 'craft' });
});

function containerFlowTest(css) {
  const h = make();
  h.enter();
  const p = h.page;
  // Right click on a chest: native Tile::use opens it, the UI shows the panel.
  h.panel.onUse = 'chest';
  p.secondaryHeld = true; p.nextSecondaryAt = 0;
  p.tickSecondary(1e12);
  assert.equal(p.containerPanelOpen, true);
  assert.equal(p.worldInputBlocked, true);
  assert.equal(p.secondaryHeld, false, 'The open panel releases the right button');
  const layer = withClass(p, 'container-panel-hit');
  assert.equal(layer.length, 1);
  const style = layer[0].data.style;
  const origin = [px(style, 'left'), px(style, 'top'), px(style, 'width'), px(style, 'height')];
  assert.deepEqual(origin, NATIVE.chest.panel);
  const cells = renderedCells(p, origin, 'container-slot-hit');
  assert.deepEqual(cells, [...NATIVE.chest.inventory, ...NATIVE.chest.slots]);
  // Top buttons, hotbar layer and HUD hide while the panel owns the screen.
  for (const name of ['settings-button', 'bag-button', 'chat-button', 'hotbar-hit-layer']) {
    assert.equal(withClass(p, name).length, 0, name + ' hidden');
  }
  // Side column clears the panel edge.
  const tools = new RegExp('\\.container-tools\\s*\\{[^}]*left:\\s*(\\d+)px').exec(css);
  assert.ok(tools && Number(tools[1]) >= NATIVE.chest.panel[0] + NATIVE.chest.panel[2]);
  // Tap inventory 9, then chest slot 4 -> one native transfer with the whole stack.
  const hits = p.containerHits;
  p.clickPanelSlot(hits.find(hit => hit.side === 'inventory' && hit.slot === 9));
  p.clickPanelSlot(hits.find(hit => hit.side === 'container' && hit.slot === 4));
  const move = h.calls.find(call => call[0] === 'containerTransfer');
  assert.deepEqual(move.slice(1), [9, 4, 64, true]);
  // Double tap on a chest slot = quick move (-1 sentinel).
  p.clickPanelSlot(hits.find(hit => hit.side === 'container' && hit.slot === 4));
  p.clickPanelSlot(hits.find(hit => hit.side === 'container' && hit.slot === 4));
  assert.deepEqual(h.calls.filter(call => call[0] === 'containerTransfer')[1].slice(1), [-1, 4, 64, false]);
  // Close returns control to the world.
  assert.equal(p.closeContainerPanel(), true);
  assert.equal(count(h, 'closeContainer'), 1);
  assert.equal(p.containerPanelOpen, false);
  assert.equal(withClass(p, 'bag-button').length, 1);

  // 2x2 grid in the bag screen: grid cells are separate from the 36 slot cells.
  h.panel.onUse = '';
  p.openInventory();
  assert.equal(withClass(p, 'inventory-slot-hit').length, 36);
  const grid = renderedCells(p, NATIVE_PANEL, 'inventory-grid-hit');
  assert.deepEqual(grid, [...NATIVE.inventory.slots, NATIVE.inventory.result]);
  // Painting: inventory slot 0 then two grid cells -> two one-item moves.
  p.clickInventorySlot(0);
  const gridHits = p.inventoryGridHits;
  p.clickPanelSlot(gridHits[0]);
  p.clickPanelSlot(gridHits[2]);
  const moves = h.calls.filter(call => call[0] === 'containerTransfer').slice(2);
  assert.deepEqual(moves.map(call => call.slice(1)), [[0, 0, 1, true], [0, 2, 1, true]]);
  // Result cell crafts; a no-recipe answer is reported, not hidden.
  p.clickPanelSlot(gridHits[4]);
  assert.equal(count(h, 'craft'), 1);
  assert.equal(p.actionMessage, '没有匹配的配方');
  h.panel.recipe = { key: 'item:5:0', count: 4, name: '橡木木板' };
  p.clickPanelSlot(gridHits[4]);
  assert.equal(p.actionMessage, '');
  assert.equal(p.craftRecipeName, '橡木木板 ×4');
  // The painted source stays selected (a third tap paints again) until it is tapped again.
  assert.equal(p.inventoryFrom, 0);
  p.clickInventorySlot(0);
  assert.equal(p.inventoryFrom, -1);
  // Grid -> inventory: select grid cell, tap an inventory cell.
  p.clickPanelSlot(gridHits[1]);
  p.clickInventorySlot(20);
  assert.deepEqual(h.calls.filter(call => call[0] === 'containerTransfer').pop().slice(1), [20, 1, 64, false]);
  assert.equal(p.closeInventory(), true);
  assert.deepEqual(h.calls.filter(call => call[0] === 'inventoryOpen').map(call => call[1]), [true, false]);
}

(async () => {
  const css = (await less.render(fs.readFileSync(path.join(pageDir, 'index.less'), 'utf8'), {
    filename: path.join(pageDir, 'index.less'),
  })).css;
  test('a right-click opened chest and the 2x2 grid route taps to the native container API',
    () => containerFlowTest(css));
  console.log('RESULT ' + passed + ' UI container layout checks passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
