/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::marker::PhantomData;
use std::ops::Deref;

/// A wrapper for raw JSContext pointers that are strongly associated with
/// the [Runtime] type.
#[derive(Copy, Clone)]
pub struct JSContext<'a> {
    pub(crate) raw: *mut crate::jsapi::JSContext,
    pub(crate) anchor: PhantomData<&'a ()>,
}

impl<'a> JSContext<'a> {
    /// Wrap an existing raw JSContext pointer.
    ///
    /// SAFETY:
    /// - cx must point to non-null, valid JSContext object.
    /// - the resulting lifetime must not exceed the actual lifetime of the
    ///   associated JS runtime.
    pub unsafe fn from_ptr(cx: *mut crate::jsapi::JSContext) -> JSContext<'a> {
        JSContext {
            raw: cx,
            anchor: PhantomData,
        }
    }
}

impl<'a> Deref for JSContext<'a> {
    type Target = *mut crate::jsapi::JSContext;

    fn deref(&self) -> &Self::Target {
        &self.raw
    }
}
