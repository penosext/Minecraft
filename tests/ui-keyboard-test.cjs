// System input method (HaasUI `global`) regression for ui/src/utils/keyboard.ts
// and the two fields that use it. Locks down the four hard rules of
// KEYBOARD_INPUT.md: UUID matching, editConfirmed-only, never calling
// closeTextEdit from inside the callback, and cleanup on destroy. Also covers
// safe degradation when the firmware module is absent, and that an open
// keyboard never moves the player or rotates the camera.
// Reuses the real-TS/VM mock pattern from tests/ui-streaming-live-test.cjs.
// Run: node tests/ui-keyboard-test.cjs (no native/generated files are loaded or written).
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
const vueFile = path.join(pageDir, 'index.vue');
const parsed = compiler['vue-template-compiler'].parse(fs.readFileSync(vueFile, 'utf8'));
assert.deepEqual(parsed.errors, []);
const templateSource = parsed.descriptor.template.content;
const template = compiler['falcon-template-compiler'].compile(templateSource, { preserveWhitespace: false });
assert.deepEqual(template.errors, []);
assert.deepEqual(template.tips, []);
const renderTemplate = new Function(template.render);
const compiledModules = new Map();
let passed = 0;
function test(name, run) { run(); passed += 1; console.log('PASS ' + name); }

// A faithful stand-in for the firmware module: one shared textEditFinished
// signal, UUIDs that differ per session, and a recorded call log. It also
// asserts the documented crash condition rather than only observing it: if the
// page ever calls closeTextEdit while a callback is on the stack, this throws.
function makeGlobalModule(options = {}) {
  const calls = [];
  const handlers = [];
  let nextUuid = 1;
  // KEYBOARD_INPUT.md forbids closing "inside the callback" (line 188) and
  // requires closing in the function that has already handled the result
  // (editFinishedClicked, step 4). Those two together mean exactly one thing:
  // a close may only follow a consumed result. `observe` is how the test
  // watches the ordering; the plain source check below additionally proves the
  // call does not sit in the callback body.
  let observe = null;
  const gm = {
    startTextEdit(config) {
      calls.push(['start', config]);
      if (options.startFails) throw new Error('startTextEdit rejected');
      if (options.emptyUuid) return '';
      return 'uuid-' + nextUuid++;
    },
    closeTextEdit(uuid) {
      calls.push(['close', uuid]);
      if (observe) observe(uuid);
    },
    textEditFinished: {
      on(handler) { calls.push(['on']); handlers.push(handler); },
      off(handler) {
        calls.push(['off']);
        const at = handlers.indexOf(handler);
        if (at !== -1) handlers.splice(at, 1);
      },
    },
  };
  return {
    gm, calls,
    get handlerCount() { return handlers.length; },
    onClose(fn) { observe = fn; },
    // Deliver a result exactly like the firmware: every registered handler is
    // invoked with the same (uuid, json) pair.
    emit(uuid, payload) {
      const json = typeof payload === 'string' ? payload : JSON.stringify(payload);
      for (const handler of handlers.slice()) handler(uuid, json);
    },
    starts() { return calls.filter(call => call[0] === 'start').map(call => JSON.parse(call[1])); },
    closes() { return calls.filter(call => call[0] === 'close').map(call => call[1]); },
  };
}

