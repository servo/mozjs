/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use crate::glue::CallPropertyDescriptorTracer;
use crate::jsapi::js::TraceValueArray;
use crate::jsapi::JS;
use crate::jsapi::{jsid, JSFunction, JSObject, JSScript, JSString, JSTracer};

use crate::jsid::VoidId;
use std::cell::UnsafeCell;
use std::ffi::{c_char, c_void};
use std::mem;
use std::ptr;

/// A trait for JS types that can be registered as roots.
pub trait RootKind {
    type Vtable;
    const VTABLE: Self::Vtable;
    const KIND: JS::RootKind;
}

impl RootKind for *mut JSObject {
    type Vtable = ();
    const VTABLE: Self::Vtable = ();
    const KIND: JS::RootKind = JS::RootKind::Object;
}

impl RootKind for *mut JSFunction {
    type Vtable = ();
    const VTABLE: Self::Vtable = ();
    const KIND: JS::RootKind = JS::RootKind::Object;
}

impl RootKind for *mut JSString {
    type Vtable = ();
    const VTABLE: Self::Vtable = ();
    const KIND: JS::RootKind = JS::RootKind::String;
}

impl RootKind for *mut JS::Symbol {
    type Vtable = ();
    const VTABLE: Self::Vtable = ();
    const KIND: JS::RootKind = JS::RootKind::Symbol;
}

impl RootKind for *mut JS::BigInt {
    type Vtable = ();
    const VTABLE: Self::Vtable = ();
    const KIND: JS::RootKind = JS::RootKind::BigInt;
}

impl RootKind for *mut JSScript {
    type Vtable = ();
    const VTABLE: Self::Vtable = ();
    const KIND: JS::RootKind = JS::RootKind::Script;
}

impl RootKind for jsid {
    type Vtable = ();
    const VTABLE: Self::Vtable = ();
    const KIND: JS::RootKind = JS::RootKind::Id;
}

impl RootKind for JS::Value {
    type Vtable = ();
    const VTABLE: Self::Vtable = ();
    const KIND: JS::RootKind = JS::RootKind::Value;
}

impl<T: TraceableTrace> RootKind for T {
    type Vtable = *const RootedVFTable;
    const VTABLE: Self::Vtable = &<Self as TraceableTrace>::VTABLE;
    const KIND: JS::RootKind = JS::RootKind::Traceable;
}

/// A vtable for use in RootedTraceable<T>, which must be present for stack roots using
/// RootKind::Traceable. The C++ tracing implementation uses a virtual trace function
/// which is only present for C++ Rooted<T> values that use the Traceable root kind.
#[repr(C)]
pub struct RootedVFTable {
    #[cfg(windows)]
    pub padding: [usize; 1],
    #[cfg(not(windows))]
    pub padding: [usize; 2],
    pub trace: unsafe extern "C" fn(this: *mut c_void, trc: *mut JSTracer, name: *const c_char),
}

impl RootedVFTable {
    #[cfg(windows)]
    pub const PADDING: [usize; 1] = [0];
    #[cfg(not(windows))]
    pub const PADDING: [usize; 2] = [0, 0];
}

/// `Rooted<T>` with a T that uses the Traceable RootKind uses dynamic dispatch on the C++ side
/// for custom tracing. This trait provides trace logic via a vtable when creating a Rust instance
/// of the object.
pub unsafe trait TraceableTrace: Sized {
    const VTABLE: RootedVFTable = RootedVFTable {
        padding: RootedVFTable::PADDING,
        trace: Self::trace,
    };

    unsafe extern "C" fn trace(this: *mut c_void, trc: *mut JSTracer, _name: *const c_char) {
        let rooted = this as *mut Rooted<Self>;
        let rooted = rooted.as_mut().unwrap();
        Self::do_trace(&mut rooted.ptr, trc);
    }

    /// Used by `TraceableTrace` implementer to trace its contents.
    /// Corresponds to virtual `trace` call in a `Rooted` that inherits from
    /// StackRootedTraceableBase (C++).
    unsafe fn do_trace(&mut self, trc: *mut JSTracer);
}

unsafe impl TraceableTrace for JS::PropertyDescriptor {
    unsafe fn do_trace(&mut self, trc: *mut JSTracer) {
        CallPropertyDescriptorTracer(trc, self);
    }
}

// The C++ representation of Rooted<T> inherits from StackRootedBase, which
// contains the actual pointers that get manipulated. The Rust representation
// also uses the pattern, which is critical to ensuring that the right pointers
// to Rooted<T> values are used, since some Rooted<T> values are prefixed with
// a vtable pointer, and we don't want to store pointers to that vtable where
// C++ expects a StackRootedBase.
#[repr(C)]
#[derive(Debug)]
pub struct RootedBase {
    pub stack: *mut *mut RootedBase,
    pub prev: *mut RootedBase,
}

