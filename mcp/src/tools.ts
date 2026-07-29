/**
 * The two tools this server exposes. Kept deliberately small -- one tool
 * for the common case (markdown), one for when an agent needs coordinates
 * or confidence to filter on. No tool per internal naina stage.
 */

import { z } from 'zod';
import type { McpServer } from '@modelcontextprotocol/sdk/server/mcp.js';
import type { CallToolResult } from '@modelcontextprotocol/sdk/types.js';

import type { EnginePool } from './engine-pool';
import { decodeImage } from './image';
import { NainaMcpError, translateEngineError } from './errors';

const readInputShape = {
    path: z.string().min(1).optional().describe('Local file path to the document image to read.'),
    imageBase64: z
        .string()
        .min(1)
        .optional()
        .describe(
            'Base64-encoded image bytes (a "data:image/...;base64," prefix is also accepted). ' +
                'Provide this or "path", not both.',
        ),
    tier: z
        .enum(['auto', 'tiny', 'small', 'medium'])
        .optional()
        .describe(
            'Model size tier: "tiny" (~11 MB, fastest), "small" (~55 MB), "medium" (~269 MB, most accurate). ' +
                'Default "auto". Weights for a tier are downloaded and cached on that tier\'s first use -- ' +
                'that first call can take up to a minute on a slow connection; later calls are fast.',
        ),
    language: z
        .enum(['latin', 'devanagari'])
        .optional()
        .describe(
            'Script of the document. "latin" (default) also covers Chinese, Japanese and Korean. ' +
                '"devanagari" reads Hindi, Marathi, Nepali and Sanskrit. ' +
                'IMPORTANT: choosing wrong does not produce an error -- reading a Hindi page as ' +
                '"latin" returns plausible-looking but entirely wrong Latin text at high ' +
                'confidence. Set this from the document, and do not trust confidence to warn you.',
        ),
};

type ReadArgs = {
    path?: string;
    imageBase64?: string;
    tier?: 'auto' | 'tiny' | 'small' | 'medium';
    language?: 'latin' | 'devanagari';
};

function toolError(err: unknown): CallToolResult {
    const known = err instanceof NainaMcpError ? err : translateEngineError(err);
    return { content: [{ type: 'text', text: known.message }], isError: true };
}

export function registerTools(server: McpServer, engines: EnginePool): void {
    server.registerTool(
        'read_document',
        {
            title: 'Read document',
            description:
                'Read a document image (a photo or scan of a page) and return its content as structured ' +
                'markdown -- headings, paragraphs and tables where naina\'s layout model detects them. Give ' +
                'either a local file "path" or "imageBase64", not both.',
            inputSchema: readInputShape,
            annotations: { readOnlyHint: true, openWorldHint: false },
        },
        async (args: ReadArgs): Promise<CallToolResult> => {
            try {
                const image = await decodeImage({ path: args.path, imageBase64: args.imageBase64 });
                const engine = engines.get(
                    args.tier ?? 'auto',
                    args.language === 'devanagari' ? 'devanagari' : '',
                );
                const page = await engine.read(image);
                return { content: [{ type: 'text', text: page.markdown }] };
            } catch (err) {
                return toolError(err);
            }
        },
    );

    server.registerTool(
        'read_document_detailed',
        {
            title: 'Read document (detailed)',
            description:
                'Like "read_document", but returns per-line text together with recognition confidence, ' +
                'detection score, and the four-point quadrilateral (clockwise from top-left, in source-image ' +
                'pixel coordinates) for each line -- for when an agent needs coordinates or wants to filter ' +
                'out low-confidence lines. Same input as "read_document".',
            inputSchema: readInputShape,
            annotations: { readOnlyHint: true, openWorldHint: false },
        },
        async (args: ReadArgs): Promise<CallToolResult> => {
            try {
                const image = await decodeImage({ path: args.path, imageBase64: args.imageBase64 });
                const engine = engines.get(
                    args.tier ?? 'auto',
                    args.language === 'devanagari' ? 'devanagari' : '',
                );
                const page = await engine.read(image);
                const detailed = {
                    width: image.width,
                    height: image.height,
                    lines: page.lines,
                };
                return { content: [{ type: 'text', text: JSON.stringify(detailed, null, 2) }] };
            } catch (err) {
                return toolError(err);
            }
        },
    );
}
