// Chat and command regression. Every command has both a success and a failure
// path here, and the central rule is that nothing may report a result it did
// not actually get from a native bridge: an unknown command is rejected, a bad
// argument gets its usage line, and a missing bridge says it is missing.
// Reuses the real-TS/VM mock pattern from tests/ui-streaming-live-test.cjs.
// Run: node tests/ui-chat-test.cjs (no native/generated files are loaded or written).
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
const utilsDir = path.join(root, 'ui/src/utils');
const parsed = compiler['vue-template-compiler'].parse(fs.readFileSync(path.join(pageDir, 'index.vue'), 'utf8'));
assert.deepEqual(parsed.errors, []);
const templateSource = parsed.descriptor.template.content;
const template = compiler['falcon-template-compiler'].compile(templateSource, { preserveWhitespace: false });
assert.deepEqual(template.errors, []);
assert.deepEqual(template.tips, []);
const renderTemplate = new Function(template.render);
const compiledModules = new Map();
let passed = 0;
function test(name, run) { run(); passed += 1; console.log('PASS ' + name); }

// Canonical native rule names and their enforcement status, exactly as
// mcworld::WorldRules reports them (WorldRules.cpp kRules). Six of the ten are
// stored-but-not-enforced, which a /gamerule query has to state honestly.
const RULES = {
  DoDaylightCycle: 'enforced',
  DoTileDrops: 'exported-query-caller-enforces',
  DoMobLoot: 'stored-but-not-enforced',
  DoMobSpawning: 'enforced',
  KeepInventory: 'exported-query-caller-enforces',
  NaturalRegeneration: 'stored-but-not-enforced',
  FireSpreads: 'stored-but-not-enforced',
  MobGriefing: 'stored-but-not-enforced',
  TNT: 'stored-but-not-enforced',
  CheatsEnabled: 'stored-but-not-enforced',
};
const STORED_ONLY = Object.keys(RULES).filter(name => RULES[name] === 'stored-but-not-enforced');

function makeGlobalModule() {
  const handlers = [];
  let nextUuid = 1;
  const gm = {
    startTextEdit() { return 'uuid-' + nextUuid++; },
    closeTextEdit() {},
    textEditFinished: {
      on(handler) { handlers.push(handler); },
      off(handler) { const at = handlers.indexOf(handler); if (at !== -1) handlers.splice(at, 1); },
    },
  };
  return {
    gm,
    emit(uuid, payload) {
      const json = typeof payload === 'string' ? payload : JSON.stringify(payload);
      for (const handler of handlers.slice()) handler(uuid, json);
    },
  };
}

