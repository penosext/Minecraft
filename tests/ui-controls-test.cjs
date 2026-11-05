// World-mode control keys: original mouse semantics (left = attack/mine, right = place).
// Reuses the real-TS/VM mock pattern from tests/ui-streaming-live-test.cjs.
// Run: node tests/ui-controls-test.cjs (no native/generated files are loaded or written).
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
let passed = 0;
function test(name, run) {
  run();
  passed += 1;
  console.log('PASS ' + name);
}

function make() {
  let now = 1000;
  let nextTimer = 1;
  let touches = [];
  // Native owns target identity/HP; the mock mob survives so the left button keeps attacking.
  let target = null;
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
    chunkLoadStatus() {
      return JSON.stringify({ busy: false, loading: true, error: '', stream });
    },
    worldInfo() { calls.push(['info']); return JSON.stringify(info()); },
    targetInfo() { calls.push(['target']); return JSON.stringify(target || {}); },
    mineWorld(...args) { calls.push(['mine', ...args]); return true; },
    attackWorld(...args) {
      calls.push(['attack', ...args]);
      if (!target) return JSON.stringify({ hit: false, damaged: false, killed: false, killCount: 0 });
      return JSON.stringify({
        ...target, hit: true, damaged: true, killed: false, damage: 1, cooldownTicks: 0, killCount: 0,
      });
    },
    placeWorld(...args) { calls.push(['place', ...args]); return true; },
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
  const def = load(path.join(pageDir, 'index.ts')).default;
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
    page, native, calls, timers,
    enter() {
      page.enterWorld({ ...info(), path: '/tmp/mc/live', seed: 42 }, false);
      page.refreshInventory();
      page.startRenderLoop();
    },
    frame() { assert.notEqual(page.renderTimer, null); runTimer(page.renderTimer); },
    setTouches(value) { touches = value; },
    setTarget(value) { target = value; },
    advance(ms) { now += ms; },
    get now() { return now; },
  };
}
const MOB = { id: 7, kind: 'Pig', health: 10, maxHealth: 10, distance: 2 };
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
function allText(page) { return nodes(page).map(node => node.text || '').join('|'); }

test('left button attacks the crosshair mob and never falls back to mining', () => {
  const h = make();
  h.enter();
  h.setTarget(MOB);
  const p = h.page;
  p.refreshTargetInfo();
  // Compare by value: the page builds its snapshot inside the VM realm.
  assert.equal(JSON.stringify(p.targetHP), JSON.stringify(MOB));
  // Native touch inside the left-button zone claims the primary role.
  h.setTouches([{ id: 1, x: 640, y: 235 }]);
  for (let i = 0; i < 40; i += 1) h.frame();
  assert.equal(p.primaryHeld, true);
  assert.equal(p.worldContacts['native:1'].role, 'primary');
  assert.ok(count(h, 'attack') >= 3, 'Left button repeatedly calls attackWorld');
  assert.equal(count(h, 'mine'), 0, 'A live mob target must never be mined');
  assert.equal(count(h, 'place'), 0);
  // Native owns HP: the UI reports exactly the snapshot it was handed.
  assert.equal(p.targetHP.health, 10);
  assert.match(p.targetStatus, /目标 猪 10\/10/);
  const attacks = h.calls.filter(call => call[0] === 'attack');
  assert.ok(attacks.every(call => call[1] === p.yaw && call[2] === p.pitch), 'attackWorld(yaw, pitch) unchanged');
});

test('left button mines when no mob is under the crosshair', () => {
  const h = make();
  h.enter();
  const p = h.page;
  p.refreshTargetInfo();
  assert.equal(p.targetHP, null);
  h.setTouches([{ id: 1, x: 700, y: 250 }]);
  for (let i = 0; i < 40; i += 1) h.frame();
  assert.equal(p.primaryHeld, true);
  assert.ok(count(h, 'mine') >= 3, 'Left button repeatedly calls mineWorld');
  assert.equal(count(h, 'attack'), 0, 'No target means no attack call');
  assert.equal(count(h, 'place'), 0);
  const mines = h.calls.filter(call => call[0] === 'mine');
  assert.ok(mines.every(call => call[1] === p.yaw && call[2] === p.pitch), 'mineWorld(yaw, pitch) unchanged');
});

