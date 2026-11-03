// HaasUI system input method wrapper. The only place in the UI that touches the
// firmware `global` module; everything else goes through TextEditSession.
//
// Implements KEYBOARD_INPUT.md literally, including its four hard rules:
//   1. the textEditFinished callback ignores any UUID that is not its own,
//   2. only `editConfirmed === true` is processed at all,
//   3. closeTextEdit is NEVER written inside the callback body - it lives in
//      finish(), the function that has already consumed the result, because
//      closing from inside the callback crashes the device (doc line 188),
//   4. detach() closes a still-open session, so a destroyed component can never
//      leave the system keyboard owning a dangling task.
//
// The module is firmware-provided and left external by the Falcon bundler, so a
// host/mock runtime may not have it. Every access is guarded and failures
// degrade to a no-op stub instead of throwing, and setGlobalModuleProvider()
// gives tests and mock hosts a seam to inject their own implementation.

import globalModule from 'global';

export type TextEditFinishedHandler = (uuid: string, jsonData: string) => void;

export interface TextEditFinishedSignal {
  on(handler: TextEditFinishedHandler): void;
  off(handler: TextEditFinishedHandler): void;
}

// Everything is optional: a partially implemented host must still be usable.
export interface GlobalModule {
  startTextEdit?(config: string): string;
  closeTextEdit?(uuid: string): void;
  textEditFinished?: TextEditFinishedSignal;
}

// Field names and value ranges are KEYBOARD_INPUT.md's TextEditConfig.
export type TextEditInputType =
  'ZhCNPreferred' | 'ZhCNOnly' | 'EnUSPreferred' | 'EnUSOnly' | 'SymbolsPreferred' | 'Number';

export interface TextEditConfig {
  text?: string;
  placeholder?: string;
  placeholderColor?: string;
  autofocus?: boolean;
  maxlength?: number;
  showCursor?: boolean;
  cursorColor?: string;
  cursorSize?: number;
  confirmButtonDisabledOnTextEmpty?: boolean;
  inputType?: TextEditInputType;
  multiLinesEditVisible?: boolean;
  capsLockSwitchOn?: boolean;
  enterButtonText?: string;
}

export type GlobalModuleProvider = () => GlobalModule | null | undefined;

// A module with no capabilities: every guard below sees `undefined` and skips.
const UNAVAILABLE: GlobalModule = {};

let provider: GlobalModuleProvider | null = null;
let instance: GlobalModule | null = null;
let instanceResolved = false;

// Test/mock seam. Passing null restores the firmware module and drops any
// cached instance, so one test's stub can never leak into the next.
export function setGlobalModuleProvider(next: GlobalModuleProvider | null): void {
  provider = next;
  instance = null;
  instanceResolved = false;
}

function construct(): GlobalModule | null {
  // `globalModule` is an external firmware binding: it may be undefined, or a
  // shape that does not match the documentation at all. Never trust it.
  const factory: any = globalModule;
  if (!factory || typeof factory.Global !== 'function') return null;
  return new factory.Global() as GlobalModule;
}

// Always returns an object, so callers can use plain `if (gm.startTextEdit)`
// capability checks exactly as the documentation shows.
export function getGlobalModule(): GlobalModule {
  if (instanceResolved && instance) return instance;
  instanceResolved = true;
  try {
    const resolved = provider ? provider() : construct();
    instance = resolved || UNAVAILABLE;
  } catch (_) {
    // A missing or broken module is an unsupported environment, not an error
    // the page has to handle: fall back to "no keyboard".
    instance = UNAVAILABLE;
  }
  return instance;
}

export function isKeyboardAvailable(): boolean {
  const gm = getGlobalModule();
  return typeof gm.startTextEdit === 'function' && !!gm.textEditFinished;
}

// Device text edits are multi-line capable; a single-line field must not keep
// the newlines the keyboard may insert (KEYBOARD_INPUT.md step 4).
export function cleanEditedText(text: unknown): string {
  return typeof text === 'string' ? text.replace(/\n/g, '') : '';
}

