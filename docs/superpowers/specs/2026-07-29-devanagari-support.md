# Devanagari (and multi-script) support

**Status:** designed and verified, not implemented.
**Priority:** highest open item. See "Why this is urgent".

## The problem

naina cannot read Devanagari, and it does not say so. Fed a Hindi page it
returns lines like `3rarearanlus Tarafaaa: f:` at **0.758 confidence**.

The recognition model's alphabet is Latin + CJK. When it meets a Devanagari
glyph it does what CTC always does: picks the most probable class from the
alphabet it *has*. Confidence measures certainty *within that alphabet*, so it
stays high. There is no signal anywhere in the output that the script is
unreadable.

## Why this is urgent

- The project is named a Hindi word. Hindi input is not an edge case.
- The web app is public. Anyone can feed it Hindi today and get fabricated text.
- Silent wrong output is worse than an error. A user who gets `Error: script not
  supported` retries elsewhere; a user who gets fluent-looking nonsense may
  believe it.

## What already works

**Detection is script-agnostic and already correct.** Measured on a Devanagari
page: DBNet found **82 text lines**, correctly located. Layout also ran.

Only *recognition* is wrong. That narrows the change enormously — no new engine,
no detection work, no layout work.

## The model (verified 2026-07-29)

`PaddlePaddle/devanagari_PP-OCRv5_mobile_rec_onnx` on Hugging Face.

| Property | Value | Compatible? |
|---|---|---|
| Licence | Apache-2.0 | yes, same as everything naina ships |
| Format | ONNX (`inference.onnx`) | **yes — no paddle2onnx step needed** |
| Size | 7.9 MB | fits the tiny tier's budget |
| Input | `x`: `[N, 3, 48, W]` | **yes — 48px height, same as PP-OCRv6 rec** |
| Output | `[N, T, 570]` | yes, CTC logits |
| PostProcess | `CTCLabelDecode` | **yes — naina's existing greedy decoder** |
| Preprocess | `RecResizeImg [3, 48, 320]` | yes, same as the current rec path |
| `character_dict` | 568 entries → `num_classes` 570 | registry already parameterises this |

Everything drops into machinery that exists. `charset.cc` prepends blank and
appends space, so 568 + 2 = 570 works with no change.

Same family covers Arabic, Cyrillic, Tamil, Telugu, Kannada, Korean, Japanese
and more — roughly 20 scripts, all the same shape.

## Design

Add a **language axis** orthogonal to the existing tier axis. Tier picks size;
language picks the recognition alphabet. Detection and layout models are shared
across languages and are not duplicated.

### Registry

```yaml
- id: ppocrv5_rec.devanagari
  task: text_recognize
  tier: tiny
  lang: devanagari          # new field; absent means "latin_cjk"
  arch: PP-OCRv5-mobile-rec
  num_classes: 570
  files:
    onnx:
      url: "${release_base}/ppocrv5_devanagari_rec.onnx"
      sha256: <fill after mirroring>
      source_url: "https://huggingface.co/PaddlePaddle/devanagari_PP-OCRv5_mobile_rec_onnx/resolve/main/inference.onnx"
    charset_yaml:
      url: "${release_base}/ppocrv5_devanagari_rec.yml"
      sha256: <fill after mirroring>
```

`ModelRegistry::resolve(task, tier)` becomes
`resolve(task, tier, lang)`; entries with no `lang` match the default.

### C ABI — additive, version 3

```c
/* naina_config gains, at the END of the struct: */
const char* language;   /* NULL or "" → default (Latin + CJK).
                           honoured when version >= 3 */
```

Appending keeps the additive-only rule: v1 and v2 configs still work, and the
existing `test_engine_lifecycle` assertion on that must keep passing. Bump
`version` to 3, keep accepting 1 and 2.

A string rather than an enum, deliberately: the set of scripts is upstream's to
grow, and an enum would need an ABI change per language.

### Failure behaviour

If a language is requested and its model is not in the registry, `naina_init`
returns `NAINA_E_UNSUPPORTED` — **not** a silent fallback to Latin. Falling back
is what produces the current problem.

### Bindings

- Python: `naina.read(path, language="devanagari")`, `Reader(language=...)`
- Node: `read(path, { language: 'devanagari' })`
- WASM: `createReader({ language: 'devanagari' })`; `stagingPlan` already returns
  whatever the registry resolves, so staging needs no change beyond passing lang
- Flutter: `Naina.open(language: 'devanagari')`
- MCP: a `language` argument on both tools
- Web app: a language selector next to the tier selector

## Also required: stop guessing

Even with Devanagari added, someone will feed it Thai. Independently of the
above, naina should be able to say it cannot read a script.

Cheapest honest signal, from evidence already gathered: the *distribution* of
per-line confidence separates the cases well. On a clean in-alphabet page the
mean was **0.99** (33 lines, min 0.96). On the Devanagari page it was roughly
**0.62** with a spread down to 0.42.

Do not ship a threshold tuned on two samples. Collect a corpus across several
scripts first, then either expose a page-level `script_confidence` and let
callers threshold, or set one with real evidence behind it. Exposing the
aggregate is the safe half and can land immediately.

## Steps

1. Mirror `inference.onnx` and `inference.yml` into the models release via
   `tools/mirror_models.py`, renamed as above. Record real sha256s.
2. Add `lang` to `ModelEntry` and to the manifest schema; thread through
   `resolve()`. Default absent → current behaviour.
3. Registry entries for devanagari at tiny and small.
4. C ABI: append `language`, bump to version 3, keep 1 and 2 accepted. Extend the
   ABI-compatibility test.
5. `api.cc`: pass language into resolution; return `NAINA_E_UNSUPPORTED` for an
   unknown one.
6. Bindings, then the web app selector.
7. Test with a real Hindi page and assert actual Devanagari characters come back
   — not merely that confidence is high, which is exactly the trap that hid this.

## Estimate

Two to three days. Step 4 is the only part that is hard to reverse; the rest is
additive.
