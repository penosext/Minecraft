// Survival UI: death screen, respawn, hold-to-eat release and updated strings.
// Harness copied from ui-chunk-range-test.cjs. Run: node tests/ui-survival-test.cjs
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
  let survival = { health: 20, maxHealth: 20, food: 20, saturation: 5, air: 300, maxAir: 300, exhaustion: 0,
    dead: false, difficulty: 2, hurtTicks: 0, fireTicks: -20, armor: 0, eating: false, eatTicksLeft: 0 };
  const info = () => ({ player: { x: 0, y: 65, z: 8 }, size: [5120, 256, 5120], stream: { ...stream }, scene: scene(),
    survival: { ...survival } });
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
    placeWorld() { calls.push(['place']); return false; },
    eatHeld(held, pressed) { calls.push(['eat', held, pressed]); return held; },
    respawnPlayer() { calls.push(['respawn']); if (!survival.dead) return false; survival = { ...survival, dead: false, health: 20 }; return true; },
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
    setSurvival(value) { survival = { ...survival, ...value }; },
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


test('survival info from worldInfo is parsed and no death screen while alive', () => {
  const h = make(); const p = h.page;
  p.refreshWorldInfo();
  assert.equal(p.worldInfoError, '');
  assert.equal(p.survival.health, 20);
  assert.equal(p.playerDead, false);
  assert.equal(byClass(p, 'death-screen').length, 0);
});

test('death shows the respawn screen and blocks world input; respawn clears it', () => {
  const h = make(); const p = h.page;
  h.setSurvival({ dead: true, health: 0 }); p.refreshWorldInfo();
  assert.equal(p.playerDead, true);
  assert.equal(p.worldInputBlocked, true);
  const screen = byClass(p, 'death-screen');
  assert.equal(screen.length, 1);
  assert.match(text(screen[0]), /你死了/);
  assert.match(text(screen[0]), /重生/);
  p.respawn();
  assert.ok(h.calls.some(call => call[0] === 'respawn'));
  assert.equal(p.respawnError, '');
  assert.equal(p.playerDead, false);
  assert.equal(byClass(p, 'death-screen').length, 0);
  p.respawn(); // not dead anymore: native refuses, error is shown, nothing breaks
  assert.match(p.respawnError, /重生失败/);
});

test('right button falls back to eatHeld after placement and releases it', () => {
  const h = make(); const p = h.page;
  p.secondaryHeld = true; p.nextSecondaryAt = 0;
  p.tickSecondary(1e9);
  const first = h.calls.filter(call => call[0] === 'eat');
  assert.deepEqual(first, [['eat', true, true]]);
  p.nextSecondaryAt = 0; p.tickSecondary(2e9);
  assert.deepEqual(h.calls.filter(call => call[0] === 'eat').slice(-1), [['eat', true, false]]);
  p.setSecondary(false);
  assert.deepEqual(h.calls.filter(call => call[0] === 'eat').slice(-1), [['eat', false, false]]);
});

test('keepInventory note no longer claims survival is missing', () => {
  const h = make(); const p = h.page;
  p.refreshWorldInfo();
  assert.doesNotMatch(p.keepInventoryNote, /暂无玩家生命/);
  assert.match(p.keepInventoryNote, /死亡/);
});

console.log('RESULT ' + passed + ' UI survival checks passed');