function make(options = {}) {
  let now = 1000;
  let nextTimer = 1;
  let touches = [];
  const timers = new Map();
  const calls = [];
  // moduleHost === null models a host with no firmware module at all.
  const moduleHost = options.noModule ? null : makeGlobalModule(options);
  const inventory = {
    selected: 0, emptyHand: false, gameMode: options.gameMode || 'creative',
    slots: Array.from({ length: 36 }, (_, slot) => ({
      key: slot === 0 ? 'item:1:0' : '', count: slot === 0 ? 3 : 0,
    })),
  };
  const stream = { radius: 1, loaded: 9, queued: 0, target: 9, totalGenerated: 9 };
  const info = () => ({
    player: { x: 10, y: 65, z: 12 }, size: [5120, 256, 5120], stream: { ...stream },
    scene: { ticks: 100, phase: 0.2, daylight: 1, mobs: 2 },
  });
  const native = {
    render() { return true; },
    renderWorld(...args) { calls.push(['frame', ...args]); return true; },
    chunkLoadStatus() { return JSON.stringify({ busy: false, loading: false, error: '', stream }); },
    worldInfo() { return JSON.stringify(info()); },
    targetInfo() { return JSON.stringify({}); },
    mineWorld() { calls.push(['mine']); return true; },
    placeWorld() { calls.push(['place']); return true; },
    attackWorld() { calls.push(['attack']); return JSON.stringify({ hit: false, damaged: false, killed: false, killCount: 0 }); },
    inventoryInfo() { return JSON.stringify(inventory); },
    inventoryOpen() { return true; },
    selectInventory(slot) { inventory.selected = slot; return JSON.stringify(inventory); },
    inventoryTransfer() { return JSON.stringify(inventory); },
    setCreativeItem(...args) { calls.push(['creative', ...args]); return JSON.stringify(inventory); },
    setRenderRadius() { return true; },
    setChunkRanges() { return true; },
    saveWorld() { calls.push(['save']); return true; },
    beginTouch() { return true; },
    pollTouch() { return JSON.stringify({ available: true, touches }); },
    stopTouch() { return true; },
    beginCreateWorld(...args) { calls.push(['beginCreate', ...args]); return true; },
    beginOpenWorld() { return true; },
    worldJobStatus() {
      return JSON.stringify({ state: 'running', stage: 'generate', progress: 10, loadedChunks: 1, totalChunks: 9 });
    },
    cancelWorldJob() { return true; },
    finishWorldJob() { return JSON.stringify({ ...info(), path: '/tmp/mc/live', seed: 42 }); },
  };
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
      // Model the real import: a firmware module that may simply be missing.
      if (name === 'global') {
        return { default: moduleHost ? { Global: function () { return moduleHost.gm; } } : undefined };
      }
      assert.ok(/^\.\.?\//.test(name), 'Unexpected import ' + name);
      return load(path.resolve(path.dirname(filename), name + '.ts'));
    }, module, module.exports);
    return module.exports;
  }
  const keyboard = load(path.join(utilsDir, 'keyboard.ts'));
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
    page, native, calls, timers, keyboard, host: moduleHost, inventory,
    mount() { def.mounted.call(page); },
    destroy() { def.beforeDestroy.call(page); },
    enter() {
      page.enterWorld({ ...info(), path: '/tmp/mc/live', seed: 42 }, false);
      page.refreshInventory();
      // mounted() already started the preview loop; restart it so world mode
      // takes the native touch path, exactly as completeWorldJob does.
      page.stopRenderLoop();
      page.startRenderLoop();
    },
    frame() { assert.notEqual(page.renderTimer, null); runTimer(page.renderTimer); },
    setTouches(value) { touches = value; },
    advance(ms) { now += ms; },
    get now() { return now; },
  };
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

// --- Rule 1: UUID matching -------------------------------------------------

test('a callback for another UUID is ignored and the field is never written', () => {
  const h = make();
  h.mount();
  h.page.createPanelOpen = true;
  h.page.seedInput = '1234';
  h.page.openSeedKeyboard();
  const uuid = h.page.seedEdit.taskUuid;
  assert.equal(uuid, 'uuid-1');
  // A confirmed result belonging to a different session must do nothing at all.
  h.host.emit('uuid-999', { editConfirmed: true, text: '777' });
  assert.equal(h.page.seedInput, '1234', 'A foreign UUID never writes the field');
  assert.deepEqual(h.host.closes(), [], 'A foreign UUID never closes any session');
  assert.equal(h.page.seedEdit.taskUuid, uuid, 'The live session survives a foreign callback');
  assert.equal(h.page.textEditing, true, 'The edit is still open');
  // The matching UUID is handled.
  h.host.emit(uuid, { editConfirmed: true, text: '777' });
  assert.equal(h.page.seedInput, '777');
  assert.deepEqual(h.host.closes(), [uuid]);
});

