// Python bindings for naina. Wraps naina::Engine + naina::Image and adds a
// thin numpy-array adapter so users can pass `np.ndarray` directly.

#include "naina/naina.hpp"

#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

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

using NpImage = py::array_t<uint8_t, py::array::c_style | py::array::forcecast>;

naina::Image image_from_numpy(const NpImage& arr) {
    const auto fmt = infer_pixfmt(arr);
    const int h = static_cast<int>(arr.shape(0));
    const int w = static_cast<int>(arr.shape(1));
    const int channels = (arr.ndim() == 3) ? static_cast<int>(arr.shape(2)) : 1;
    const int stride = w * channels;
    return naina::Image(arr.data(), w, h, stride, fmt);
}

}  // namespace

PYBIND11_MODULE(_binding, m) {
    m.doc() = "naina — embeddable document reading runtime (Python binding)";
    m.attr("__version__") = naina_version_string();

    py::register_exception<naina::Error>(m, "NainaError");

    py::enum_<naina::Backend>(m, "Backend")
        .value("AUTO", naina::Backend::Auto)
        .value("ONNXRUNTIME", naina::Backend::ONNXRuntime)
        .value("NCNN", naina::Backend::NCNN)
        .value("COREML", naina::Backend::CoreML)
        .value("TENSORRT", naina::Backend::TensorRT)
        .value("OPENVINO", naina::Backend::OpenVINO);

    // Device tier: model size, not licence. Every model naina ships is
    // Apache-2.0; tiers differ in size and the hardware they suit.
    py::enum_<naina::Tier>(m, "Tier")
        .value("AUTO", naina::Tier::Auto)
        .value("TINY", naina::Tier::Tiny)
        .value("SMALL", naina::Tier::Small)
        .value("MEDIUM", naina::Tier::Medium);

    py::class_<naina::Point>(m, "Point")
        .def_readonly("x", &naina::Point::x)
        .def_readonly("y", &naina::Point::y)
        .def("__repr__", [](const naina::Point& p) {
            return "Point(x=" + std::to_string(p.x) + ", y=" + std::to_string(p.y) + ")";
        });

    py::class_<naina::Line>(m, "Line")
        .def_readonly("text", &naina::Line::text)
        .def_readonly("confidence", &naina::Line::confidence)
        .def_readonly("score", &naina::Line::score)
        .def_readonly("quad", &naina::Line::quad)
        .def("__str__", [](const naina::Line& l) { return l.text; })
        .def("__repr__", [](const naina::Line& l) {
            return "Line(text=" + py::repr(py::cast(l.text)).cast<std::string>() +
                   ", confidence=" + std::to_string(l.confidence) + ")";
        });

    py::class_<naina::Page>(m, "Page")
        .def_property_readonly("lines", &naina::Page::lines)
        .def_property_readonly("markdown", &naina::Page::markdown)
        .def_property_readonly("json", &naina::Page::json)
        .def_property_readonly(
            "text", [](const naina::Page& p) { return p.markdown(); },
            "Alias for markdown — the plain reading of the page.")
        .def("__str__", &naina::Page::markdown)
        .def("__len__", [](const naina::Page& p) { return p.lines().size(); })
        .def("__repr__", [](const naina::Page& p) {
            return "Page(" + std::to_string(p.lines().size()) + " lines)";
        });

    py::class_<naina::Engine>(m, "Engine")
        .def(py::init([](naina::Backend backend,
                         naina::Tier tier,
                         const std::string& models_root,
                         int num_threads,
                         const std::string& language) {
                 naina::Config cfg;
                 cfg.backend = backend;
                 cfg.tier = tier;
                 cfg.num_threads = num_threads;
                 cfg.language = language;
                 if (!models_root.empty()) {
                     cfg.models_root = models_root;
                 }
                 return new naina::Engine(cfg);
             }),
             py::arg("backend") = naina::Backend::Auto,
             py::arg("tier") = naina::Tier::Auto,
             py::arg("models_root") = std::string(),
             py::arg("num_threads") = 0,
             // Recognition alphabet. "" is Latin + CJK; "devanagari" reads
             // Hindi, Marathi, Nepali and Sanskrit. An unknown value raises
             // rather than silently reading with the wrong alphabet.
             py::arg("language") = std::string())
        .def(
            "read",
            [](naina::Engine& self, const NpImage& image) {
                const naina::Image img = image_from_numpy(image);
                // Inference is pure native work and can take hundreds of
                // milliseconds on a dense page, so release the GIL.
                py::gil_scoped_release unlock;
                return self.read(img);
            },
            py::arg("image"),
            "Read a document. Returns a Page with .markdown, .json and .lines.")
        .def(
            "detect_text",
            [](naina::Engine& self, const NpImage& image) {
                const naina::Image img = image_from_numpy(image);
                py::gil_scoped_release unlock;
                return self.detect_text(img);
            },
            py::arg("image"),
            "Detect text quads without recognising them. Returns a list of "
            "4-corner quads in source image coordinates.");

    // The agent-facing one-liner.
    m.def(
        "read",
        [](const NpImage& image, naina::Tier tier) {
            naina::Config cfg;
            cfg.tier = tier;
            naina::Engine engine(cfg);
            const naina::Image img = image_from_numpy(image);
            py::gil_scoped_release unlock;
            return engine.read(img).markdown();
        },
        py::arg("image"),
        py::arg("tier") = naina::Tier::Auto,
        "Read an image and return markdown. Constructs a throwaway Engine, so "
        "prefer Engine.read for repeated calls.");
}