test('the left button switches between attack and mine as the target appears and dies', () => {
  const h = make();
  h.enter();
  const p = h.page;
  // Fallback path: an immediate first action makes each switch observable.
  p.stopNativeTouch();
  p.primaryStart(touch(1, 640, 235));
  assert.equal(p.primaryHeld, true);
  assert.equal(count(h, 'mine'), 1);
  assert.equal(count(h, 'attack'), 0);
  h.setTarget(MOB);
  p.refreshTargetInfo();
  h.advance(300);
  p.tickPrimary(h.now);
  assert.equal(count(h, 'attack'), 1, 'A newly acquired mob is attacked by the same held button');
  assert.equal(count(h, 'mine'), 1);
  h.setTarget(null);
  p.refreshTargetInfo();
  assert.equal(p.targetHP, null);
  h.advance(300);
  p.tickPrimary(h.now);
  assert.equal(count(h, 'mine'), 2, 'Losing the target returns the same button to mining');
  assert.equal(count(h, 'attack'), 1);
});

test('right button delegates to placeWorld without re-deriving the native outcome', () => {
  const h = make();
  h.enter();
  const p = h.page;
  // Native placeWorld tries Tile::use first and only then places, so a false
  // result is a legitimate no-op (e.g. an iron door) and must not be reported
  // as a placement failure anywhere in the UI.
  h.native.placeWorld = (...args) => { h.calls.push(['place', ...args]); return false; };
  p.stopNativeTouch();
  p.secondaryStart(touch(1, 790, 235));
  for (let i = 0; i < 5; i += 1) { h.advance(300); p.tickSecondary(h.now); }
  assert.ok(count(h, 'place') >= 3, 'A false result keeps the button usable');
  assert.equal(p.secondaryHeld, true, 'A no-op never releases the held button');
  assert.equal(/放置失败/.test(p.actionMessage), false, 'No misleading placement-failure text');
  assert.equal(allText(p).includes('放置失败'), false);
  assert.equal(pageSource.includes('放置失败'), false, 'The old wording is gone from the page');
  assert.equal(count(h, 'mine'), 0);
  assert.equal(count(h, 'attack'), 0);
  // Only a real bridge error is surfaced, and then without claiming a failure.
  h.native.placeWorld = () => { throw new Error('bridge offline'); };
  h.advance(300);
  p.tickSecondary(h.now);
  assert.match(p.actionMessage, /^右键无效果: /);
  assert.equal(p.secondaryHeld, false, 'A bridge error releases the button');
  // The UI never calls the interaction-only binding itself.
  assert.equal(pageSource.includes('useWorld'), false, 'Interaction judgement stays in native');
});

test('right button places and reports an empty hand, never mining or attacking', () => {
  const h = make();
  h.enter();
  const p = h.page;
  h.setTouches([{ id: 1, x: 790, y: 235 }]);
  for (let i = 0; i < 40; i += 1) h.frame();
  assert.equal(p.secondaryHeld, true);
  assert.equal(p.worldContacts['native:1'].role, 'secondary');
  assert.ok(count(h, 'place') >= 3, 'Right button repeatedly calls placeWorld');
  assert.equal(count(h, 'mine'), 0);
  assert.equal(count(h, 'attack'), 0);
  const places = h.calls.filter(call => call[0] === 'place');
  assert.ok(places.every(call => call[1] === p.yaw && call[2] === p.pitch), 'placeWorld(yaw, pitch) unchanged');
  // A mob target does not turn the right button into an attack.
  h.setTarget(MOB);
  p.refreshTargetInfo();
  const before = count(h, 'place');
  for (let i = 0; i < 20; i += 1) h.frame();
  assert.ok(count(h, 'place') > before);
  assert.equal(count(h, 'attack'), 0);
});

