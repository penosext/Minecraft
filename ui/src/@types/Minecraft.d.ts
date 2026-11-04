export declare class MinecraftRenderer {
    static render(surfacePtr: number, width: number, height: number, frame?: number, yaw?: number, pitch?: number, blockIndex?: number): boolean;
    static postFrame(surfacePtr: number, buffer: any, width: number, height: number): boolean;
    static beginCreateWorld(seed: number, worldSize: number, radius: number): boolean;
    static beginOpenWorld(path: string, radius: number): boolean;
    static worldJobStatus(): string;
    static finishWorldJob(): string;
    static cancelWorldJob(): boolean;
    static setRenderRadius(radius: number): boolean;
    static setChunkRanges(displayRadius: number, preloadRadius: number): boolean;
    static chunkLoadStatus(): string;
    static createWorld(seed: number, worldSize?: number): string;
    static openWorld(path: string): string;
    static saveWorld(): boolean;
    static worldInfo(): string;
    static renderWorld(surfacePtr: number, width: number, height: number,
                       yaw: number, pitch: number, forward: number, strafe: number,
                       // pixelStep: ray tile side, clamped to 1..4 (1 = full resolution, 2 = original, 3 = default).
                       jump: boolean, dt: number, viewMode?: number, pixelStep?: number): boolean;
    static mineWorld(yaw: number, pitch: number): boolean;
    static setMiningInput(held: boolean, yaw: number, pitch: number): boolean;
    static miningInfo(): string;
    static setGameMode(mode: 'creative' | 'survival'): string;
    static setSceneTime(dayTicks: number): string;
    static setWorldRule(rule: string, value: boolean): string;
    static teleportPlayer(x: number, y: number, z: number): string;
    static attackWorld(yaw: number, pitch: number): string;
    static targetInfo(yaw: number, pitch: number): string;
    static landmarks(): string;
    static inventoryInfo(): string;
    static selectInventory(slot: number): string;
    static setCreativeSlot(slot: number, blockIndex: number): string;
    static setCreativeItem(slot: number, itemIndex: number): string;
    static inventoryTransfer(from: number, to: number, count?: number): string;
    static inventoryOpen(open: boolean): boolean;
    static placeWorld(yaw: number, pitch: number): boolean;
    static useWorld(yaw: number, pitch: number): boolean;
    static beginTouch(): boolean;
    static pollTouch(): string;
    static stopTouch(): boolean;
}
