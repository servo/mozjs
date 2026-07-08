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
    /// By passing a `&NoGC` we statically prevent GC from being run while the borrow is active,
    /// to prevent panic when tracing (which calls `borrow`).
    ///
    /// # Example
    ///
    /// In simple cases one can use `NoGC` to statically ensure no GC can happen in the whole DOM method:
    ///
    /// ```
    /// use mozjs::context::{JSContext, NoGC};
    /// use mozjs::cell::JSRefCell;
    /// fn DomMethod(no_gc: &NoGC, cell: &JSRefCell<usize>) {
    ///     let mut mutably_borrowed = cell.borrow_mut(no_gc);
    /// }
    /// ```
    ///
    /// But in more complex cases, method might trigger a GC, and thus require a `&mut JSContext`.
    /// In that case `&JSContext` can be used in place of `NoGC`,
    /// which will make `RefMut` bounded to the lifetime of the `&JSContext`
    /// and thus prevent any GC from happening while it is alive.
    ///
    /// ```
    /// use mozjs::context::{JSContext, NoGC};
    /// use mozjs::cell::JSRefCell;
    /// fn GC(cx: &mut JSContext) {}
    ///
    /// fn DomMethod(cell: &JSRefCell<usize>, cx: &mut JSContext) {
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
    /// ```compile_fail
    /// use mozjs::context::{JSContext, NoGC};
    /// use mozjs::cell::JSRefCell;
    /// fn GC(cx: &mut JSContext) {}
    ///
    /// fn DomMethod(cell: &JSRefCell<usize>, cx: &mut JSContext) {
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

/// A cell for interior mutability, that (ab)uses [JSContext]
/// as affinity token for ensuring safety.
///
/// If inner type is `Copy` one should prefer using normal [`std::cell::Cell`] instead.
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

    pub fn set<'a, 'cx>(&'a self, _exclusive: &'cx mut NoGC, val: T) {
        // SAFETY: `&mut NoGC` is used as an affinity token to ensure that no other borrows are alive at the same time.
        unsafe { *self.inner.get() = val }
    }

    pub fn borrow_mut<'a: 'r, 'cx: 'r, 'r>(&'a self, _exclusive: &'cx mut NoGC) -> &'r mut T {
        // SAFETY: `&mut NoGC` is used as an affinity token to ensure that no other borrows are alive at the same time.
        unsafe { &mut *self.inner.get() }
    }

    pub fn borrow<'a: 'r, 'no_cx: 'r, 'r>(&'a self, _no_gc: &'no_cx NoGC) -> &'r T {
        // SAFETY: `&NoGC` is used as an affinity token to ensure that no other mutable borrows are alive at the same time.
        unsafe { &*self.inner.get() }
    }
}

unsafe impl<T: Traceable> Traceable for JSCell<T> {
    unsafe fn trace(&self, trc: *mut JSTracer) {
        // SAFETY: Tracing happens as part of GC, which requires &mut JSContext, so there cannot be any borrow alive at the same time.
        unsafe { (&*self.inner.get()).trace(trc) };
    }
}
