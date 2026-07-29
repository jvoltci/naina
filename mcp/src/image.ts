/**
 * Turn a tool's `path` or `imageBase64` input into the raw RGB buffer naina
 * requires. naina ships no image decoder by design (see repo README), so
 * this is entirely on us.
 */

import * as fs from 'node:fs/promises';
import * as path from 'node:path';
import sharp from 'sharp';

import { FileNotFoundError, InvalidInputError, UnsupportedImageError } from './errors';

/** Encoded-file size cap, checked before decoding. A raster image this
 * large is almost certainly a mistake (or a decompression bomb), not a
 * document page -- reject it before libvips ever touches it. */
export const MAX_INPUT_BYTES = 20 * 1024 * 1024; // 20 MB

/** Decoded-pixel cap. 40 megapixels comfortably covers a 300 DPI A3 scan
 * (~12 MP) with headroom; beyond that we're almost certainly decoding
 * something the caller didn't intend to send whole. */
export const MAX_PIXELS = 40_000_000;

/** Per-side cap, independent of the area cap above -- guards against a
 * degenerate 1 x 50,000,000 image that would pass the area check. */
export const MAX_DIMENSION = 10_000;

export interface RawImage {
    data: Buffer;
    width: number;
    height: number;
}

export interface ImageInput {
    path?: string;
    imageBase64?: string;
}

function decodeBase64(raw: string): Buffer {
    // Tolerate a data: URL prefix -- agents copy-paste these whole.
    const commaIndex = raw.startsWith('data:') ? raw.indexOf(',') : -1;
    const b64 = commaIndex >= 0 ? raw.slice(commaIndex + 1) : raw;
    const stripped = b64.replace(/\s+/g, '');
    if (!/^[A-Za-z0-9+/]+={0,2}$/.test(stripped) || stripped.length === 0) {
        throw new InvalidInputError(
            '"imageBase64" is not valid base64 (or is empty). Strip any surrounding quotes/whitespace ' +
                'and, if it came from a data: URL, either keep the "data:...;base64," prefix intact or ' +
                'pass just the part after the comma.',
        );
    }
    return Buffer.from(stripped, 'base64');
}

async function readBytes(input: ImageInput): Promise<Buffer> {
    if (input.path !== undefined && input.imageBase64 !== undefined) {
        throw new InvalidInputError('Provide either "path" or "imageBase64", not both.');
    }
    if (input.path === undefined && input.imageBase64 === undefined) {
        throw new InvalidInputError(
            'Provide "path" (a local file path to an image) or "imageBase64" (base64-encoded image bytes).',
        );
    }

    if (input.path !== undefined) {
        const resolved = path.resolve(input.path);
        let stat;
        try {
            stat = await fs.stat(resolved);
        } catch {
            throw new FileNotFoundError(
                `No file at "${resolved}". Check the path is correct -- use an absolute path if the ` +
                    "server's working directory is uncertain.",
            );
        }
        if (!stat.isFile()) {
            throw new InvalidInputError(`"${resolved}" is not a regular file (it's a directory or special file).`);
        }
        if (stat.size > MAX_INPUT_BYTES) {
            throw new InvalidInputError(
                `"${resolved}" is ${(stat.size / (1024 * 1024)).toFixed(1)} MB, over this server's ` +
                    `${MAX_INPUT_BYTES / (1024 * 1024)} MB limit. Downscale or re-encode it first.`,
            );
        }
        return fs.readFile(resolved);
    }

    const bytes = decodeBase64(input.imageBase64 as string);
    if (bytes.length > MAX_INPUT_BYTES) {
        throw new InvalidInputError(
            `Decoded "imageBase64" is ${(bytes.length / (1024 * 1024)).toFixed(1)} MB, over this ` +
                `server's ${MAX_INPUT_BYTES / (1024 * 1024)} MB limit. Downscale or re-encode it first.`,
        );
    }
    return bytes;
}

/**
 * Decode to tightly-packed RGB8. `flatten` composites any alpha channel
 * onto white and forces the output to 3 channels -- naina's Node binding
 * defaults `channels` to 3 / `format` to 'rgb', so this must actually be
 * RGB rather than RGBA or grayscale.
 */
export async function decodeImage(input: ImageInput): Promise<RawImage> {
    const bytes = await readBytes(input);

    let metadata;
    try {
        metadata = await sharp(bytes).metadata();
    } catch (e) {
        throw new UnsupportedImageError(
            `Could not identify the image format (${(e as Error).message}). Supported formats: ` +
                'PNG, JPEG, WebP, TIFF, GIF, AVIF, BMP.',
        );
    }

    const { width, height } = metadata;
    if (!width || !height) {
        throw new UnsupportedImageError('Decoded image has no dimensions -- the file is likely corrupt or empty.');
    }
    if (width > MAX_DIMENSION || height > MAX_DIMENSION) {
        throw new InvalidInputError(
            `Image is ${width}x${height}, over this server's ${MAX_DIMENSION}px-per-side limit. Downscale it first.`,
        );
    }
    if (width * height > MAX_PIXELS) {
        throw new InvalidInputError(
            `Image is ${width}x${height} (${(width * height / 1_000_000).toFixed(1)} MP), over this ` +
                `server's ${MAX_PIXELS / 1_000_000} MP limit. Downscale it first.`,
        );
    }

    try {
        const { data, info } = await sharp(bytes)
            .rotate() // apply EXIF orientation before handing off raw pixels
            .flatten({ background: '#ffffff' }) // composite alpha onto white, forces 3 channels
            .toColourspace('srgb')
            .raw()
            .toBuffer({ resolveWithObject: true });

        if (info.channels !== 3) {
            // Shouldn't happen given flatten() + srgb above; fail loudly rather
            // than hand naina a buffer whose stride assumption is wrong.
            throw new Error(`unexpected channel count ${info.channels} after RGB conversion`);
        }
        return { data, width: info.width, height: info.height };
    } catch (e) {
        if (e instanceof UnsupportedImageError) throw e;
        throw new UnsupportedImageError(`Failed to decode image to raw pixels (${(e as Error).message}).`);
    }
}