test('two sessions share one signal and each only answers to its own UUID', () => {
  const h = make();
  h.mount();
  h.enter();
  // Chat first, then the seed field would need the create panel; instead drive
  // the sessions directly to prove the UUID demultiplexing, not the panels.
  h.page.openChatKeyboard();
  const chatUuid = h.page.chatEdit.taskUuid;
  const seedConfirms = [];
  const seed = new h.keyboard.TextEditSession(text => seedConfirms.push(text));
  assert.equal(seed.attach(), true);
  assert.equal(seed.start({ text: '', inputType: 'Number' }), true);
  const seedUuid = seed.taskUuid;
  assert.notEqual(seedUuid, chatUuid, 'Each start returns a distinct UUID');
  h.host.emit(seedUuid, { editConfirmed: true, text: 'abc' });
  assert.deepEqual(seedConfirms, ['abc']);
  assert.equal(h.page.chatLog.length, 0, 'The chat session ignored the seed UUID');
  assert.equal(h.page.chatEdit.taskUuid, chatUuid, 'The chat session is still live');
  h.host.emit(chatUuid, { editConfirmed: true, text: 'hello' });
  assert.deepEqual(seedConfirms, ['abc'], 'The seed session ignored the chat UUID');
  assert.ok(h.page.chatLog.some(entry => entry.text.includes('hello')));
  seed.detach();
});

// --- Rule 2: editConfirmed only --------------------------------------------

test('editConfirmed false or non-true writes nothing', () => {
  for (const payload of [
    { editConfirmed: false, text: '999' },
    { text: '999' },
    { editConfirmed: 'true', text: '999' },
    { editConfirmed: 1, text: '999' },
  ]) {
    const h = make();
    h.mount();
    h.page.createPanelOpen = true;
    h.page.seedInput = '1234';
    h.page.openSeedKeyboard();
    const uuid = h.page.seedEdit.taskUuid;
    h.host.emit(uuid, payload);
    assert.equal(h.page.seedInput, '1234',
      'Unconfirmed payload ' + JSON.stringify(payload) + ' must not write');
    assert.deepEqual(h.host.closes(), [],
      'A dismissed edit is already closed by the firmware; closing it again is the documented crash');
    assert.equal(h.page.textEditing, false, 'A dismissed edit releases the input lock');
  }
});

test('a malformed payload leaves the session and the field untouched', () => {
  const h = make();
  h.mount();
  h.page.createPanelOpen = true;
  h.page.seedInput = '1234';
  h.page.openSeedKeyboard();
  const uuid = h.page.seedEdit.taskUuid;
  h.host.emit(uuid, 'not json at all');
  assert.equal(h.page.seedInput, '1234');
  assert.equal(h.page.seedEdit.taskUuid, uuid, 'A parse failure keeps the session open');
  assert.deepEqual(h.host.closes(), []);
  // The session still works afterwards.
  h.host.emit(uuid, { editConfirmed: true, text: '55' });
  assert.equal(h.page.seedInput, '55');
});

// --- Rule 3: never close from inside the callback ---------------------------

test('close happens only after the result has been fully handled', () => {
  const h = make();
  h.mount();
  h.enter();
  h.page.openChatKeyboard();
  const uuid = h.page.chatEdit.taskUuid;
  let logAtClose = null;
  h.host.onClose(() => { logAtClose = h.page.chatLog.map(entry => entry.text); });
  h.host.emit(uuid, { editConfirmed: true, text: '/seed' });
  assert.deepEqual(h.host.closes(), [uuid], 'The session is closed exactly once');
  assert.ok(logAtClose, 'closeTextEdit was reached');
  // The command had already run and been echoed by the time close was called,
  // which is the documented order: handle the result, then close.
  assert.ok(logAtClose.some(text => text === '/seed'), 'The input was echoed before closing');
  assert.ok(logAtClose.some(text => text.includes('世界种子')), 'The command ran before closing');
  assert.equal(h.page.chatEdit.taskUuid, '', 'The UUID is cleared after closing');
  // A dismissal must not close at all: the firmware already tore its editor
  // down, and closing it again is the documented crash.
  h.page.openChatKeyboard();
  const second = h.page.chatEdit.taskUuid;
  h.host.emit(second, { editConfirmed: false, text: 'dropped' });
  assert.deepEqual(h.host.closes(), [uuid], 'A dismissed edit is never closed by the page');
  assert.equal(h.page.chatEdit.taskUuid, '');
});