test('native primary/secondary/jump zones match the rendered button geometry', () => {
  const h = make();
  h.enter();
  const p = h.page;
  const roleAt = (x, y) => {
    h.setTouches([{ id: 1, x, y }]);
    h.frame();
    const role = p.worldContacts['native:1'].role;
    h.setTouches([]);
    h.frame();
    return role;
  };
  // Left button (626,218) 104×40; right button (738,218) 104×40; jump (850,218) 98×40.
  for (const [x, y] of [[626, 218], [678, 238], [730, 258]]) {
    assert.equal(roleAt(x, y), 'primary', 'primary at ' + x + ',' + y);
  }
  for (const [x, y] of [[738, 218], [790, 238], [842, 258]]) {
    assert.equal(roleAt(x, y), 'secondary', 'secondary at ' + x + ',' + y);
  }
  for (const [x, y] of [[850, 218], [899, 238], [948, 258]]) {
    assert.equal(roleAt(x, y), 'jump', 'jump at ' + x + ',' + y);
  }
  // The 1.5× hotbar box (x 341..618) ends before the strip; the gaps are free look.
  assert.equal(roleAt(622, 240), 'look');
  assert.equal(roleAt(734, 240), 'look');
  assert.equal(roleAt(846, 240), 'look');
  // The zones are disjoint and the movement pad keeps its own roles.
  assert.equal(roleAt(90, 190), 'forward');
  assert.equal(roleAt(40, 238), 'left');
  assert.equal(roleAt(100, 238), 'backward');
  assert.equal(roleAt(160, 238), 'right');
  // Above the strip is still free look, so the buttons never steal camera drags.
  assert.equal(roleAt(678, 200), 'look');
  assert.equal(roleAt(790, 200), 'look');
});

test('a native contact keeps its button role until release, even after sliding away', () => {
  const h = make();
  h.enter();
  const p = h.page;
  h.setTouches([{ id: 1, x: 640, y: 235 }, { id: 2, x: 790, y: 235 }]);
  h.frame();
  assert.equal(p.worldContacts['native:1'].role, 'primary');
  assert.equal(p.worldContacts['native:2'].role, 'secondary');
  assert.equal(p.primaryHeld, true);
  assert.equal(p.secondaryHeld, true);
  const yaw = p.yaw;
  // Slide the left-button finger across the right button and into the look area.
  h.setTouches([{ id: 1, x: 790, y: 235 }, { id: 2, x: 640, y: 150 }]);
  h.frame();
  assert.equal(p.worldContacts['native:1'].role, 'primary', 'Leaving a button never changes its role');
  assert.equal(p.worldContacts['native:2'].role, 'secondary');
  assert.equal(p.primaryHeld, true);
  assert.equal(p.secondaryHeld, true);
  assert.equal(p.yaw, yaw, 'A button contact never becomes a look drag');
  // Release only the left-button finger.
  h.setTouches([{ id: 2, x: 640, y: 150 }]);
  h.frame();
  assert.equal(p.worldContacts['native:1'], undefined);
  assert.equal(p.primaryHeld, false);
  assert.equal(p.secondaryHeld, true, 'One release must not drop the other button');
  h.setTouches([]);
  h.frame();
  assert.equal(p.secondaryHeld, false);
  assert.equal(p.touchCount, 0);
});

