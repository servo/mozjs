use crate::gc::RootedTraceableSet;
use crate::jsapi::{Heap, JSTracer};
use crate::rust::Handle;
use mozjs_sys::jsapi::JS;
use mozjs_sys::jsgc::GCMethods;
use mozjs_sys::jsval::JSVal;
use mozjs_sys::trace::Traceable;
use std::marker::PhantomData;
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

/// Inline, fixed capacity buffer of JS values with GC barriers.
/// Backed by `[Heap<JSVal>; N]`, and a manual `len`.
pub struct FixedValueArray<const N: usize> {
    elems: [Heap<JSVal>; N],
    len: usize,
}

unsafe impl<const N: usize> Traceable for FixedValueArray<N> {
    unsafe fn trace(&self, trc: *mut JSTracer) {
        for i in 0..self.len {
            self.elems[i].trace(trc);
        }
    }
}

impl<const N: usize> FixedValueArray<N> {
    /// Create a new empty array, with all slots initialized to Undefined.
    pub fn new() -> Self {
        Self {
            elems: std::array::from_fn(|_| Heap::<JSVal>::default()),
            len: 0,
        }
    }

    /// Current logical length.
    pub fn len(&self) -> usize {
        self.len
    }

    /// Max capacity (const generic).
    pub fn capacity(&self) -> usize {
        N
    }

    /// Push a JSVal into the next slot.
    /// Panics if you exceed capacity N.
    pub fn push(&mut self, v: JSVal) {
        assert!(self.len < N, "FixedValueArray capacity ({}) exceeded", N);
        self.elems[self.len].set(v);
        self.len += 1;
    }

    /// Return a stable HandleValueArray that SpiderMonkey can consume.
    pub fn as_handle_value_array(&self) -> JS::HandleValueArray {
        JS::HandleValueArray {
            length_: self.len,
            elements_: self.elems.as_ptr() as *const JSVal,
        }
    }
}

/// A rooted wrapper for a FixedValueArray<N>.
pub struct RootedFixedValueArray<'a, const N: usize> {
    ptr: *mut FixedValueArray<N>,
    _phantom: PhantomData<&'a mut FixedValueArray<N>>,
}

impl<'a, const N: usize> RootedFixedValueArray<'a, N> {
    /// Allocate a FixedValueArray<N>, register it in RootedTraceableSet so the GC
    pub fn new() -> Self {
        let boxed = Box::new(FixedValueArray::<N>::new());
        let raw = Box::into_raw(boxed);

        unsafe {
            RootedTraceableSet::add(raw);
        }

        RootedFixedValueArray {
            ptr: raw,
            _phantom: PhantomData,
        }
    }

    /// Push a JSVal into the underlying fixed array.
    pub fn push(&mut self, v: JSVal) {
        unsafe { (&mut *self.ptr).push(v) }
    }

    /// Produce a stable HandleValueArray view into the initialized prefix.
    pub fn as_handle_value_array(&self) -> JS::HandleValueArray {
        unsafe { (&*self.ptr).as_handle_value_array() }
    }

    pub fn len(&self) -> usize {
        unsafe { (&*self.ptr).len() }
    }

    pub fn capacity(&self) -> usize {
        unsafe { (&*self.ptr).capacity() }
    }
}

impl<'a, const N: usize> Drop for RootedFixedValueArray<'a, N> {
    fn drop(&mut self) {
        unsafe {
            RootedTraceableSet::remove(self.ptr);
            let _ = Box::from_raw(self.ptr);
        }
    }
}

/// A growable, rooted, GC traceable collection of JS values.
/// Elements are stored in boxed Heap<JSVal> cells so each GC cell has a stable
/// address even if the Vec reallocates.
pub struct DynamicValueArray {
    elems: Vec<Box<Heap<JSVal>>>,
}

unsafe impl Traceable for DynamicValueArray {
    unsafe fn trace(&self, trc: *mut JSTracer) {
        for heap_box in &self.elems {
            heap_box.trace(trc);
        }
    }
}

impl DynamicValueArray {
    pub fn new() -> Self {
        DynamicValueArray { elems: Vec::new() }
    }

    pub fn with_capacity(cap: usize) -> Self {
        DynamicValueArray {
            elems: Vec::with_capacity(cap),
        }
    }

    pub fn push(&mut self, v: JSVal) {
        let cell = Heap::boxed(v);
        self.elems.push(cell);
    }

    pub fn len(&self) -> usize {
        self.elems.len()
    }
}

/// A rooted, wrapper for DynamicValueArray which also owns
/// Vec<JSVal> used to present a HandleValueArray view to SpiderMonkey.
pub struct RootedDynamicValueArray {
    ptr: *mut DynamicValueArray,
    scratch: Vec<JSVal>,
}

unsafe impl Traceable for RootedDynamicValueArray {
    unsafe fn trace(&self, trc: *mut JSTracer) {
        (&*self.ptr).trace(trc)
    }
}

impl RootedDynamicValueArray {
    pub fn new() -> Self {
        let boxed = Box::new(DynamicValueArray::new());
        let raw = Box::into_raw(boxed);

        unsafe {
            RootedTraceableSet::add(raw);
        }

        RootedDynamicValueArray {
            ptr: raw,
            scratch: Vec::new(),
        }
    }

    pub fn with_capacity(cap: usize) -> Self {
        let boxed = Box::new(DynamicValueArray::with_capacity(cap));
        let raw = Box::into_raw(boxed);

        unsafe {
            RootedTraceableSet::add(raw);
        }

        RootedDynamicValueArray {
            ptr: raw,
            scratch: Vec::with_capacity(cap),
        }
    }

    pub fn push(&mut self, v: JSVal) {
        unsafe {
            (&mut *self.ptr).push(v);
        }
    }

    fn rebuild_scratch(&mut self) {
        let inner = unsafe { &*self.ptr };
        self.scratch.clear();
        self.scratch.reserve(inner.len());
        for heap_box in &inner.elems {
            self.scratch.push(heap_box.get());
        }
    }

    pub fn as_handle_value_array(&mut self) -> JS::HandleValueArray {
        self.rebuild_scratch();
        JS::HandleValueArray {
            length_: self.scratch.len(),
            elements_: self.scratch.as_ptr(),
        }
    }

    pub fn len(&self) -> usize {
        unsafe { (&*self.ptr).len() }
    }
}

impl Drop for RootedDynamicValueArray {
    fn drop(&mut self) {
        unsafe {
            RootedTraceableSet::remove(self.ptr);
            let _ = Box::from_raw(self.ptr);
        }
    }
}