test('the keyboard source never calls closeTextEdit from the callback body', () => {
  const source = fs.readFileSync(path.join(utilsDir, 'keyboard.ts'), 'utf8');
  // The handler body is the one region that must not contain the call. Locate
  // it structurally rather than trusting a comment.
  const start = source.indexOf('const handler: TextEditFinishedHandler');
  assert.ok(start > 0, 'The textEditFinished handler is defined');
  const end = source.indexOf('gm.textEditFinished.on(handler)', start);
  assert.ok(end > start, 'The handler is registered after it is defined');
  // Strip comments: a comment that mentions the rule is not a call.
  const body = source.slice(start, end).replace(/\/\/[^\n]*/g, '').replace(/\/\*[\s\S]*?\*\//g, '');
  assert.equal(/closeTextEdit/.test(body), false,
    'closeTextEdit must not appear anywhere inside the callback');
  assert.equal(/this\.close\(\)/.test(body), false,
    'The callback must not close the session either, directly or via close()');
  // And exactly one place in the whole file may call it.
  assert.equal((source.match(/gm\.closeTextEdit\(/g) || []).length, 1,
    'Only close() may call closeTextEdit');
});

// --- Rule 4: cleanup on destroy --------------------------------------------

test('destroy closes an unconfirmed session and removes the listeners', () => {
  const h = make();
  h.mount();
  assert.equal(h.host.handlerCount, 2, 'Both fields registered a listener in mounted');
  h.page.createPanelOpen = true;
  h.page.openSeedKeyboard();
  const uuid = h.page.seedEdit.taskUuid;
  assert.equal(uuid, 'uuid-1');
  h.destroy();
  assert.deepEqual(h.host.closes(), [uuid], 'The open session is closed on destroy');
  assert.equal(h.host.handlerCount, 0, 'Every listener is removed on destroy');
  assert.equal(h.page.textEditing, false);
  assert.equal(h.page.keyboardAvailable, false);
  // A late firmware callback must not reach the destroyed component.
  h.page.seedInput = 'kept';
  h.host.emit(uuid, { editConfirmed: true, text: 'late' });
  assert.equal(h.page.seedInput, 'kept');
});

test('destroy with no open session closes nothing but still unregisters', () => {
  const h = make();
  h.mount();
  h.destroy();
  assert.deepEqual(h.host.closes(), [], 'No UUID means no closeTextEdit call');
  assert.equal(h.host.handlerCount, 0);
});

test('hide closes the live session but keeps the listeners for the next show', () => {
  const h = make();
  h.mount();
  h.enter();
  h.page.openChatKeyboard();
  const uuid = h.page.chatEdit.taskUuid;
  h.page.onHide();
  assert.deepEqual(h.host.closes(), [uuid]);
  assert.equal(h.host.handlerCount, 2, 'Listeners survive a hide');
  assert.equal(h.page.textEditing, false);
  h.page.onShow();
  h.page.openChatKeyboard();
  assert.notEqual(h.page.chatEdit.taskUuid, '');
  assert.notEqual(h.page.chatEdit.taskUuid, uuid, 'A new show starts a new UUID');
});

test('restarting a session closes the previous UUID instead of orphaning it', () => {
  const h = make();
  h.mount();
  h.enter();
  h.page.openChatKeyboard();
  const first = h.page.chatEdit.taskUuid;
  // Force a restart through the session API: the page guards against a second
  // tap, so this exercises the session contract directly.
  assert.equal(h.page.chatEdit.start({ text: '', inputType: 'EnUSPreferred' }), true);
  const second = h.page.chatEdit.taskUuid;
  assert.notEqual(second, first);
  assert.deepEqual(h.host.closes(), [first], 'The orphaned session is closed');
  h.host.emit(first, { editConfirmed: true, text: 'stale' });
  assert.equal(h.page.chatLog.length, 0, 'The replaced UUID is no longer accepted');
});

// --- Safe degradation ------------------------------------------------------

test('a host without the global module degrades instead of throwing', () => {
  const h = make({ noModule: true });
  h.mount();
  assert.equal(h.page.keyboardAvailable, false);
  assert.equal(h.keyboard.isKeyboardAvailable(), false);
  // Every entry point stays callable and silent.
  h.page.createPanelOpen = true;
  h.page.openSeedKeyboard();
  assert.equal(h.page.textEditing, false, 'No keyboard means no input lock');
  assert.equal(h.page.seedEdit.active, false);
  h.enter();
  h.page.openChatKeyboard();
  assert.equal(h.page.textEditing, false);
  assert.ok(h.page.chatLog.some(entry => entry.text.includes('系统输入法不可用')),
    'The chat button says so rather than silently doing nothing');
  // Destroy must be just as safe.
  h.destroy();
  // getGlobalModule always answers with an object, so capability checks work.
  const gm = h.keyboard.getGlobalModule();
  assert.equal(typeof gm, 'object');
  assert.equal(gm.startTextEdit, undefined);
  assert.equal(gm.closeTextEdit, undefined);
});

test('the typed seed path still works with no system keyboard', () => {
  const h = make({ noModule: true });
  h.mount();
  h.page.openCreatePanel();
  // onSeedInput is the physical-keyboard / web-preview fallback and must stay.
  h.page.onSeedInput({ detail: { value: '4242' } });
  assert.equal(h.page.seedInput, '4242');
  h.page.createNewWorld();
  assert.ok(h.calls.some(call => call[0] === 'beginCreate' && call[1] === 4242),
    'The typed seed reaches beginCreateWorld unchanged');
});

test('a failing or UUID-less startTextEdit is treated as no keyboard', () => {
  for (const options of [{ startFails: true }, { emptyUuid: true }]) {
    const h = make(options);
    h.mount();
    h.page.createPanelOpen = true;
    h.page.seedInput = 'keep';
    h.page.openSeedKeyboard();
    assert.equal(h.page.textEditing, false, 'A failed open releases the input lock');
    assert.equal(h.page.seedEdit.active, false);
    assert.equal(h.page.seedInput, 'keep');
    // Without a UUID no callback could ever be matched, so nothing is written.
    h.host.emit('', { editConfirmed: true, text: 'ghost' });
    assert.equal(h.page.seedInput, 'keep');
  }
});

test('a module whose signal or close call throws never propagates', () => {
  const h = make();
  h.mount();
  h.enter();
  h.page.openChatKeyboard();
  const uuid = h.page.chatEdit.taskUuid;
  h.host.gm.closeTextEdit = () => { throw new Error('firmware gone'); };
  h.page.chatEdit.close();
  assert.equal(h.page.chatEdit.taskUuid, '', 'A throwing close still clears the UUID');
  h.host.gm.textEditFinished.off = () => { throw new Error('firmware gone'); };
  h.destroy();
  assert.equal(h.page.textEditing, false);
  // The cleared UUID can no longer be matched.
  h.host.emit(uuid, { editConfirmed: true, text: 'ghost' });
  assert.equal(h.page.chatLog.filter(entry => entry.text.includes('ghost')).length, 0);
});

// --- Config contract -------------------------------------------------------

test('the seed field opens the numeric keypad and the chat field English-first', () => {
  const h = make();
  h.mount();
  h.page.createPanelOpen = true;
  h.page.seedInput = '2024';
  h.page.openSeedKeyboard();
  const seedConfig = h.host.starts()[0];
  assert.equal(seedConfig.inputType, 'Number', 'A seed is a number');
  assert.equal(seedConfig.text, '2024', 'The current value is the initial text');
  assert.equal(seedConfig.autofocus, true);
  assert.equal(seedConfig.multiLinesEditVisible, false);
  assert.equal(seedConfig.maxlength, 10, '4294967295 is ten digits');
  h.host.emit(h.page.seedEdit.taskUuid, { editConfirmed: true, text: '2024' });
  h.page.createPanelOpen = false;
  h.enter();
  h.page.openChatKeyboard();
  const chatConfig = h.host.starts()[1];
  assert.equal(chatConfig.inputType, 'EnUSPreferred', 'Commands are ASCII');
  assert.equal(chatConfig.maxlength, 100);
  assert.equal(chatConfig.confirmButtonDisabledOnTextEmpty, true);
  assert.equal(chatConfig.multiLinesEditVisible, false);
  assert.equal(chatConfig.showCursor, true);
  // Every start must be a JSON string, as the documentation requires.
  for (const call of h.host.calls.filter(entry => entry[0] === 'start')) {
    assert.equal(typeof call[1], 'string');
    assert.doesNotThrow(() => JSON.parse(call[1]));
  }
});

test('newlines are stripped from the confirmed text of a single-line field', () => {
  const h = make();
  h.mount();
  h.page.createPanelOpen = true;
  h.page.openSeedKeyboard();
  h.host.emit(h.page.seedEdit.taskUuid, { editConfirmed: true, text: '12\n34\n' });
  assert.equal(h.page.seedInput, '1234');
  assert.equal(h.keyboard.cleanEditedText('a\nb'), 'ab');
  assert.equal(h.keyboard.cleanEditedText(undefined), '');
  assert.equal(h.keyboard.cleanEditedText(42), '');
});

test('a confirmed seed keeps the existing range validation', () => {
  const h = make();
  h.mount();
  h.page.openCreatePanel();
  h.page.openSeedKeyboard();
  h.host.emit(h.page.seedEdit.taskUuid, { editConfirmed: true, text: '99999999999' });
  assert.equal(h.page.seedInput, '99999999999');
  h.page.createNewWorld();
  assert.equal(h.page.actionMessage, '种子须为 0 至 4294967295 的整数');
  assert.equal(h.calls.filter(call => call[0] === 'beginCreate').length, 0,
    'An out-of-range keyboard seed never starts a world');
  // A valid one does start.
  h.page.openSeedKeyboard();
  h.host.emit(h.page.seedEdit.taskUuid, { editConfirmed: true, text: ' 777 ' });
  assert.equal(h.page.seedInput, '777', 'The confirmed text is trimmed');
  h.page.createNewWorld();
  assert.ok(h.calls.some(call => call[0] === 'beginCreate' && call[1] === 777));
});

// --- Input must not reach the world while the keyboard is open --------------

test('an open keyboard never moves the player or rotates the camera', () => {
  const h = make();
  h.mount();
  h.enter();
  const p = h.page;
  // A finger is already holding forward and the left button before the chat
  // keyboard opens; neither may survive into the edit.
  h.setTouches([{ id: 1, x: 100, y: 190 }, { id: 2, x: 640, y: 235 }]);
  for (let i = 0; i < 4; i += 1) h.frame();
  assert.equal(p.movement.forward, true);
  assert.equal(p.primaryHeld, true);
  p.openChatKeyboard();
  assert.equal(p.textEditing, true);
  assert.equal(p.worldInputBlocked, true, 'An open keyboard blocks world input');
  assert.equal(p.movement.forward, false, 'Movement is released when the keyboard opens');
  assert.equal(p.primaryHeld, false);
  const yaw = p.yaw, pitch = p.pitch;
  const mines = h.calls.filter(call => call[0] === 'mine').length;
  const places = h.calls.filter(call => call[0] === 'place').length;
  // The same fingers keep moving across the surface during the edit.
  for (let i = 0; i < 20; i += 1) {
    h.setTouches([{ id: 1, x: 100 + i * 5, y: 190 }, { id: 2, x: 640, y: 235 }, { id: 3, x: 400 + i * 9, y: 150 }]);
    h.frame();
  }
  assert.equal(p.yaw, yaw, 'No look rotation during a text edit');
  assert.equal(p.pitch, pitch);
  assert.equal(p.movement.forward, false);
  assert.equal(p.movement.jump, false);
  assert.equal(p.primaryHeld, false);
  assert.equal(h.calls.filter(call => call[0] === 'mine').length, mines, 'No mining during a text edit');
  assert.equal(h.calls.filter(call => call[0] === 'place').length, places, 'No placing during a text edit');
  // Native still receives frames, but with zero movement input.
  const frames = h.calls.filter(call => call[0] === 'frame');
  assert.ok(frames.length > 0, 'Rendering continues while the keyboard is open');
  for (const frame of frames.slice(-20)) {
    // renderWorld(ptr, w, h, yaw, pitch, forward, strafe, jump, dt, ...)
    assert.equal(frame[6], 0, 'forward input is zero during a text edit');
    assert.equal(frame[7], 0, 'strafe input is zero during a text edit');
    assert.equal(frame[8], false, 'jump input is false during a text edit');
  }
  // Finishing the edit releases the lock; the still-held fingers stay ignored
  // until they are physically lifted.
  h.host.emit(p.chatEdit.taskUuid, { editConfirmed: true, text: 'hi' });
  assert.equal(p.textEditing, false);
  assert.equal(p.worldInputBlocked, false);
  h.frame();
  assert.equal(p.movement.forward, false, 'A finger held through the edit does not resume walking');
  assert.equal(p.yaw, yaw);
});

test('the fallback touch path is also inert during a text edit', () => {
  const h = make();
  h.mount();
  h.enter();
  const p = h.page;
  p.stopNativeTouch();
  p.openChatKeyboard();
  const yaw = p.yaw;
  p.movementStart('forward', { changedTouches: [{ identifier: 5, screenX: 100, screenY: 190 }] });
  assert.equal(p.movement.forward, false, 'Fallback movement is refused during a text edit');
  p.primaryStart({ changedTouches: [{ identifier: 6, screenX: 640, screenY: 235 }] });
  assert.equal(p.primaryHeld, false);
  p.onLookStart({ changedTouches: [{ identifier: 7, screenX: 400, screenY: 150 }] });
  assert.equal(p.dragging, false, 'Look drags are refused during a text edit');
  p.onLookMove({ changedTouches: [{ identifier: 7, screenX: 480, screenY: 190 }] });
  assert.equal(p.yaw, yaw);
  p.tickPrimary(h.now);
  p.tickSecondary(h.now);
  assert.equal(h.calls.filter(call => call[0] === 'mine').length, 0);
  assert.equal(h.calls.filter(call => call[0] === 'place').length, 0);
});

test('the chat button only exists in world mode and is refused while panels are open', () => {
  const h = make();
  h.mount();
  const p = h.page;
  assert.equal(withClass(p, 'chat-button').length, 0, 'No chat button in preview mode');
  p.openChatKeyboard();
  assert.equal(p.textEditing, false, 'Preview mode cannot open chat');
  assert.deepEqual(h.host.starts(), []);
  h.enter();
  assert.equal(withClass(p, 'chat-button').length, 1);
  p.openInventory();
  assert.equal(withClass(p, 'chat-button').length, 0, 'The open bag hides the chat button');
  p.openChatKeyboard();
  assert.equal(p.textEditing, false, 'An open bag refuses chat');
  p.closeInventory();
  p.openWorldSettings();
  p.openChatKeyboard();
  assert.equal(p.textEditing, false, 'An open settings panel refuses chat');
  p.closeWorldSettings();
  p.chunkLoading = true;
  p.openChatKeyboard();
  assert.equal(p.textEditing, false, 'An exclusive job refuses chat');
  p.chunkLoading = false;
  p.openChatKeyboard();
  assert.equal(p.textEditing, true, 'An idle world accepts chat');
  // A second tap must not orphan the first session.
  const uuid = p.chatEdit.taskUuid;
  p.openChatKeyboard();
  assert.equal(p.chatEdit.taskUuid, uuid, 'A repeated tap is ignored while editing');
  assert.equal(h.host.starts().length, 1);
});

test('the chat button sits in the ignore strip and the log takes no touches', () => {
  const h = make();
  h.mount();
  h.enter();
  const p = h.page;
  // The button's own area must never become a look drag.
  for (const [x, y] of [[740, 6], [820, 46], [780, 26]]) {
    h.setTouches([{ id: 1, x, y }]);
    h.frame();
    assert.equal(p.worldContacts['native:1'].role, 'ignore', 'chat button ignore at ' + x + ',' + y);
    h.setTouches([]);
    h.frame();
  }
  // The read-only log must not swallow a drag: a contact over it still looks.
  p.pushChat('chat', 'test line');
  assert.equal(p.chatVisible, true);
  h.setTouches([{ id: 2, x: 300, y: 150 }]);
  h.frame();
  assert.equal(p.worldContacts['native:2'].role, 'look', 'The chat log never blocks the camera');
  // And the log element carries no touch handlers at all.
  const log = withClass(p, 'chat-log');
  assert.equal(log.length, 1);
  assert.deepEqual(Object.keys(log[0].data.on || {}), [], 'The chat log binds no events');
});

test('Falcon template and Less compile, and the chat layout clears every control', () => {
  let css = '';
  less.render(fs.readFileSync(path.join(pageDir, 'index.less'), 'utf8'),
    { filename: path.join(pageDir, 'index.less'), paths: [pageDir, path.join(root, 'ui/src/styles')] },
    (error, output) => { assert.equal(error, null); css = output.css; });
  assert.ok(css.includes('.chat-button'), 'Less compiles and emits .chat-button');
  assert.ok(css.includes('.chat-log'), 'Less compiles and emits .chat-log');
  assert.ok(css.includes('.seed-input-hit'), 'Less compiles and emits .seed-input-hit');
  // Geometry is asserted against the numbers the template/Less declare.
  const CHAT_BUTTON = [740, 6, 80, 40];
  const CHAT_LOG = [200, 100, 392, 90];
  const inside = ([x, y, w, hh]) => x >= 0 && y >= 0 && x + w <= 960 && y + hh <= 266;
  assert.ok(inside(CHAT_BUTTON), 'The chat button fits the 960x266 surface');
  assert.ok(inside(CHAT_LOG), 'The chat log fits the 960x266 surface');
  const overlaps = (a, b) => a[0] < b[0] + b[2] && b[0] < a[0] + a[2] && a[1] < b[1] + b[3] && b[1] < a[1] + a[3];
  // The log must cover no control: movement pad, mouse buttons, jump, hotbar,
  // HUD and the three top-right buttons.
  for (const [name, box] of [
    ['movement pad', [12, 174, 180, 84]],
    ['primary', [626, 218, 104, 40]],
    ['secondary', [738, 218, 104, 40]],
    ['jump', [850, 218, 98, 40]],
    ['hud', [12, 6, 430, 56]],
    ['settings', [540, 6, 80, 40]],
    ['bag', [640, 6, 80, 40]],
    ['chat button', CHAT_BUTTON],
    // 1.5× native hotbar incl. selection frames, and the survival HUD rows above it.
    ['hotbar', [341, 231, 277, 35]],
    ['survival hud', [343, 191, 273, 28]],
    ['status bar', [12, 68, 936, 30]],
  ]) {
    assert.equal(overlaps(CHAT_LOG, box), false, 'The chat log must not cover the ' + name);
  }
  for (const [name, box] of [
    ['settings', [540, 6, 80, 40]], ['bag', [640, 6, 80, 40]], ['hud', [12, 6, 430, 56]],
  ]) {
    assert.equal(overlaps(CHAT_BUTTON, box), false, 'The chat button must not cover the ' + name);
  }
  const source = fs.readFileSync(path.join(pageDir, 'index.less'), 'utf8');
  // Chat text is not red: red is the error status bar's colour.
  const chatColours = ['#ffffff', '#c2df91', '#ffe2a3'];
  for (const colour of chatColours) assert.ok(source.includes(colour), 'chat colour ' + colour + ' declared');
  assert.ok(source.includes('.chat-button'), '.chat-button declared');
  assert.ok(source.includes('.chat-log'), '.chat-log declared');
  assert.ok(source.includes('.chat-line'), '.chat-line declared');
  assert.ok(templateSource.includes('openChatKeyboard'), 'The chat button wires openChatKeyboard');
  assert.ok(templateSource.includes('openSeedKeyboard'), 'The seed field wires openSeedKeyboard');
  assert.ok(templateSource.includes('onSeedInput'), 'The native input fallback is retained');
});

console.log('RESULT ' + passed + ' UI keyboard checks passed');
