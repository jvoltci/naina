import { defineConfig } from 'vitest/config';

export default defineConfig({
    test: {
        // Run tests in child processes, not worker threads.
        //
        // The native addon reads configuration through std::getenv (NAINA_REGISTRY,
        // NAINA_CACHE, NAINA_OFFLINE). Under vitest's default `threads` pool,
        // process.env writes made inside a test file do not reach the addon's
        // getenv, so those tests silently skip instead of asserting anything.
        // Verified: `--pool=threads` skips 3 of 6, `--pool=forks` runs 5 of 6
        // (the sixth is the NAINA_E2E-gated real-weights test).
        pool: 'forks',

        // Real inference plus first-run CoreML EP compilation takes several
        // seconds, well past the 5s default.
        testTimeout: 30_000,
    },
});
