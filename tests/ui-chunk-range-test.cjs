// Offline real-TS + compiled Falcon event regression. No generated/native files written.
// Run: node tests/ui-chunk-range-test.cjs
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
const source = fs.readFileSync(path.join(pageDir, 'index.ts'), 'utf8');
const parsed = compiler['vue-template-compiler'].parse(fs.readFileSync(path.join(pageDir, 'index.vue'), 'utf8'));
assert.deepEqual(parsed.errors, []);
const template = compiler['falcon-template-compiler'].compile(parsed.descriptor.template.content, { preserveWhitespace: false });
assert.deepEqual(template.errors, []);
assert.deepEqual(template.tips, []);
const render = new Function(template.render);
const compiled = new Map();
let passed = 0;
function test(name, run) { run(); passed++; console.log('PASS ' + name); }
const json = value => JSON.parse(JSON.stringify(value));
const initial = { radius: 1, preloadRadius: 2, displayTarget: 9, displayLoaded: 7,
  target: 25, loaded: 13, queued: 12, totalGenerated: 31 };
function make(snapshot = initial) {
  let stream = { ...snapshot };
  let touches = [];
  let busy = false;
  let now = 1000;
  let timerId = 0;
  const timers = new Map();
  const calls = [];
  let gameMode = 'creative';
  const RULES = ['DoDaylightCycle', 'DoTileDrops', 'DoMobLoot', 'DoMobSpawning', 'KeepInventory',
    'NaturalRegeneration', 'FireSpreads', 'MobGriefing', 'TNT', 'CheatsEnabled'];
  const rules = Object.fromEntries(RULES.map(name => [name, name !== 'CheatsEnabled']));
  const scene = () => ({ ticks: 100, phase: 0.1, daylight: 1, mobs: 3, rules: { version: 1, rules: { ...rules },
    enforcement: Object.fromEntries(RULES.map(name => [name, name === 'KeepInventory'
      ? 'exported-query-caller-enforces' : 'enforced'])) } });
  const inventory = () => ({ selected: 0, emptyHand: true, gameMode,
    slots: Array.from({ length: 36 }, () => ({ key: '', count: 0 })) });
  const info = () => ({ player: { x: 0, y: 65, z: 8 }, size: [5120, 256, 5120], stream: { ...stream }, scene: scene() });
  const native = {
    worldInfo() { calls.push(['info']); return JSON.stringify(info()); },
    chunkLoadStatus() { return JSON.stringify({ busy, loading: true, error: '', stream }); },
    setChunkRanges(displayRadius, preloadRadius) {
      calls.push(['ranges', displayRadius, preloadRadius]);
      stream = { ...stream, radius: displayRadius, preloadRadius,
        displayTarget: (displayRadius * 2 + 1) ** 2, target: (preloadRadius * 2 + 1) ** 2 };
      return true;
    },
    setRenderRadius(radius) { calls.push(['legacy', radius]); return true; },
    beginCreateWorld(...args) { calls.push(['create', ...args]); return true; },
    beginOpenWorld(...args) { calls.push(['open', ...args]); return true; },
    saveWorld() { calls.push(['save']); return true; },
    beginTouch() { return true; },
    pollTouch() { return JSON.stringify({ available: true, touches }); },
    stopTouch() { return true; },
    renderWorld(...args) { calls.push(['frame', ...args]); return true; },
    inventoryInfo() { return JSON.stringify(inventory()); },
    setGameMode(mode) { calls.push(['mode', mode]); gameMode = mode; return JSON.stringify(inventory()); },
    setWorldRule(rule, value) {
      calls.push(['rule', rule, value]); rules[rule] = value;
      return JSON.stringify({ ...info(), scene: scene() });
    },
  };
  const context = vm.createContext({ console: { error() {} }, Date, performance: { now: () => now },
    setTimeout(fn) { const id = ++timerId; timers.set(id, fn); return id; },
    clearTimeout(id) { timers.delete(id); } });
  const modules = new Map();
  function load(filename) {
    if (modules.has(filename)) return modules.get(filename).exports;
    if (!compiled.has(filename)) {
      const result = ts.transpileModule(fs.readFileSync(filename, 'utf8'), {
        fileName: filename, reportDiagnostics: true,
        compilerOptions: { target: ts.ScriptTarget.ES2020, module: ts.ModuleKind.CommonJS, strict: true },
      });
      assert.deepEqual(result.diagnostics, []);
      compiled.set(filename, new vm.Script('(function(require,module,exports){' + result.outputText + '\n})', { filename }));
    }
    const module = { exports: {} };
    modules.set(filename, module);
    compiled.get(filename).runInContext(context)(name => {
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
  page.enterWorld({ ...info(), path: '/tmp/mc/chunk-range', seed: 42 }, false);
  return { page, native, calls, timers,
    setStream(value) { stream = { ...value }; },
    setTouches(value) { touches = value; },
    setBusy(value) { busy = value; },
    advance() { now += 600; },
  };
}
function nodes(page) {
  const helpers = {
    _c(tag, data, children) {
      if (Array.isArray(data)) { children = data; data = {}; }
      return { tag, data: data || {}, children: (children || []).flat(Infinity) };
    },
    _s: String, _v: text => ({ text }), _e: () => null, _l: (values, fn) => values.map(fn),
  };
  const all = [];
  function visit(node) { if (node) { all.push(node); for (const child of node.children || []) visit(child); } }
  visit(render.call(new Proxy(page, {
    has: (target, key) => key in helpers || key in target,
    get: (target, key) => key in helpers ? helpers[key] : target[key],
  })));
  return all;
}
function byClass(page, name) { return nodes(page).filter(node => (node.data?.staticClass || []).includes(name)); }
function text(node) { return !node ? '' : node.text || (node.children || []).map(text).join(''); }
function point(id, x, y, coordinate = 'screen', alias = 'identifier') {
  return { [alias]: id, [coordinate === 'xy' ? 'x' : coordinate + 'X']: x,
    [coordinate === 'xy' ? 'y' : coordinate + 'Y']: y };
}
function event(...touches) { return { changedTouches: touches }; }
function values(page) { return [page.settingsRenderRadius, page.settingsPreloadRadius]; }
function mutations(h) { return h.calls.filter(call => ['ranges', 'legacy', 'create', 'open'].includes(call[0])); }

function chips(page, kind) {
  const rows = byClass(page, 'settings-range-row');
  const row = rows[kind === 'display' ? 0 : 1];
  const found = [];
  (function visit(node) { if (!node) return; if ((node.data?.staticClass || []).includes('chunk-chip')) found.push(node);
    for (const child of node.children || []) visit(child); })(row);
  return found;
}
function tap(page, kind, radius) { chips(page, kind)[radius - 1].data.on.click(); }
function selected(node) { return !!(node.data.class && node.data.class['chunk-chip-selected']); }

test('settings are a full-screen page with tabs, not a centred popup', () => {
  const h = make(); const p = h.page;
  p.openWorldSettings();
  assert.equal(byClass(p, 'settings-screen').length, 1);
  assert.equal(byClass(p, 'settings-panel').length, 0, 'The old popup is gone');
  assert.equal(byClass(p, 'chunk-range-slider').length, 0, 'No drag sliders remain');
  assert.deepEqual(byClass(p, 'settings-tab').map(text), ['区块距离', '游戏', '调试信息']);
  assert.equal(byClass(p, 'settings-save-exit').length, 1, 'Save/exit is in the header on every tab');
  byClass(p, 'settings-tab')[1].data.on.click();
  assert.equal(p.settingsTab, 'game');
  assert.equal(byClass(p, 'settings-save-exit').length, 1);
  assert.equal(byClass(p, 'chunk-chip').length, 0);
  p.selectSettingsTab('bogus');
  assert.equal(p.settingsTab, 'game');
});

test('each range has twelve tap chips with Chinese labels and the current stop highlighted', () => {
  const h = make(); const p = h.page; p.openWorldSettings();
  assert.equal(/@\w+\.\w/.test(parsed.descriptor.template.content), false, 'No .stop/event modifiers');
  for (const kind of ['display', 'preload']) {
    const row = chips(p, kind);
    assert.equal(row.length, 12, kind);
    assert.deepEqual(row.map(text), Array.from({ length: 12 }, (_, i) => String(i + 1)));
    for (const chip of row) assert.equal(typeof chip.data.on.click, 'function');
  }
  assert.deepEqual(values(p), [1, 2]);
  assert.deepEqual(chips(p, 'display').map(selected).indexOf(true), 0);
  assert.deepEqual(chips(p, 'preload').map(selected).indexOf(true), 1);
  for (const [radius, side, total] of [[1, 3, 9], [6, 13, 169], [12, 25, 625]]) {
    p.setSettingsRange('display', radius); p.setSettingsRange('preload', radius);
    assert.deepEqual(byClass(p, 'settings-range-label').map(text), [`显示范围 ${radius}（周围${side}×${side} / 总数${total}区块）`,
      `预加载范围 ${radius}（周围${side}×${side} / 总数${total}区块）`]);
  }
  assert.deepEqual(mutations(h), []);
});

test('chip taps raise preload with display, never lower it below display, and Apply sends one call', () => {
  const h = make(); const p = h.page; p.openWorldSettings();
  tap(p, 'display', 7);
  assert.deepEqual(values(p), [7, 7]);
  tap(p, 'display', 3);
  assert.deepEqual(values(p), [3, 7], 'Lowering display keeps preload');
  tap(p, 'preload', 2);
  assert.deepEqual(values(p), [3, 3], 'Preload clamps at display');
  const disabled = chips(p, 'preload').map(node => !!node.data.class['chunk-chip-disabled']);
  assert.deepEqual(disabled, [true, true, false, false, false, false, false, false, false, false, false, false]);
  tap(p, 'preload', 12);
  assert.deepEqual(values(p), [3, 12]);
  const before = values(p);
  for (const bad of [0, 13, 1.5, NaN, Infinity, '2']) p.selectSettingsRange('display', bad);
  assert.deepEqual(values(p), before);
  assert.deepEqual(mutations(h), [], 'Nothing is applied before Apply');
  byClass(p, 'settings-apply')[0].data.on.click();
  assert.deepEqual(mutations(h), [['ranges', 3, 12]]);
  assert.equal(p.worldStream.radius, 3);
  assert.equal(p.worldStream.preloadRadius, 12);
  assert.equal(p.worldJobActive, false);
  assert.equal(h.timers.size, 0);
});

test('close cancels drafts using current native values, and periodic snapshots do not overwrite drafts', () => {
  const h = make(); const p = h.page;
  h.setStream({ ...initial, radius: 2, preloadRadius: 3, displayTarget: 25, target: 49 });
  p.openWorldSettings();
  assert.deepEqual(values(p), [2, 3], 'Open refreshes before copying native values');
  tap(p, 'display', 1); tap(p, 'preload', 1);
  h.advance(); p.refreshChunkLoadStatus(); p.refreshWorldInfo();
  assert.deepEqual(values(p), [1, 1], 'Live stats leave unsaved drafts alone');
  byClass(p, 'screen-close')[0].data.on.click();
  assert.equal(p.settingsPanelOpen, false);
  assert.deepEqual(values(p), [2, 3]);
  assert.deepEqual(mutations(h), []);
});

test('new stream counters stay independent and legacy snapshots normalize through both read paths', () => {
  const h = make(); const p = h.page; p.openWorldSettings();
  assert.match(text(byClass(p, 'settings-stream')[0]), /显示已载 7\/9 · 预加载已载 13\/25 · 队列 12 · 累计生成 31/);
  const legacy = { radius: 3, target: 49, loaded: 8, queued: 41, totalGenerated: 90 };
  h.setStream(legacy); p.refreshWorldInfo();
  assert.deepEqual(json(p.worldStream), { ...legacy, preloadRadius: 3, displayTarget: 49, displayLoaded: 8 });
  const valid = json(p.worldStream);
  for (const invalid of [{ ...initial, preloadRadius: 0 }, { ...initial, radius: 3, preloadRadius: 2 },
    { ...initial, displayLoaded: 1.5 }]) {
    h.setStream(invalid); h.advance(); p.refreshChunkLoadStatus();
    assert.deepEqual(json(p.worldStream), valid, 'Bad chunk status keeps the valid snapshot');
  }
});

test('failed Apply stays local and retry is one call; busy/pending/closed states never apply', () => {
  const h = make(); const p = h.page; p.openWorldSettings();
  tap(p, 'display', 12);
  const setter = h.native.setChunkRanges;
  h.native.setChunkRanges = (...args) => { h.calls.push(['ranges', ...args]); return false; };
  p.applyChunkRanges();
  assert.deepEqual(mutations(h), [['ranges', 12, 12]]);
  assert.match(p.settingsError, /设置失败/);
  assert.equal(p.worldStream.radius, 1);
  h.native.setChunkRanges = setter;
  for (const flag of ['worldBusy', 'chunkLoading', 'saveExitPending']) {
    p[flag] = true; p.applyChunkRanges(); tap(p, 'display', 2);
    assert.deepEqual(values(p), [12, 12]); p[flag] = false;
  }
  p.applyChunkRanges();
  assert.deepEqual(mutations(h), [['ranges', 12, 12], ['ranges', 12, 12]]);
  p.closeWorldSettings(); p.applyChunkRanges();
  assert.equal(mutations(h).length, 2);
  assert.equal(source.match(/worldRenderer\.setChunkRanges\(/g).length, 1, 'Only Apply calls the setter');
});

test('game tab switches creative/survival through native and shows the native result', () => {
  const h = make(); const p = h.page; p.openWorldSettings(); p.selectSettingsTab('game');
  assert.match(text(byClass(p, 'settings-mode-label')[0]), /当前：创造/);
  byClass(p, 'settings-mode-creative')[0].data.on.click();
  assert.equal(h.calls.filter(call => call[0] === 'mode').length, 0, 'Picking the current mode is a no-op');
  byClass(p, 'settings-mode-survival')[0].data.on.click();
  assert.deepEqual(h.calls.filter(call => call[0] === 'mode'), [['mode', 'survival']]);
  assert.equal(p.inventoryState.gameMode, 'survival');
  assert.match(text(byClass(p, 'settings-mode-label')[0]), /当前：生存/);
  assert.equal(byClass(p, 'settings-mode-survival')[0].data.class['settings-choice-selected'], true);
  h.native.setGameMode = () => { throw new Error('Game mode change rejected'); };
  byClass(p, 'settings-mode-creative')[0].data.on.click();
  assert.equal(p.inventoryState.gameMode, 'survival', 'A rejected switch changes nothing');
  assert.match(text(byClass(p, 'settings-game-error')[0]), /切换模式失败/);
});

test('game tab toggles KeepInventory via setWorldRule and says death is not implemented', () => {
  const h = make(); const p = h.page; p.openWorldSettings(); p.selectSettingsTab('game');
  assert.equal(text(byClass(p, 'settings-keep-toggle')[0]), '已开启');
  assert.match(text(byClass(p, 'settings-keep-note')[0]), /暂无玩家生命与死亡/);
  byClass(p, 'settings-keep-toggle')[0].data.on.click();
  assert.deepEqual(h.calls.filter(call => call[0] === 'rule'), [['rule', 'KeepInventory', false]]);
  assert.equal(text(byClass(p, 'settings-keep-toggle')[0]), '已关闭');
  byClass(p, 'settings-keep-toggle')[0].data.on.click();
  assert.equal(text(byClass(p, 'settings-keep-toggle')[0]), '已开启');
  p.worldRules = null;
  byClass(p, 'settings-keep-toggle')[0].data.on.click();
  assert.equal(h.calls.filter(call => call[0] === 'rule').length, 2, 'No rule report, no blind write');
  assert.equal(text(byClass(p, 'settings-keep-toggle')[0]), '--');
});

test('debug tab checkboxes choose HUD items; hidden lines vanish and free their touch area', () => {
  const h = make(); const p = h.page;
  p.fpsText = '29.5';
  assert.equal(p.hudLines.length, 4);
  assert.equal(p.hudHeight, 56);
  assert.match(p.hudLines[0], /FPS 29\.5/);
  assert.match(p.hudLines[1], /X 0\.0 {2}Y 65\.0 {2}Z 8\.0/);
  p.openWorldSettings(); p.selectSettingsTab('hud');
  const options = byClass(p, 'hud-option');
  assert.equal(options.length, 12);
  const labels = options.map(text);
  for (const name of ['FPS', 'XYZ 坐标', '群系', '游戏模式']) assert.ok(labels.includes(name), name);
  options[labels.indexOf('FPS')].data.on.click();
  assert.equal(p.hudLines[0].includes('FPS'), false);
  assert.equal(byClass(p, 'hud-check')[labels.indexOf('FPS')].data.class['hud-check-on'], false);
  byClass(p, 'hud-all-off')[0].data.on.click();
  assert.equal(p.hudLines.length, 0);
  assert.equal(p.hudHeight, 0);
  p.closeWorldSettings();
  assert.equal(byClass(p, 'world-hud').length, 0, 'No empty HUD box');
  p.toggleHudItem('xyz');
  assert.equal(p.hudLines.length, 1);
  assert.equal(p.hudHeight, 20);
  assert.equal(byClass(p, 'world-hud')[0].data.style.height, '20px');
  // A touch below the shrunk HUD now rotates the camera instead of being swallowed.
  p.beginNativeTouch();
  h.setTouches([{ id: 3, x: 200, y: 50 }]); p.pollNativeTouch();
  assert.equal(p.worldContacts['native:3'].role, 'look');
  h.setTouches([]); p.pollNativeTouch();
  h.setTouches([{ id: 4, x: 200, y: 20 }]); p.pollNativeTouch();
  assert.equal(p.worldContacts['native:4'].role, 'ignore', 'The visible HUD line still blocks');
  p.setAllHudItems(true);
  assert.equal(p.hudHeight, 56);
});

test('chat button opens a full-screen chat that keeps command output and blocks world input', () => {
  const h = make(); const p = h.page;
  byClass(p, 'chat-button')[0].data.on.click();
  assert.equal(p.chatPanelOpen, true);
  assert.equal(p.worldInputBlocked, true);
  assert.equal(byClass(p, 'chat-screen').length, 1);
  assert.match(text(byClass(p, 'chat-history')[0]), /暂无消息/);
  byClass(p, 'chat-quick')[1].data.on.click();
  assert.match(text(byClass(p, 'chat-history')[0]), /\/seed[\s\S]*世界种子: 42/);
  byClass(p, 'chat-quick')[0].data.on.click();
  assert.ok(p.chatHistory.length >= 8, 'Help lines stay in the history');
  for (let i = 0; i < 30; i += 1) p.submitChat('line ' + i);
  assert.equal(byClass(p, 'chat-history-line').length, 9);
  assert.match(text(byClass(p, 'chat-history-line')[8]), /line 29/);
  byClass(p, 'chat-page-up')[0].data.on.click();
  assert.match(text(byClass(p, 'chat-history-line')[8]), /line 21/);
  byClass(p, 'chat-page-down')[0].data.on.click();
  assert.match(text(byClass(p, 'chat-history-line')[8]), /line 29/);
  for (let i = 0; i < 100; i += 1) p.submitChat('spam ' + i);
  assert.ok(p.chatHistory.length <= 60, 'History is bounded');
  assert.equal(byClass(p, 'chat-log').length, 0, 'The fading overlay hides under the chat screen');
  const reply = byClass(p, 'chat-history-line').find(node => node.data.class && node.data.class['chat-history-reply'] !== undefined);
  assert.ok(reply, 'History lines carry reply/notice classes');
  byClass(p, 'screen-close')[0].data.on.click();
  assert.equal(p.chatPanelOpen, false);
  assert.equal(p.worldInputBlocked, false);
  assert.equal(byClass(p, 'chat-log').length, 1, 'Recent lines fade in the world view again');
  p.leaveWorldPreview();
  assert.equal(p.chatHistory.length, 0);
});

(async () => {
  const css = (await less.render(fs.readFileSync(path.join(pageDir, 'index.less'), 'utf8'),
    { filename: path.join(pageDir, 'index.less') })).css;
  const rule = selector => {
    // A selector may sit in a comma group (".a,\n.b {"); later rules win, so merge in order.
    const found = [...css.matchAll(new RegExp('\\.' + selector + '(?![\\w-])[^{}]*\\{([^}]+)\\}', 'g'))];
    assert.ok(found.length, selector + ' exists'); return found.map(match => match[1]).join(';');
  };
  const px = (selector, property) => {
    const all = [...rule(selector).matchAll(new RegExp('(?:^|[;\\s])' + property + ':\\s*(\\d+)px', 'g'))];
    assert.ok(all.length, selector + '.' + property); return Number(all[all.length - 1][1]);
  };
  const box = name => [px(name, 'left'), px(name, 'top'), px(name, 'width'), px(name, 'height')];
  const overlaps = (a, b) => a[0] < b[0] + b[2] && b[0] < a[0] + a[2] && a[1] < b[1] + b[3] && b[1] < a[1] + a[3];
  const inside = (b, w, hh) => b[0] >= 0 && b[1] >= 0 && b[0] + b[2] <= w && b[1] + b[3] <= hh;
  test('full-screen settings and chat fit 960x266 with no overlapping controls', () => {
    assert.deepEqual([px('settings-screen', 'width'), px('settings-screen', 'height')], [960, 266]);
    // Header: three tabs at 90 + i*112 (inline style), save/exit, close.
    const header = [[90, 5, 104, 30], [202, 5, 104, 30], [314, 5, 104, 30],
      box('settings-save-exit'), box('screen-close')];
    for (let i = 0; i < header.length; i += 1) {
      assert.ok(inside(header[i], 960, px('screen-header', 'height')), 'header item ' + i);
      for (let j = 0; j < i; j += 1) assert.equal(overlaps(header[i], header[j]), false, 'header ' + i + '/' + j);
    }
    // Chunk tab: 12 chips of 66+6 must fit the 936px row.
    assert.ok(12 * (px('chunk-chip', 'width') + px('chunk-chip', 'margin-right')) <= px('chunk-chip-row', 'width'));
    const bodyH = px('settings-body', 'height');
    const rows = [[12, 4, 936, 56], [12, 62, 936, 56], [12, px('settings-stream', 'top'), 936, 16],
      [12, px('settings-hint', 'top'), 936, 16], [12, px('settings-error', 'top'), 936, 16], box('settings-apply')];
    for (let i = 0; i < rows.length; i += 1) {
      assert.ok(inside(rows[i], 960, bodyH), 'chunk row ' + i);
      for (let j = 0; j < i; j += 1) assert.equal(overlaps(rows[i], rows[j]), false, 'chunk ' + i + '/' + j);
    }
    // Choice buttons take width/height from .settings-choice and position from their own class.
    const choice = name => [px(name, 'left'), px(name, 'top'), px('settings-choice', 'width'), px('settings-choice', 'height')];
    const game = [choice('settings-mode-creative'), choice('settings-mode-survival'), choice('settings-keep-toggle')];
    for (const b of game) assert.ok(inside(b, 960, bodyH));
    assert.equal(overlaps(game[0], game[1]), false);
    // HUD grid: 4 per row x 3 rows = 12 options above the two buttons.
    const per = px('hud-option', 'width') + px('hud-option', 'margin-right');
    assert.ok(4 * per <= px('hud-option-grid', 'width'));
    assert.ok(px('hud-option-grid', 'top') + 3 * (px('hud-option', 'height') + px('hud-option', 'margin-bottom')) <= px('hud-all-on', 'top'));
    assert.ok(px('hud-all-on', 'top') + px('hud-all-on', 'height') <= bodyH);
    assert.equal(overlaps(choice('hud-all-on'), choice('hud-all-off')), false);
    // Chat: history above, input + 3 quick (500 + i*92, 86 wide) + 2 page buttons.
    const page = name => [px(name, 'left'), px('chat-page', 'top'), px(name, 'width'), px('chat-page', 'height')];
    const chat = [box('chat-history'), box('chat-input'), [500, 222, 86, 36], [592, 222, 86, 36],
      [684, 222, 86, 36], page('chat-page-up'), page('chat-page-down')];
    chat[1][2] += px('chat-input', 'padding-left') + 2; // padding + border
    chat[0][2] += 4; chat[0][3] += 4; // padding, if the box model adds it
    for (let i = 0; i < chat.length; i += 1) {
      assert.ok(inside(chat[i], 960, 266), 'chat item ' + i);
      for (let j = 0; j < i; j += 1) assert.equal(overlaps(chat[i], chat[j]), false, 'chat ' + i + '/' + j);
    }
    assert.ok(px('chat-history', 'height') - 4 >= 9 * px('chat-history-line', 'height'), 'Nine lines fit the history box');
    assert.ok(css.indexOf('.chat-history-reply') > css.indexOf('.chat-history-line'), 'Reply colour wins');
  });
  console.log('RESULT ' + passed + ' UI chunk range checks passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