// Annoyingly, bindgen can't cope with SM's use of templates, so we have to roll our own.
#[repr(C)]
#[derive(Debug)]
pub struct Rooted<T: RootKind> {
    pub vtable: T::Vtable,
    pub base: RootedBase,
    pub ptr: T,
}

/// A trait for types which can place appropriate GC barriers.
/// * https://developer.mozilla.org/en-US/docs/Mozilla/Projects/SpiderMonkey/Internals/Garbage_collection#Incremental_marking
/// * https://dxr.mozilla.org/mozilla-central/source/js/src/gc/Barrier.h
pub trait GCMethods {
    /// Create a default value
    unsafe fn initial() -> Self;

    /// Place a post-write barrier
    unsafe fn post_barrier(v: *mut Self, prev: Self, next: Self);
}

impl GCMethods for *mut JSObject {
    unsafe fn initial() -> *mut JSObject {
        ptr::null_mut()
    }
    unsafe fn post_barrier(v: *mut *mut JSObject, prev: *mut JSObject, next: *mut JSObject) {
        JS::HeapObjectWriteBarriers(v, prev, next);
    }
}

impl GCMethods for *mut JSFunction {
    unsafe fn initial() -> *mut JSFunction {
        ptr::null_mut()
    }
    unsafe fn post_barrier(v: *mut *mut JSFunction, prev: *mut JSFunction, next: *mut JSFunction) {
        JS::HeapObjectWriteBarriers(
            mem::transmute(v),
            mem::transmute(prev),
            mem::transmute(next),
        );
    }
}

impl GCMethods for *mut JSString {
    unsafe fn initial() -> *mut JSString {
        ptr::null_mut()
    }
    unsafe fn post_barrier(v: *mut *mut JSString, prev: *mut JSString, next: *mut JSString) {
        JS::HeapStringWriteBarriers(v, prev, next);
    }
}

impl GCMethods for *mut JS::Symbol {
    unsafe fn initial() -> *mut JS::Symbol {
        ptr::null_mut()
    }
    unsafe fn post_barrier(_: *mut *mut JS::Symbol, _: *mut JS::Symbol, _: *mut JS::Symbol) {}
}

impl GCMethods for *mut JS::BigInt {
    unsafe fn initial() -> *mut JS::BigInt {
        ptr::null_mut()
    }
    unsafe fn post_barrier(v: *mut *mut JS::BigInt, prev: *mut JS::BigInt, next: *mut JS::BigInt) {
        JS::HeapBigIntWriteBarriers(v, prev, next);
    }
}

impl GCMethods for *mut JSScript {
    unsafe fn initial() -> *mut JSScript {
        ptr::null_mut()
    }
    unsafe fn post_barrier(v: *mut *mut JSScript, prev: *mut JSScript, next: *mut JSScript) {
        JS::HeapScriptWriteBarriers(v, prev, next);
    }
}

impl GCMethods for jsid {
    unsafe fn initial() -> jsid {
        VoidId()
    }
    unsafe fn post_barrier(_: *mut jsid, _: jsid, _: jsid) {}
}

impl GCMethods for JS::Value {
    unsafe fn initial() -> JS::Value {
        JS::Value::default()
    }
    unsafe fn post_barrier(v: *mut JS::Value, prev: JS::Value, next: JS::Value) {
        JS::HeapValueWriteBarriers(v, &prev, &next);
    }
}

impl GCMethods for JS::PropertyDescriptor {
    unsafe fn initial() -> JS::PropertyDescriptor {
        JS::PropertyDescriptor::default()
    }
    unsafe fn post_barrier(
        _: *mut JS::PropertyDescriptor,
        _: JS::PropertyDescriptor,
        _: JS::PropertyDescriptor,
    ) {
    }
}

/// A fixed-size array of values, for use inside Rooted<>.
///
/// https://searchfox.org/mozilla-central/source/js/public/ValueArray.h#31
pub struct ValueArray<const N: usize> {
    elements: [JS::Value; N],
}

impl<const N: usize> ValueArray<N> {
    pub fn new(elements: [JS::Value; N]) -> Self {
        Self { elements }
    }

    pub fn to_handle_value_array(&self) -> JS::HandleValueArray {
        JS::HandleValueArray {
            length_: N,
            elements_: self.elements.as_ptr(),
        }
    }

    pub unsafe fn get_ptr(&self) -> *const JS::Value {
        self.elements.as_ptr()
    }

    pub unsafe fn get_mut_ptr(&self) -> *mut JS::Value {
        self.elements.as_ptr() as *mut _
    }
}

unsafe impl<const N: usize> TraceableTrace for ValueArray<N> {
    unsafe fn do_trace(&mut self, trc: *mut JSTracer) {
        TraceValueArray(trc, N, self.get_mut_ptr());
    }
}

impl<const N: usize> GCMethods for ValueArray<N> {
    unsafe fn initial() -> Self {
        Self {
            elements: [JS::Value::initial(); N],
        }
    }
    unsafe fn post_barrier(_: *mut Self, _: Self, _: Self) {}
}

