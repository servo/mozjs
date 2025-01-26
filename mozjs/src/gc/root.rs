use std::marker::PhantomData;
use std::mem::MaybeUninit;
use std::ops::{Deref, DerefMut};
use std::ptr;

use crate::jsapi::{jsid, JSContext, JSFunction, JSObject, JSScript, JSString, Symbol, Value, JS};
use mozjs_sys::jsgc::{RootKind, Rooted};

use crate::jsapi::Handle as RawHandle;
use crate::jsapi::HandleValue as RawHandleValue;
use crate::jsapi::MutableHandle as RawMutableHandle;
use mozjs_sys::jsgc::IntoHandle as IntoRawHandle;
use mozjs_sys::jsgc::IntoMutableHandle as IntoRawMutableHandle;
use mozjs_sys::jsgc::ValueArray;

/// Rust API for keeping a Rooted value in the context's root stack.
/// Example usage: `rooted!(in(cx) let x = UndefinedValue());`.
/// `RootedGuard::new` also works, but the macro is preferred.
#[cfg_attr(
    feature = "crown",
    crown::unrooted_must_root_lint::allow_unrooted_interior
)]
pub struct RootedGuard<'a, T: 'a + RootKind> {
    root: &'a mut Rooted<T>,
}

impl<'a, T: 'a + RootKind> RootedGuard<'a, T> {
    pub fn new(cx: *mut JSContext, root: &'a mut Rooted<T>, initial: T) -> Self {
        root.ptr.write(initial);
        unsafe {
            root.add_to_root_stack(cx);
        }
        RootedGuard { root }
    }

    pub fn handle(&'a self) -> Handle<'a, T> {
        Handle::new(&self)
    }

    pub fn handle_mut(&mut self) -> MutableHandle<T> {
        unsafe { MutableHandle::from_marked_location(self.deref_mut()) }
    }

    pub fn get(&self) -> T
    where
        T: Copy,
    {
        // SAFETY: The rooted value is initialized as long as we exist
        unsafe { self.root.ptr.assume_init() }
    }

    pub fn set(&mut self, v: T) {
        // SAFETY: The rooted value is initialized as long as we exist
        unsafe {
            // Make sure the drop impl for T is called
            self.root.ptr.assume_init_drop()
        }
        self.root.ptr.write(v);
    }
}

impl<'a, T: 'a + RootKind> Deref for RootedGuard<'a, T> {
    type Target = T;
    fn deref(&self) -> &T {
        // SAFETY: The rooted value is initialized as long as we exist
        unsafe { self.root.ptr.assume_init_ref() }
    }
}

impl<'a, T: 'a + RootKind> DerefMut for RootedGuard<'a, T> {
    fn deref_mut(&mut self) -> &mut T {
        // SAFETY: The rooted value is initialized as long as we exist
        unsafe { self.root.ptr.assume_init_mut() }
    }
}

impl<'a, T: 'a + RootKind> Drop for RootedGuard<'a, T> {
    fn drop(&mut self) {
        // SAFETY: The rooted value is initialized as long as we exist
        unsafe {
            // Make sure the drop impl for T is called
            self.root.ptr.assume_init_drop()
        }
        self.root.ptr = MaybeUninit::zeroed();

        unsafe {
            self.root.remove_from_root_stack();
        }
    }
}

impl<'a, const N: usize> From<&RootedGuard<'a, ValueArray<N>>> for JS::HandleValueArray {
    fn from(array: &RootedGuard<'a, ValueArray<N>>) -> JS::HandleValueArray {
        JS::HandleValueArray::from(&*array.root)
    }
}

#[derive(Clone, Copy)]
pub struct Handle<'a, T: 'a> {
    pub(crate) ptr: &'a T,
}

#[derive(Copy, Clone)]
pub struct MutableHandle<'a, T: 'a> {
    pub(crate) ptr: *mut T,
    anchor: PhantomData<&'a mut T>,
}

