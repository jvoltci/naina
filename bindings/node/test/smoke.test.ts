// Smoke test: confirm naina imports, an Engine can be created (or fails
// cleanly when no inference backend is compiled in), and that the public
// OCR surface has the expected shape. Mirrors bindings/python/tests/test_smoke.py.

import { describe, it, expect } from 'vitest';
import * as fs from 'node:fs';
import * as os from 'node:os';
import * as path from 'node:path';
import * as naina from '..';
import { Engine, read, version } from '..';
import type { Page, Line, Point } from '..';

describe('naina (node)', () => {
    it('exports a version string', () => {
        expect(typeof version).toBe('string');
        expect(version).toContain('.');
    });

    it('exports the OCR-shaped public API', () => {
        for (const name of ['Engine', 'read', 'version']) {
            expect(naina).toHaveProperty(name);
        }
        // The face API is gone; it lives on the face-stack branch.
        for (const gone of ['detectFaces', 'embedFace', 'similarity', 'Face', 'BBox']) {
            expect((naina as Record<string, unknown>)[gone]).toBeUndefined();
        }
    });

    it('Page/Line/Point shapes type-check', () => {
        const point: Point = { x: 1, y: 2 };
        const line: Line = { text: 'hi', confidence: 0.9, score: 0.9, quad: [point, point, point, point] };
        const page: Page = { markdown: '# hi', json: '{}', lines: [line] };
        expect(page.lines).toHaveLength(1);
        expect(page.lines[0].quad).toHaveLength(4);
    });
});

function engineOrSkip(ctx: { skip: () => void }, options?: ConstructorParameters<typeof Engine>[0]): Engine {
    try {
        return new Engine(options);
    } catch (e) {
        // No backend compiled in: acceptable for the core-only build matrix.
        ctx.skip();
        throw e;
    }
}

describe('Engine surface', () => {
    it('constructs and exposes read/detectText, not the old face API', (ctx) => {
        const engine = engineOrSkip(ctx);
        expect(typeof engine.read).toBe('function');
        expect(typeof engine.detectText).toBe('function');
        for (const gone of ['detectFaces', 'embedFace', 'faceLiveness', 'faceEmbedDim']) {
            expect((engine as unknown as Record<string, unknown>)[gone]).toBeUndefined();
        }
    });

    it('read rejects rather than returning an empty page when weights are absent', async (ctx) => {
        // With an empty cache and NAINA_OFFLINE=1, read must raise rather than
        // return an empty page — a silent empty result would look like a blank
        // document instead of a missing model.
        const cacheDir = fs.mkdtempSync(path.join(os.tmpdir(), 'naina-node-empty-cache-'));
        process.env.NAINA_OFFLINE = '1';
        process.env.NAINA_CACHE = cacheDir;

        const engine = engineOrSkip(ctx);
        const w = 128;
        const h = 128;
        const img = { data: new Uint8Array(w * h * 3).fill(128), width: w, height: h, channels: 3 };
        await expect(engine.read(img)).rejects.toThrow();
    });
});

// ── End-to-end, gated behind real downloaded weights ──────────────────

/** Locate any scalable TrueType font, on any platform.
 *
 * A bitmap fallback renders text far too small for a text detector to find,
 * so a real font is required — but hardcoding one path makes the test
 * macOS-only. Returns undefined if nothing usable is found, so the caller
 * can skip rather than fail. */
function findScalableFont(): string | undefined {
    const candidates = [
        // macOS
        '/System/Library/Fonts/Supplemental/Arial.ttf',
        '/System/Library/Fonts/Helvetica.ttc',
        // Debian / Ubuntu
        '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',
        '/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf',
        // Fedora / RHEL / Arch
        '/usr/share/fonts/dejavu/DejaVuSans.ttf',
        '/usr/share/fonts/TTF/DejaVuSans.ttf',
        '/usr/share/fonts/liberation-sans/LiberationSans-Regular.ttf',
        // Alpine
        '/usr/share/fonts/dejavu/DejaVuSans-Bold.ttf',
        // Windows
        'C:/Windows/Fonts/arial.ttf',
        'C:/Windows/Fonts/segoeui.ttf',
    ];
    for (const p of candidates) {
        if (fs.existsSync(p)) return p;
    }
    return undefined;
}

const RUN_E2E = process.env.NAINA_E2E === '1';

describe.skipIf(!RUN_E2E)('end-to-end against real PP-OCRv6 weights (NAINA_E2E=1)', () => {
    // Real inference (plus first-run backend warm-up, e.g. CoreML EP
    // compilation) comfortably exceeds vitest's default 5s test timeout.
    it('reads real rendered text', async (ctx) => {
        let canvasMod: typeof import('@napi-rs/canvas');
        try {
            // Optional devDependency, only needed to rasterise this fixture.
            // eslint-disable-next-line @typescript-eslint/no-var-requires
            canvasMod = require('@napi-rs/canvas');
        } catch {
            ctx.skip();
            return;
        }
        const fontPath = findScalableFont();
        if (fontPath === undefined) {
            ctx.skip();
            return;
        }

        delete process.env.NAINA_OFFLINE;

        const { createCanvas, GlobalFonts } = canvasMod;
        GlobalFonts.registerFromPath(fontPath, 'NainaSmokeTestFont');
        const width = 480;
        const height = 100;
        const canvas = createCanvas(width, height);
        const rc = canvas.getContext('2d');
        rc.fillStyle = 'white';
        rc.fillRect(0, 0, width, height);
        rc.fillStyle = 'black';
        rc.font = '40px NainaSmokeTestFont';
        rc.fillText('HELLO WORLD', 24, 60);

        // naina wants tightly-packed RGB8; canvas hands back RGBA.
        const rgba = rc.getImageData(0, 0, width, height).data;
        const rgb = new Uint8Array(width * height * 3);
        for (let i = 0, j = 0; i < rgba.length; i += 4, j += 3) {
            rgb[j] = rgba[i];
            rgb[j + 1] = rgba[i + 1];
            rgb[j + 2] = rgba[i + 2];
        }

        const engine = engineOrSkip(ctx, { tier: 'tiny' });
        const page = await engine.read({ data: rgb, width, height, channels: 3 });

        expect(page.lines.length).toBeGreaterThanOrEqual(1);
        expect(page.markdown.toUpperCase()).toContain('HELLO');
        expect(page.markdown.toUpperCase()).toContain('WORLD');
        expect(page.json.startsWith('{')).toBe(true);
        const line = page.lines[0];
        expect(line.confidence).toBeGreaterThan(0.3);
        expect(line.quad).toHaveLength(4);
        expect(typeof line.text).toBe('string');
    }, 30_000);
});