function make(options = {}) {
  let now = 1000;
  let nextTimer = 1;
  const timers = new Map();
  const calls = [];
  const host = makeGlobalModule();
  // Mutable native state: the mocks behave like the real bridges, so the UI
  // can only ever report what this state actually became.
  const state = {
    ticks: 100, frozen: 0, gameMode: options.gameMode || 'creative',
    player: { x: 10, y: 65, z: 12 },
    rules: Object.keys(RULES).reduce((all, name) => {
      all[name] = name !== 'CheatsEnabled';
      return all;
    }, {}),
  };
  const inventory = {
    selected: 0, emptyHand: false,
    get gameMode() { return state.gameMode; },
    slots: Array.from({ length: 36 }, (_, slot) => ({
      key: slot === 0 ? 'item:1:0' : '', count: slot === 0 ? 3 : 0,
    })),
  };
  const stream = { radius: 1, loaded: 9, queued: 0, target: 9, totalGenerated: 9 };
  // phase/daylight are derived from the celestial clock, as native does; the
  // exact easing is native's business, so a plain fraction is enough here.
  const scene = () => {
    const dayTicks = state.ticks - state.frozen;
    const phase = (dayTicks % 24000) / 24000;
    return {
      ticks: state.ticks, dayTicks, phase,
      daylight: phase < 0.25 || phase >= 0.75 ? 1 : 0.2, mobs: 2,
      rules: { version: 1, rules: { ...state.rules }, enforcement: { ...RULES } },
    };
  };
  const info = () => ({
    player: { ...state.player }, size: [5120, 256, 5120], stream: { ...stream }, scene: scene(),
  });
  const inventoryJson = () => JSON.stringify({ ...inventory, gameMode: state.gameMode });
  const native = {
    render() { return true; },
    renderWorld() { return true; },
    chunkLoadStatus() { return JSON.stringify({ busy: false, loading: false, error: '', stream }); },
    worldInfo() { return JSON.stringify(info()); },
    targetInfo() { return JSON.stringify({}); },
    mineWorld() { return true; },
    placeWorld() { return true; },
    inventoryInfo() { return inventoryJson(); },
    inventoryOpen() { return true; },
    selectInventory(slot) { inventory.selected = slot; return inventoryJson(); },
    inventoryTransfer() { return inventoryJson(); },
    setRenderRadius() { return true; },
    setChunkRanges() { return true; },
    saveWorld() { return true; },
    beginTouch() { return true; },
    pollTouch() { return JSON.stringify({ available: true, touches: [] }); },
    stopTouch() { return true; },
    beginCreateWorld() { return true; },
    beginOpenWorld() { return true; },
    worldJobStatus() {
      return JSON.stringify({ state: 'running', stage: 'generate', progress: 10, loadedChunks: 1, totalChunks: 9 });
    },
    cancelWorldJob() { return true; },
    finishWorldJob() { return JSON.stringify({ ...info(), path: '/tmp/mc/live', seed: 42 }); },
    // --- command bridges, modelled on the real native contracts -----------
    setGameMode(mode) {
      calls.push(['setGameMode', mode]);
      if (mode !== 'creative' && mode !== 'survival') throw new Error('Invalid game mode');
      state.gameMode = mode;
      return inventoryJson();
    },
    // Scene::setDayTicks: absolute celestial tick setter, answers with the
    // scene-bearing world snapshot so the HUD updates from one source.
    setSceneTime(ticks) {
      calls.push(['setSceneTime', ticks]);
      if (!Number.isSafeInteger(ticks) || ticks < 0) throw new Error('Invalid tick count');
      state.frozen = state.ticks - ticks;
      return JSON.stringify(info());
    },
    // Scene::setRule, answering with the same snapshot.
    setWorldRule(rule, value) {
      calls.push(['setWorldRule', rule, value]);
      if (!(rule in state.rules)) throw new Error('Unknown rule');
      if (typeof value !== 'boolean') throw new Error('Rule value must be boolean');
      state.rules[rule] = value;
      return JSON.stringify(info());
    },
    teleportPlayer(x, y, z) {
      calls.push(['teleportPlayer', x, y, z]);
      if (![x, y, z].every(value => typeof value === 'number' && isFinite(value))) {
        throw new Error('Invalid destination');
      }
      // Native owns the landing spot: y snaps to the ground here, so the UI
      // can only pass the test if it reports the value it read back.
      state.player = { x, y: Math.floor(y), z };
      return JSON.stringify(info());
    },
    setCreativeItem(slot, itemIndex) {
      calls.push(['setCreativeItem', slot, itemIndex]);
      if (state.gameMode !== 'creative') throw new Error('Creative item selection failed');
      if (!Number.isInteger(slot) || slot < 0 || slot > 8) throw new Error('Invalid creative slot');
      // Native decides the count it places; the UI must report this, not the
      // number the player typed.
      inventory.slots[slot] = { key: 'item:' + itemIndex, count: 64 };
      return inventoryJson();
    },
  };
  for (const name of options.withoutBridges || []) delete native[name];
  const context = vm.createContext({
    console: { error() {}, log() {} }, Date, performance: { now: () => now },
    setTimeout(fn, delay) { const id = nextTimer++; timers.set(id, { fn, delay, due: now + delay }); return id; },
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
      if (name === 'global') return { default: { Global: function () { return host.gm; } } };
      assert.ok(/^\.\.?\//.test(name), 'Unexpected import ' + name);
      return load(path.resolve(path.dirname(filename), name + '.ts'));
    }, module, module.exports);
    return module.exports;
  }
  load(path.join(utilsDir, 'keyboard.ts'));
  const indexModule = load(path.join(pageDir, 'index.ts'));
  const def = indexModule.default;
  const page = def.data();
  for (const [key, fn] of Object.entries(def.methods)) page[key] = fn.bind(page);
  for (const [key, fn] of Object.entries(def.computed)) Object.defineProperty(page, key, { get: fn.bind(page) });
  page.$refs = { sf: { getSurface: () => 1 } };
  page.pageMounted = true;
  page.pageVisible = true;
  const h = {
    page, native, calls, state, host, inventory, timers,
    mount() { def.mounted.call(page); },
    enter() {
      page.enterWorld({ ...info(), path: '/tmp/mc/live', seed: 42 }, false);
      page.refreshInventory();
      page.refreshWorldInfo();
    },
    advance(ms) { now += ms; },
    get now() { return now; },
    // The command path exactly as a confirmed keyboard edit drives it.
    // Lines are identified by their sequence id, because the log array trims
    // itself and an index would go stale.
    say(text) {
      const before = page.chatSequence;
      page.openChatKeyboard();
      host.emit(page.chatEdit.taskUuid, { editConfirmed: true, text });
      return page.chatLog.filter(entry => entry.id > before);
    },
    // The last reply/notice line, which is the command's own answer.
    reply(text) {
      const lines = h.say(text);
      const answers = lines.filter(entry => entry.kind !== 'chat');
      assert.ok(answers.length > 0, 'Command produced an answer: ' + text);
      return answers[answers.length - 1];
    },
  };
  return h;
}

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

function ready(options) {
  const h = make(options);
  h.mount();
  h.enter();
  return h;
}

// --- Chat vs command -------------------------------------------------------

