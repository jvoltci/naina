//! Read documents. OCR from naina's C++ core.
//!
//! Detect text, recognise it, work out the reading order, and get markdown back —
//! all from one compiled core, with no Python and no OpenCV.
//!
//! ```no_run
//! use naina::{Reader, Tier};
//! # let (width, height) = (640, 480);
//! # let rgb = vec![0u8; (width * height * 3) as usize];
//!
//! let reader = Reader::builder().tier(Tier::Small).build()?;
//! let page = reader.read_rgb(&rgb, width, height)?;
//!
//! println!("{}", page.markdown());
//! for line in page.lines() {
//!     println!("{:.3}  {}", line.confidence, line.text);
//! }
//! # Ok::<(), naina::Error>(())
//! ```
//!
//! # Scripts
//!
//! Latin and CJK by default; Devanagari (Hindi, Marathi, Nepali, Sanskrit) on
//! request:
//!
//! ```no_run
//! # use naina::Reader;
//! let reader = Reader::builder().language("devanagari").build()?;
//! # Ok::<(), naina::Error>(())
//! ```
//!
//! Reading Devanagari with the default alphabet does not fail — it returns
//! plausible-looking Latin at high confidence. Confidence measures certainty
//! *within* the model's own alphabet and cannot express "wrong alphabet", so
//! choose deliberately. An unrecognised language is an error, never a fallback.
//!
//! # Building
//!
//! naina links ONNX Runtime. Point at it with `ONNXRUNTIME_ROOT`, or link an
//! existing build of the core with `NAINA_LIB_DIR`.

#![warn(missing_docs)]
#![warn(clippy::all)]

mod ffi;

use std::ffi::{CStr, CString};
use std::fmt;
use std::os::raw::c_int;

/// Model size. Not a licence or capability axis — every model naina ships is
/// Apache-2.0 and every tier does detection, recognition and layout.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum Tier {
    /// Resolves to [`Tier::Small`] today.
    #[default]
    Auto,
    /// ~11 MB. Phones, browsers, a Pi Zero.
    Tiny,
    /// ~54 MB. Laptops, a Pi 5.
    Small,
    /// ~269 MB. Servers and desktops.
    Medium,
}

impl Tier {
    fn to_c(self) -> c_int {
        match self {
            Tier::Auto => 0,
            Tier::Tiny => 1,
            Tier::Small => 2,
            Tier::Medium => 3,
        }
    }
}

/// What kind of thing a layout region is.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[allow(missing_docs)]
pub enum RegionKind {
    Unknown,
    Title,
    Text,
    List,
    Table,
    Figure,
    Caption,
    Formula,
    Header,
    Footer,
    PageNumber,
}

impl RegionKind {
    fn from_c(v: c_int) -> Self {
        match v {
            1 => RegionKind::Title,
            2 => RegionKind::Text,
            3 => RegionKind::List,
            4 => RegionKind::Table,
            5 => RegionKind::Figure,
            6 => RegionKind::Caption,
            7 => RegionKind::Formula,
            8 => RegionKind::Header,
            9 => RegionKind::Footer,
            10 => RegionKind::PageNumber,
            _ => RegionKind::Unknown,
        }
    }

    /// Whether this is page furniture rather than part of the reading flow.
    /// Furniture is omitted from [`Page::markdown`].
    pub fn is_furniture(self) -> bool {
        matches!(
            self,
            RegionKind::Header | RegionKind::Footer | RegionKind::PageNumber
        )
    }
}

