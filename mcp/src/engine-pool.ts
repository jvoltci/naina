/**
 * A process-local cache of naina Engines, one per tier actually requested.
 *
 * Constructing an Engine is cheap and downloads nothing by itself -- model
 * weights are fetched lazily on the first `.read()` call (see
 * bindings/node/index.ts). But *re*constructing an Engine on every tool call
 * re-runs backend session init, which is not cheap. So we keep one Engine
 * per tier alive for the lifetime of the process and reuse it.
 *
 * This is a plain memoisation cache, not session state: it's keyed only on
 * `tier`, never on which client or call asked for it, and every read()
 * through it is still fully self-contained.
 */

import { Engine, type Backend, type Tier } from '@jvoltci/naina';

export interface BaseEngineOptions {
    backend?: Backend;
    modelsRoot?: string;
    numThreads?: number;
}

/** Reads the engine-wide (not per-call) knobs from the environment. These
 * configure the whole server process, unlike `tier`, which is a per-call
 * tool argument -- see mcp/README.md. */
export function baseOptionsFromEnv(env: NodeJS.ProcessEnv = process.env): BaseEngineOptions {
    const opts: BaseEngineOptions = {};
    if (env.NAINA_MCP_BACKEND) {
        opts.backend = env.NAINA_MCP_BACKEND as Backend;
    }
    if (env.NAINA_MCP_MODELS_ROOT) {
        opts.modelsRoot = env.NAINA_MCP_MODELS_ROOT;
    }
    if (env.NAINA_MCP_NUM_THREADS) {
        const n = Number(env.NAINA_MCP_NUM_THREADS);
        if (Number.isFinite(n) && n > 0) {
            opts.numThreads = n;
        }
    }
    return opts;
}

export class EnginePool {
    private readonly base: BaseEngineOptions;
    private readonly engines = new Map<Tier, Engine>();

    constructor(base: BaseEngineOptions = {}) {
        this.base = base;
    }

    get(tier: Tier = 'auto'): Engine {
        let engine = this.engines.get(tier);
        if (engine === undefined) {
            engine = new Engine({ ...this.base, tier });
            this.engines.set(tier, engine);
        }
        return engine;
    }
}