test('text without a leading slash is local chat and is never sent anywhere', () => {
  const h = ready();
  const lines = h.say('hello world');
  assert.equal(lines.length, 1, 'Plain chat produces one line and no command reply');
  assert.equal(lines[0].kind, 'chat');
  assert.ok(lines[0].text.includes('hello world'));
  assert.ok(lines[0].text.includes('仅本地显示'), 'The echo states it is local only');
  // Nothing may be interpreted as a command.
  assert.equal(h.calls.length, 0, 'Plain chat calls no native bridge');
});

test('a leading slash is a command, and a slash inside the text is not', () => {
  const h = ready();
  const command = h.say('/seed');
  assert.equal(command.length, 2, 'A command echoes the input and one reply');
  assert.equal(command[0].kind, 'chat');
  assert.equal(command[0].text, '/seed');
  assert.equal(command[1].kind, 'reply');
  assert.ok(command[1].text.includes('42'));
  const chat = h.say('look at my/house');
  assert.equal(chat.length, 1);
  assert.equal(chat[0].kind, 'chat');
  assert.ok(chat[0].text.includes('仅本地显示'));
});

test('empty and whitespace-only input is dropped entirely', () => {
  const h = ready();
  for (const text of ['', '   ', '\n', ' \n ']) {
    const before = h.page.chatLog.length;
    h.page.submitChat(text);
    assert.equal(h.page.chatLog.length, before, 'Dropped: ' + JSON.stringify(text));
  }
  // A bare slash is a command with no name, so it is rejected, not dropped.
  const lines = h.say('/');
  assert.equal(lines[lines.length - 1].kind, 'notice');
  assert.ok(lines[lines.length - 1].text.includes('未知指令'));
});

test('an unknown command is refused and never reaches a native bridge', () => {
  const h = ready();
  for (const text of ['/fly', '/kill', '/weather clear', '/op me', '/summon Pig']) {
    const answer = h.reply(text);
    assert.equal(answer.kind, 'notice', text + ' is refused');
    assert.ok(answer.text.includes('未知指令'), text + ' says 未知指令');
    assert.ok(answer.text.includes('/help'), text + ' points at /help');
  }
  assert.equal(h.calls.length, 0, 'No unknown command touched native');
});

// --- /help and /seed -------------------------------------------------------

test('/help lists only the commands that really work', () => {
  const h = ready();
  const lines = h.say('/help');
  const text = lines.filter(entry => entry.kind === 'reply').map(entry => entry.text).join('\n');
  for (const name of ['/help', '/seed', '/time', '/gamemode', '/give', '/tp', '/gamerule']) {
    assert.ok(text.includes(name), '/help mentions ' + name);
  }
  // Nothing that is not implemented may be advertised.
  for (const absent of ['/fly', '/kill', '/weather', '/summon', '/effect', '/xp', '/clear', '/difficulty']) {
    assert.equal(text.includes(absent), false, '/help must not advertise ' + absent);
  }
  // Every advertised command must actually be dispatched, not just listed.
  for (const name of ['help', 'seed', 'time', 'gamemode', 'give', 'tp', 'gamerule']) {
    const outcome = h.page.runCommand(name);
    assert.equal(/未知指令/.test(outcome.text), false, '/' + name + ' is dispatched');
  }
});

test('/seed reports the world seed and refuses outside a world and with arguments', () => {
  const h = ready();
  assert.ok(h.reply('/seed').text.includes('42'));
  assert.equal(h.reply('/seed 1').kind, 'notice');
  assert.ok(h.reply('/seed 1').text.includes('用法: /seed'));
  // Preview mode has no world and therefore no seed.
  const preview = make();
  preview.mount();
  const outcome = preview.page.runCommand('seed');
  assert.equal(outcome.ok, false);
  assert.ok(outcome.text.includes('需要先进入世界'));
});

// --- /time -----------------------------------------------------------------

test('/time set day and night reach native with the original tick marks', () => {
  const h = ready();
  const day = h.reply('/time set day');
  assert.equal(day.kind, 'reply');
  assert.deepEqual(h.calls.filter(call => call[0] === 'setSceneTime'), [['setSceneTime', 1000]]);
  assert.ok(day.text.includes('1000'));
  const night = h.reply('/time set night');
  assert.equal(night.kind, 'reply');
  assert.deepEqual(h.calls.filter(call => call[0] === 'setSceneTime').pop(), ['setSceneTime', 13000]);
  assert.ok(night.text.includes('13000'));
  // The HUD now reads the value native reported, not a local guess.
  assert.equal(h.page.worldScene.dayTicks, 13000);
  assert.ok(h.page.sceneStatus.includes('夜'), 'The scene line follows the new time');
  assert.equal(h.page.worldDirty, true);
});

