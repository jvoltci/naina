#!/usr/bin/env node
/**
 * Entry point. Defaults to stdio, since that's what desktop MCP clients
 * (Claude Desktop, etc.) speak. `--http` switches to the Streamable HTTP
 * transport instead, run statelessly (no sessionIdGenerator) so a request
 * carries everything needed to answer it -- no server-side session to
 * pin a client to one process or one region.
 */

import * as http from 'node:http';

import { StdioServerTransport } from '@modelcontextprotocol/sdk/server/stdio.js';
import { StreamableHTTPServerTransport } from '@modelcontextprotocol/sdk/server/streamableHttp.js';

import { createServer } from './server';

interface Args {
    http: boolean;
    port: number;
}

function parseArgs(argv: string[]): Args {
    const args: Args = { http: false, port: 3000 };
    for (let i = 0; i < argv.length; i++) {
        if (argv[i] === '--http') {
            args.http = true;
        } else if (argv[i] === '--port') {
            const value = Number(argv[i + 1]);
            if (!Number.isFinite(value) || value <= 0) {
                throw new Error(`--port needs a positive number, got "${argv[i + 1]}"`);
            }
            args.port = value;
            i++;
        }
    }
    return args;
}

async function main(): Promise<void> {
    const args = parseArgs(process.argv.slice(2));
    const server = createServer();

    if (args.http) {
        const transport = new StreamableHTTPServerTransport({
            sessionIdGenerator: undefined, // stateless: every request is self-contained
            enableJsonResponse: true, // plain JSON responses; no SSE stream needed for request/response tools
        });
        await server.connect(transport);

        const httpServer = http.createServer((req, res) => {
            transport.handleRequest(req, res).catch((err: unknown) => {
                console.error('naina-mcp: request failed:', err);
                if (!res.headersSent) {
                    res.writeHead(500).end();
                }
            });
        });
        httpServer.listen(args.port, () => {
            console.error(`naina-mcp: listening on http://localhost:${args.port} (stateless Streamable HTTP)`);
        });
    } else {
        const transport = new StdioServerTransport();
        await server.connect(transport);
        console.error('naina-mcp: connected on stdio');
    }
}

main().catch((err: unknown) => {
    console.error('naina-mcp: fatal error:', err);
    process.exit(1);
});
