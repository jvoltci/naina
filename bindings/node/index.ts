/**
 * naina — embeddable face & person CV runtime (Node binding).
 *
 * Quickstart:
 *
 *   import { Engine, similarity } from 'naina';
 *
 *   const engine = new Engine();
 *   const img = { data: rgbBuffer, width: 1280, height: 720, channels: 3 };
 *   const faces = await engine.detectFaces(img);
 *   const emb   = await engine.embedFace(img, faces[0]);
 *
 * Image data must be a raw pixel buffer (Uint8Array or Buffer). Use `sharp`
 * or another decoder to load files; this package intentionally doesn't
 * pull in an image decoder.
 *
 * Models are fetched from GitHub Releases the first time each task runs,
 * and cached under $NAINA_CACHE (default ~/.cache/naina/models). Set
 * NAINA_OFFLINE=1 to disable network and use only the local cache.
 */

import * as path from 'node:path';
import * as fs from 'node:fs';

// Auto-set NAINA_REGISTRY. Walk up from this file until we find
// models/registry.yaml; works both when running compiled JS (from dist/)
// and when running the TS source directly under vitest.
function findRegistry(): string | undefined {
    let dir = __dirname;
    for (let i = 0; i < 8; ++i) {
        const candidate = path.join(dir, 'models', 'registry.yaml');
        if (fs.existsSync(candidate)) return candidate;
        const parent = path.dirname(dir);
        if (parent === dir) break;
        dir = parent;
    }
    return undefined;
}
if (!process.env.NAINA_REGISTRY) {
    const r = findRegistry();
    if (r !== undefined) process.env.NAINA_REGISTRY = r;
}

// eslint-disable-next-line @typescript-eslint/no-var-requires
const native = require('../build/Release/naina-node.node') as NativeModule;

// ── Types ────────────────────────────────────────────────────────────

export type Backend = 'auto' | 'onnxruntime' | 'ncnn' | 'coreml' | 'tensorrt';
export type PixelFormat = 'rgb' | 'bgr' | 'gray';

export interface BBox {
    x: number;
    y: number;
    w: number;
    h: number;
    score: number;
}

export interface Point {
    x: number;
    y: number;
}

export interface Face {
    bbox: BBox;
    landmarks: [Point, Point, Point, Point, Point];
    quality: number;
    trackId: number;
}

export interface ImageInput {
    /** Raw pixel buffer. Length must equal width * height * channels. */
    data: Uint8Array | Buffer;
    width: number;
    height: number;
    /** Default: 3 for `rgb`/`bgr`, 1 for `gray`. */
    channels?: number;
    /** Default: 'rgb' when channels=3, else 'gray'. */
    format?: PixelFormat;
}

export interface EngineOptions {
    backend?: Backend;
    modelsRoot?: string;
    numThreads?: number;
    enableResearchModels?: boolean;
}

// ── Native module surface ────────────────────────────────────────────

interface NativeEngine {
    detectFaces(image: ImageInput): Promise<Face[]>;
    embedFace(image: ImageInput, face: Face): Promise<Float32Array>;
    faceEmbedDim(): number;
}

interface NativeModule {
    Engine: new (options?: EngineOptions) => NativeEngine;
    similarity: (a: Float32Array, b: Float32Array) => number;
    version: string;
}

// ── Public API ───────────────────────────────────────────────────────

export const version: string = native.version;

/** Cosine similarity of two L2-normalised embedding vectors. */
export function similarity(a: Float32Array, b: Float32Array): number {
    return native.similarity(a, b);
}

export class Engine {
    private readonly inner: NativeEngine;

    constructor(options: EngineOptions = {}) {
        this.inner = new native.Engine(options);
    }

    detectFaces(image: ImageInput): Promise<Face[]> {
        return this.inner.detectFaces(image);
    }

    embedFace(image: ImageInput, face: Face): Promise<Float32Array> {
        return this.inner.embedFace(image, face);
    }

    faceEmbedDim(): number {
        return this.inner.faceEmbedDim();
    }
}