test('/time set accepts explicit ticks and rejects every malformed value', () => {
  const h = ready();
  assert.ok(h.reply('/time set 0').text.includes('0'));
  assert.deepEqual(h.calls.filter(call => call[0] === 'setSceneTime').pop(), ['setSceneTime', 0]);
  h.reply('/time set 17000');
  assert.deepEqual(h.calls.filter(call => call[0] === 'setSceneTime').pop(), ['setSceneTime', 17000]);
  const before = h.calls.filter(call => call[0] === 'setSceneTime').length;
  for (const text of ['/time set -1', '/time set 1.5', '/time set abc', '/time set 0x10',
    '/time set 1e3', '/time set', '/time set day night', '/time', '/time nonsense', '/time query extra']) {
    const answer = h.reply(text);
    assert.equal(answer.kind, 'notice', text + ' is refused');
    assert.ok(/用法|ticks 须为/.test(answer.text), text + ' explains itself: ' + answer.text);
  }
  assert.equal(h.calls.filter(call => call[0] === 'setSceneTime').length, before,
    'No malformed /time reached native');
});

test('/time query reports the celestial clock, which a frozen cycle holds still', () => {
  const h = ready();
  assert.ok(h.reply('/time query').text.includes('100'));
  h.reply('/time set 6000');
  const answer = h.reply('/time query');
  assert.ok(answer.text.includes('6000'), 'query follows a set: ' + answer.text);
  // dayTicks and ticks diverge once the cycle is frozen; query must report the
  // celestial value, which is what the sky actually uses.
  h.state.ticks += 500;
  h.page.refreshWorldInfo();
  assert.equal(h.page.worldScene.ticks, 600);
  assert.equal(h.page.worldScene.dayTicks, 6500);
  assert.ok(h.reply('/time query').text.includes('6500'));
});

test('/time refuses to run while the world is busy and when the bridge is absent', () => {
  const h = ready();
  h.page.chunkLoading = true;
  // The chat button itself is refused while a job owns the world, so the guard
  // inside the command is checked directly.
  const busy = h.page.runCommand('time set day');
  assert.equal(busy.ok, false);
  assert.ok(busy.text.includes('世界忙'));
  assert.equal(h.calls.filter(call => call[0] === 'setSceneTime').length, 0);
  h.page.chunkLoading = false;
  const bare = ready({ withoutBridges: ['setSceneTime'] });
  const missing = bare.reply('/time set day');
  assert.equal(missing.kind, 'notice');
  assert.ok(missing.text.includes('指令未接入'), 'A missing bridge says so: ' + missing.text);
  assert.ok(missing.text.includes('setSceneTime'));
  // A query still works without the setter.
  assert.equal(bare.reply('/time query').kind, 'reply');
});

test('a native /time failure is reported, never swallowed as success', () => {
  const h = ready();
  h.native.setSceneTime = () => { throw new Error('scene rejected'); };
  const answer = h.reply('/time set day');
  assert.equal(answer.kind, 'notice');
  assert.ok(answer.text.includes('设置时间失败'));
  assert.ok(answer.text.includes('scene rejected'));
  // An invalid native reply is also a failure, not a silent accept.
  h.native.setSceneTime = () => JSON.stringify({ player: { x: 'bad' }, size: [1, 1, 1] });
  const invalid = h.reply('/time set night');
  assert.equal(invalid.kind, 'notice');
  assert.ok(invalid.text.includes('设置时间失败'));
});

// --- /gamemode -------------------------------------------------------------

test('/gamemode switches the native mode in both directions', () => {
  const h = ready();
  const survival = h.reply('/gamemode survival');
  assert.equal(survival.kind, 'reply');
  assert.ok(survival.text.includes('生存'));
  assert.deepEqual(h.calls.filter(call => call[0] === 'setGameMode'), [['setGameMode', 'survival']]);
  assert.equal(h.page.inventoryState.gameMode, 'survival');
  const creative = h.reply('/gamemode creative');
  assert.equal(creative.kind, 'reply');
  assert.ok(creative.text.includes('创造'));
  assert.equal(h.page.inventoryState.gameMode, 'creative');
  assert.equal(h.page.worldDirty, true);
});

test('/gamemode rejects bad arguments and a missing bridge without calling native', () => {
  const h = ready();
  for (const text of ['/gamemode', '/gamemode adventure', '/gamemode 1',
    '/gamemode creative survival', '/gamemode spectator']) {
    const answer = h.reply(text);
    assert.equal(answer.kind, 'notice', text + ' is refused');
    assert.ok(/用法|模式须为/.test(answer.text), text + ': ' + answer.text);
  }
  assert.equal(h.calls.filter(call => call[0] === 'setGameMode').length, 0);
  const bare = ready({ withoutBridges: ['setGameMode'] });
  const missing = bare.reply('/gamemode creative');
  assert.equal(missing.kind, 'notice');
  assert.ok(missing.text.includes('指令未接入'));
  // A native refusal surfaces as a failure.
  const h2 = ready();
  h2.native.setGameMode = () => { throw new Error('mode change rejected'); };
  const failed = h2.reply('/gamemode survival');
  assert.equal(failed.kind, 'notice');
  assert.ok(failed.text.includes('切换模式失败'));
});

