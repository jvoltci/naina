//! Reads a real page through the safe API.
//!
//! Skips loudly, naming what is missing, rather than passing vacuously — a silent
//! skip is indistinguishable from a pass.
//!
//!   NAINA_RUST_FIXTURE=/path/page.rgb cargo test
//!
//! Fixture format: i32 LE width, i32 LE height, then width*height*3 RGB8.

use std::{env, fs};

fn fixture() -> Option<(Vec<u8>, i32, i32)> {
    let path = env::var("NAINA_RUST_FIXTURE").ok()?;
    let raw = fs::read(path).ok()?;
    if raw.len() < 8 {
        return None;
    }
    let w = i32::from_le_bytes(raw[0..4].try_into().unwrap());
    let h = i32::from_le_bytes(raw[4..8].try_into().unwrap());
    let need = w as usize * h as usize * 3;
    if w <= 0 || h <= 0 || raw.len() < 8 + need {
        return None;
    }
    Some((raw[8..8 + need].to_vec(), w, h))
}

#[test]
fn version_is_reported() {
    let v = naina::Reader::version().expect("version");
    assert!(!v.is_empty());
    assert!(v.starts_with("0."), "unexpected version {v}");
}

#[test]
fn an_unknown_language_is_refused_not_silently_downgraded() {
    // The whole reason `language` exists. Falling back to Latin here is what made
    // Hindi pages return fluent nonsense.
    match naina::Reader::builder().language("klingon").build() {
        Err(naina::Error::Unsupported(_)) => {}
        Err(naina::Error::ModelNotFound(m)) => {
            // No registry available in this environment; init failed before it
            // could validate. Named so this is not mistaken for a pass.
            eprintln!("SKIP: no registry to validate against ({m})");
        }
        Err(e) => panic!("expected Unsupported, got {e:?}"),
        Ok(_) => panic!("an unknown language must not be accepted"),
    }
}

#[test]
fn rejects_a_buffer_too_small_for_its_dimensions() {
    let Ok(reader) = naina::Reader::builder().tier(naina::Tier::Tiny).build() else {
        eprintln!("SKIP: no reader (models or backend unavailable)");
        return;
    };
    let err = reader.read_rgb(&[0u8; 12], 100, 100).unwrap_err();
    assert!(
        matches!(err, naina::Error::InvalidArgument(_)),
        "got {err:?}"
    );
}

#[test]
fn reads_a_real_page() {
    let Some((rgb, w, h)) = fixture() else {
        eprintln!("SKIP: set NAINA_RUST_FIXTURE to a raw RGB fixture");
        return;
    };
    let reader = match naina::Reader::builder().tier(naina::Tier::Tiny).build() {
        Ok(r) => r,
        Err(e) => {
            eprintln!("SKIP: could not build a reader: {e}");
            return;
        }
    };

    let page = reader.read_rgb(&rgb, w, h).expect("read");
    eprintln!(
        "read {} lines, {} regions, mean confidence {:.3}",
        page.lines().len(),
        page.regions().len(),
        page.mean_confidence()
    );

    assert!(!page.lines().is_empty(), "no text found");
    assert!(!page.markdown().is_empty(), "no markdown produced");
    assert!(page.json().starts_with('{'), "json malformed");
    assert!(page.mean_confidence() > 0.5);

    // text() must drop markup that markdown() adds.
    assert!(!page.text().contains("# "));

    // Determinism: the same input twice must give one answer.
    let again = reader.read_rgb(&rgb, w, h).expect("second read");
    assert_eq!(page.markdown(), again.markdown());
}