/// Anything the native core can refuse to do.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Error {
    /// An argument was rejected.
    InvalidArgument(String),
    /// The context was not usable.
    NotInitialized(String),
    /// A required model file is missing from the cache.
    ModelNotFound(String),
    /// No inference backend was available.
    BackendUnavailable(String),
    /// The model ran but failed.
    InferenceFailed(String),
    /// Out of memory.
    OutOfMemory(String),
    /// Unsupported — an unknown language, or an unrecognised model architecture.
    Unsupported(String),
    /// Filesystem or network failure.
    Io(String),
    /// A string from the core was not valid UTF-8.
    InvalidUtf8,
    /// Built with the `docsrs` feature, so there is no native library.
    NotBuilt,
    /// A status code this crate does not know.
    Other(i32, String),
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Error::InvalidArgument(m)
            | Error::NotInitialized(m)
            | Error::ModelNotFound(m)
            | Error::BackendUnavailable(m)
            | Error::InferenceFailed(m)
            | Error::OutOfMemory(m)
            | Error::Unsupported(m)
            | Error::Io(m) => write!(f, "naina: {m}"),
            Error::InvalidUtf8 => write!(f, "naina: native string was not valid UTF-8"),
            Error::NotBuilt => {
                write!(f, "naina: built without the native core (docsrs feature)")
            }
            Error::Other(c, m) => write!(f, "naina: status {c}: {m}"),
        }
    }
}

impl std::error::Error for Error {}

#[cfg(not(naina_no_native))]
fn status_text(code: c_int) -> String {
    // Safety: naina_status_str returns a static NUL-terminated string and never
    // null, for any input.
    unsafe { CStr::from_ptr(ffi::naina_status_str(code)) }
        .to_string_lossy()
        .into_owned()
}

#[cfg(not(naina_no_native))]
fn check(code: c_int) -> Result<(), Error> {
    let msg = || status_text(code);
    match code {
        ffi::NAINA_OK => Ok(()),
        ffi::NAINA_E_INVALID_ARG => Err(Error::InvalidArgument(msg())),
        ffi::NAINA_E_NOT_INITIALIZED => Err(Error::NotInitialized(msg())),
        ffi::NAINA_E_MODEL_NOT_FOUND => Err(Error::ModelNotFound(msg())),
        ffi::NAINA_E_BACKEND_UNAVAIL => Err(Error::BackendUnavailable(msg())),
        ffi::NAINA_E_INFERENCE_FAILED => Err(Error::InferenceFailed(msg())),
        ffi::NAINA_E_OOM => Err(Error::OutOfMemory(msg())),
        ffi::NAINA_E_UNSUPPORTED => Err(Error::Unsupported(msg())),
        ffi::NAINA_E_IO => Err(Error::Io(msg())),
        other => Err(Error::Other(other, msg())),
    }
}

/// One recognised line of text.
#[derive(Clone, Debug)]
pub struct Line {
    /// The recognised text.
    pub text: String,
    /// Mean per-character probability over the characters actually emitted.
    ///
    /// This cannot express "wrong alphabet" — see the crate documentation.
    pub confidence: f32,
    /// Detector score for the quad.
    pub score: f32,
    /// Index into [`Page::regions`], or `None` when layout matched nothing.
    pub region: Option<usize>,
    /// Four corners, clockwise from top-left, in source pixels. Not necessarily
    /// axis-aligned: rotated text yields a rotated quad.
    pub corners: [(f32, f32); 4],
}

/// A layout region.
#[derive(Clone, Debug)]
pub struct Region {
    /// What kind of region this is.
    pub kind: RegionKind,
    /// Reading-order index within the page.
    pub order: i32,
    /// `(x, y, width, height)` in source pixels.
    pub bbox: (f32, f32, f32, f32),
    /// Detector score.
    pub score: f32,
}

/// The result of reading one image.
///
/// Everything is copied out of the native page before it is released, so a `Page`
/// is plain owned data with no lifetime tied to the reader.
#[derive(Clone, Debug)]
pub struct Page {
    lines: Vec<Line>,
    regions: Vec<Region>,
    markdown: String,
    json: String,
}

impl Page {
    /// Recognised lines, in reading order.
    pub fn lines(&self) -> &[Line] {
        &self.lines
    }

    /// Layout regions.
    pub fn regions(&self) -> &[Region] {
        &self.regions
    }

    /// The document as markdown: headings, paragraphs, captions, with page
    /// furniture omitted.
    pub fn markdown(&self) -> &str {
        &self.markdown
    }

    /// Lines and regions as JSON, matching the other bindings' shape.
    pub fn json(&self) -> &str {
        &self.json
    }

    /// Every line joined by newlines, with no markup.
    pub fn text(&self) -> String {
        self.lines
            .iter()
            .map(|l| l.text.as_str())
            .collect::<Vec<_>>()
            .join("\n")
    }