/// RootedValueArray roots an internal fixed-size array of Values
pub type RootedValueArray<const N: usize> = Rooted<ValueArray<N>>;

/// Heap values encapsulate GC concerns of an on-heap reference to a JS
/// object. This means that every reference to a JS object on heap must
/// be realized through this structure.
///
/// # Safety
/// For garbage collection to work correctly in SpiderMonkey, modifying the
/// wrapped value triggers a GC barrier, pointing to the underlying object.
///
/// This means that after calling the `set()` function with a non-null or
/// non-undefined value, the `Heap` wrapper *must not* be moved, since doing
/// so will invalidate the local reference to wrapped value, still held by
/// SpiderMonkey.
///
/// For safe `Heap` construction with value see `Heap::boxed` function.
#[repr(C)]
#[derive(Debug)]
pub struct Heap<T: GCMethods + Copy> {
    pub ptr: UnsafeCell<T>,
}

impl<T: GCMethods + Copy> Heap<T> {
    /// This creates a `Box`-wrapped Heap value. Setting a value inside Heap
    /// object triggers a barrier, referring to the Heap object location,
    /// hence why it is not safe to construct a temporary Heap value, assign
    /// a non-null value and move it (e.g. typical object construction).
    ///
    /// Using boxed Heap value guarantees that the underlying Heap value will
    /// not be moved when constructed.
    pub fn boxed(v: T) -> Box<Heap<T>>
    where
        Heap<T>: Default,
    {
        let boxed = Box::new(Heap::default());
        boxed.set(v);
        boxed
    }

    pub fn set(&self, v: T) {
        unsafe {
            let ptr = self.ptr.get();
            let prev = *ptr;
            *ptr = v;
            T::post_barrier(ptr, prev, v);
        }
    }

    pub fn get(&self) -> T {
        unsafe { *self.ptr.get() }
    }

    pub fn get_unsafe(&self) -> *mut T {
        self.ptr.get()
    }

    /// Retrieves a Handle to the underlying value.
    ///
    /// # Safety
    ///
    /// This is only safe to do on a rooted object (which Heap is not, it needs
    /// to be additionally rooted), like RootedGuard, so use this only if you
    /// know what you're doing.
    ///
    /// # Notes
    ///
    /// Since Heap values need to be informed when a change to underlying
    /// value is made (e.g. via `get()`), this does not allow to create
    /// MutableHandle objects, which can bypass this and lead to crashes.
    pub unsafe fn handle(&self) -> JS::Handle<T> {
        JS::Handle::from_marked_location(self.ptr.get() as *const _)
    }
}

impl<T> Default for Heap<*mut T>
where
    *mut T: GCMethods + Copy,
{
    fn default() -> Heap<*mut T> {
        Heap {
            ptr: UnsafeCell::new(ptr::null_mut()),
        }
    }
}

impl Default for Heap<JS::Value> {
    fn default() -> Heap<JS::Value> {
        Heap {
            ptr: UnsafeCell::new(JS::Value::default()),
        }
    }
}

impl<T: GCMethods + Copy> Drop for Heap<T> {
    fn drop(&mut self) {
        unsafe {
            let ptr = self.ptr.get();
            T::post_barrier(ptr, *ptr, T::initial());
        }
    }
}

impl<T: GCMethods + Copy + PartialEq> PartialEq for Heap<T> {
    fn eq(&self, other: &Self) -> bool {
        self.get() == other.get()
    }
}

/// Trait for things that can be converted to handles
/// For any type `T: IntoHandle` we have an implementation of `From<T>`
/// for `MutableHandle<T::Target>`. This is a way round the orphan
/// rule.
pub trait IntoHandle {
    /// The type of the handle
    type Target;

    /// Convert this object to a handle.
    fn into_handle(self) -> JS::Handle<Self::Target>;
}

pub trait IntoMutableHandle: IntoHandle {
    /// Convert this object to a mutable handle.
    fn into_handle_mut(self) -> JS::MutableHandle<Self::Target>;
}

impl<T: IntoHandle> From<T> for JS::Handle<T::Target> {
    fn from(value: T) -> Self {
        value.into_handle()
    }
}

impl<T: IntoMutableHandle> From<T> for JS::MutableHandle<T::Target> {
    fn from(value: T) -> Self {
        value.into_handle_mut()
    }
}

/// Methods for a CustomAutoRooter
#[repr(C)]
pub struct CustomAutoRooterVFTable {
    #[cfg(windows)]
    pub padding: [usize; 1],
    #[cfg(not(windows))]
    pub padding: [usize; 2],
    pub trace: unsafe extern "C" fn(this: *mut c_void, trc: *mut JSTracer),
}

impl CustomAutoRooterVFTable {
    #[cfg(windows)]
    pub const PADDING: [usize; 1] = [0];
    #[cfg(not(windows))]
    pub const PADDING: [usize; 2] = [0, 0];
}
