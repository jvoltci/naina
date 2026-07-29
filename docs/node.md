# Node

```bash
npm install @jvoltci/naina
```

Inference runs on a libuv worker thread, so a read never blocks the event loop.

## Read a page

```js
import { read } from '@jvoltci/naina';

const page = await read('invoice.png', { tier: 'small' });

console.log(page.markdown);
console.log(page.text);

for (const line of page.lines) {
  console.log(line.confidence.toFixed(3), line.text);
}
```

## Reuse the reader

```js
import { Reader } from '@jvoltci/naina';

const reader = new Reader({ tier: 'small' });
try {
  for (const file of await readdir('scans')) {
    const page = await reader.read(join('scans', file));
    console.log(page.markdown);
  }
} finally {
  reader.close();          // releases the native context
}
```

`close()` matters. The context holds loaded models — tens to hundreds of
megabytes — and it is not released by garbage collection.

## Raw pixels

```js
const page = await reader.readRgb(rgbBuffer, width, height);
```

`rgbBuffer` is packed RGB8, `width * height * 3` bytes. Use this with `sharp`,
`canvas`, or frames from a video pipeline.

## Layout regions

```js
for (const r of page.regions.sort((a, b) => a.order - b.order)) {
  console.log(r.order, r.kind, r.bbox);
}
```

## TypeScript

Types ship with the package; no `@types` install.

```ts
import { Reader, type NainaPage, type NainaLine } from '@jvoltci/naina';
```

## Configuration

Reads the same environment variables as every other binding: `NAINA_CACHE`,
`NAINA_OFFLINE`, `NAINA_REGISTRY`. See [Python](python.md#configuration).

!!! note "Setting them from Node"
    The native addon reads these with `getenv()` at load time. Writing
    `process.env.NAINA_CACHE` from inside a worker thread does not always reach
    it — under vitest's default thread pool it does not. Set them in the
    environment before the process starts, or use `pool: 'forks'`.