// --- /give -----------------------------------------------------------------

test('/give accepts the item key, the English catalog key and the Chinese name', () => {
  for (const name of ['item:1:0', 'stone', 'STONE', '石头']) {
    const h = ready();
    const answer = h.reply('/give ' + name);
    assert.equal(answer.kind, 'reply', name + ': ' + answer.text);
    const give = h.calls.filter(call => call[0] === 'setCreativeItem').pop();
    assert.deepEqual(give, ['setCreativeItem', 0, 0], name + ' resolves to itemIndex 0');
    assert.ok(answer.text.includes('石头'), name + ' names the item: ' + answer.text);
  }
});

test('/give reports the count native placed, not the count that was asked for', () => {
  const h = ready();
  // The mock places a full stack whatever the request is, like a creative pick.
  const answer = h.reply('/give stone 5');
  assert.equal(answer.kind, 'reply');
  assert.ok(answer.text.includes('64'), 'The reported count is native\'s: ' + answer.text);
  assert.equal(answer.text.includes('×5'), false, 'The requested count is never reported as placed');
  // And the mismatch is stated rather than left looking accepted.
  assert.ok(answer.text.includes('未按 5 计'), 'The ignored count is admitted: ' + answer.text);
  assert.equal(h.page.worldDirty, true);
  // With no count argument there is nothing to disclaim.
  assert.equal(h.reply('/give stone').text.includes('未按'), false);
});

test('/give refuses survival mode before calling the creative bridge', () => {
  const h = ready({ gameMode: 'survival' });
  const answer = h.reply('/give stone');
  assert.equal(answer.kind, 'notice');
  assert.ok(answer.text.includes('生存模式不能使用 /give'), answer.text);
  assert.equal(h.calls.filter(call => call[0] === 'setCreativeItem').length, 0,
    'Survival never reaches setCreativeItem');
  // Switching to creative makes the same command work.
  h.reply('/gamemode creative');
  assert.equal(h.reply('/give stone').kind, 'reply');
  assert.equal(h.calls.filter(call => call[0] === 'setCreativeItem').length, 1);
});

test('/give rejects unknown items and bad counts', () => {
  const h = ready();
  const unknown = h.reply('/give nonexistent_block');
  assert.equal(unknown.kind, 'notice');
  assert.ok(unknown.text.includes('未知物品'), unknown.text);
  for (const text of ['/give', '/give stone 0', '/give stone -1', '/give stone 65',
    '/give stone 1.5', '/give stone abc', '/give stone 1 2']) {
    const answer = h.reply(text);
    assert.equal(answer.kind, 'notice', text + ' is refused');
    assert.ok(/用法|数量须为/.test(answer.text), text + ': ' + answer.text);
  }
  assert.equal(h.calls.filter(call => call[0] === 'setCreativeItem').length, 0);
  // A stack limit smaller than the request is refused with the real limit.
  const limited = ready();
  const item = limited.page.creativeItems.find(entry => entry.maxStack === 1);
  if (item) {
    const answer = limited.reply('/give ' + item.key + ' 2');
    assert.equal(answer.kind, 'notice');
    assert.ok(answer.text.includes('单格最多 1'), answer.text);
  }
  const bare = ready({ withoutBridges: ['setCreativeItem'] });
  assert.ok(bare.reply('/give stone').text.includes('指令未接入'));
});

// --- /tp -------------------------------------------------------------------

test('/tp teleports and reports the position native read back', () => {
  const h = ready();
  const answer = h.reply('/tp 100 70.5 200');
  assert.equal(answer.kind, 'reply');
  assert.deepEqual(h.calls.filter(call => call[0] === 'teleportPlayer'), [['teleportPlayer', 100, 70.5, 200]]);
  // The mock snaps y to the ground; the reply must show 70.0, not 70.5.
  assert.equal(h.page.playerY, 70);
  assert.ok(answer.text.includes('70.0'), 'The reply shows the native position: ' + answer.text);
  assert.equal(answer.text.includes('70.5'), false, 'The requested y is never reported as final');
  assert.equal(h.page.playerX, 100);
  assert.equal(h.page.playerZ, 200);
  assert.equal(h.page.worldDirty, true);
});

test('/tp validates the world bounds on every axis before calling native', () => {
  const h = ready();
  // size is [5120, 256, 5120] in native width/height/depth order.
  for (const text of ['/tp -1 64 0', '/tp 0 -1 0', '/tp 0 64 -1',
    '/tp 5120 64 0', '/tp 0 256 0', '/tp 0 64 5120', '/tp 99999 64 0']) {
    const answer = h.reply(text);
    assert.equal(answer.kind, 'notice', text + ' is refused');
    assert.ok(answer.text.includes('超出世界范围'), text + ': ' + answer.text);
  }
  assert.equal(h.calls.filter(call => call[0] === 'teleportPlayer').length, 0,
    'No out-of-bounds destination reached native');
  // The inclusive edges are accepted.
  assert.equal(h.reply('/tp 0 0 0').kind, 'reply');
  assert.equal(h.reply('/tp 5119 255 5119').kind, 'reply');
});

