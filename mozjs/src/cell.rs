/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! A shareable mutable container.

pub use std::cell::{Ref, RefMut};
use std::cell::{RefCell, UnsafeCell};

use mozjs_sys::trace::Traceable;

use crate::context::NoGC;
use crate::jsapi::JSTracer;

/// [`std::cell::RefCell`] that statically prevents borrow_mut panics
/// from occurring by borrowing due to GC using the [`NoGC`] marker.
///
/// If dynamic borrow checking is not needed, one should use [`JSCell`] instead.
/// If inner type is [`Copy`] one should use [`std::cell::Cell`] instead.
#[derive(Clone, Debug, Default)]
pub struct JSRefCell<T> {
    value: RefCell<T>,
}

impl<T> JSRefCell<T> {
    /// Create a new `JSRefCell` containing `value`.
    pub fn new(value: T) -> JSRefCell<T> {
        JSRefCell {
            value: RefCell::new(value),
        }
    }

    /// Immutably borrows the wrapped value.
    ///
    /// The borrow lasts until the returned `Ref` exits scope. Multiple
    /// immutable borrows can be taken out at the same time.
    ///
    /// This is exactly the same as [`std::cell::RefCell::borrow`].
    ///
    /// # Panics
    ///
    /// Panics if the value is currently mutably borrowed.
    #[track_caller]
    pub fn borrow(&self) -> Ref<'_, T> {
        self.value.borrow()
    }

    /// Mutably borrows the wrapped value.
    ///
    /// The borrow lasts until the returned `RefMut` exits scope. The value
    /// cannot be borrowed while this borrow is active.
    ///
    /// This is similar to [`std::cell::RefCell::borrow_mut`],
    /// but it requires a [`&NoGC`][NoGC] token to ensure that no GC can happen while the borrow is active,
    /// which would otherwise cause a panic when tracing (which calls `borrow`).
    ///
    /// ## Examples
    ///
    /// In simple cases one can use [`&NoGC`][NoGC] to statically ensure no GC can happen in the whole function:
    ///
    /// ```
    /// use mozjs::context::{JSContext, NoGC};
    /// use mozjs::cell::JSRefCell;
    /// fn f(no_gc: &NoGC, cell: &JSRefCell<String>) {
    ///     let mut mutably_borrowed = cell.borrow_mut(no_gc);
    /// }
    /// ```
    ///
    /// But in more complex cases, method might trigger a GC, and thus require a [`&mut JSContext`][crate::context::JSContext].
    /// In that case [`&JSContext`][crate::context::JSContext] can be used in place of [`&NoGC`][NoGC],
    /// which will make [`RefMut`] bounded to the lifetime of the [`&JSContext`][crate::context::JSContext]
    /// and thus prevent any GC from happening while it is alive.
    ///
    /// ```
    /// use mozjs::context::{JSContext, NoGC};
    /// use mozjs::cell::JSRefCell;
    /// fn GC(cx: &mut JSContext) {}
    ///
    /// fn f(cx: &mut JSContext, cell: &JSRefCell<String>) {
    ///     {
    ///         let mut mutably_borrowed = cell.borrow_mut(cx);
    ///         // do something with mutably_borrowed
    ///
    ///         // only &JSContext is available here
    ///     } // mutably_borrowed goes out of scope here
    ///     // so one can now use &mut JSContext
    ///     GC(cx);
    /// }
    /// ```
    ///
    /// ## Non-Example
    ///
    /// ```compile_fail
    /// use mozjs::context::{JSContext, NoGC};
    /// use mozjs::cell::JSRefCell;
    /// fn GC(cx: &mut JSContext) {}
    ///
    /// fn f(cx: &mut JSContext, cell: &JSRefCell<String>) {
    ///     {
    ///         let mut mutably_borrowed = cell.borrow_mut(cx);
    ///         // do something with mutably_borrowed
    ///
    ///         // here one cannot use anything that might trigger a GC
    ///         // as that would require &mut JSContext
    ///         // but there is already existing &JSContext bounded at RefMut
    ///         GC(cx);
    ///     } // mutably_borrowed goes out of scope here
    /// }
    /// ```
    ///
    /// # Panics
    ///
    /// Panics if the value is currently borrowed.
    #[track_caller]
    pub fn borrow_mut<'a: 'r, 'no_cx: 'r, 'r>(&'a self, _no_gc: &'no_cx NoGC) -> RefMut<'r, T> {
        self.value.borrow_mut()
    }
}

impl<T: Default> JSRefCell<T> {
    /// Takes the wrapped value, leaving `Default::default()` in its place.
    ///
    /// # Panics
    ///
    /// Panics if the value is currently borrowed.
    pub fn take(&self, _no_gc: &NoGC) -> T {
        self.value.take()
    }
}

