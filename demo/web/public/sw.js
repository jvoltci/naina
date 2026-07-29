// Offline for the app shell.
//
// Two caches, on purpose:
//
//   naina-shell-<v>   HTML, JS, CSS, naina.wasm. Versioned, because these change
//                     with every deploy; old versions are deleted on activate.
//   naina-models-v1   Written by the naina runtime, NOT by this worker. Keyed on
//                     immutable release URLs, so entries never need revalidating
//                     and must survive a shell upgrade — a 269 MB re-download
//                     because the CSS changed would be indefensible.
//
// Bumping SHELL forces fresh app code. Never add the model cache to the delete
// list below.

const SHELL = 'naina-shell-v2';

self.addEventListener('install', (event) => {
  // Take over immediately rather than waiting for every tab to close.
  self.skipWaiting();
  event.waitUntil(
    caches.open(SHELL).then((cache) => cache.addAll(['./', './index.html'])),
  );
});

self.addEventListener('activate', (event) => {
  event.waitUntil(
    (async () => {
      const names = await caches.keys();
      await Promise.all(
        names
          .filter((n) => n.startsWith('naina-shell-') && n !== SHELL)
          .map((n) => caches.delete(n)),
      );
      await self.clients.claim();
    })(),
  );
});

self.addEventListener('fetch', (event) => {
  const req = event.request;
  if (req.method !== 'GET') return;

  const url = new URL(req.url);

  // Model weights: the runtime already manages naina-models-v1 through the Cache
  // API. Intercepting here would create a second copy of a file that can be
  // 269 MB, so leave these alone entirely.
  if (url.pathname.includes('/releases/download/')) return;

  // Cross-origin (fonts, ort-web CDN chunks): network, falling back to whatever
  // was cached, so a cold offline start still renders.
  if (url.origin !== self.location.origin) {
    event.respondWith(
      fetch(req)
        .then((res) => {
          const copy = res.clone();
          caches.open(SHELL).then((c) => c.put(req, copy));
          return res;
        })
        .catch(() => caches.match(req)),
    );
    return;
  }

  // Same-origin: cache first for speed, then refresh in the background so the
  // next load gets the new build.
  event.respondWith(
    caches.match(req).then((hit) => {
      const fetching = fetch(req)
        .then((res) => {
          if (res.ok) {
            const copy = res.clone();
            caches.open(SHELL).then((c) => c.put(req, copy));
          }
          return res;
        })
        .catch(() => hit);
      return hit ?? fetching;
    }),
  );
});
