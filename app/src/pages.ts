// Turning whatever the user gave us into pages of pixels.
//
// naina ships no image or PDF decoder on purpose — the browser has both, and a
// decoder in the core would be a large dependency used by exactly one platform.
//
// PDF matters more than it looks: most real documents are PDFs, and a tool that
// only takes PNGs is a tool most people cannot use. pdf.js rasterises here on
// the main thread (it is fast and already async), and only the slow part — OCR —
// goes to the worker.

export interface SourcePage {
  /** 1-based, for display. */
  number: number;
  /** Total pages in the source this came from. */
  of: number;
  label: string;
  bitmap: ImageBitmap;
}

/** Rendering PDFs at CSS scale 1.0 gives ~72 dpi, which is far too coarse for
 *  OCR — strokes blur together and recognition degrades badly. 2.5 lands near
 *  180 dpi, which reads reliably without making enormous bitmaps. */
const PDF_SCALE = 2.5;

/** Above this, a page is downscaled before OCR. A 600 dpi A4 scan is ~5000px on
 *  the long side; detection gains nothing from that and memory use is quadratic.
 *  naina's own resize handles the rest. */
const MAX_SIDE = 3000;

function isPdf(file: File | Blob): boolean {
  return file.type === 'application/pdf' || ('name' in file && /\.pdf$/i.test(file.name));
}

async function shrinkIfHuge(bitmap: ImageBitmap): Promise<ImageBitmap> {
  const longest = Math.max(bitmap.width, bitmap.height);
  if (longest <= MAX_SIDE) return bitmap;

  const scale = MAX_SIDE / longest;
  const w = Math.round(bitmap.width * scale);
  const h = Math.round(bitmap.height * scale);

  // resizeQuality 'high' asks for a decent filter. This is the one place the
  // app scales an image, and it only triggers on inputs too large to be useful
  // at full size — normal pages go to naina untouched.
  const shrunk = await createImageBitmap(bitmap, { resizeWidth: w, resizeHeight: h, resizeQuality: 'high' });
  bitmap.close();
  return shrunk;
}

async function pdfToPages(file: File | Blob, onPage?: (n: number, of: number) => void) {
  // Imported lazily so a user who only ever drops images never downloads pdf.js.
  const pdfjs = await import('pdfjs-dist');
  const workerSrc = (await import('pdfjs-dist/build/pdf.worker.mjs?url')).default;
  pdfjs.GlobalWorkerOptions.workerSrc = workerSrc;

  const data = new Uint8Array(await file.arrayBuffer());

  // Keep the loading task: destroy() lives on it, not on the document proxy,
  // and skipping it leaks the pdf.js worker for the life of the tab.
  const task = pdfjs.getDocument({ data });
  const doc = await task.promise;

  try {
    const pages: SourcePage[] = [];
    for (let n = 1; n <= doc.numPages; n++) {
      onPage?.(n, doc.numPages);
      const page = await doc.getPage(n);
      const viewport = page.getViewport({ scale: PDF_SCALE });

      const canvas = new OffscreenCanvas(Math.ceil(viewport.width), Math.ceil(viewport.height));
      const ctx = canvas.getContext('2d');
      if (!ctx) throw new Error('could not get a 2D context to render the PDF');

      // White background. A PDF page is transparent where nothing is drawn, and
      // transparent composited over black inverts the text for the detector.
      ctx.fillStyle = '#ffffff';
      ctx.fillRect(0, 0, canvas.width, canvas.height);

      // pdf.js types these as the HTML variants, but it only uses the 2D
      // drawing surface, which OffscreenCanvas provides identically. Rendering
      // on an OffscreenCanvas avoids attaching a throwaway element to the DOM
      // for every page of a long PDF.
      await page.render({
        canvas: canvas as unknown as HTMLCanvasElement,
        canvasContext: ctx as unknown as CanvasRenderingContext2D,
        viewport,
      }).promise;
      page.cleanup();

      pages.push({
        number: n,
        of: doc.numPages,
        label: doc.numPages > 1 ? `Page ${n}` : 'Page',
        bitmap: await shrinkIfHuge(canvas.transferToImageBitmap()),
      });
    }
    return pages;
  } finally {
    await task.destroy();
  }
}

/**
 * Decode one dropped file into one or more pages.
 *
 * @throws when the file is neither a decodable image nor a readable PDF
 */
export async function toPages(
  file: File | Blob,
  onPage?: (n: number, of: number) => void,
): Promise<SourcePage[]> {
  if (isPdf(file)) {
    return pdfToPages(file, onPage);
  }
  const bitmap = await shrinkIfHuge(await createImageBitmap(file));
  const label = 'name' in file && file.name ? file.name : 'Image';
  return [{ number: 1, of: 1, label, bitmap }];
}

/**
 * Packed RGB8 bytes for naina.
 *
 * No scaling here beyond what shrinkIfHuge already did. Canvas scaling uses a
 * browser-defined filter — the HTML spec leaves it implementation-defined and
 * Chrome, Safari and Firefox disagree — so resizing at this point would make
 * results depend on the browser. naina's own resize is the same code everywhere.
 */
export function toRgb(bitmap: ImageBitmap): Uint8Array {
  const canvas = new OffscreenCanvas(bitmap.width, bitmap.height);
  const ctx = canvas.getContext('2d');
  if (!ctx) throw new Error('could not get a 2D canvas context');
  ctx.drawImage(bitmap, 0, 0);
  const { data } = ctx.getImageData(0, 0, bitmap.width, bitmap.height);

  const rgb = new Uint8Array(bitmap.width * bitmap.height * 3);
  for (let i = 0, j = 0; i < data.length; i += 4, j += 3) {
    rgb[j] = data[i];
    rgb[j + 1] = data[i + 1];
    rgb[j + 2] = data[i + 2];
  }
  return rgb;
}
