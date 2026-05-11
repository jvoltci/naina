import { describe, it, expect } from 'vitest';
import { Engine, similarity, version } from '..';

describe('naina (node)', () => {
    it('exports a version string', () => {
        expect(typeof version).toBe('string');
        expect(version).toContain('.');
    });

    it('similarity of identical L2-normalised vectors is ~1.0', () => {
        const v = new Float32Array([0.5, 0.5, 0.5, 0.5]);
        expect(similarity(v, v)).toBeCloseTo(1.0, 5);
    });

    it('similarity of orthogonal vectors is ~0', () => {
        const a = new Float32Array([1, 0]);
        const b = new Float32Array([0, 1]);
        expect(similarity(a, b)).toBeCloseTo(0.0, 6);
    });

    it('Engine lifecycle (offline mode rejects detect with model-not-found)', async () => {
        process.env.NAINA_OFFLINE = '1';
        let engine: Engine;
        try {
            engine = new Engine();
        } catch (e) {
            // No backend compiled in: acceptable for the core-only build matrix.
            console.warn('skipping: no backend available', e);
            return;
        }

        const w = 128;
        const h = 128;
        const img = {
            data: new Uint8Array(w * h * 3).fill(128),
            width: w,
            height: h,
            channels: 3,
        };
        await expect(engine.detectFaces(img)).rejects.toThrow();
    });
});