    /// Mean confidence across all lines, or 0.0 when there are none.
    pub fn mean_confidence(&self) -> f32 {
        if self.lines.is_empty() {
            return 0.0;
        }
        self.lines.iter().map(|l| l.confidence).sum::<f32>() / self.lines.len() as f32
    }
}

/// Builds a [`Reader`].
#[derive(Clone, Debug, Default)]
pub struct ReaderBuilder {
    tier: Tier,
    language: Option<String>,
    models_root: Option<String>,
    num_threads: i32,
}

impl ReaderBuilder {
    /// Model size.
    pub fn tier(mut self, tier: Tier) -> Self {
        self.tier = tier;
        self
    }

    /// Recognition alphabet, e.g. `"devanagari"`. Unset means Latin + CJK.
    ///
    /// An unrecognised value makes [`build`](Self::build) fail with
    /// [`Error::Unsupported`] rather than quietly reading with the wrong
    /// alphabet.
    pub fn language(mut self, language: impl Into<String>) -> Self {
        self.language = Some(language.into());
        self
    }

    /// Where model weights are cached. Defaults to `$NAINA_CACHE` or
    /// `~/.cache/naina/models`.
    pub fn models_root(mut self, path: impl Into<String>) -> Self {
        self.models_root = Some(path.into());
        self
    }

    /// Inference threads. 0 lets the backend decide.
    pub fn num_threads(mut self, n: i32) -> Self {
        self.num_threads = n;
        self
    }

    /// Open the reader, loading models.
    #[cfg(not(naina_no_native))]
    pub fn build(self) -> Result<Reader, Error> {
        // CStrings must outlive naina_init, which copies from them.
        let lang = match self.language.as_deref() {
            Some(s) if !s.is_empty() => Some(
                CString::new(s)
                    .map_err(|_| Error::InvalidArgument("language contains a NUL byte".into()))?,
            ),
            _ => None,
        };
        let root =
            match self.models_root.as_deref() {
                Some(s) if !s.is_empty() => Some(CString::new(s).map_err(|_| {
                    Error::InvalidArgument("models_root contains a NUL byte".into())
                })?),
                _ => None,
            };

        let cfg = ffi::naina_config {
            version: 3,
            tier: self.tier.to_c(),
            num_threads: self.num_threads,
            language: lang.as_ref().map_or(std::ptr::null(), |c| c.as_ptr()),
            models_root: root.as_ref().map_or(std::ptr::null(), |c| c.as_ptr()),
            ..Default::default()
        };

        let mut ctx: *mut ffi::naina_ctx = std::ptr::null_mut();
        // Safety: cfg is a valid, fully initialised config; ctx is a valid out
        // pointer. Any borrowed string lives until after this call returns.
        check(unsafe { ffi::naina_init(&cfg, &mut ctx) })?;
        if ctx.is_null() {
            return Err(Error::NotInitialized(
                "naina_init returned no context".into(),
            ));
        }
        Ok(Reader { ctx })
    }

    /// Always fails: this build has no native core.
    #[cfg(naina_no_native)]
    pub fn build(self) -> Result<Reader, Error> {
        Err(Error::NotBuilt)
    }
}

/// An open naina context.
///
/// Holds loaded models — tens to hundreds of megabytes — released on drop.
#[derive(Debug)]
pub struct Reader {
    #[cfg(not(naina_no_native))]
    ctx: *mut ffi::naina_ctx,
    #[cfg(naina_no_native)]
    ctx: (),
}

impl Reader {
    /// Start configuring a reader.
    pub fn builder() -> ReaderBuilder {
        ReaderBuilder::default()
    }

    /// A reader with every default: Latin + CJK, small tier.
    pub fn new() -> Result<Self, Error> {
        Self::builder().build()
    }

    /// naina's version string.
    #[cfg(not(naina_no_native))]
    pub fn version() -> Result<String, Error> {
        // Safety: returns a static NUL-terminated string, never null.
        unsafe { CStr::from_ptr(ffi::naina_version_string()) }
            .to_str()
            .map(str::to_owned)
            .map_err(|_| Error::InvalidUtf8)
    }

