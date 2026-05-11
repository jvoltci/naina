// YuNet face detector. Input is a 1x3x640x640 BGR float32 letterboxed image
// (raw 0..255 values, no normalisation). Outputs are 12 tensors, three per
// stride s ∈ {8, 16, 32}: cls_{s}, obj_{s}, bbox_{s}, kps_{s}.

#include "face_detect.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_map>

namespace naina::internal::face_detect {

namespace {

constexpr std::array<int, 3> kStrides = {8, 16, 32};
constexpr int kNumKps = 5;

float iou(const naina_bbox& a, const naina_bbox& b) {
    const float x1 = std::max(a.x, b.x);
    const float y1 = std::max(a.y, b.y);
    const float x2 = std::min(a.x + a.w, b.x + b.w);
    const float y2 = std::min(a.y + a.h, b.y + b.h);
    const float inter = std::max(0.0F, x2 - x1) * std::max(0.0F, y2 - y1);
    const float uni = a.w * a.h + b.w * b.h - inter;
    return uni > 0 ? inter / uni : 0;
}

void nms_inplace(std::vector<naina_face>& faces, float iou_thr) {
    std::sort(faces.begin(), faces.end(), [](const naina_face& l, const naina_face& r) {
        return l.bbox.score > r.bbox.score;
    });
    std::vector<naina_face> kept;
    kept.reserve(faces.size());
    for (const auto& f : faces) {
        bool overlap = false;
        for (const auto& k : kept) {
            if (iou(f.bbox, k.bbox) > iou_thr) {
                overlap = true;
                break;
            }
        }
        if (!overlap) {
            kept.push_back(f);
        }
    }
    faces.swap(kept);
}

}  // namespace

naina_status detect(backend::ISession* session,
                    const ImageView& src,
                    const Config& cfg,
                    std::vector<naina_face>* out_faces) {
    if (session == nullptr || out_faces == nullptr) {
        return NAINA_E_INVALID_ARG;
    }
    out_faces->clear();

    // 1) Preprocess: letterbox to size x size BGR planar float32.
    const int size = cfg.input_size;
    const size_t sz = static_cast<size_t>(size);
    std::vector<float> input(3 * sz * sz);
    const Letterbox lb = letterbox_to_bgr_planar_f32(src, size, 0.0F, input.data());

    Tensor in = Tensor::view(input.data(), {1, 3, size, size}, DType::F32);

    // 2) Allocate output tensors. YuNet's 12 outputs vary in size — query the
    //    session for actual shapes (the model is fixed-shape so dims are known).
    auto out_descs = session->outputs();
    std::vector<Tensor> outs;
    std::vector<std::vector<float>> out_storage(out_descs.size());
    outs.reserve(out_descs.size());
    for (size_t i = 0; i < out_descs.size(); ++i) {
        // Compute element count from declared shape, substituting 1 for any
        // dynamic dim (YuNet's outputs are static).
        size_t n = 1;
        for (auto d : out_descs[i].shape) {
            if (d > 0) {
                n *= static_cast<size_t>(d);
            }
        }
        out_storage[i].assign(n, 0.0F);
        outs.push_back(Tensor::view(out_storage[i].data(), out_descs[i].shape, DType::F32));
    }

    std::vector<Tensor> ins;
    ins.push_back(std::move(in));
    const naina_status rs = session->run(ins, outs);
    if (rs != NAINA_OK) {
        return rs;
    }

    // 3) Index outputs by name for stride-wise decoding.
    std::unordered_map<std::string, const float*> by_name;
    for (size_t i = 0; i < out_descs.size(); ++i) {
        by_name[out_descs[i].name] = static_cast<const float*>(outs[i].data());
    }

    auto get = [&](const std::string& key) -> const float* {
        auto it = by_name.find(key);
        return it == by_name.end() ? nullptr : it->second;
    };

    std::vector<naina_face> raw;
    raw.reserve(64);

    for (int s : kStrides) {
        const std::string suffix = std::to_string(s);
        const float* cls = get("cls_" + suffix);
        const float* obj = get("obj_" + suffix);
        const float* bbox = get("bbox_" + suffix);
        const float* kps = get("kps_" + suffix);
        if (cls == nullptr || obj == nullptr || bbox == nullptr || kps == nullptr) {
            return NAINA_E_INFERENCE_FAILED;
        }
        const int gw = size / s;
        const int gh = size / s;
        for (int gy = 0; gy < gh; ++gy) {
            for (int gx = 0; gx < gw; ++gx) {
                const int idx = gy * gw + gx;
                const float score = std::sqrt(std::max(0.0F, cls[idx] * obj[idx]));
                if (score < cfg.conf) {
                    continue;
                }
                const float ax = static_cast<float>(gx) + 0.5F;
                const float ay = static_cast<float>(gy) + 0.5F;
                const float cx = (ax + bbox[idx * 4 + 0]) * static_cast<float>(s);
                const float cy = (ay + bbox[idx * 4 + 1]) * static_cast<float>(s);
                const float w = std::exp(bbox[idx * 4 + 2]) * static_cast<float>(s);
                const float h = std::exp(bbox[idx * 4 + 3]) * static_cast<float>(s);

                naina_face f{};
                // Unletterbox to source coords.
                f.bbox.x = ((cx - w / 2) - static_cast<float>(lb.pad_x)) / lb.scale;
                f.bbox.y = ((cy - h / 2) - static_cast<float>(lb.pad_y)) / lb.scale;
                f.bbox.w = w / lb.scale;
                f.bbox.h = h / lb.scale;
                f.bbox.score = score;
                for (int k = 0; k < kNumKps; ++k) {
                    const float kx = (ax + kps[idx * 10 + 2 * k]) * static_cast<float>(s);
                    const float ky = (ay + kps[idx * 10 + 2 * k + 1]) * static_cast<float>(s);
                    f.landmarks[k].x = (kx - static_cast<float>(lb.pad_x)) / lb.scale;
                    f.landmarks[k].y = (ky - static_cast<float>(lb.pad_y)) / lb.scale;
                }
                f.quality = std::min(1.0F, score * 1.25F);
                f.track_id = -1;
                raw.push_back(f);
            }
        }
    }

    nms_inplace(raw, cfg.nms_iou);
    *out_faces = std::move(raw);
    return NAINA_OK;
}

}  // namespace naina::internal::face_detect
