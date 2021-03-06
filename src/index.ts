import * as os from 'os';
import * as path from 'path';

let obs: obs.ObsNode;
let cwd = process.cwd();
const isWindows = os.platform() === "win32";
try {
    if (isWindows) {
        // for windows, we need set working directory to obs binary path to load obs dependencies correctly.
        process.chdir(path.resolve(__dirname, '../prebuild/obs-studio/bin/64bit'));
    }
    obs = require('../prebuild/obs-node.node');
} finally {
    if (isWindows) {
        process.chdir(cwd);
    }
}

// set obs studio path before calling any function.
const obsPath = path.resolve(__dirname, '../prebuild/obs-studio');
obs.setObsPath(obsPath);

declare namespace obs {

    export type RateControl = 'CBR' | 'VBR';

    export type SourceType = 'Image' | 'MediaSource';

    export type Position = 'top' | 'top-right' | 'right' | 'bottom-right' | 'bottom' | 'bottom-left' | 'left' | 'top-left' | 'center';

    export type TransitionType = 'cut_transition' | 'fade_transition' | 'swipe_transition' | 'slide_transition';

    export type OverlayType = 'cg';

    export type OverlayStatus = 'up' | 'down';

    export type CGItemType = 'image' | 'text';

    export interface Source {
        id: string;
        sceneId: string;
        type: SourceType;
        url: string;
        volume: number;
        audioLock: boolean;
        audioMonitor: boolean;
    }

    export interface UpdateSourceSettings {
        url?: string;
        volume?: number;
        audioLock?: boolean;
        audioMonitor?: boolean;
    }

    export interface VideoSettings {
        baseWidth: number;
        baseHeight: number;
        outputWidth: number;
        outputHeight: number;
        fpsNum: number;
        fpsDen: number;
    }

    export interface AudioSettings {
        sampleRate: number;
    }

    export interface SourceSettings {
        isFile: boolean;
        type: SourceType;
        url: string;
        hardwareDecoder: boolean;
        startOnActive: boolean;
        enableBuffer?: boolean;
        bufferSize?: number;
        output?: OutputSettings;
    }

    export interface OutputSettings {
        server: string;
        key: string;
        hardwareEnable: boolean;
        width: number;
        height: number;
        keyintSec: number;
        rateControl: RateControl;
        preset: string;
        profile: string;
        tune: string;
        x264opts?: string;
        videoBitrateKbps: number;
        audioBitrateKbps: number;
    }

    export interface Settings {
        video: VideoSettings;
        audio: AudioSettings;
        outputs?: OutputSettings[];
    }

    export type VolmeterCallback = (
        sceneId: string,
        sourceId: string,
        channels: number,
        magnitude: number[],
        peak: number[],
        input_peak: number[]) => void;

    export interface Audio {
        masterVolume: number;
        audioWithVideo: boolean;
    }

    export interface UpdateAudioRequest {
        masterVolume?: number;
        audioWithVideo?: boolean;
        pgmMonitor?: boolean;
    }

    export interface Overlay {
        id: string;
        name: string;
        type: OverlayType;
        status?: OverlayStatus;
    }

    export interface CG extends Overlay {
        baseWidth: number;
        baseHeight: number;
        items: CGItem[];
    }

    export interface CGItem {
        type: CGItemType;
        x: number;
        y: number;
        width: number;
        height: number;
    }

    export interface CGText extends CGItem {
        content: string;
        fontSize: number;
        fontFamily: string;
        colorABGR: string;
    }

    export interface CGImage extends CGItem {
        url: string;
    }

    export interface ObsNode {
        setObsPath(obsPath: string): void
        startup(settings: Settings): void;
        shutdown(): void;
        addScene(sceneId: string): string;
        addSource(sceneId: string, sourceId: string, settings: SourceSettings): void;
        getSource(sceneId: string, sourceId: string): Source;
        updateSource(sceneId: string, sourceId: string, request: UpdateSourceSettings): void;
        restartSource(sceneId: string, sourceId: string): void;
        switchToScene(sceneId: string, transitionType: TransitionType, transitionMs: number): void;
        createDisplay(name: string, parentWindow: Buffer, scaleFactor: number, sourceId: string): void;
        destroyDisplay(name: string): void;
        moveDisplay(name: string, x: number, y: number, width: number, height: number): void;
        addDSK(id: string, position: Position, url: string, left: number, top: number, width: number, height: number): void;
        addVolmeterCallback(callback: VolmeterCallback): void;
        getAudio(): Audio;
        updateAudio(request: UpdateAudioRequest): void;
        screenshot(sceneId: string, sourceId: string): Promise<Buffer>;
        addOverlay(overlay: Overlay): void;
        removeOverlay(overlayId: string): void;
        upOverlay(overlayId: string): void;
        downOverlay(overlayId: string): void;
        getOverlays(): Overlay[];
    }
}

export = obs;