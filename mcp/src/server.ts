/**
 * Builds the MCP server instance. Separate from cli.ts so the transport
 * choice (stdio vs HTTP) doesn't tangle with tool registration.
 */

import { McpServer } from '@modelcontextprotocol/sdk/server/mcp.js';

import { version as nainaVersion } from '@jvoltci/naina';

import { EnginePool, baseOptionsFromEnv } from './engine-pool';
import { registerTools } from './tools';

// eslint-disable-next-line @typescript-eslint/no-var-requires
const packageVersion = (require('../package.json') as { version: string }).version;

export function createServer(): McpServer {
    const server = new McpServer(
        { name: 'naina-mcp', version: packageVersion },
        { instructions: `Reads document images (photos/scans) into markdown via naina ${nainaVersion}.` },
    );

    const engines = new EnginePool(baseOptionsFromEnv());
    registerTools(server, engines);

    return server;
}