test('/tp rejects malformed coordinates and a missing bridge', () => {
  const h = ready();
  for (const text of ['/tp', '/tp 1', '/tp 1 2', '/tp 1 2 3 4', '/tp a b c',
    '/tp 1 2 three', '/tp NaN 2 3', '/tp Infinity 2 3', '/tp 1e2 2 3', '/tp 0x10 2 3', '/tp ~ ~ ~']) {
    const answer = h.reply(text);
    assert.equal(answer.kind, 'notice', text + ' is refused');
    assert.ok(/用法|坐标须为/.test(answer.text), text + ': ' + answer.text);
  }
  assert.equal(h.calls.filter(call => call[0] === 'teleportPlayer').length, 0);
  const bare = ready({ withoutBridges: ['teleportPlayer'] });
  const missing = bare.reply('/tp 10 64 10');
  assert.ok(missing.text.includes('指令未接入'));
  assert.ok(missing.text.includes('teleportPlayer'));
  // A native failure is reported as a failure.
  const h2 = ready();
  h2.native.teleportPlayer = () => { throw new Error('no ground there'); };
  const failed = h2.reply('/tp 10 64 10');
  assert.equal(failed.kind, 'notice');
  assert.ok(failed.text.includes('传送失败'));
});

// --- /gamerule -------------------------------------------------------------

test('/gamerule sets every one of the ten native rules', () => {
  const h = ready();
  for (const rule of Object.keys(RULES)) {
    const answer = h.reply('/gamerule ' + rule + ' false');
    assert.equal(answer.kind, 'reply', rule + ': ' + answer.text);
    assert.deepEqual(h.calls.filter(call => call[0] === 'setWorldRule').pop(),
      ['setWorldRule', rule, false]);
    assert.equal(h.state.rules[rule], false, rule + ' really changed natively');
    assert.ok(answer.text.includes(rule));
    assert.ok(answer.text.includes('false'));
    const back = h.reply('/gamerule ' + rule + ' true');
    assert.equal(back.kind, 'reply');
    assert.equal(h.state.rules[rule], true);
  }
});

test('/gamerule query states the real value and the real enforcement status', () => {
  const h = ready();
  // Six of ten are stored-only; a query that hid that would be misleading.
  for (const rule of STORED_ONLY) {
    const answer = h.reply('/gamerule ' + rule);
    assert.equal(answer.kind, 'reply');
    assert.ok(answer.text.includes('仅保存未生效'),
      rule + ' must be shown as stored-only: ' + answer.text);
  }
  assert.ok(h.reply('/gamerule DoDaylightCycle').text.includes('已生效'));
  assert.ok(h.reply('/gamerule DoMobSpawning').text.includes('已生效'));
  assert.ok(h.reply('/gamerule DoTileDrops').text.includes('已导出给玩法层'));
  assert.ok(h.reply('/gamerule KeepInventory').text.includes('已导出给玩法层'));
  // A query reports the current value, and tracks a change.
  assert.ok(h.reply('/gamerule CheatsEnabled').text.includes('false'), 'CheatsEnabled defaults off');
  h.reply('/gamerule CheatsEnabled true');
  assert.ok(h.reply('/gamerule CheatsEnabled').text.includes('true'));
  // A query never writes.
  const before = h.calls.filter(call => call[0] === 'setWorldRule').length;
  h.reply('/gamerule TNT');
  assert.equal(h.calls.filter(call => call[0] === 'setWorldRule').length, before);
  // Setting a rule keeps stating its enforcement status too.
  assert.ok(h.reply('/gamerule TNT false').text.includes('仅保存未生效'));
});

test('/gamerule accepts a case-insensitive name but always uses the canonical one', () => {
  const h = ready();
  const answer = h.reply('/gamerule dodaylightcycle false');
  assert.equal(answer.kind, 'reply');
  assert.deepEqual(h.calls.filter(call => call[0] === 'setWorldRule').pop(),
    ['setWorldRule', 'DoDaylightCycle', false], 'Native receives the canonical name');
  assert.ok(answer.text.includes('DoDaylightCycle'), 'The reply shows the canonical name');
});

