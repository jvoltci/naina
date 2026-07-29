# Use it from an LLM

naina ships an MCP server, so an assistant can read documents directly instead of
being handed a wall of pre-extracted text.

## Setup

```bash
cd mcp && npm install && npm run build
```

Then register it. For Claude Code:

```bash
claude mcp add naina -- node /absolute/path/to/naina/mcp/dist/index.js
```

Or by hand, in your MCP client's config:

```json
{
  "mcpServers": {
    "naina": {
      "command": "node",
      "args": ["/absolute/path/to/naina/mcp/dist/index.js"]
    }
  }
}
```

## Tools

### `read_document`

Returns markdown. This is the one to reach for by default — it is what you want
in a context window.

| Argument | Type | Notes |
|---|---|---|
| `path` | string | absolute path to an image |
| `tier` | string | `tiny` / `small` / `medium`, default `small` |

### `read_document_detailed`

Returns JSON: every line with its confidence and its quad, plus layout regions
and reading order.

Use this when the model needs to reason about *where* something is — checking a
signature block, pulling a figure caption, or deciding whether a low-confidence
line is trustworthy.

## Why markdown by default

A bag of text loses the difference between a heading and a paragraph, and between
a table cell and prose. Markdown keeps that structure in a form models already
read well, at almost no token cost.

## Protocol version

The server speaks MCP `2025-11-25`.

The newer `2026-07-28` revision suits naina well — it is stateless
request/response, and this server holds no session state — but
`@modelcontextprotocol/sdk@1.30.0` (the newest published) tops out at
`2025-11-25`, and a client asking for the newer revision negotiates down.
Measured, not assumed. Moving up should be a dependency bump rather than a
rewrite; tracked in the [roadmap](ROADMAP.md).
