/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::marker::PhantomData;
use std::ops::Deref;
use std::ptr::NonNull;

/// A wrapper for raw JSContext pointers that are strongly associated with
/// the [Runtime] type.
///
/// Each function that can trigger GC accepts `&mut JSContext`
/// and each value that should not be held across GC is created using [NoGC]
/// which borrows [JSContext] and thus prevents calling any function that triggers GC
/// (because &mut requires exclusive access).
#[derive(Copy, Clone)]
pub struct JSContext<'rt> {
    pub(crate) ptr: NonNull<crate::jsapi::JSContext>,
    pub(crate) runtime_anchor: PhantomData<&'rt ()>,
}

impl<'rt> JSContext<'rt> {
    /// Wrap an existing raw JSContext pointer.
    ///
    /// SAFETY:
    /// - cx must be valid JSContext object.
    /// - the resulting lifetime must not exceed the actual lifetime of the
    ///   associated JS runtime.
    /// - only one JSContext can be alive (it's safe to construct only one from ptr provided from callbacks, but you are not allowed to make more from thin air)
    pub unsafe fn from_ptr(cx: NonNull<mozjs_sys::jsapi::JSContext>) -> JSContext<'rt> {
        JSContext {
            ptr: cx,
            runtime_anchor: PhantomData,
        }
    }

    /// Returns [NoGC] token bounded to this [JSContext].
    /// No function that accepts `&mut JSContext` (read: triggers GC)
    /// can be called while this is alive.
    #[inline]
    #[must_use]
    pub fn no_gc<'cx: 'rt>(&'cx self) -> &'cx NoGC<'cx> {
        &NoGC(PhantomData)
    }
}

// This will be eventually removed, because it currently breaks safety invariants
impl<'rt> Deref for JSContext<'rt> {
    type Target = *mut crate::jsapi::JSContext;

    fn deref(&self) -> &Self::Target {
        unsafe {
            std::mem::transmute::<&NonNull<crate::jsapi::JSContext>, &*mut crate::jsapi::JSContext>(
                &self.ptr,
            )
        }
    }
}

/// Token that ensures that no GC can happen while this is alive.
///
/// Each GC triggering function require mutable access to [JSContext],
/// which cannot be while this exists, because it's lifetime is bounded with [JSContext].
pub struct NoGC<'cx>(PhantomData<&'cx ()>);

/// Special case of [JSContext], which can be passed to functions which will not trigger GC, but still take `*mut JSContext`
/// (they usually take `cx: *mut root::JSContext, nogc: *const root::JS::AutoRequireNoGC` like `JS_GetTwoByteStringCharsAndLength`).
///
/// Because they do not trigger GC, this can be alive while holding [NoGC].
pub struct NoGcJSContext<'cx> {
    ptr: NonNull<crate::jsapi::JSContext>,
    no_gc: PhantomData<&'cx ()>,
}
