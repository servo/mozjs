use crate::gc::RootedTraceableSet;
use crate::jsapi::{Heap, JSTracer};
use crate::rust::Handle;
use mozjs_sys::jsapi::JS;
use mozjs_sys::jsgc::GCMethods;
use mozjs_sys::jsval::JSVal;
use mozjs_sys::trace::Traceable;
use std::ops::{Deref, DerefMut};

/// A vector of items to be rooted with `RootedVec`.
/// Guaranteed to be empty when not rooted.
#[cfg_attr(feature = "crown", allow(crown::unrooted_must_root))]
#[cfg_attr(
    feature = "crown",
    crown::unrooted_must_root_lint::allow_unrooted_interior
)]
pub struct RootableVec<T: Traceable> {
    v: Vec<T>,
}

impl<T: Traceable> RootableVec<T> {
    /// Create a vector of items of type T that can be rooted later.
    pub fn new_unrooted() -> RootableVec<T> {
        RootableVec { v: Vec::new() }
    }
}

unsafe impl<T: Traceable> Traceable for RootableVec<T> {
    unsafe fn trace(&self, trc: *mut JSTracer) {
        self.v.trace(trc);
    }
}

/// A vector of items rooted for the lifetime 'a.
#[cfg_attr(
    feature = "crown",
    crown::unrooted_must_root_lint::allow_unrooted_interior
)]
pub struct RootedVec<'a, T: Traceable + 'static> {
    root: &'a mut RootableVec<T>,
}

impl From<&RootedVec<'_, JSVal>> for JS::HandleValueArray {
    fn from(vec: &RootedVec<'_, JSVal>) -> JS::HandleValueArray {
        JS::HandleValueArray {
            length_: vec.root.v.len(),
            elements_: vec.root.v.as_ptr(),
        }
    }
}

impl<'a, T: Traceable + 'static> RootedVec<'a, T> {
    pub fn new(root: &'a mut RootableVec<T>) -> RootedVec<'a, T> {
        unsafe {
            RootedTraceableSet::add(root);
        }
        RootedVec { root }
    }

    pub fn from_iter<I>(root: &'a mut RootableVec<T>, iter: I) -> Self
    where
        I: Iterator<Item = T>,
    {
        unsafe {
            RootedTraceableSet::add(root);
        }
        root.v.extend(iter);
        RootedVec { root }
    }
}

impl<'a, T: Traceable + 'static> Drop for RootedVec<'a, T> {
    fn drop(&mut self) {
        self.clear();
        unsafe {
            RootedTraceableSet::remove(self.root);
        }
    }
}

impl<'a, T: Traceable> Deref for RootedVec<'a, T> {
    type Target = Vec<T>;
    fn deref(&self) -> &Vec<T> {
        &self.root.v
    }
}

impl<'a, T: Traceable> DerefMut for RootedVec<'a, T> {
    fn deref_mut(&mut self) -> &mut Vec<T> {
        &mut self.root.v
    }
}

/// Roots any JSTraceable thing
///
/// If you have GC things like *mut JSObject or JSVal, use rooted!.
/// If you know what you're doing, use this.
pub struct RootedTraceableBox<T: Traceable + 'static> {
    ptr: *mut T,
}

impl<T: Traceable + 'static> RootedTraceableBox<T> {
    /// Root a JSTraceable thing for the life of this RootedTraceableBox
    pub fn new(traceable: T) -> RootedTraceableBox<T> {
        Self::from_box(Box::new(traceable))
    }

    /// Consumes a boxed JSTraceable and roots it for the life of this RootedTraceableBox.
    pub fn from_box(boxed_traceable: Box<T>) -> RootedTraceableBox<T> {
        let traceable = Box::into_raw(boxed_traceable);
        unsafe {
            RootedTraceableSet::add(traceable);
        }
        RootedTraceableBox { ptr: traceable }
    }

    /// Returns underlying pointer
    pub unsafe fn ptr(&self) -> *mut T {
        self.ptr
    }
}

impl<T> RootedTraceableBox<Heap<T>>
where
    Heap<T>: Traceable + 'static,
    T: GCMethods + Copy,
{
    pub fn handle(&self) -> Handle<T> {
        unsafe { Handle::from_raw((*self.ptr).handle()) }
    }
}

unsafe impl<T: Traceable + 'static> Traceable for RootedTraceableBox<T> {
    unsafe fn trace(&self, trc: *mut JSTracer) {
        (*self.ptr).trace(trc)
    }
}

impl<T: Traceable> Deref for RootedTraceableBox<T> {
    type Target = T;
    fn deref(&self) -> &T {
        unsafe { &*self.ptr }
    }
}

impl<T: Traceable> DerefMut for RootedTraceableBox<T> {
    fn deref_mut(&mut self) -> &mut T {
        unsafe { &mut *self.ptr }
    }
}

impl<T: Traceable + 'static> Drop for RootedTraceableBox<T> {
    fn drop(&mut self) {
        unsafe {
            RootedTraceableSet::remove(self.ptr);
            let _ = Box::from_raw(self.ptr);
        }
    }
}
