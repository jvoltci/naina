/* naina — C ABI
 *
 * This header is the contract. Every binding (Python, Node, Rust, Swift,
 * Kotlin, WASM) targets this surface. C++ users may prefer naina.hpp,
 * which is a thin wrapper over these same symbols.
 *
 * ABI rules:
 *   - No exceptions cross this boundary.
 *   - Status codes for all fallible calls.
 *   - Caller owns inputs; lib owns outputs unless documented otherwise.
 *   - Opaque handles only; never expose layout.
 *   - Add fields only via versioned config structs.
 */
#ifndef NAINA_H
#define NAINA_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NAINA_VERSION_MAJOR 0
#define NAINA_VERSION_MINOR 2
#define NAINA_VERSION_PATCH 0

/* Symbol export. CMake defines NAINA_BUILDING_SHARED when compiling the
 * shared library; consumers don't need to define anything. */
#if defined(_WIN32) || defined(__CYGWIN__)
#if defined(NAINA_BUILDING_SHARED)
#define NAINA_API __declspec(dllexport)
#else
#define NAINA_API __declspec(dllimport)
#endif
#else
#if defined(NAINA_BUILDING_SHARED)
#define NAINA_API __attribute__((visibility("default")))
#else
#define NAINA_API
#endif
#endif

/* ─── Status & enums ──────────────────────────────────────────────── */

typedef enum {
    NAINA_OK = 0,
    NAINA_E_INVALID_ARG = 1,
    NAINA_E_NOT_INITIALIZED = 2,
    NAINA_E_MODEL_NOT_FOUND = 3,
    NAINA_E_BACKEND_UNAVAIL = 4,
    NAINA_E_INFERENCE_FAILED = 5,
    NAINA_E_OOM = 6,
    NAINA_E_UNSUPPORTED = 7,
    NAINA_E_IO = 8,
} naina_status;

typedef enum {
    NAINA_BACKEND_AUTO = 0,
    NAINA_BACKEND_ONNXRUNTIME,
    NAINA_BACKEND_OPENVINO,
    NAINA_BACKEND_NCNN,
    NAINA_BACKEND_MNN,
    NAINA_BACKEND_COREML,
    NAINA_BACKEND_TENSORRT,
    NAINA_BACKEND_EXECUTORCH,
} naina_backend;

typedef enum {
    NAINA_DEVICE_AUTO = 0,
    NAINA_DEVICE_CPU,
    NAINA_DEVICE_GPU,
    NAINA_DEVICE_NPU,
} naina_device;

typedef enum {
    NAINA_PIXFMT_RGB8 = 0,
    NAINA_PIXFMT_BGR8,
    NAINA_PIXFMT_NV12, /* common from V4L2 / Android cameras */
    NAINA_PIXFMT_YUV420P,
    NAINA_PIXFMT_GRAY8,
} naina_pixfmt;

/* Device tier. Selects model size, not licence — every model naina ships is
 * Apache-2.0.
 *   TINY    ~11 MB  — browser, phone, Pi Zero
 *   SMALL   ~54 MB  — laptop, Pi 5, mobile app
 *   MEDIUM  ~269 MB — server, desktop
 * AUTO resolves to SMALL today; it exists so a future release can choose by
 * probing available memory without an ABI change. */
typedef enum {
    NAINA_TIER_AUTO = 0,
    NAINA_TIER_TINY,
    NAINA_TIER_SMALL,
    NAINA_TIER_MEDIUM,
} naina_tier;

/* Layout region classes emitted by naina_layout_detect. */
typedef enum {
    NAINA_REGION_UNKNOWN = 0,
    NAINA_REGION_TITLE,
    NAINA_REGION_TEXT,
    NAINA_REGION_LIST,
    NAINA_REGION_TABLE,
    NAINA_REGION_FIGURE,
    NAINA_REGION_CAPTION,
    NAINA_REGION_FORMULA,
    NAINA_REGION_HEADER,
    NAINA_REGION_FOOTER,
    NAINA_REGION_PAGENUM,
} naina_region_kind;

/* ─── POD types ───────────────────────────────────────────────────── */

typedef struct {
    float x, y, w, h, score;
} naina_bbox;
typedef struct {
    float x, y;
} naina_point;

/* A detected text quad. Corners are clockwise from top-left, in SOURCE image
 * coordinates. Quads are not necessarily axis-aligned — skewed and rotated
 * text produces genuinely rotated quads. */