test('fallback primary/secondary handlers hold roles per touch id until release', () => {
  const h = make();
  h.enter();
  const p = h.page;
  p.stopNativeTouch();
  assert.equal(p.nativeTouchAvailable, false);
  p.primaryStart(touch(1, 640, 235));
  p.secondaryStart(touch(2, 790, 235));
  assert.equal(p.primaryHeld, true);
  assert.equal(p.secondaryHeld, true);
  // Arrays are created inside the VM realm, so compare their contents.
  assert.equal(p.primaryTouches.join(), '1');
  assert.equal(p.secondaryTouches.join(), '2');
  assert.equal(p.fallbackRoles['touch:1'], 'primary');
  assert.equal(p.fallbackRoles['touch:2'], 'secondary');
  // A claimed id cannot be stolen by the other button or by look.
  p.secondaryStart(touch(1, 790, 235));
  assert.equal(p.fallbackRoles['touch:1'], 'primary');
  p.onLookStart(touch(1, 300, 120));
  assert.equal(p.dragging, false, 'A button-owned finger never starts a look drag');
  p.primaryEnd(touch(1, 640, 235));
  assert.equal(p.primaryHeld, false);
  assert.equal(p.fallbackRoles['touch:1'], undefined);
  assert.equal(p.secondaryHeld, true);
  p.releaseTouches(touch(2, 790, 235));
  assert.equal(p.secondaryHeld, false);
  assert.equal(p.touchCount, 0);
});

test('blocked world input and exclusive jobs suppress both buttons', () => {
  const h = make();
  h.enter();
  const p = h.page;
  p.openInventory();
  assert.equal(p.worldInputBlocked, true);
  const before = h.calls.length;
  h.setTouches([{ id: 1, x: 640, y: 235 }, { id: 2, x: 790, y: 235 }]);
  for (let i = 0; i < 6; i += 1) h.frame();
  assert.equal(p.worldContacts['native:1'].role, 'ignore');
  assert.equal(p.worldContacts['native:2'].role, 'ignore');
  assert.equal(h.calls.slice(before).some(call => ['mine', 'attack', 'place'].includes(call[0])), false);
  // Held-while-closing fingers stay ignored until they are released.
  assert.equal(p.closeInventory(), true);
  for (let i = 0; i < 6; i += 1) h.frame();
  assert.equal(p.worldContacts['native:1'].role, 'ignore');
  assert.equal(p.primaryHeld, false);
  assert.equal(p.secondaryHeld, false);
  assert.equal(h.calls.slice(before).some(call => ['mine', 'attack', 'place'].includes(call[0])), false);
  // Fallback handlers are gated too.
  const blocked = make();
  blocked.enter();
  blocked.page.stopNativeTouch();
  blocked.page.openWorldSettings();
  const mark = blocked.calls.length;
  blocked.page.primaryStart(touch(3, 640, 235));
  blocked.page.secondaryStart(touch(4, 790, 235));
  assert.equal(blocked.page.primaryHeld, false);
  assert.equal(blocked.page.secondaryHeld, false);
  assert.equal(blocked.calls.slice(mark).some(call => ['mine', 'attack', 'place'].includes(call[0])), false);
});

test('viewMode stays 0 and native renderWorld always receives first person', () => {
  const h = make();
  h.enter();
  const p = h.page;
  assert.equal(p.viewMode, 0);
  assert.equal(p.toggleViewMode, undefined, 'The view-mode toggle method is gone');
  for (let i = 0; i < 10; i += 1) h.frame();
  const frames = h.calls.filter(call => call[0] === 'frame');
  assert.ok(frames.length >= 10);
  assert.ok(frames.every(call => call[10] === 0), 'renderWorld viewMode argument is always 0');
  assert.ok(frames.every(call => call[11] === 3), 'renderWorld pixelStep argument unchanged');
  assert.equal(p.viewMode, 0);
  assert.equal(/viewMode = /.test(pageSource), false, 'viewMode is never reassigned');
});

