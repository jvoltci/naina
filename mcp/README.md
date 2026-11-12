# naina-mcp

An MCP server that lets an AI agent read documents through
[naina](https://github.com/jvoltci/naina).

Two tools, deliberately:

| Tool | Returns |
| --- | --- |
| `read_document` | structured markdown — headings, paragraphs, tables |
| `read_document_detailed` | JSON with per-line text, confidence and quad coordinates |

Both accept either a local `path` or `imageBase64`, plus an optional
`tier` (`tiny` / `small` / `medium`).

## Client configuration

```json
{
  "mcpServers": {
    "naina": {
      "command": "npx",
      "args": ["-y", "@jvoltci/naina-mcp"]
    }
  }
}
```

## Protocol revision — read this

The MCP spec revision **2026-07-28** made the protocol stateless
request/response, which suits naina exactly: reading a page carries no session
state. This server is written that way — no per-session state anywhere; the
cached `Engine` is a process-local optimisation, not conversation state.

**But the official SDK does not support that revision yet.** Measured against
`@modelcontextprotocol/sdk@1.30.0`, the newest published version:

```
LATEST_PROTOCOL_VERSION    2025-11-25
SUPPORTED                  2025-11-25, 2025-06-18, 2025-03-26, 2024-11-05, 2024-10-07
```

A client requesting `2026-07-28` negotiates down to `2025-11-25`. Verified, not
assumed. When the SDK adds the newer revision this should be a dependency bump
rather than a rewrite, because the server holds no state to unwind.

## Notes

- naina ships no image decoder, so this server decodes with `sharp` and hands
  naina raw RGB.
- Model weights download on first use per tier and cache under `$NAINA_CACHE`.
  The first call for a tier can take a while; later calls are fast.
- Errors are written to be actionable by an agent — a missing file, an
  undecodable image and absent weights each say something different.

## Build from source

```bash
cd mcp
npm install
npm run build
node dist/cli.js      # speaks MCP over stdio
```