typedef struct {
    naina_point corners[4];
    float score;
} naina_textbox;

/* A recognised line of text. `text` is UTF-8, NUL-terminated, and owned by
 * the naina_page_t that produced it — it dangles after naina_page_release. */
typedef struct {
    naina_textbox box;
    const char* text;
    float confidence;
    int32_t region_id; /* index into the page's regions, -1 if unassigned */
} naina_textline;

/* A layout region. `order` is the reading-order index within the page. */
typedef struct {
    naina_bbox bbox;
    naina_region_kind kind;
    int32_t order;
} naina_region;

/* ─── Config ──────────────────────────────────────────────────────── */

typedef struct {
    int32_t version; /* 1 = pre-OCR; 2 adds `tier`; 3 adds `language` */
    naina_backend backend;
    naina_device device;
    const char* models_root;        /* NULL → $NAINA_CACHE / default */
    int32_t num_threads;            /* 0 = auto */
    int32_t enable_research_models; /* retained for ABI compatibility; ignored */
    naina_tier tier;                /* honoured when version >= 2 */

    /* Recognition alphabet, e.g. "devanagari". NULL or "" selects the default
     * (Latin + CJK). Honoured when version >= 3.
     *
     * A string rather than an enum on purpose: the set of scripts belongs to
     * upstream and grows, and an enum would force an ABI change per language.
     *
     * An unknown language is an error (NAINA_E_UNSUPPORTED), never a silent
     * fall back to the default alphabet. Recognising Devanagari with a Latin
     * alphabet is what produced text like "3rarearanlus Tarafaaa:" at 0.758
     * confidence, and that failure mode is the reason this field exists. */
    const char* language;
} naina_config;

/* ─── Opaque handles ──────────────────────────────────────────────── */

typedef struct naina_ctx naina_ctx_t;
typedef struct naina_image naina_image_t;
typedef struct naina_page naina_page_t;

/* ─── Lifecycle ───────────────────────────────────────────────────── */

NAINA_API naina_status naina_init(const naina_config* cfg, naina_ctx_t** out_ctx);
NAINA_API void naina_release(naina_ctx_t* ctx);
NAINA_API const char* naina_version_string(void);
NAINA_API const char* naina_status_str(naina_status s);

/* ─── Image wrapping (zero-copy view; lib does not own pixels) ───── */

NAINA_API naina_status naina_image_wrap(const uint8_t* data,
                                        int32_t width,
                                        int32_t height,
                                        int32_t stride,
                                        naina_pixfmt fmt,
                                        naina_image_t** out_image);
NAINA_API void naina_image_release(naina_image_t* image);

/* ─── Reading a page (the primary API) ────────────────────────────── */

/* Run the full pipeline: detect → rectify → recognise → layout → assemble.
 * The returned page owns every string it hands out. Release exactly once. */
NAINA_API naina_status naina_read(naina_ctx_t* ctx,
                                  const naina_image_t* image,
                                  naina_page_t** out_page);
NAINA_API void naina_page_release(naina_page_t* page);

/* Borrowed views into the page. Valid until naina_page_release. */
NAINA_API naina_status naina_page_lines(const naina_page_t* page,
                                        const naina_textline** out_lines,
                                        int32_t* out_count);
NAINA_API naina_status naina_page_regions(const naina_page_t* page,
                                          const naina_region** out_regions,
                                          int32_t* out_count);

/* Serialised views. Borrowed, UTF-8, NUL-terminated. Never null; the empty
 * string means the page had no text. */
NAINA_API const char* naina_page_markdown(const naina_page_t* page);
NAINA_API const char* naina_page_json(const naina_page_t* page);

/* ─── Stage-level access ──────────────────────────────────────────── */

/* Lib allocates; caller frees with the matching free function. */
NAINA_API naina_status naina_text_detect(naina_ctx_t* ctx,
                                         const naina_image_t* image,
                                         naina_textbox** out_boxes,
                                         int32_t* out_count);
NAINA_API void naina_free_textboxes(naina_textbox* boxes, int32_t count);

NAINA_API naina_status naina_layout_detect(naina_ctx_t* ctx,
                                           const naina_image_t* image,
                                           naina_region** out_regions,
                                           int32_t* out_count);
NAINA_API void naina_free_regions(naina_region* regions, int32_t count);

/* Human-readable name for a region class. Static string, never null. */
NAINA_API const char* naina_region_kind_str(naina_region_kind k);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* NAINA_H */
