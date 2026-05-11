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
#define NAINA_VERSION_MINOR 1
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

/* ─── POD types ───────────────────────────────────────────────────── */

typedef struct {
    float x, y, w, h, score;
} naina_bbox;
typedef struct {
    float x, y;
} naina_point;

typedef struct {
    naina_bbox bbox;
    naina_point landmarks[5]; /* L-eye, R-eye, nose, L-mouth, R-mouth */
    float quality;            /* 0..1, useful for "best frame" picks */
    int32_t track_id;         /* -1 if not from a tracker */
} naina_face;

typedef struct {
    naina_bbox bbox;
    int32_t class_id;
    int32_t track_id; /* -1 if not from a tracker */
} naina_person;

/* ─── Config ──────────────────────────────────────────────────────── */

typedef struct {
    int32_t version; /* must be 1 */
    naina_backend backend;
    naina_device device;
    const char* models_root;        /* NULL → $NAINA_CACHE / default */
    int32_t num_threads;            /* 0 = auto */
    int32_t enable_research_models; /* 0 = permissive only (default) */
} naina_config;

/* ─── Opaque handles ──────────────────────────────────────────────── */

typedef struct naina_ctx naina_ctx_t;
typedef struct naina_image naina_image_t;
typedef struct naina_tracker naina_tracker_t;

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

/* ─── Face stack ──────────────────────────────────────────────────── */

/* Detect: lib allocates output array, caller frees via naina_free_faces. */
NAINA_API naina_status naina_face_detect(naina_ctx_t* ctx,
                                         const naina_image_t* image,
                                         naina_face** out_faces,
                                         int32_t* out_count);
NAINA_API void naina_free_faces(naina_face* faces, int32_t count);

/* Embed: caller provides buffer of size naina_face_embed_dim() floats. */
NAINA_API int32_t naina_face_embed_dim(const naina_ctx_t* ctx);
NAINA_API naina_status naina_face_embed(naina_ctx_t* ctx,
                                        const naina_image_t* image,
                                        const naina_face* face,
                                        float* out_embedding);

/* Liveness: out in [0,1], higher = more live. Threshold per use case. */
NAINA_API naina_status naina_face_liveness(naina_ctx_t* ctx,
                                           const naina_image_t* image,
                                           const naina_face* face,
                                           float* out_score);

/* Pure-function similarity (cosine). Exposed for convenience. */
NAINA_API float naina_embed_similarity(const float* a, const float* b, int32_t dim);

/* ─── Person stack ────────────────────────────────────────────────── */

NAINA_API naina_status naina_person_detect(naina_ctx_t* ctx,
                                           const naina_image_t* image,
                                           naina_person** out_persons,
                                           int32_t* out_count);
NAINA_API void naina_free_persons(naina_person* persons, int32_t count);

NAINA_API int32_t naina_reid_embed_dim(const naina_ctx_t* ctx);
NAINA_API naina_status naina_person_reid(naina_ctx_t* ctx,
                                         const naina_image_t* image,
                                         const naina_person* person,
                                         float* out_embedding);

/* ─── Tracking (stateful, per video stream) ───────────────────────── */

NAINA_API naina_status naina_tracker_create(naina_ctx_t* ctx, naina_tracker_t** out);
NAINA_API void naina_tracker_release(naina_tracker_t* tracker);

/* Feed per-frame detections; receive same boxes annotated with track_ids. */
NAINA_API naina_status naina_tracker_update(naina_tracker_t* tracker,
                                            const naina_person* detections,
                                            int32_t det_count,
                                            naina_person** out_tracked,
                                            int32_t* out_count);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* NAINA_H */
