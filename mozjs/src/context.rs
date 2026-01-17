/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::ops::Deref;
use std::ptr::NonNull;

pub use crate::jsapi::JSContext as RawJSContext;

/// A wrapper for [raw JSContext pointers](RawJSContext) that are strongly associated with the [Runtime](crate::rust::Runtime) type.
///
/// This type is fundamental for safe SpiderMonkey usage.
/// Each (SpiderMonkey) function which takes [`&mut JSContext`](JSContext) as argument can trigger GC.
/// SpiderMonkey functions that takes [`&JSContext`](JSContext) are guaranteed to not trigger GC.
/// We must not hold any unrooted or borrowed data while calling any functions that can trigger GC.
/// That can causes panics or UB.
/// For cases where notion of no GC but no actual context is needed, we have [`&NoGC`](NoGC) token.
///
/// ```rust
/// use std::marker::PhantomData;
/// use mozjs::context::*;
///
/// struct ShouldNotBeHoldAcrossGC<'a>(PhantomData<&'a ()>);
///
/// impl<'a> Drop for ShouldNotBeHoldAcrossGC<'a> {
///     fn drop(&mut self) {}
/// }
///
/// fn something_that_should_not_hold_across_gc<'a>(_no_gc: &'a NoGC) -> ShouldNotBeHoldAcrossGC<'a> {
///     ShouldNotBeHoldAcrossGC(PhantomData)
/// }
///
/// fn SM_function_that_can_trigger_gc(_cx: *mut RawJSContext) {}
///
/// // this lives in mozjs
/// fn safe_wrapper_to_SM_function_that_can_trigger_gc(cx: &mut JSContext) {
///     unsafe { SM_function_that_can_trigger_gc(cx.raw_cx()) }
/// }
///
/// fn can_cause_gc(cx: &mut JSContext) {
///     safe_wrapper_to_SM_function_that_can_trigger_gc(cx);
///     {
///         let t = something_that_should_not_hold_across_gc(&cx.no_gc());
///         // do something with it
///     } // t get dropped
///     safe_wrapper_to_SM_function_that_can_trigger_gc(cx); // we can call GC again
/// }
/// ```
///
/// One cannot call any GC function, while any [`&JSContext`](JSContext) or [`&NoGC`](NoGC) is alive,
/// because they require such functions accept [`&mut JSContext`](JSContext):
///
/// ```compile_fail
/// use std::marker::PhantomData;
/// use mozjs::context::*;
/// use mozjs::jsapi::JSContext as RawJSContext;
///
/// struct ShouldNotBeHoldAcrossGC<'a>(PhantomData<&'a ()>);
///
/// impl<'a> Drop for ShouldNotBeHoldAcrossGC<'a> {
///     fn drop(&mut self) {} // make type not trivial, or else compiler can shorten it's lifetime
/// }
///
/// fn something_that_should_not_hold_across_gc<'a>(_no_gc: &'a NoGC) -> ShouldNotBeHoldAcrossGC<'a> {
///     ShouldNotBeHoldAcrossGC(PhantomData)
/// }
///
/// fn safe_wrapper_to_SM_function_that_can_trigger_gc(_cx: &mut JSContext) {
/// }
///
/// fn can_cause_gc(cx: &mut JSContext) {
///     safe_wrapper_to_SM_function_that_can_trigger_gc(cx);
///     let t = something_that_should_not_hold_across_gc(&cx.no_gc());
///     // this will create compile error, because we cannot hold NoGc across C triggering function.
///     // more specifically we cannot borrow `JSContext` as mutable because it is also borrowed as immutable (NoGC).
///     safe_wrapper_to_SM_function_that_can_trigger_gc(cx);
/// }
/// ```
///
/// ### WIP
///
/// This model is still being incrementally introduced, so there are currently some escape hatches.
pub struct JSContext {
    pub(crate) ptr: NonNull<RawJSContext>,
}

impl JSContext {
    /// Wrap an existing [RawJSContext] pointer.
    ///
    /// SAFETY:
    /// - `cx` must be valid [RawJSContext] object.
    /// - only one [JSContext] can be alive and it should not outlive [Runtime].
    /// This in turn means that [JSContext] always needs to be passed down as an argument,
    /// but for the SpiderMonkey callbacks which provide [RawJSContext] it's safe to construct **one** from provided [RawJSContext].
    pub unsafe fn from_ptr(cx: NonNull<RawJSContext>) -> JSContext {
        JSContext { ptr: cx }
    }

    /// Returns [NoGC] token bounded to this [JSContext].
    /// No function that accepts `&mut JSContext` (read: triggers GC)
    /// can be called while this is alive.
    #[inline]
    #[must_use]
    pub fn no_gc<'cx>(&'cx self) -> &'cx NoGC {
        &NoGC(())
    }

    /// Obtain [RawJSContext] mutable pointer.
    ///
    /// # Safety
    ///
    /// No [NoGC] tokens should be constructed while returned pointer is available to user.
    /// In practices this means that one should use the result
    /// as direct argument to SpiderMonkey function and not store it in variable.
    ///
    /// ```rust
    /// use mozjs::context::*;
    /// use mozjs::jsapi::JSContext as RawJSContext;
    ///
    /// fn SM_function_that_can_trigger_gc(_cx: *mut RawJSContext) {}
    ///
    /// fn can_trigger_gc(cx: &mut JSContext) {
    ///     unsafe { SM_function_that_can_trigger_gc(cx.raw_cx()) } // returned pointer is immediately used
    ///     cx.no_gc(); // this is ok because no outstanding raw pointer is alive
    /// }
    /// ```
    pub unsafe fn raw_cx(&mut self) -> *mut RawJSContext {
        self.ptr.as_ptr()
    }

    /// Obtain [RawJSContext] mutable pointer, that will not be used for GC.
    ///
    /// # Safety
    ///
    /// No &mut calls should be done on [JSContext] while returned pointer is available.
    /// In practices this means that one should use the result
    /// as direct argument to SpiderMonkey function and not store it in variable.
    ///
    /// ```rust
    /// use mozjs::context::*;
    /// use mozjs::jsapi::JSContext as RawJSContext;
    ///
    /// fn SM_function_that_cannot_trigger_gc(_cx: *mut RawJSContext) {}
    ///
    /// fn f(cx: &mut JSContext) {
    ///     unsafe { SM_function_that_cannot_trigger_gc(cx.raw_cx_no_gc()) } // returned pointer is immediately used
    /// }
    /// ```
    pub unsafe fn raw_cx_no_gc(&self) -> *mut RawJSContext {
        self.ptr.as_ptr()
    }
}

impl Deref for JSContext {
    type Target = NoGC;

    /// Deref [`&JSContext`](JSContext) into [`&NoGC`](NoGC) so that
    /// one can pass [`&JSContext`](JSContext) to functions that require [`&NoGC`](NoGC).
    fn deref<'cx>(&'cx self) -> &'cx Self::Target {
        self.no_gc()
    }
}

/// Token that ensures that no GC can happen while it is alive.
///
/// This type is similar to `&JSContext`,
/// but it is used in cases where no actual context is needed.
///
/// For more info and examples see [JSContext].
pub struct NoGC(()); // zero-sized type that cannot be constructed from outside