test('/gamerule rejects unknown rules and non-boolean values', () => {
  const h = ready();
  const unknown = h.reply('/gamerule doFireTick false');
  assert.equal(unknown.kind, 'notice');
  assert.ok(unknown.text.includes('未知规则'), unknown.text);
  // The refusal lists what is actually available.
  for (const rule of Object.keys(RULES)) assert.ok(unknown.text.includes(rule), 'lists ' + rule);
  // 'True'/'FALSE' are accepted, like the rule name, so only genuinely
  // non-boolean values are listed here.
  assert.equal(h.reply('/gamerule TNT True').kind, 'reply');
  assert.equal(h.reply('/gamerule TNT FALSE').kind, 'reply');
  const writes = h.calls.filter(call => call[0] === 'setWorldRule').length;
  for (const text of ['/gamerule', '/gamerule TNT yes', '/gamerule TNT 1',
    '/gamerule TNT on', '/gamerule TNT false extra']) {
    const answer = h.reply(text);
    assert.equal(answer.kind, 'notice', text + ' is refused');
    assert.ok(/用法|值须为|未知规则/.test(answer.text), text + ': ' + answer.text);
  }
  assert.equal(h.calls.filter(call => call[0] === 'setWorldRule').length, writes,
    'No malformed /gamerule reached native');
  const bare = ready({ withoutBridges: ['setWorldRule'] });
  const missing = bare.reply('/gamerule TNT false');
  assert.ok(missing.text.includes('指令未接入'));
  assert.ok(missing.text.includes('setWorldRule'));
  // A query still works without the setter, because it reads the scene report.
  assert.equal(bare.reply('/gamerule TNT').kind, 'reply');
  const h2 = ready();
  h2.native.setWorldRule = () => { throw new Error('rule rejected'); };
  const failed = h2.reply('/gamerule TNT false');
  assert.equal(failed.kind, 'notice');
  assert.ok(failed.text.includes('设置规则失败'));
});

test('a world snapshot without rules leaves a query honest instead of inventing one', () => {
  const h = ready();
  h.native.worldInfo = () => JSON.stringify({
    player: { x: 1, y: 2, z: 3 }, size: [5120, 256, 5120],
    scene: { ticks: 10, phase: 0.2, daylight: 1, mobs: 0 },
  });
  // enterWorld clears the cached report, so a rules-less world has none.
  h.page.enterWorld({ player: { x: 1, y: 2, z: 3 }, size: [5120, 256, 5120],
    scene: { ticks: 10, phase: 0.2, daylight: 1, mobs: 0 }, path: '/tmp/mc/live', seed: 7 }, false);
  h.page.refreshWorldInfo();
  assert.equal(h.page.worldInfoError, '', 'A rules-less snapshot is still valid');
  assert.equal(h.page.worldRules, null);
  const answer = h.reply('/gamerule TNT');
  assert.equal(answer.kind, 'notice');
  assert.ok(answer.text.includes('未就绪'), answer.text);
});

test('an invalid rules report is rejected rather than displayed', () => {
  const h = ready();
  h.native.worldInfo = () => JSON.stringify({
    player: { x: 1, y: 2, z: 3 }, size: [5120, 256, 5120],
    scene: { ticks: 10, phase: 0.2, daylight: 1, mobs: 0, rules: { rules: { TNT: 'yes' }, enforcement: {} } },
  });
  h.page.refreshWorldInfo();
  assert.ok(h.page.worldInfoError.includes('世界规则信息无效'), h.page.worldInfoError);
});

// --- Command guards --------------------------------------------------------

test('every world command refuses to run outside a world', () => {
  const h = make();
  h.mount();
  for (const text of ['time set day', 'gamemode creative', 'give stone', 'tp 1 2 3', 'gamerule TNT false']) {
    const outcome = h.page.runCommand(text);
    assert.equal(outcome.ok, false, '/' + text + ' is refused in preview mode');
    assert.ok(/世界忙或未进入|需要先进入世界/.test(outcome.text), text + ': ' + outcome.text);
  }
  assert.equal(h.calls.length, 0, 'No command touched native outside a world');
});

test('an exclusive job blocks every mutating command', () => {
  const h = ready();
  h.page.worldBusy = true;
  for (const text of ['time set day', 'gamemode creative', 'give stone', 'tp 1 2 3', 'gamerule TNT false']) {
    const outcome = h.page.runCommand(text);
    assert.equal(outcome.ok, false, '/' + text + ' is refused while busy');
    assert.ok(outcome.text.includes('世界忙'), text + ': ' + outcome.text);
  }
  assert.equal(h.calls.length, 0);
});

test('command names are case-insensitive but arguments stay meaningful', () => {
  const h = ready();
  assert.equal(h.reply('/SEED').kind, 'reply');
  assert.equal(h.reply('/Help').kind, 'reply');
  assert.equal(h.reply('/TIME QUERY').kind, 'reply');
  assert.equal(h.reply('/GameMode CREATIVE').kind, 'reply');
  // Extra whitespace between arguments is tolerated.
  assert.equal(h.reply('/tp   10    64    10').kind, 'reply');
  assert.deepEqual(h.calls.filter(call => call[0] === 'teleportPlayer').pop(), ['teleportPlayer', 10, 64, 10]);
});

// --- Chat log --------------------------------------------------------------

