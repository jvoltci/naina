/** naina — document reading in the browser, from the same C++ core. */

/** naina_tier, mirrored from core/include/naina/naina.h. */
export declare const TIER: {
  readonly auto: 0;
  readonly tiny: 1;
  readonly small: 2;
  readonly medium: 3;
};

export type TierName = 'auto' | 'tiny' | 'small' | 'medium';

/** One recognised line. `quad` is x0,y0,x1,y1,x2,y2,x3,y3 in source pixels,
 *  clockwise from top-left. */
export interface NainaLine {
  text: string;
  confidence: number;
  score: number;
  /** Index into `regions`, or -1 when layout matched no region. */
  region_id: number;
  quad: number[];
}

/** A layout region. `bbox` is x,y,w,h,score. */
export interface NainaRegion {
  kind:
    | 'unknown'
    | 'title'
    | 'text'
    | 'list'
    | 'table'
    | 'figure'
    | 'caption'
    | 'formula'
    | 'header'
    | 'footer'
    | 'pagenum';
  /** Reading-order index within the page. */
  order: number;
  bbox: number[];
}

export interface NainaPage {
  lines: NainaLine[];
  regions: NainaRegion[];
}

export interface CreateReaderOptions {
  /** Defaults to 'tiny' — 11 MB of weights, the right choice for a browser. */
  tier?: TierName;
  /** ONNX Runtime execution providers, in preference order.
   *  Defaults to ['webgpu', 'wasm']; ort falls back silently. */
  executionProviders?: string[];
  /** Called once per weight file as it is staged. */
  onProgress?: (done: number, total: number, path: string) => void;
  /** Set false to skip staging and call stageTier yourself later. */
  stage?: boolean;
}

export interface NainaReader {
  /**
   * Read a page and return markdown.
   *
   * Async because ASYNCIFY turns any suspending export into a Promise — the C++
   * core is synchronous, but ort-web's run() is not. Returns '' on failure;
   * check lastError().
   *
   * @param rgb packed RGB8 bytes, `width * height * 3` long
   */
  readMarkdown(rgb: Uint8Array, width: number, height: number): Promise<string>;

  /** Read a page and return lines, regions and confidences. */
  readJson(rgb: Uint8Array, width: number, height: number): Promise<NainaPage | null>;

  /** Human-readable status of the last read. */
  lastError(): string;

  /** naina version string. */
  version(): string;

  /** Release the native context. The reader is unusable afterwards. */
  close(): void;

  /** The raw Emscripten module, for callers needing FS or heap access. */
  module: unknown;
}

/** Instantiate naina and fetch a tier's weights. */
export declare function createReader(opts?: CreateReaderOptions): Promise<NainaReader>;

/** Install the JS bridge the C++ backend calls into. Called by createReader. */
export declare function installBridge(
  module: unknown,
  opts?: { executionProviders?: string[] },
): unknown;

/** Fetch and stage every weight file for a tier. */
export declare function stageTier(
  module: unknown,
  tier: number,
  onProgress?: (done: number, total: number, path: string) => void,
): Promise<void>;

/** Stage an explicit file list. Prefer stageTier. */
export declare function stageModels(
  module: unknown,
  files: Array<{ path: string; url: string }>,
  onProgress?: (done: number, total: number, path: string) => void,
): Promise<void>;