test('removed tool keys are gone while the functional entries stay', () => {
  const h = make();
  h.enter();
  const p = h.page;
  for (const name of ['camera-button', 'mine-button', 'attack-button', 'place-button']) {
    assert.equal(withClass(p, name).length, 0, name + ' removed');
    assert.equal(templateSource.includes(name), false, name + ' absent from template');
  }
  for (const label of ['放置', '攻击', '挖掘', '第一人称', '角色预览', '原图 1× · 原生背包']) {
    assert.equal(templateSource.includes(label), false, label + ' absent from template');
  }
  // The two mouse buttons and jump are present with the required Chinese labels.
  assert.equal(nodeText(withClass(p, 'primary-button')[0]), '左键');
  assert.equal(nodeText(withClass(p, 'secondary-button')[0]), '右键');
  assert.equal(nodeText(withClass(p, 'jump-button')[0]), '跳跃');
  assert.equal(withClass(p, 'primary-button').length, 1);
  assert.equal(withClass(p, 'secondary-button').length, 1);
  // Functional entries must survive.
  assert.equal(nodeText(withClass(p, 'settings-button')[0]), '设置');
  assert.equal(nodeText(withClass(p, 'bag-button')[0]), '背包');
  // Save/exit is no longer a world-mode HUD button; it now lives in settings.
  assert.equal(withClass(p, 'save-button').length, 0, 'The permanent save button is gone');
  assert.equal(templateSource.includes('save-button'), false, 'save-button absent from template');
  assert.equal(allText(p).includes('保存退出'), false, 'World mode shows no save/exit label');
  p.openWorldSettings();
  assert.equal(p.settingsPanelOpen, true);
  assert.equal(nodeText(withClass(p, 'settings-save-exit')[0]), '保存退出');
  p.closeWorldSettings();
  // No event modifiers and no compound selectors in the template.
  assert.equal(/@\w+\.\w/.test(templateSource), false, 'No event modifiers such as .stop');
});

test('settings save/exit keeps every state label and guard of the former HUD button', () => {
  const h = make();
  h.enter();
  const p = h.page;
  const label = () => {
    const inPanel = withClass(p, 'settings-save-exit');
    assert.equal(inPanel.length, 1, 'Save/exit stays inside the settings panel');
    return nodeText(inPanel[0]);
  };
  p.openWorldSettings();
  assert.equal(label(), '保存退出');
  // Busy label: an exclusive WorldJob also disables the action entirely.
  p.worldBusy = true;
  assert.equal(label(), '保存中…');
  const marker = count(h, 'save');
  p.saveAndExit();
  assert.equal(count(h, 'save'), marker, 'An exclusive WorldJob blocks save/exit');
  assert.equal(p.worldMode, true);
  p.worldBusy = false;
  // Queued label while native holds exclusive ownership of the chunk pump.
  p.saveExitPending = true;
  assert.equal(label(), '等待任务…');
  p.saveExitPending = false;
  // The real action still saves once and leaves world mode.
  p.saveAndExit();
  assert.equal(count(h, 'save'), marker + 1);
  assert.equal(p.worldMode, false);
  assert.equal(p.settingsPanelOpen, false, 'Leaving the world closes the settings panel');
  // hide/unload still auto-saves without any HUD button.
  const hidden = make();
  hidden.enter();
  hidden.page.worldDirty = true;
  hidden.page.onHide();
  assert.equal(count(hidden, 'save'), 1);
  assert.equal(hidden.page.worldMode, true);
});

test('the freed top-right strip and the open inventory panel leave no dead zones', () => {
  const h = make();
  h.enter();
  const p = h.page;
  const roleAt = (x, y) => {
    h.setTouches([{ id: 1, x, y }]);
    h.frame();
    const role = p.worldContacts['native:1'].role;
    h.setTouches([]);
    h.frame();
    return role;
  };
  // The former save button reserved (844,6)-(948,46); with no button there the
  // whole strip must be an ordinary look area again.
  for (const [x, y] of [[844, 6], [896, 26], [948, 46], [860, 40]]) {
    assert.equal(roleAt(x, y), 'look', 'freed save strip at ' + x + ',' + y);
  }
  // The surviving buttons keep their own ignore boxes.
  assert.equal(roleAt(560, 26), 'ignore', 'settings button still ignored');
  assert.equal(roleAt(680, 26), 'ignore', 'bag button still ignored');
  // Opening the inventory hides both, but world input is blocked, so every
  // contact is ignored wholesale rather than by a leftover button rectangle.
  p.openInventory();
  assert.equal(withClass(p, 'settings-button').length, 0, 'Settings hidden under the 1.5× panel');
  assert.equal(withClass(p, 'bag-button').length, 0, 'Bag hidden under the 1.5× panel');
  assert.equal(p.worldInputBlocked, true);
  assert.equal(roleAt(560, 26), 'ignore');
  assert.equal(roleAt(896, 26), 'ignore');
  // Closing restores both buttons and the freed strip stays a look area.
  assert.equal(p.closeInventory(), true);
  assert.equal(withClass(p, 'settings-button').length, 1, 'Settings restored after closing');
  assert.equal(withClass(p, 'bag-button').length, 1, 'Bag restored after closing');
  assert.equal(nodeText(withClass(p, 'settings-button')[0]), '设置');
  assert.equal(roleAt(896, 26), 'look');
  assert.equal(roleAt(560, 26), 'ignore');
  // And the settings button really still works after the inventory round trip.
  p.openWorldSettings();
  assert.equal(p.settingsPanelOpen, true);
  p.closeWorldSettings();
});