pub type HandleFunction<'a> = Handle<'a, *mut JSFunction>;
pub type HandleId<'a> = Handle<'a, jsid>;
pub type HandleObject<'a> = Handle<'a, *mut JSObject>;
pub type HandleScript<'a> = Handle<'a, *mut JSScript>;
pub type HandleString<'a> = Handle<'a, *mut JSString>;
pub type HandleSymbol<'a> = Handle<'a, *mut Symbol>;
pub type HandleValue<'a> = Handle<'a, Value>;

pub type MutableHandleFunction<'a> = MutableHandle<'a, *mut JSFunction>;
pub type MutableHandleId<'a> = MutableHandle<'a, jsid>;
pub type MutableHandleObject<'a> = MutableHandle<'a, *mut JSObject>;
pub type MutableHandleScript<'a> = MutableHandle<'a, *mut JSScript>;
pub type MutableHandleString<'a> = MutableHandle<'a, *mut JSString>;
pub type MutableHandleSymbol<'a> = MutableHandle<'a, *mut Symbol>;
pub type MutableHandleValue<'a> = MutableHandle<'a, Value>;

impl<'a, T> Handle<'a, T> {
    pub fn get(&self) -> T
    where
        T: Copy,
    {
        *self.ptr
    }

    pub(crate) fn new(ptr: &'a T) -> Self {
        Handle { ptr }
    }

    pub unsafe fn from_marked_location(ptr: *const T) -> Self {
        Handle::new(&*ptr)
    }

    pub unsafe fn from_raw(handle: RawHandle<T>) -> Self {
        Handle::from_marked_location(handle.ptr)
    }
}

impl<'a, T> IntoRawHandle for Handle<'a, T> {
    type Target = T;
    fn into_handle(self) -> RawHandle<T> {
        unsafe { RawHandle::from_marked_location(self.ptr) }
    }
}

impl<'a, T> IntoRawHandle for MutableHandle<'a, T> {
    type Target = T;
    fn into_handle(self) -> RawHandle<T> {
        unsafe { RawHandle::from_marked_location(self.ptr) }
    }
}

impl<'a, T> IntoRawMutableHandle for MutableHandle<'a, T> {
    fn into_handle_mut(self) -> RawMutableHandle<T> {
        unsafe { RawMutableHandle::from_marked_location(self.ptr) }
    }
}

impl<'a, T> Deref for Handle<'a, T> {
    type Target = T;

    fn deref(&self) -> &T {
        self.ptr
    }
}

impl<'a, T> MutableHandle<'a, T> {
    pub unsafe fn from_marked_location(ptr: *mut T) -> Self {
        MutableHandle::new(&mut *ptr)
    }

    pub unsafe fn from_raw(handle: RawMutableHandle<T>) -> Self {
        MutableHandle::from_marked_location(handle.ptr)
    }

    pub fn handle(&self) -> Handle<T> {
        unsafe { Handle::new(&*self.ptr) }
    }

    pub fn new(ptr: &'a mut T) -> Self {
        Self {
            ptr,
            anchor: PhantomData,
        }
    }

    pub fn get(&self) -> T
    where
        T: Copy,
    {
        unsafe { *self.ptr }
    }

    pub fn set(&mut self, v: T)
    where
        T: Copy,
    {
        unsafe { *self.ptr = v }
    }

    pub(crate) fn raw(&mut self) -> RawMutableHandle<T> {
        unsafe { RawMutableHandle::from_marked_location(self.ptr) }
    }
}

impl<'a, T> Deref for MutableHandle<'a, T> {
    type Target = T;

    fn deref(&self) -> &T {
        unsafe { &*self.ptr }
    }
}

impl<'a, T> DerefMut for MutableHandle<'a, T> {
    fn deref_mut(&mut self) -> &mut T {
        unsafe { &mut *self.ptr }
    }
}

impl HandleValue<'static> {
    pub fn null() -> Self {
        unsafe { Self::from_raw(RawHandleValue::null()) }
    }

    pub fn undefined() -> Self {
        unsafe { Self::from_raw(RawHandleValue::undefined()) }
    }
}

const ConstNullValue: *mut JSObject = ptr::null_mut();

impl<'a> HandleObject<'a> {
    pub fn null() -> Self {
        unsafe { HandleObject::from_marked_location(&ConstNullValue) }
    }
}
