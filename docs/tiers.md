# Tiers

A tier selects **model size**, not licence or capability. Every model naina ships
is Apache-2.0, and all three tiers do detection, recognition and layout.

| Tier | Total | Detection | Recognition | Layout | Built for |
|---|---|---|---|---|---|
| `tiny` | **11.1 MB** | PP-OCRv6 mobile | PP-OCRv6 mobile (6,904 chars) | PP-DocLayout-S | browser, phone, Pi Zero |
| `small` | **54.5 MB** | PP-OCRv6 server | PP-OCRv6 server (18,708 chars) | PP-DocLayout-M | laptop, Pi 5, mobile app |
| `medium` | **268.0 MB** | PP-OCRv6 server | PP-OCRv6 server (18,708 chars) | PP-DocLayout-L | server, desktop |

```python
page = naina.read("scan.png", tier="small")
```

`auto` resolves to `small` today. It exists so a future release can choose by
probing available memory without an ABI change.

## Picking one

**Start with `small`.** It is the best accuracy-per-megabyte point for anything
running on a real computer.

**Use `tiny`** in a browser or on a constrained device. Its recognition charset is
6,904 characters against small's 18,708, so it covers Latin and common CJK but
drops rarer glyphs. Its layout model is also measurably weaker — see below.

**Use `medium`** when layout structure matters more than disk. Measured on an A4
academic page it labelled 14 of 14 regions correctly; `tiny` on a harder synthetic
page found only 4 of 7.

## What layout quality actually looks like

Honest numbers, because "supports layout" is not a useful claim on its own.

**In distribution** — an A4 academic page, the shape PP-DocLayout was trained on,
at `medium`:

- 14 of 14 regions found **and correctly labelled**
- reading order correct
- 33 of 33 text lines at 0.99–1.00 confidence
- markdown with correct `#`/`##` hierarchy, running head omitted as furniture

**Out of distribution** — a synthetic wide-spaced report page:

- a body paragraph labelled `doc_title`, both section headings labelled `text`,
  so the markdown structure came out wrong
- `tiny` found 4 of 7 regions on the same page

Text recognition holds up far better than layout does outside the training
distribution. If you only need text, tier matters much less.

## Browser tiers

The web app offers `tiny` and `small` only. `medium` includes a 129 MB file and
GitHub Pages caps a single file at 100 MB — and a 268 MB download is the wrong
thing to ask of a browser anyway.
