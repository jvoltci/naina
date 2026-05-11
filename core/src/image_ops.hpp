// Internal image helpers: letterbox resize, color conversion, bilinear warp.
// Plain C++ for now; SIMD specialisation is a v1.1 optimisation.
#ifndef NAINA_INTERNAL_IMAGE_OPS_HPP
#define NAINA_INTERNAL_IMAGE_OPS_HPP

#include "naina/naina.h"

#include <cstdint>

namespace naina::internal {

struct ImageView {
    const uint8_t* data;
    int32_t width;
    int32_t height;
    int32_t stride;  // bytes per row
    naina_pixfmt fmt;
};

// Letterbox metadata for mapping detections back to the source image.
struct Letterbox {
    float scale;  // = target / max(src_w, src_h)
    int32_t pad_x;
    int32_t pad_y;
    int32_t src_w;
    int32_t src_h;
};

// Letterbox `src` into a `size`x`size` BGR planar float32 buffer.
// `dst` must hold 3*size*size floats. Pixel values are passed through unmodified
// (raw 0..255 as float). Pad area is filled with `pad_value`.
//
// Returns the Letterbox parameters needed to invert the mapping.
Letterbox letterbox_to_bgr_planar_f32(const ImageView& src, int size, float pad_value, float* dst);

// Same as above but produces an RGB planar float32, with per-channel
// (x - mean) / std normalisation. Used by face_embed preprocessing.
// `dst` holds 3*out*out floats.
void warp_5pt_rgb_planar_f32(const ImageView& src,
                             const naina_point landmarks[5],
                             int out_size,
                             const float mean[3],
                             const float scale[3],
                             float* dst);

// Square-crop around a face bbox with `expand` ratio (e.g. 2.7 for liveness),
// resize to `out_size`x`out_size`, and write to RGB planar float32 with
// per-channel (x - mean) / scale normalisation. Out-of-bounds samples fall
// back to nearest-edge clamping (no padding).
void crop_face_rgb_planar_f32(const ImageView& src,
                              const naina_bbox& bbox,
                              float expand,
                              int out_size,
                              const float mean[3],
                              const float scale[3],
                              float* dst);

}  // namespace naina::internal

#endif  // NAINA_INTERNAL_IMAGE_OPS_HPP
