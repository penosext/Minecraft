type HttpRequestMethod = 'GET' | 'POST' | 'DELETE' | 'PATCH' | 'PUT';
type HttpResponse = {
    statusCode: number,
    headers: { [key: string]: string },
    data: any,
    statusText?: string,
    error?: string
}

type Falcon = {
    on<T>(eventName: string, callback: FalconCallback<T>): void,
    off<T>(eventName: string, callback?: FalconCallback<T>): void,
    trigger<T>(eventName: string, data: T): void,
    navTo<T>(target: String, options: T): void,
    trigger<T>(eventName: string, data: T): void,
    jsapi: {
        storage: {
            setStorage(params: { key: string; data: string }): Promise<any>;
            getStorage(params: { key: string }): Promise<{ data: string }>;
            getStorageInfo(params: {}): Promise<{ keys: string[]; currentSize: number; limitSize: number; }>;
        },
        http: {
            request(params: {
                url: string,
                method?: HttpRequestMethod,
                headers?: { [key: string]: string },
                data?: any,
                timeout?: number,
            }): Promise<HttpResponse>;
        }
    },
    closeApp: () => void,
    closePageByName: (pageName: string) => void,
    closePageById: (pageId: string) => void,
    $app: {
        finish: () => void,
    }
};

type FalconEvent<T> = {
    type: string,
    timestamp: string,
    data: T
};

type FalconCallback<T> = (data: FalconEvent<T>) => void;

declare const $falcon: Falcon;

type FalconPage<T> = {
    $falcon: Falcon,
    $root: Object,
    $pageName: string,
    $pageId: string,
    loadOptions: T,
    newOptions: T,
    setRootComponent: (component: any) => void,
    finish: () => void,
    $npage: {
        setSupportBack: (support: boolean) => void
        on: (eventName: string, callback: () => void) => void
        off: (eventName: string, callback: () => void) => void
    }
    on: (eventName: string, callback: () => void) => void
    off: (eventName: string, callback: () => void) => void
}