    /// Read packed RGB8 bytes.
    ///
    /// `rgb` must be at least `width * height * 3` long. naina ships no image
    /// decoder; use `image` or any decoder you already have.
    #[cfg(not(naina_no_native))]
    pub fn read_rgb(&self, rgb: &[u8], width: i32, height: i32) -> Result<Page, Error> {
        let needed = width as usize * height as usize * 3;
        if width <= 0 || height <= 0 || rgb.len() < needed {
            return Err(Error::InvalidArgument(format!(
                "expected at least {needed} bytes for {width}x{height}, got {}",
                rgb.len()
            )));
        }

        let mut image: *mut ffi::naina_image = std::ptr::null_mut();
        // Safety: rgb outlives the image view, which is released before this
        // function returns. naina_image_wrap does not copy.
        check(unsafe {
            ffi::naina_image_wrap(
                rgb.as_ptr(),
                width,
                height,
                width * 3,
                ffi::NAINA_PIXFMT_RGB8,
                &mut image,
            )
        })?;
        let _image = ImageGuard(image);

        let mut page: *mut ffi::naina_page = std::ptr::null_mut();
        // Safety: ctx and image are both non-null and valid here.
        check(unsafe { ffi::naina_read(self.ctx, image, &mut page) })?;
        if page.is_null() {
            return Err(Error::InferenceFailed("naina_read returned no page".into()));
        }
        let page = PageGuard(page);

        collect(page.0)
    }
}

impl Drop for Reader {
    fn drop(&mut self) {
        #[cfg(not(naina_no_native))]
        // Safety: ctx came from naina_init and is released exactly once.
        unsafe {
            ffi::naina_release(self.ctx);
        }
    }
}

// Reader owns its context and the C ABI takes no global locks, but naina_ctx is
// NOT internally synchronised: concurrent reads on one context would race on its
// session cache. Send is sound (ownership can move between threads); Sync is not.
#[cfg(not(naina_no_native))]
unsafe impl Send for Reader {}

#[cfg(not(naina_no_native))]
struct ImageGuard(*mut ffi::naina_image);

#[cfg(not(naina_no_native))]
impl Drop for ImageGuard {
    fn drop(&mut self) {
        // Safety: released exactly once, and only if non-null.
        unsafe {
            if !self.0.is_null() {
                ffi::naina_image_release(self.0);
            }
        }
    }
}

#[cfg(not(naina_no_native))]
struct PageGuard(*mut ffi::naina_page);

#[cfg(not(naina_no_native))]
impl Drop for PageGuard {
    fn drop(&mut self) {
        // Safety: released exactly once, and only if non-null.
        unsafe {
            if !self.0.is_null() {
                ffi::naina_page_release(self.0);
            }
        }
    }
}

