// HaasUI system input method module, as documented in KEYBOARD_INPUT.md.
// Ambient declaration only: the module is provided by the device firmware and
// is intentionally left external by the Falcon bundler, so it can be absent on
// a host/mock runtime. utils/keyboard.ts is the only place allowed to touch it
// and degrades safely when it is missing.
declare module 'global' {
    // Result payload of one finished edit session (JSON string in the callback).
    interface TextEditFinishedResult {
        editConfirmed: boolean;
        text: string;
        cursorIndex?: number;
    }

    type TextEditFinishedHandler = (uuid: string, jsonData: string) => void;

    interface TextEditFinishedSignal {
        on(handler: TextEditFinishedHandler): void;
        off(handler: TextEditFinishedHandler): void;
    }

    class Global {
        // Takes the JSON-serialised TextEditConfig, returns the session UUID.
        startTextEdit(config: string): string;
        // Must only be called after the result has been processed, never from
        // inside the textEditFinished callback (documented crash).
        closeTextEdit(uuid: string): void;
        textEditFinished: TextEditFinishedSignal;
    }

    const globalModule: { Global: typeof Global };
    export default globalModule;
}