test('inventory tools keep only the needed actions, held item and migration warning', () => {
  const h = make();
  h.enter();
  const p = h.page;
  p.openInventory();
  assert.equal(p.inventoryPanelOpen, true);
  assert.equal(withClass(p, 'inventory-tools').length, 1);
  const buttons = withClass(p, 'inventory-tool-button');
  assert.deepEqual(buttons.map(nodeText), ['创造物品', '关闭背包']);
  const hints = withClass(p, 'inventory-hint');
  assert.deepEqual(hints.map(nodeText), ['持物：石头']);
  assert.equal(allText(p).includes('原图'), false);
  assert.equal(allText(p).includes('点槽位'), false, 'The pure-hint selection text is gone');
  // The migration warning is retained and still driven by native legacy entries.
  assert.equal(withClass(p, 'inventory-migration-warning').length, 0);
  p.inventoryState.legacyUnresolved = [{ slot: 10, key: 'legacy:protected', count: 7 }];
  assert.equal(nodeText(withClass(p, 'inventory-migration-warning')[0]), '旧状态物品已归一，1项保留待处理');
  // The surviving actions still work.
  p.toggleBlockPicker();
  assert.equal(p.pickerOpen, true);
  p.closeBlockPicker();
  assert.equal(p.closeInventory(), true);
});

(async () => {
  const css = (await less.render(fs.readFileSync(path.join(pageDir, 'index.less'), 'utf8'), {
    filename: path.join(pageDir, 'index.less'),
  })).css;
  // Less geometry must match CONTROL_BOUNDS exactly.
  assert.match(css, /\.primary-button\s*\{[^}]*left: 626px;[^}]*width: 104px;/);
  assert.match(css, /\.secondary-button\s*\{[^}]*left: 738px;[^}]*width: 104px;/);
  assert.match(css, /\.primary-button,\s*\n\.secondary-button,\s*\n\.jump-button\s*\{[^}]*bottom: 8px;[^}]*height: 40px;/);
  assert.match(css, /\.jump-button\s*\{\s*right: 12px;\s*width: 98px;/); // 960-12-98 = 850
  for (const removed of ['.camera-button', '.mine-button', '.attack-button', '.place-button', '.save-button']) {
    assert.equal(css.includes(removed), false, removed + ' removed from Less');
  }
  // Save/exit now exists only in the full-screen settings header.
  assert.match(css, /\.settings-save-exit\s*\{[^}]*top: 5px;[^}]*left: 700px;[^}]*width: 140px;[^}]*height: 30px;/);
  // The mouse/jump strip is translucent so the native first-person hand under it stays visible.
  for (const selector of ['primary-button', 'secondary-button']) {
    const alpha = new RegExp('\\.' + selector + '\\s*\\{[^}]*background-color: rgba\\([^)]*, (0\\.\\d+)\\)').exec(css);
    assert.ok(alpha && Number(alpha[1]) <= 0.5, selector + ' must not hide the hand');
  }
  assert.match(css, /\.control-held\s*\{/);
  test('Falcon template and Less compile with button geometry matching CONTROL_BOUNDS', () => {});
  console.log('RESULT ' + passed + ' UI control checks passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