/// Copy everything out of the native page.
///
/// This must happen before the page is released: the page owns its strings, so
/// holding the pointers afterwards would read freed memory.
#[cfg(not(naina_no_native))]
fn collect(page: *mut ffi::naina_page) -> Result<Page, Error> {
    let mut lines_ptr: *const ffi::naina_textline = std::ptr::null();
    let mut count: i32 = 0;
    // Safety: page is valid; both out pointers are valid.
    check(unsafe { ffi::naina_page_lines(page, &mut lines_ptr, &mut count) })?;

    let mut lines = Vec::with_capacity(count.max(0) as usize);
    for i in 0..count.max(0) as usize {
        // Safety: the core reported `count` contiguous lines at lines_ptr.
        let l = unsafe { &*lines_ptr.add(i) };
        let text = if l.text.is_null() {
            String::new()
        } else {
            // Safety: NUL-terminated and owned by the page, which is still alive.
            unsafe { CStr::from_ptr(l.text) }
                .to_str()
                .map_err(|_| Error::InvalidUtf8)?
                .to_owned()
        };
        lines.push(Line {
            text,
            confidence: l.confidence,
            score: l.box_.score,
            region: (l.region_id >= 0).then_some(l.region_id as usize),
            corners: [
                (l.box_.corners[0].x, l.box_.corners[0].y),
                (l.box_.corners[1].x, l.box_.corners[1].y),
                (l.box_.corners[2].x, l.box_.corners[2].y),
                (l.box_.corners[3].x, l.box_.corners[3].y),
            ],
        });
    }

    let mut regions_ptr: *const ffi::naina_region = std::ptr::null();
    let mut rcount: i32 = 0;
    // Safety: as above.
    check(unsafe { ffi::naina_page_regions(page, &mut regions_ptr, &mut rcount) })?;

    let mut regions = Vec::with_capacity(rcount.max(0) as usize);
    for i in 0..rcount.max(0) as usize {
        // Safety: the core reported `rcount` contiguous regions.
        let r = unsafe { &*regions_ptr.add(i) };
        regions.push(Region {
            kind: RegionKind::from_c(r.kind),
            order: r.order,
            bbox: (r.bbox.x, r.bbox.y, r.bbox.w, r.bbox.h),
            score: r.bbox.score,
        });
    }

    // Safety: both return borrowed NUL-terminated strings, never null.
    let markdown = unsafe { CStr::from_ptr(ffi::naina_page_markdown(page)) }
        .to_str()
        .map_err(|_| Error::InvalidUtf8)?
        .to_owned();
    let json = unsafe { CStr::from_ptr(ffi::naina_page_json(page)) }
        .to_str()
        .map_err(|_| Error::InvalidUtf8)?
        .to_owned();

    Ok(Page {
        lines,
        regions,
        markdown,
        json,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn tier_maps_to_the_c_enum() {
        assert_eq!(Tier::Auto.to_c(), 0);
        assert_eq!(Tier::Tiny.to_c(), 1);
        assert_eq!(Tier::Small.to_c(), 2);
        assert_eq!(Tier::Medium.to_c(), 3);
    }

    #[test]
    fn region_kinds_round_trip_from_the_c_enum() {
        assert_eq!(RegionKind::from_c(0), RegionKind::Unknown);
        assert_eq!(RegionKind::from_c(1), RegionKind::Title);
        assert_eq!(RegionKind::from_c(10), RegionKind::PageNumber);
        // Anything outside the table must not panic or alias a real kind.
        assert_eq!(RegionKind::from_c(99), RegionKind::Unknown);
        assert_eq!(RegionKind::from_c(-1), RegionKind::Unknown);
    }

    #[test]
    fn furniture_is_exactly_header_footer_pagenum() {
        assert!(RegionKind::Header.is_furniture());
        assert!(RegionKind::Footer.is_furniture());
        assert!(RegionKind::PageNumber.is_furniture());
        assert!(!RegionKind::Title.is_furniture());
        assert!(!RegionKind::Text.is_furniture());
        assert!(!RegionKind::Table.is_furniture());
    }

    #[test]
    fn config_layout_matches_the_c_struct() {
        // Measured from C on a 64-bit target: 48 bytes, offsets 0, 4, 8, 16, 24,
        // 28, 32, 40. It grew from 40 when `language` was appended, and the
        // earlier offsets did not move — which is what makes that addition
        // ABI-safe. A mismatch here would write `tier` into the wrong slot and
        // silently load a different model.
        use std::mem::{align_of, size_of};
        assert_eq!(size_of::<ffi::naina_config>(), 48);
        assert_eq!(align_of::<ffi::naina_config>(), 8);
    }

    #[test]
    fn textbox_is_four_points_plus_a_score() {
        use std::mem::size_of;
        assert_eq!(size_of::<ffi::naina_point>(), 8);
        assert_eq!(size_of::<ffi::naina_textbox>(), 4 * 8 + 4);
    }

    #[test]
    fn default_config_declares_version_3() {
        let c = ffi::naina_config::default();
        assert_eq!(c.version, 3);
        assert!(c.language.is_null());
        assert!(c.models_root.is_null());
    }

    #[test]
    fn builder_rejects_an_interior_nul() {
        let e = Reader::builder()
            .language("dev\0anagari")
            .build()
            .unwrap_err();
        assert!(matches!(e, Error::InvalidArgument(_)), "got {e:?}");
    }
}
