// Reuses the real-TS/VM mock pattern from the existing UI job regressions.
// Run: node tests/ui-streaming-live-test.cjs (no native/generated files are loaded or written).
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
const template = compiler['falcon-template-compiler'].compile(parsed.descriptor.template.content, {
  preserveWhitespace: false,
});
assert.deepEqual(template.errors, []);
assert.deepEqual(template.tips, []);
const renderTemplate = new Function(template.render);
const compiledModules = new Map();
let passed = 0;
function test(name, run) {
  run();
  passed += 1;
  console.log('PASS ' + name);
}

function make() {
  let now = 1000;
  let nextTimer = 1;
  let exclusive = false;
  let loading = true;
  let chunkError = '';
  let statusFailure = null;
  let inventoryFailure = false;
  let saveResult = true;
  let touches = [];
  let ticks = 100;
  let x = 0;
  let job = { state: 'running', stage: 'generate', progress: 23, loadedChunks: 2, totalChunks: 9 };
  let stream = { radius: 1, loaded: 4, queued: 5, target: 9, totalGenerated: 12 };
  const timers = new Map();
  const calls = [];
  const inventory = {
    selected: 0, emptyHand: false,
    slots: Array.from({ length: 36 }, (_, slot) => ({
      key: slot === 0 ? 'item:1:0' : '', count: slot === 0 ? 3 : 0,
    })),
    legacyUnresolved: [{ slot: 10, key: 'legacy:protected', count: 7 }],
  };
  const info = () => ({
    player: { x, y: 65, z: 8 }, size: [5120, 256, 5120], stream: { ...stream },
    scene: { ticks, phase: 0.2, daylight: 1, mobs: 2 },
  });
  function worldCall(name, ...args) {
    assert.equal(exclusive, false, name + ' during exclusive native WorldJob');
    calls.push([name, ...args]);
  }
  const native = {
    render() { calls.push(['preview']); return true; },
    renderWorld(...args) {
      worldCall('frame', ...args);
      ticks += 1;
      x += args[5] * args[8];
      return true;
    },
    chunkLoadStatus() {
      calls.push(['chunks']);
      if (statusFailure) throw statusFailure;
      return JSON.stringify({ busy: exclusive, loading, error: chunkError, stream });
    },
    worldInfo() { worldCall('info'); return JSON.stringify(info()); },
    targetInfo() { worldCall('target'); return '{}'; },
    mineWorld() { worldCall('mine'); return true; },
    attackWorld() {
      worldCall('attack');
      return JSON.stringify({ hit: false, damaged: false, killed: false, killCount: 0 });
    },
    placeWorld() { worldCall('place'); return true; },
    inventoryInfo() {
      worldCall('inventory');
      if (inventoryFailure) throw new Error('temporary inventory failure');
      return JSON.stringify(inventory);
    },
    inventoryOpen(open) { worldCall('inventoryOpen', open); return true; },
    selectInventory(slot) {
      worldCall('select', slot);
      inventory.selected = slot;
      return JSON.stringify(inventory);
    },
    inventoryTransfer(...args) { worldCall('transfer', ...args); return JSON.stringify(inventory); },
    setRenderRadius(radius) {
      worldCall('radius', radius);
      stream = { ...stream, radius, queued: 21, target: (radius * 2 + 1) ** 2 };
      return true;
    },
    setChunkRanges(displayRadius, preloadRadius) {
      worldCall('ranges', displayRadius, preloadRadius);
      stream = { ...stream, radius: displayRadius, preloadRadius, queued: 21,
        target: (preloadRadius * 2 + 1) ** 2, displayTarget: (displayRadius * 2 + 1) ** 2, displayLoaded: 4 };
      return true;
    },
    saveWorld() { worldCall('save'); return saveResult; },
    beginTouch() { calls.push(['beginTouch']); return true; },
    pollTouch() { calls.push(['touch']); return JSON.stringify({ available: true, touches }); },
    stopTouch() { calls.push(['stopTouch']); return true; },
    beginCreateWorld(...args) { calls.push(['beginCreate', ...args]); exclusive = true; return true; },
    beginOpenWorld(...args) { calls.push(['beginOpen', ...args]); exclusive = true; return true; },
    worldJobStatus() { calls.push(['jobStatus']); return JSON.stringify(job); },
    cancelWorldJob() { calls.push(['cancel']); return true; },
    finishWorldJob() {
      calls.push(['finish']);
      exclusive = false;
      return JSON.stringify({ ...info(), path: '/tmp/mc/live', seed: 42 });
    },
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
      compiledModules.set(filename, new vm.Script('(function(require, module, exports) {' + result.outputText + '\n})', { filename }));
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
  const def = load(path.join(pageDir, 'index.ts')).default;
  const page = def.data();
  for (const [key, fn] of Object.entries(def.methods)) page[key] = fn.bind(page);
  for (const [key, fn] of Object.entries(def.computed)) Object.defineProperty(page, key, { get: fn.bind(page) });
  page.$refs = { sf: { getSurface: () => 1 } };
  page.pageMounted = true;
  page.pageVisible = true;
  let actionMessage = page.actionMessage;
  const messages = [];
  Object.defineProperty(page, 'actionMessage', {
    get: () => actionMessage,
    set(value) { messages.push(value); actionMessage = value; },
  });
  function runTimer(id) {
    const timer = timers.get(id);
    assert.ok(timer, 'Scheduled timer exists');
    timers.delete(id);
    now = timer.due;
    timer.fn();
  }
  return {
    page, native, calls, timers, messages,
    enter() {
      page.enterWorld({ ...info(), path: '/tmp/mc/live', seed: 42 }, false);
      page.refreshInventory();
      page.startRenderLoop();
      messages.length = 0;
    },
    frame() { assert.notEqual(page.renderTimer, null); runTimer(page.renderTimer); },
    poll() { assert.notEqual(page.worldJobTimer, null); runTimer(page.worldJobTimer); },
    setExclusive(value) { exclusive = value; },
    setLoading(value) { loading = value; },
    setChunkError(value) { chunkError = value; },
    setStatusFailure(value) { statusFailure = value; },
    setInventoryFailure(value) { inventoryFailure = value; },
    setSaveResult(value) { saveResult = value; },
    setStream(value) { stream = { ...stream, ...value }; },
    setTouches(value) { touches = value; },
    setJob(value) {
      job = { ...job, ...value };
      if (['cancelled', 'failed', 'idle'].includes(job.state)) exclusive = false;
    },
    get ticks() { return ticks; },
  };
}
function count(h, name) { return h.calls.filter(call => call[0] === name).length; }
function touch(id, x, y) { return { changedTouches: [{ identifier: id, screenX: x, screenY: y }] }; }
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
function noStreamingNotice(h) {
  assert.equal(withClass(h.page, 'render-status').length, 0);
  assert.equal(h.page.actionMessage, '');
  assert.equal(h.page.nativeResult, '');
  assert.ok(h.messages.every(message => !/正在加载|加载中|等待加载|附近区块/.test(message)));
}

test('busy=false/loading=true/queued>0 stays live across 90 real render ticks', () => {
  const h = make();
  h.enter();
  // 650 is inside the left-button zone (600..720); with no mob target it mines.
  h.setTouches([{ id: 1, x: 90, y: 190 }, { id: 2, x: 650, y: 235 }, { id: 3, x: 450, y: 82 }]);
  const before = count(h, 'frame');
  const startTicks = h.ticks;
  for (let i = 0; i < 90; i += 1) {
    h.setTouches([{ id: 1, x: 90, y: 190 }, { id: 2, x: 650, y: 235 }, { id: 3, x: 450 + i, y: 82 }]);
    h.frame();
    assert.equal(h.page.chunkLoading, false);
    assert.equal(h.page.worldInputBlocked, false);
    assert.equal(h.page.worldContacts['native:2'].role, 'primary');
    assert.equal(h.page.worldContacts['native:3'].role, 'look', 'No phantom status-bar touch exclusion');
    noStreamingNotice(h);
  }
  assert.equal(count(h, 'frame') - before, 90, 'Every tick submits native render while pump runs');
  assert.equal(h.ticks - startTicks, 90);
  assert.ok(h.page.worldScene.ticks > startTicks);
  assert.ok(h.page.playerX > 0);
  assert.ok(count(h, 'mine') >= 10);
  assert.ok(count(h, 'info') >= 5);
  assert.ok(count(h, 'target') >= 5);
  assert.ok(count(h, 'inventory') >= 5);
  assert.ok(h.calls.filter(call => call[0] === 'frame').slice(-90).every(call => call[6] === 1));
  assert.ok(h.page.worldStream.queued > 0);
  assert.ok(Number(h.page.fpsText) > 0);
  assert.notEqual(h.page.yaw, 45);
  assert.deepEqual(h.messages, [], 'No per-frame actionMessage assignments');
});

test('a submitted frame starting the pump still reads World/target/inventory that tick', () => {
  const h = make();
  h.setLoading(false);
  const p = h.page;
  p.enterWorld({ path: '/tmp/mc/live', seed: 42, player: { x: 0, y: 65, z: 0 }, size: [5120, 256, 5120] }, false);
  const render = h.native.renderWorld;
  h.native.renderWorld = (...args) => { const ok = render(...args); h.setLoading(true); return ok; };
  p.lastWorldInfoAt = 0;
  p.startRenderLoop();
  assert.equal(count(h, 'frame'), 1);
  assert.equal(count(h, 'info'), 1);
  assert.equal(count(h, 'target'), 1);
  assert.equal(count(h, 'inventory'), 1);
  assert.equal(p.chunkLoading, false);
  noStreamingNotice(h);
});

test('hotbar/inventory/read and radius settings work while loading, without status notifications', () => {
  const h = make();
  h.enter();
  const p = h.page;
  p.selectInventory(2);
  p.openInventory();
  assert.equal(p.inventoryPanelOpen, true);
  p.clickInventorySlot(0);
  p.clickInventorySlot(10);
  assert.equal(count(h, 'transfer'), 1);
  assert.equal(p.closeInventory(), true);
  p.openWorldSettings();
  assert.equal(p.settingsPanelOpen, true);
  assert.match(nodeText(withClass(p, 'settings-stream')[0]), /显示已载 4\/9 · 预加载已载 4\/9 · 队列 5/);
  assert.equal(nodeText(withClass(p, 'settings-apply')[0]), '应用');
  const chips = withClass(p, 'chunk-chip');
  assert.equal(chips.length, 24, 'Twelve tappable stops per range');
  chips[6].data.on.click();
  withClass(p, 'chunk-chip')[12 + 11].data.on.click();
  assert.equal(count(h, 'ranges'), 0, 'Tapping only changes drafts');
  withClass(p, 'settings-apply')[0].data.on.click();
  assert.deepEqual(h.calls.filter(call => call[0] === 'ranges'), [['ranges', 7, 12]]);
  assert.equal(count(h, 'radius'), 0, 'The legacy setter is not used for settings');
  assert.equal(p.selectedRenderRadius, 7);
  assert.equal(p.worldStream.preloadRadius, 12);
  assert.match(nodeText(withClass(p, 'settings-stream')[0]), /显示已载 4\/225 · 预加载已载 4\/625/);
  const before = count(h, 'frame');
  for (let i = 0; i < 20; i += 1) h.frame();
  assert.equal(count(h, 'frame') - before, 20, 'Settings pause input, not rendering');
  assert.match(nodeText(withClass(p, 'settings-stream')[0]), /队列 21/);
  p.closeWorldSettings();
  p.stopNativeTouch();
  p.movementStart('forward', touch(5, 90, 190));
  p.primaryStart(touch(6, 650, 235));
  p.secondaryStart(touch(7, 790, 235));
  assert.equal(p.movement.forward, true);
  assert.ok(count(h, 'mine') > 0, 'Left button mines without a mob target');
  assert.ok(count(h, 'place') > 0, 'Right button places');
  p.releaseTouches(touch(6, 650, 235));
  assert.equal(p.primaryHeld, false);
  assert.equal(p.secondaryHeld, true, 'Releasing one finger keeps the other button held');
  assert.equal(p.movement.forward, true);
  noStreamingNotice(h);
});

test('chunk errors stay short/non-red in settings; neither overlay nor touch-role blocking', () => {
  const h = make();
  h.enter();
  h.setChunkError('disk full: ' + 'detail '.repeat(100));
  h.frame();
  assert.equal(h.page.chunkLoading, false);
  assert.equal(h.page.chunkLoadError, '区块补载暂不可用');
  h.setTouches([{ id: 3, x: 450, y: 82 }]);
  h.frame();
  assert.equal(h.page.worldContacts['native:3'].role, 'look');
  h.page.openWorldSettings();
  assert.equal(nodeText(withClass(h.page, 'settings-hint')[0]), '区块补载暂不可用');
  assert.equal(nodeText(withClass(h.page, 'settings-error')[0]), '');
  h.page.closeWorldSettings();
  h.setChunkError('');
  h.frame();
  assert.equal(h.page.chunkLoadError, '');
  noStreamingNotice(h);
});

test('status/inventory failures retain snapshots, protected stash and current save; recovery stays live', () => {
  const h = make();
  h.enter();
  const p = h.page;
  const inventory = JSON.stringify(p.inventoryState);
  const stream = JSON.stringify(p.worldStream);
  h.setStatusFailure(new Error('bridge offline'));
  const before = count(h, 'frame');
  for (let i = 0; i < 5; i += 1) h.frame();
  assert.equal(count(h, 'frame') - before, 5);
  assert.equal(p.chunkLoading, false);
  assert.equal(p.chunkLoadError, '区块状态暂不可用');
  assert.equal(JSON.stringify(p.worldStream), stream);
  noStreamingNotice(h);
  h.setInventoryFailure(true);
  assert.equal(p.refreshInventory(), false);
  assert.equal(JSON.stringify(p.inventoryState), inventory);
  assert.equal(p.inventoryState.legacyUnresolved[0].count, 7);
  p.worldDirty = true;
  h.setSaveResult(false);
  p.saveAndExit();
  assert.equal(p.worldMode, true);
  assert.equal(p.worldDirty, true);
  assert.equal(p.worldBusy, false);
  assert.equal(p.worldPath, '/tmp/mc/live');
  assert.equal(p.lastWorldPath, '/tmp/mc/live');
  assert.equal(JSON.stringify(p.inventoryState), inventory);
  assert.match(p.actionMessage, /保存失败，仍保留当前世界/);
  h.setStatusFailure(null);
  h.setInventoryFailure(false);
  h.setSaveResult(true);
  p.saveAndExit();
  assert.equal(p.worldMode, false);
  assert.equal(count(h, 'save'), 2);
});

test('settings save/exit saves immediately during an ordinary pump and queues only for exclusive jobs', () => {
  const h = make();
  h.enter();
  const p = h.page;
  // The only entry point now: the button inside the settings panel.
  p.openWorldSettings();
  assert.equal(p.settingsPanelOpen, true);
  assert.equal(nodeText(withClass(p, 'settings-save-exit')[0]), '保存退出');
  p.saveAndExit();
  assert.equal(count(h, 'save'), 1, 'An ordinary live pump never delays the save');
  assert.equal(p.worldMode, false);
  assert.equal(p.saveExitPending, false);
  assert.equal(p.settingsPanelOpen, false);
  assert.ok(h.messages.every(message => !/等待.*加载/.test(message)));
  // Exclusive native ownership queues the exit instead of touching the World.
  const queued = make();
  queued.enter();
  const q = queued.page;
  queued.setExclusive(true);
  const mark = count(queued, 'save');
  q.saveAndExit();
  assert.equal(count(queued, 'save'), mark, 'No save while native owns the World');
  assert.equal(q.saveExitPending, true);
  assert.equal(q.worldMode, true);
  assert.equal(q.worldInputBlocked, true);
  assert.match(q.actionMessage, /等待世界任务结束后保存退出/);
  // Releasing exclusive ownership lets the queued exit complete on a render tick.
  queued.setExclusive(false);
  queued.frame();
  assert.equal(count(queued, 'save'), mark + 1);
  assert.equal(q.worldMode, false);
  assert.equal(q.saveExitPending, false);
});

test('save-and-exit and hide save immediately during ordinary pump without waiting notifications', () => {
  const h = make();
  h.enter();
  h.page.saveAndExit();
  assert.equal(count(h, 'save'), 1);
  assert.equal(h.page.worldMode, false);
  assert.equal(h.page.saveExitPending, false);
  assert.ok(h.messages.every(message => !/等待.*加载/.test(message)));
  const hidden = make();
  hidden.enter();
  hidden.page.onHide();
  assert.equal(count(hidden, 'save'), 1);
  assert.equal(hidden.page.worldMode, true);
  assert.equal(hidden.page.worldDirty, false);
  assert.equal(hidden.timers.size, 0);
});

test('busy=true remains exclusive: no render/mine/read/inventory or native look roles until released', () => {
  const h = make();
  h.enter();
  h.setExclusive(true);
  h.setTouches([{ id: 1, x: 90, y: 190 }, { id: 2, x: 810, y: 235 }, { id: 3, x: 450, y: 82 }]);
  const before = h.calls.length;
  for (let i = 0; i < 5; i += 1) h.frame();
  assert.equal(h.page.chunkLoading, true);
  assert.equal(h.page.worldInputBlocked, true);
  assert.equal(h.page.worldContacts['native:3'].role, 'ignore');
  assert.equal(h.page.nativeRender(0.03), false);
  h.page.refreshWorldInfo();
  assert.equal(h.page.refreshInventory(), false);
  h.page.selectInventory(1);
  h.page.openInventory();
  assert.equal(h.calls.slice(before).some(call => ['frame', 'mine', 'info', 'target', 'inventory', 'select', 'inventoryOpen'].includes(call[0])), false);
  h.setStatusFailure(new Error('status unavailable'));
  h.frame();
  assert.equal(h.page.chunkLoading, true, 'Unknown status does not discard known exclusive ownership');
  h.setStatusFailure(null);
  h.setExclusive(false);
  h.setTouches([]);
  h.frame();
  assert.equal(h.page.chunkLoading, false);
  assert.ok(count(h, 'frame') > 1);
});

test('real exclusive WorldJob preserves progress/cancel/ready and stops timers/render/input', () => {
  const h = make();
  const p = h.page;
  p.seedInput = '42';
  p.createNewWorld();
  assert.equal(p.worldBusy, true);
  assert.equal(p.worldJobVisible, true);
  assert.equal(p.worldInputBlocked, true);
  assert.equal(p.loopRunning, false);
  assert.equal(p.nativeRender(0.03), false);
  assert.equal(p.refreshInventory(), false);
  h.poll();
  assert.equal(p.worldJob.progress, 23);
  assert.equal(nodeText(withClass(p, 'job-progress-fill')[0]), '');
  assert.equal(withClass(p, 'job-progress-fill')[0].data.style.width, '92px');
  p.cancelWorldJob();
  assert.equal(count(h, 'cancel'), 1);
  assert.equal(p.worldJobCancelRequested, true);
  h.setJob({ state: 'ready', stage: 'ready', progress: 100 });
  h.poll();
  assert.equal(count(h, 'finish'), 0, 'Ready/cancel race cannot replace current world');
  h.setJob({ state: 'cancelled', stage: 'cancelled' });
  h.poll();
  assert.equal(p.worldBusy, false);
  assert.equal(p.worldMode, false);
  assert.equal(p.loopRunning, true);
  const ready = make();
  ready.page.createNewWorld();
  ready.setJob({ state: 'ready', stage: 'ready', progress: 100, loadedChunks: 9 });
  ready.poll();
  assert.equal(count(ready, 'finish'), 1);
  assert.equal(ready.page.worldMode, true);
  assert.equal(ready.page.chunkLoading, false);
  assert.ok(count(ready, 'frame') > 0);
});

(async () => {
  const css = (await less.render(fs.readFileSync(path.join(pageDir, 'index.less'), 'utf8'), {
    filename: path.join(pageDir, 'index.less'),
  })).css;
  assert.match(css, /\.settings-hint,[\s\S]*?color: #c7ddc9;/);
  assert.match(css, /\.settings-hint\s*\{\s*top: 142px;/);
  assert.match(css, /\.world-job-panel\s*\{[^}]*left: 230px;[^}]*top: 50px;[^}]*width: 500px;[^}]*height: 150px;/);
  assert.match(css, /\.job-progress-track\s*\{[^}]*width: 400px;[^}]*height: 14px;/);
  // The two side columns must clear the 1.25× panel, whose right edge is x=590.
  assert.match(css, /\.inventory-status\s*\{[^}]*left: 598px;/);
  assert.match(css, /\.inventory-tools\s*\{[^}]*left: 598px;/);
  assert.equal(parsed.descriptor.template.content.includes('正在加载附近区块'), false);
  assert.equal(parsed.descriptor.template.content.includes('chunkLoading'), false);
  test('Falcon template and Less compile with non-red diagnostic hint and unchanged WorldJob geometry', () => {});
  console.log('RESULT ' + passed + ' UI streaming live checks passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