test('the log keeps only the most recent lines and fades them out', () => {
  const h = ready();
  for (let i = 0; i < 12; i += 1) h.say('line ' + i);
  assert.equal(h.page.visibleChat.length, 5, 'At most five lines are rendered');
  assert.ok(h.page.visibleChat[4].text.includes('line 11'), 'The newest line is last');
  assert.ok(h.page.visibleChat[0].text.includes('line 7'), 'Older lines are trimmed');
  assert.ok(h.page.chatLog.length <= 20, 'The backing array is bounded too');
  // Lines fade on their own after the timeout, with no timer involved.
  assert.equal(h.page.chatVisible, true);
  h.advance(9000);
  assert.equal(h.page.visibleChat.length, 0, 'Everything has faded');
  assert.equal(h.page.chatVisible, false, 'The whole overlay disappears');
  // A new line brings the overlay back without reviving the faded ones.
  h.say('fresh');
  assert.equal(h.page.visibleChat.length, 1);
  assert.ok(h.page.visibleChat[0].text.includes('fresh'));
});

test('the log is not red, is readable and distinguishes replies from refusals', () => {
  const h = ready();
  h.say('hello');
  h.say('/seed');
  h.say('/nonsense');
  const kinds = h.page.visibleChat.map(entry => entry.kind);
  assert.ok(kinds.includes('chat'));
  assert.ok(kinds.includes('reply'));
  assert.ok(kinds.includes('notice'));
  const lines = withClass(h.page, 'chat-line');
  assert.equal(lines.length, h.page.visibleChat.length);
  // Class bindings carry the kind, and the Less colours are not red.
  const classes = lines.map(node => Object.keys(node.data.class || {}).filter(key => node.data.class[key]));
  assert.ok(classes.some(list => list.includes('chat-line-reply')));
  assert.ok(classes.some(list => list.includes('chat-line-notice')));
  assert.ok(classes.some(list => list.length === 0), 'Plain chat has no modifier class');
  const css = fs.readFileSync(path.join(pageDir, 'index.less'), 'utf8');
  const chatBlock = css.slice(css.indexOf('.chat-line {'), css.indexOf('.seed-input-hit'));
  for (const red of ['#ffb3a3', 'rgba(100, 30, 20']) {
    assert.equal(chatBlock.includes(red), false, 'Chat must not use the error colour ' + red);
  }
  // Every rendered line carries its text.
  for (const node of lines) assert.ok(nodeText(node).length > 0);
});

test('chat is cleared when the world is left and when a new one is entered', () => {
  const h = ready();
  h.say('/seed');
  assert.ok(h.page.chatLog.length > 0);
  h.page.leaveWorldPreview();
  assert.equal(h.page.chatLog.length, 0, 'Leaving the world clears the log');
  assert.equal(h.page.worldRules, null);
  assert.equal(h.page.chatVisible, false);
  h.enter();
  h.say('/seed');
  assert.ok(h.page.chatLog.length > 0);
  h.page.enterWorld({ player: { x: 0, y: 1, z: 0 }, size: [864, 256, 864],
    scene: { ticks: 0, phase: 0, daylight: 1, mobs: 0 }, path: '/tmp/mc/other', seed: 9 }, true);
  assert.equal(h.page.chatLog.length, 0, 'A new world starts with an empty log');
});

test('a very long line is bounded and the log never grows without limit', () => {
  const h = ready();
  const long = 'x'.repeat(500);
  h.page.submitChat(long);
  assert.ok(h.page.chatLog[0].text.length <= 220, 'A long line is truncated');
  for (let i = 0; i < 200; i += 1) h.page.submitChat('spam ' + i);
  assert.ok(h.page.chatLog.length <= 20, 'The log array stays bounded: ' + h.page.chatLog.length);
  assert.equal(h.page.visibleChat.length, 5);
});

test('Falcon template and Less compile with the chat wiring intact', () => {
  let css = '';
  less.render(fs.readFileSync(path.join(pageDir, 'index.less'), 'utf8'),
    { filename: path.join(pageDir, 'index.less'), paths: [pageDir, path.join(root, 'ui/src/styles')] },
    (error, output) => { assert.equal(error, null); css = output.css; });
  for (const selector of ['.chat-button', '.chat-log', '.chat-line', '.chat-line-reply', '.chat-line-notice']) {
    assert.ok(css.includes(selector), 'Less emits ' + selector);
  }
  // chat-line-reply/notice must come after chat-line so they win the colour.
  assert.ok(css.indexOf('.chat-line-reply') > css.indexOf('.chat-line {'),
    'Modifier classes are declared after the base class');
  assert.ok(templateSource.includes('visibleChat'), 'The template renders the bounded slice');
  assert.ok(templateSource.includes('chatVisible'), 'The overlay is conditional');
  assert.ok(templateSource.includes('openChatKeyboard'));
  const h = ready();
  assert.equal(withClass(h.page, 'chat-log').length, 0, 'An empty log renders nothing');
  h.say('hello');
  assert.equal(withClass(h.page, 'chat-log').length, 1);
});

console.log('RESULT ' + passed + ' UI chat checks passed');
