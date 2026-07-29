//! Raw declarations for naina's C ABI.
//!
//! Mirrors `core/include/naina/naina.h`. Hand-written rather than bindgen-
//! generated: the surface is small, and a hand-written module can carry the
//! ownership rules that the C types cannot express.
//!
//! Ownership, which matters more than the signatures:
//! - `naina_page_t` owns every string it hands out. A `text` pointer dangles the
//!   instant `naina_page_release` runs, so any `String` must be copied out first.
//! - `naina_image_t` is a *view* over caller pixels. They must outlive it.

#![allow(non_camel_case_types)]

use std::os::raw::{c_char, c_int};

pub const NAINA_OK: c_int = 0;
pub const NAINA_E_INVALID_ARG: c_int = 1;
pub const NAINA_E_NOT_INITIALIZED: c_int = 2;
pub const NAINA_E_MODEL_NOT_FOUND: c_int = 3;
pub const NAINA_E_BACKEND_UNAVAIL: c_int = 4;
pub const NAINA_E_INFERENCE_FAILED: c_int = 5;
pub const NAINA_E_OOM: c_int = 6;
pub const NAINA_E_UNSUPPORTED: c_int = 7;
pub const NAINA_E_IO: c_int = 8;

pub const NAINA_PIXFMT_RGB8: c_int = 0;

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct naina_point {
    pub x: f32,
    pub y: f32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct naina_bbox {
    pub x: f32,
    pub y: f32,
    pub w: f32,
    pub h: f32,
    pub score: f32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct naina_textbox {
    pub corners: [naina_point; 4],
    pub score: f32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct naina_textline {
    pub box_: naina_textbox,
    /// Owned by the page; dangles after `naina_page_release`.
    pub text: *const c_char,
    pub confidence: f32,
    pub region_id: i32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct naina_region {
    pub bbox: naina_bbox,
    pub kind: c_int,
    pub order: i32,
}

/// Field order and types must match `naina_config` exactly.
///
/// `version` is the ABI gate. 3 is what this crate writes; the core still accepts
/// 1 and 2, and fields are only ever appended — which is why this struct grew
/// from 40 to 48 bytes when `language` arrived without breaking older callers.
#[repr(C)]
pub struct naina_config {
    pub version: i32,
    pub backend: c_int,
    pub device: c_int,
    pub models_root: *const c_char,
    pub num_threads: i32,
    pub enable_research_models: i32,
    pub tier: c_int,
    pub language: *const c_char,
}

impl Default for naina_config {
    fn default() -> Self {
        Self {
            version: 3,
            backend: 0,
            device: 0,
            models_root: std::ptr::null(),
            num_threads: 0,
            enable_research_models: 0,
            tier: 0,
            language: std::ptr::null(),
        }
    }
}

pub enum naina_ctx {}
pub enum naina_image {}
pub enum naina_page {}

#[cfg(not(naina_no_native))]
extern "C" {
    pub fn naina_init(cfg: *const naina_config, out_ctx: *mut *mut naina_ctx) -> c_int;
    pub fn naina_release(ctx: *mut naina_ctx);
    pub fn naina_version_string() -> *const c_char;
    pub fn naina_status_str(s: c_int) -> *const c_char;

    pub fn naina_image_wrap(
        data: *const u8,
        width: i32,
        height: i32,
        stride: i32,
        fmt: c_int,
        out_image: *mut *mut naina_image,
    ) -> c_int;
    pub fn naina_image_release(image: *mut naina_image);

    pub fn naina_read(
        ctx: *mut naina_ctx,
        image: *const naina_image,
        out_page: *mut *mut naina_page,
    ) -> c_int;
    pub fn naina_page_release(page: *mut naina_page);

    pub fn naina_page_lines(
        page: *const naina_page,
        out_lines: *mut *const naina_textline,
        out_count: *mut i32,
    ) -> c_int;
    pub fn naina_page_regions(
        page: *const naina_page,
        out_regions: *mut *const naina_region,
        out_count: *mut i32,
    ) -> c_int;

    pub fn naina_page_markdown(page: *const naina_page) -> *const c_char;
    pub fn naina_page_json(page: *const naina_page) -> *const c_char;
}
