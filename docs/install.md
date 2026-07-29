# Install

## Python

```bash
pip install naina
```

Wheels are self-contained: ONNX Runtime is bundled, so there is nothing else to
install and no system library to match.

Supported: CPython 3.9–3.13 on macOS (arm64, x64), Linux (x64, arm64) and
Windows (x64).

```python
import naina

page = naina.read("scan.png")
print(page.markdown)

for line in page.lines:
    print(f"{line.confidence:.2f}  {line.text}")
```

## Node

```bash
npm install @jvoltci/naina
```

Inference runs off the event loop, so a read does not block the process.

```js
import { read } from '@jvoltci/naina';

const page = await read('scan.png');
console.log(page.markdown);
```

## Browser

Nothing to install — [use the tool](https://jvoltci.github.io/naina/).

To embed it in your own page:

```bash
npm install @jvoltci/naina-wasm onnxruntime-web
```

See [Browser](browser.md); there is one deployment detail you cannot skip.

## From source

Needs CMake 3.24+, a C++20 compiler, and ONNX Runtime.

```bash
git clone https://github.com/jvoltci/naina
cd naina
cmake --preset macos-arm64        # or linux-x64, windows-x64
cmake --build build/macos-arm64
ctest --test-dir build/macos-arm64
```

!!! warning "Backends are opt-in"
    `NAINA_WITH_ONNXRUNTIME` defaults to `OFF`. A build without it produces a
    working library with no inference backend, and the test suite still reports
    green because every test that needs one *skips*.

    The presets enable it. If you configure by hand, pass
    `-DNAINA_WITH_ONNXRUNTIME=ON`, and set `NAINA_REQUIRE_BACKEND=1` when
    running tests to turn those skips into failures.

## Model weights

Weights download on first use and are cached under
`~/.cache/naina/models` (override with `NAINA_CACHE`).

Every file is verified against the sha256 in `models/registry.yaml`. A mismatch
is an error, not a warning — a corrupted cache entry will not be used.

To pre-fetch, or to run air-gapped:

```bash
# Fetch ahead of time
python -c "import naina; naina.fetch(tier='small')"

# Then refuse all network access at runtime
export NAINA_OFFLINE=1
```
