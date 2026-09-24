# Antaryaatra pages: the Indic benchmark nobody else can make

No public page-level Indic OCR benchmark exists; the Devanagari 300 are word
crops and GlotOCR renders synthetic text. Jai owns printed editions of texts
whose exact Unicode lives in `one/they/`, so this folder holds real
photographed pages with ground truth that is already typed.

## What exists to align against, checked 2026-09-23

Devanagari sources in `one/they/`, by size: ramayan (14.9 M chars), sanskrit
(2.2 M), upanishad (1.1 M), gita (1.0 M), shiv-sutra, yog-sutra,
hath-pradipika, vigyan-bhairav, satipatthana. No Tamil, Telugu or other
script has a source there, so this set is Devanagari only unless a printed
edition and its Unicode arrive together for another script.

## The photographs, for Jai

- 50 pages, at least three books, at least ten pages each.
- Phone camera, ordinary room light, no flash, no scanner app, no
  perspective correction, no cropping. The point is a real page as a person
  would photograph it, imperfections included.
- One page per photograph, the whole page in frame, held roughly flat.
- File name `<book>-<page>.jpg`, where `<book>` is the folder name in
  `one/they/` (`gita`, `ramayan`, `upanishad`, ...) and `<page>` is the printed
  page number. Example: `gita-112.jpg`.
- Drop them into `pages/`. Nothing else is needed from the photographer.

## Ground truth

For each photograph, the verses on that page are located once in the
`they/<book>/corpus` source and saved as `pages/<book>-<page>.txt`, one
verse per line, in the order printed. Alignment is done by hand once (a
script can propose the span; a person confirms it) and never changed
afterwards. Commentary, footnotes and running heads that are not in the
Unicode source are excluded from the ground truth and noted in
`pages/<book>-<page>.notes.md` so the scorer knows what the page carried
that the reference does not.

## Scoring

Page-level chrF++ and code-point CER with `../score_chrf.py` (predictions
and references as one string per page, lines joined by newlines), plus
reading-order edit distance over verse order where the page has more than
one verse. Every row: tier, alphabet, device, date, command.

## Licence

The photographs are Jai's own and are released CC-BY-4.0 so anyone can rerun
the set. The reference text carries the licence of its source in
`one/they/`.
