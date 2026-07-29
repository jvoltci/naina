# Python

```bash
pip install naina
```

## Read a page

```python
import naina

page = naina.read("invoice.png", tier="small")

print(page.markdown)          # structured document
print(page.text)              # every line, reading order, no markup

for line in page.lines:
    print(f"{line.confidence:.3f}  {line.text}")
    print(f"  quad: {line.quad}")     # 4 (x, y) corners, source pixels
```

## Reuse the context

`naina.read` builds a context, loads models and tears it down. For more than one
page, keep one around — model loading is the expensive part.

```python
with naina.Reader(tier="small") as reader:
    for path in pathlib.Path("scans").glob("*.png"):
        page = reader.read(path)
        print(page.markdown)
```

## Layout regions

```python
for region in sorted(page.regions, key=lambda r: r.order):
    print(f"{region.order:2d}  {region.kind:8s}  {region.bbox}")
```

```
 0  title     (129.0, 154.0, 766.0, 92.0)
 1  text      (128.0, 293.0, 247.0, 20.0)
 2  title     (126.0, 375.0, 103.0, 22.0)
```

Kinds: `title`, `text`, `list`, `table`, `figure`, `caption`, `formula`,
`header`, `footer`, `pagenum`, `unknown`.

`header`, `footer` and `pagenum` are page furniture and are omitted from
`page.markdown` — they are not part of the document's reading flow.

## Raw pixels

naina ships no image decoder. `read()` accepts a path and uses Pillow when it is
available, but you can pass pixels directly:

```python
import numpy as np

rgb = np.asarray(pil_image.convert("RGB"))   # H x W x 3, uint8
page = reader.read_rgb(rgb)
```

This is the path to use with OpenCV, a camera feed, or a PDF you have already
rasterised — no re-encoding round trip.

## Stages on their own

```python
boxes = reader.detect("scan.png")        # quads, no recognition
regions = reader.layout("scan.png")      # regions, no text
```

Useful for cropping to a region before recognising it, or for checking whether a
page has any text at all before paying for recognition.

## Configuration

| Environment variable | Effect |
|---|---|
| `NAINA_CACHE` | where weights are cached (default `~/.cache/naina/models`) |
| `NAINA_OFFLINE=1` | refuse all network access; fail if a weight is missing |
| `NAINA_REGISTRY` | path to an alternative `registry.yaml` |

## Threads

A `Reader` is not thread-safe. Give each thread its own, or serialise access.
Inference itself releases the GIL, so other Python threads keep running during a
read.
