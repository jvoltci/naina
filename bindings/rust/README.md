# naina — Rust

Read documents. OCR from naina's C++ core over the C ABI.

```rust
use naina::{Reader, Tier};

let reader = Reader::builder().tier(Tier::Small).build()?;
let page = reader.read_rgb(&rgb, width, height)?;

println!("{}", page.markdown());
for line in page.lines() {
    println!("{:.3}  {}", line.confidence, line.text);
}
```

Text, layout regions, reading order and markdown — the same core, and the same
answers, as naina's Python and Node packages.

## Scripts

Latin and CJK by default. Devanagari (Hindi, Marathi, Nepali, Sanskrit) on request:

```rust
let reader = Reader::builder().language("devanagari").build()?;
```

Reading Devanagari with the default alphabet does **not** fail — it returns
plausible-looking Latin at high confidence. `confidence` measures certainty
*within* the model's own alphabet and cannot express "wrong alphabet". An
unrecognised language is an error, never a quiet fallback.

## Building

naina links ONNX Runtime, which is not vendored.

```bash
# Against an existing build of the core (development in the naina repo)
NAINA_LIB_DIR=/path/to/naina/build/macos-arm64/core cargo test

# Or compile the vendored core, pointing at ONNX Runtime
ONNXRUNTIME_ROOT=/path/to/onnxruntime cargo build
```

Platform floors come from ONNX Runtime, not from naina: **macOS arm64 needs 13.3**
and **Linux needs glibc 2.28**.

## Safety

The unsafe surface is confined to `src/ffi.rs` and the marshalling in `read_rgb`.
Two invariants the C types cannot express, and which the wrapper enforces:

- A `naina_page_t` owns every string it hands out, so all text is copied into
  owned `String`s before the page is released. A returned `Page` has no lifetime
  tied to the reader.
- A `naina_image_t` is a *view* over caller pixels, so the slice is kept alive
  across the read and the view is dropped first.

`Reader` is `Send` but **not** `Sync`: the native context is not internally
synchronised, and concurrent reads on one context would race on its session
cache. Use one reader per thread.

## Tests

```bash
NAINA_LIB_DIR=<...>/core NAINA_RUST_FIXTURE=/path/page.rgb cargo test
```

Tests skip loudly, naming what is missing, rather than passing vacuously. Among
them is an assertion that `naina_config` is 48 bytes with 8-byte alignment —
matching the C compiler's layout. A mismatch there would write `tier` into the
wrong slot and silently load a different model, so it is checked rather than
assumed.

## Limitations

Arabic, Tamil, Telugu, Thai, Korean and Cyrillic are not wired up yet.
Handwriting is unreliable. Tables are located and labelled but their cell
structure is not parsed. Full list:
[jvoltci.github.io/naina/doc/limits](https://jvoltci.github.io/naina/doc/limits/).

## Publishing

`vendor/` is a build product, not source. Regenerate it first:

```bash
./vendor.sh
cargo publish
```
