// Python bindings for naina. Wraps naina::Engine + naina::Image and adds a
// thin numpy-array adapter so users can pass `np.ndarray` directly.

#include "naina/naina.hpp"

#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <cstdint>
#include <stdexcept>
#include <string>

namespace py = pybind11;

namespace {

// Map a (H, W, 3) or (H, W) uint8 numpy array → naina::PixFmt.
naina::PixFmt infer_pixfmt(const py::array_t<uint8_t>& arr) {
    if (arr.ndim() == 2) {
        return naina::PixFmt::Gray8;
    }
    if (arr.ndim() == 3 && arr.shape(2) == 3) {
        return naina::PixFmt::RGB8;
    }
    if (arr.ndim() == 3 && arr.shape(2) == 1) {
        return naina::PixFmt::Gray8;
    }
    throw std::invalid_argument("naina: image must be (H, W, 3) RGB or (H, W) grayscale uint8");
}

naina::Image image_from_numpy(py::array_t<uint8_t, py::array::c_style | py::array::forcecast> arr) {
    const auto fmt = infer_pixfmt(arr);
    const int h = static_cast<int>(arr.shape(0));
    const int w = static_cast<int>(arr.shape(1));
    const int channels = (arr.ndim() == 3) ? static_cast<int>(arr.shape(2)) : 1;
    const int stride = w * channels;
    return naina::Image(arr.data(), w, h, stride, fmt);
}

py::array_t<float> embedding_to_numpy(std::vector<float>&& v) {
    py::array_t<float> out(static_cast<py::ssize_t>(v.size()));
    std::memcpy(out.mutable_data(), v.data(), v.size() * sizeof(float));
    return out;
}

}  // namespace

PYBIND11_MODULE(_binding, m) {
    m.doc() = "naina — embeddable face & person CV runtime (Python binding)";
    m.attr("__version__") = naina_version_string();

    py::register_exception<naina::Error>(m, "NainaError");

    py::enum_<naina::Backend>(m, "Backend")
        .value("AUTO", naina::Backend::Auto)
        .value("ONNXRUNTIME", naina::Backend::ONNXRuntime)
        .value("NCNN", naina::Backend::NCNN)
        .value("COREML", naina::Backend::CoreML)
        .value("TENSORRT", naina::Backend::TensorRT)
        .value("OPENVINO", naina::Backend::OpenVINO);

    py::class_<naina::BBox>(m, "BBox")
        .def_readonly("x", &naina::BBox::x)
        .def_readonly("y", &naina::BBox::y)
        .def_readonly("w", &naina::BBox::w)
        .def_readonly("h", &naina::BBox::h)
        .def_readonly("score", &naina::BBox::score)
        .def("__repr__", [](const naina::BBox& b) {
            return "<BBox x=" + std::to_string(b.x) + " y=" + std::to_string(b.y) +
                   " w=" + std::to_string(b.w) + " h=" + std::to_string(b.h) +
                   " score=" + std::to_string(b.score) + ">";
        });

    py::class_<naina::Point>(m, "Point")
        .def_readonly("x", &naina::Point::x)
        .def_readonly("y", &naina::Point::y);

    py::class_<naina::Face>(m, "Face")
        .def_readonly("bbox", &naina::Face::bbox)
        .def_readonly("quality", &naina::Face::quality)
        .def_readonly("track_id", &naina::Face::track_id)
        .def_property_readonly("landmarks",
                               [](const naina::Face& f) {
                                   py::list out;
                                   for (const auto& p : f.landmarks) {
                                       out.append(p);
                                   }
                                   return out;
                               })
        .def("__repr__", [](const naina::Face& f) {
            return "<Face bbox=(" + std::to_string(f.bbox.x) + "," + std::to_string(f.bbox.y) +
                   "," + std::to_string(f.bbox.w) + "," + std::to_string(f.bbox.h) +
                   ") score=" + std::to_string(f.bbox.score) + ">";
        });

    py::class_<naina::Engine>(m, "Engine")
        .def(py::init([](naina::Backend backend,
                         const std::string& models_root,
                         int num_threads,
                         bool enable_research_models) {
                 naina::Config cfg;
                 cfg.backend = backend;
                 cfg.models_root = std::filesystem::path(models_root);
                 cfg.num_threads = num_threads;
                 cfg.enable_research_models = enable_research_models;
                 return new naina::Engine(cfg);
             }),
             py::arg("backend") = naina::Backend::Auto,
             py::arg("models_root") = "",
             py::arg("num_threads") = 0,
             py::arg("enable_research_models") = false,
             "Construct the runtime. Set NAINA_REGISTRY=… or pass models_root "
             "to point at the registry.yaml. Set NAINA_OFFLINE=1 to skip "
             "downloads (uses cache only).")
        .def(
            "detect_faces",
            [](naina::Engine& self, py::array_t<uint8_t> image) {
                auto img = image_from_numpy(image);
                py::gil_scoped_release rel;
                return self.detect_faces(img);
            },
            py::arg("image"))
        .def(
            "embed_face",
            [](naina::Engine& self, py::array_t<uint8_t> image, const naina::Face& face) {
                auto img = image_from_numpy(image);
                std::vector<float> emb;
                {
                    py::gil_scoped_release rel;
                    emb = self.embed_face(img, face);
                }
                return embedding_to_numpy(std::move(emb));
            },
            py::arg("image"),
            py::arg("face"))
        .def("face_embed_dim", &naina::Engine::face_embed_dim)
        .def(
            "face_liveness",
            [](naina::Engine& self, py::array_t<uint8_t> image, const naina::Face& face) {
                auto img = image_from_numpy(image);
                py::gil_scoped_release rel;
                return self.face_liveness(img, face);
            },
            py::arg("image"),
            py::arg("face"),
            "Liveness probability for `face`. Caller picks a threshold; "
            "0.5 is a reasonable starting point.");

    m.def(
        "similarity",
        [](py::array_t<float> a, py::array_t<float> b) {
            if (a.ndim() != 1 || b.ndim() != 1 || a.shape(0) != b.shape(0)) {
                throw std::invalid_argument(
                    "similarity: inputs must be 1-D float arrays of equal length");
            }
            return naina::Engine::similarity(a.data(), b.data(), static_cast<int>(a.shape(0)));
        },
        py::arg("a"),
        py::arg("b"),
        "Cosine similarity between two L2-normalized embedding vectors.");
}