// One input field = one session. Several sessions may share the firmware's
// single textEditFinished signal: each only answers to the UUID it started.
export class TextEditSession {
  private uuid = '';
  private handler: TextEditFinishedHandler | null = null;
  private attached = false;

  // onConfirmed receives the cleaned text and runs BEFORE the session closes.
  // onDismissed reports a cancelled edit so the page can release whatever it
  // suspended while the keyboard was up. It never receives any text, because a
  // dismissed edit must not write anything anywhere.
  constructor(private readonly onConfirmed: (text: string) => void,
    private readonly onDismissed?: () => void) {}

  get taskUuid(): string {
    return this.uuid;
  }

  get active(): boolean {
    return this.uuid !== '';
  }

  // mounted(): register the result listener once.
  attach(): boolean {
    if (this.attached) return true;
    const gm = getGlobalModule();
    if (!gm.textEditFinished || typeof gm.textEditFinished.on !== 'function') return false;
    const handler: TextEditFinishedHandler = (uuid: string, jsonData: string) => {
      // Rule 1: a UUID that is not this session's belongs to another field.
      if (!this.uuid || uuid !== this.uuid) return;
      let confirmed = false;
      let text = '';
      try {
        const result = JSON.parse(jsonData);
        // Rule 2: cancel/dismiss reports editConfirmed false and writes nothing.
        // Strict true only: a truthy string or 1 is not a confirmation.
        if (result && result.editConfirmed === true) {
          confirmed = true;
          text = cleanEditedText(result.text);
        }
      } catch (_) {
        // Malformed payload: keep the session open and the field untouched.
        return;
      }
      // Rule 3: no closeTextEdit here, in either branch. finish() owns closing,
      // and only after it has handled the result.
      if (confirmed) this.finish(text);
      else this.abandon();
    };
    try {
      gm.textEditFinished.on(handler);
    } catch (_) {
      return false;
    }
    this.handler = handler;
    this.attached = true;
    return true;
  }

  // beforeDestroy(): rule 4 - close a session the user never confirmed, then
  // drop the listener so a late callback cannot reach a dead component.
  detach(): void {
    this.close();
    const gm = getGlobalModule();
    if (this.handler && gm.textEditFinished && typeof gm.textEditFinished.off === 'function') {
      try {
        gm.textEditFinished.off(this.handler);
      } catch (_) { /* Already gone with the module. */ }
    }
    this.handler = null;
    this.attached = false;
  }

  // Returns false when the keyboard is unavailable or refused the request, so
  // the caller can fall back to a physical keyboard / native input event.
  start(config: TextEditConfig): boolean {
    const gm = getGlobalModule();
    if (typeof gm.startTextEdit !== 'function') return false;
    // A second start would orphan the first UUID: close it first.
    if (this.uuid) this.close();
    if (!this.attached && !this.attach()) return false;
    let uuid: unknown = '';
    try {
      uuid = gm.startTextEdit(JSON.stringify(config));
    } catch (_) {
      return false;
    }
    // Some hosts answer with a non-string or an empty UUID: without a UUID the
    // callback could never be matched, so treat it as a failure to open.
    if (typeof uuid !== 'string' || uuid === '') return false;
    this.uuid = uuid;
    return true;
  }

  // The documented editFinishedClicked: consume the result first, close after.
  private finish(text: string): void {
    try {
      this.onConfirmed(text);
    } finally {
      this.close();
    }
  }

  // A dismissed edit. The firmware has already torn its own editor down, so
  // only the local UUID is dropped; calling closeTextEdit for a session the
  // user cancelled is exactly the crash the documentation warns about.
  private abandon(): void {
    this.uuid = '';
    if (this.onDismissed) this.onDismissed();
  }

  // The single place that calls closeTextEdit.
  close(): void {
    const uuid = this.uuid;
    // Clear first: a close must never leave a UUID that a later callback could
    // still match, even if the firmware call throws.
    this.uuid = '';
    if (!uuid) return;
    const gm = getGlobalModule();
    if (typeof gm.closeTextEdit !== 'function') return;
    try {
      gm.closeTextEdit(uuid);
    } catch (_) { /* The session is gone either way. */ }
  }
}