unsafe impl<T: Traceable> Traceable for JSRefCell<T> {
    unsafe fn trace(&self, trc: *mut JSTracer) {
        unsafe { (*self).borrow().trace(trc) };
    }
}

/// A cell for interior mutability, that (ab)uses [NoGC]
/// as a borrow token for ensuring safety.
///
/// If inner type is [`Copy`] one should use [`std::cell::Cell`] instead.
///
/// ## Examples
///
/// ```rust
/// use mozjs::context::NoGC;
/// use mozjs::cell::JSCell;
/// fn f(no_gc: &mut NoGC, cell: &JSCell<String>) {
///     {
///         let borrow = cell.borrow(no_gc);
///     } // borrow goes out of scope here
///     cell.set(no_gc, "Hello".to_string());
///     {
///        let borrow_mut = cell.borrow_mut(no_gc);
///     } // borrow_mut goes out of scope here
/// }
/// ```
///
/// ## Non-Examples
///
/// These fail to compile:
///
/// ```compile_fail
/// use mozjs::context::NoGC;
/// use mozjs::cell::JSCell;
/// fn mut_borrow_after_borrow(no_gc: &mut NoGC, cell: &JSCell<String>) {
///     let borrow = cell.borrow(no_gc);
///     cell.set(no_gc, "Hello".to_string());
///     drop(borrow); // otherwise compiler can shorten borrow's lifetime
/// }
/// ```
///
/// ```compile_fail
/// use mozjs::context::NoGC;
/// use mozjs::cell::JSCell;
/// fn mut_borrow_after_mut_borrow(no_gc: &mut NoGC, cell: &JSCell<String>) {
///     let borrow_mut = cell.borrow_mut(no_gc);
///     cell.set(no_gc, "Hello".to_string());
///     drop(borrow_mut); // otherwise compiler can shorten borrow_mut's lifetime
/// }
/// ```
///
/// ```compile_fail
/// use mozjs::context::NoGC;
/// use mozjs::cell::JSCell;
/// fn borrow_after_mut_borrow(no_gc: &mut NoGC, cell: &JSCell<String>) {
///     let borrow_mut = cell.borrow_mut(no_gc);
///     let borrow = cell.borrow(no_gc);
///     drop(borrow_mut); // otherwise compiler can shorten borrow_mut's lifetime
/// }
/// ```
#[derive(Debug)]
pub struct JSCell<T> {
    inner: UnsafeCell<T>,
}

impl<T> JSCell<T> {
    pub fn new(val: T) -> Self {
        JSCell {
            inner: UnsafeCell::new(val),
        }
    }

    /// Sets the wrapped value.
    ///
    /// By passing a [`&mut NoGC`][NoGC] we statically ensure no other borrows are alive at the same time.
    ///
    /// For more information see [JSCell] documentation.
    pub fn set<'a, 'cx>(&'a self, _exclusive: &'cx mut NoGC, val: T) {
        // SAFETY: `&mut NoGC` is used as a borrow token to ensure that no other borrows are alive at the same time.
        unsafe { *self.inner.get() = val }
    }

    /// Mutably borrows the wrapped value.
    ///
    /// By passing a [`&mut NoGC`][NoGC] we statically ensure no other borrows are alive at the same time.
    ///
    /// For more information see [JSCell] documentation.
    pub fn borrow_mut<'a: 'r, 'cx: 'r, 'r>(&'a self, _exclusive: &'cx mut NoGC) -> &'r mut T {
        // SAFETY: `&mut NoGC` is used as a borrow token to ensure that no other borrows are alive at the same time.
        unsafe { &mut *self.inner.get() }
    }

    /// Immutably borrows the wrapped value.
    ///
    /// By passing a [`&NoGC`][NoGC] we statically ensure no mutable borrows are alive at the same time.
    ///
    /// For more information see [JSCell] documentation.
    pub fn borrow<'a: 'r, 'no_cx: 'r, 'r>(&'a self, _no_gc: &'no_cx NoGC) -> &'r T {
        // SAFETY: `&NoGC` is used as a borrow token to ensure that no other mutable borrows are alive at the same time.
        unsafe { &*self.inner.get() }
    }
}

unsafe impl<T: Traceable> Traceable for JSCell<T> {
    unsafe fn trace(&self, trc: *mut JSTracer) {
        // SAFETY: Tracing happens as part of GC, which requires &mut JSContext, so there cannot be any borrow alive at the same time.
        unsafe { (&*self.inner.get()).trace(trc) };
    }
}
