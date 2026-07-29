/**
 * naina — embeddable document-reading (OCR) runtime (Node binding).
 *
 * Quickstart:
 *
 *   import { Engine, read } from '@jvoltci/naina';
 *
 *   const img = { data: rgbBuffer, width: 640, height: 200 };
 *
 *   // One-liner: image -> markdown
 *   const markdown = await read(img);
 *
 *   // Or keep an Engine around for repeated calls
 *   const engine = new Engine({ tier: 'tiny' });
 *   const page = await engine.read(img);
 *   console.log(page.markdown);
 *   for (const line of page.lines) console.log(line.confidence, line.text);
 *
 * Image data must be a raw pixel buffer (Uint8Array or Buffer). Use `sharp`
 * or another decoder to load files; this package intentionally doesn't
 * pull in an image decoder.
 *
 * Tiers select model size, not licence. Every model naina ships is
 * Apache-2.0: tiny (~11 MB), small (~54 MB), medium (~269 MB).
 *
 * Weights are fetched on first use and cached under $NAINA_CACHE (default
 * ~/.cache/naina/models). Set NAINA_OFFLINE=1 to disable network and use
 * only the local cache.
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

export type Backend = 'auto' | 'onnxruntime' | 'openvino' | 'ncnn' | 'coreml' | 'tensorrt';
export type Tier = 'auto' | 'tiny' | 'small' | 'medium';
export type PixelFormat = 'rgb' | 'bgr' | 'gray';

export interface Point {
    x: number;
    y: number;
}

/** One recognised line of text. */
export interface Line {
    text: string;
    /** Recognition confidence. */
    confidence: number;
    /** Detection confidence for the quad. */
    score: number;
    /** Clockwise from top-left, in source image coordinates. */
    quad: [Point, Point, Point, Point];
}

/** A read page, resolved eagerly to a plain object — no handle to release. */
export interface Page {
    markdown: string;
    json: string;
    lines: Line[];
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
    tier?: Tier;
    modelsRoot?: string;
    numThreads?: number;
}

// ── Native module surface ────────────────────────────────────────────

interface NativeEngine {
    read(image: ImageInput): Promise<Page>;
    detectText(image: ImageInput): Promise<Array<[Point, Point, Point, Point]>>;
}

interface NativeModule {
    Engine: new (options?: EngineOptions) => NativeEngine;
    version: string;
}

// ── Public API ───────────────────────────────────────────────────────

export const version: string = native.version;

export class Engine {
    private readonly inner: NativeEngine;

    constructor(options: EngineOptions = {}) {
        this.inner = new native.Engine(options);
    }

    /** Read a document: detect text, recognise it, return the page. */
    read(image: ImageInput): Promise<Page> {
        return this.inner.read(image);
    }

    /** Detection only, for callers that want geometry without recognition. */
    detectText(image: ImageInput): Promise<Array<[Point, Point, Point, Point]>> {
        return this.inner.detectText(image);
    }
}

/**
 * Read an image and return markdown. Constructs a throwaway Engine, so
 * prefer `Engine.read` for repeated calls.
 */
export async function read(image: ImageInput, options: EngineOptions = {}): Promise<string> {
    const engine = new Engine(options);
    const page = await engine.read(image);
    return page.markdown;
}
