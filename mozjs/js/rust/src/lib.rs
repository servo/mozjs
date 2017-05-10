/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#![crate_name = "js"]
#![crate_type = "rlib"]

#![feature(link_args)]
#![feature(nonzero)]
#![feature(const_fn)]
#![feature(untagged_unions)]

#![allow(non_upper_case_globals, non_camel_case_types, non_snake_case, improper_ctypes)]

extern crate core;
#[macro_use]
extern crate heapsize;
extern crate lazy_static;
extern crate libc;
#[macro_use]
extern crate log;
extern crate mozjs_sys;
extern crate num_traits;

#[macro_use]
pub mod rust;

pub mod ac;
pub mod conversions;
pub mod error;
pub mod glue;
pub mod heap;
pub mod jsval;

use jsval::JSVal;

pub mod jsapi;
pub use self::jsapi::root::*;

unsafe impl Sync for JSClass {}

#[inline(always)]
pub unsafe fn JS_ARGV(_cx: *mut JSContext, vp: *mut JSVal) -> *mut JSVal {
    vp.offset(2)
}

#[inline(always)]
pub unsafe fn JS_CALLEE(_cx: *mut JSContext, vp: *mut JSVal) -> JSVal {
    *vp
}

known_heap_size!(0, JSVal);

impl JS::ObjectOpResult {
    /// Set this ObjectOpResult to true and return true.
    pub fn succeed(&mut self) -> bool {
        self.code_ = JS::ObjectOpResult_SpecialCodes::OkCode as usize;
        true
    }
}
