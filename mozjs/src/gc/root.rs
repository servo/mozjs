use std::marker::PhantomData;
use std::mem::MaybeUninit;
use std::ops::Deref;
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
    root: *mut Rooted<T>,
    anchor: PhantomData<&'a mut Rooted<T>>,
}

impl<'a, T: 'a + RootKind> RootedGuard<'a, T> {
    pub fn new(cx: *mut JSContext, root: &'a mut MaybeUninit<Rooted<T>>, initial: T) -> Self {
        let root: *mut Rooted<T> = root.write(Rooted::new_unrooted(initial));

        unsafe {
            Rooted::add_to_root_stack(root, cx);
            RootedGuard {
                root,
                anchor: PhantomData,
            }
        }
    }

    pub fn handle(&'a self) -> Handle<'a, T> {
        Handle::new(&self)
    }

    pub fn handle_mut(&mut self) -> MutableHandle<T> {
        unsafe { MutableHandle::from_marked_location(self.as_ptr()) }
    }

    pub fn as_ptr(&self) -> *mut T {
        // SAFETY: self.root points to an inbounds allocation
        unsafe { (&raw mut (*self.root).data) }
    }

    /// Safety: GC must not run during the lifetime of the returned reference.
    pub unsafe fn as_mut<'b>(&'b mut self) -> &'b mut T
    where
        'a: 'b,
    {
        &mut *(self.as_ptr())
    }

    pub fn get(&self) -> T
    where
        T: Copy,
    {
        *self.deref()
    }

    pub fn set(&mut self, v: T) {
        // SAFETY: GC does not run during this block
        unsafe { *self.as_mut() = v };
    }
}

impl<'a, T> RootedGuard<'a, Option<T>>
where
    Option<T>: RootKind,
{
    pub fn take(&mut self) -> Option<T> {
        // Safety: No GC occurs during take call
        unsafe { self.as_mut().take() }
    }
}

impl<'a, T: 'a + RootKind> Deref for RootedGuard<'a, T> {
    type Target = T;
    fn deref(&self) -> &T {
        unsafe { &(*self.root).data }
    }
}

impl<'a, T: 'a + RootKind> Drop for RootedGuard<'a, T> {
    fn drop(&mut self) {
        // SAFETY: The `drop_in_place` invariants are upheld:
        // https://doc.rust-lang.org/std/ptr/fn.drop_in_place.html#safety
        unsafe {
            let ptr = self.as_ptr();
            ptr::drop_in_place(ptr);
            ptr.write_bytes(0, 1);
        }

        unsafe {
            (*self.root).remove_from_root_stack();
        }
    }
}

impl<'a, const N: usize> From<&RootedGuard<'a, ValueArray<N>>> for JS::HandleValueArray {
    fn from(array: &RootedGuard<'a, ValueArray<N>>) -> JS::HandleValueArray {
        JS::HandleValueArray::from(unsafe { &*array.root })
    }
}

pub struct Handle<'a, T: 'a> {
    pub(crate) ptr: &'a T,
}

impl<T> Clone for Handle<'_, T> {
    fn clone(&self) -> Self {
        *self
    }
}

impl<T> Copy for Handle<'_, T> {}

#[cfg_attr(
    feature = "crown",
    crown::unrooted_must_root_lint::allow_unrooted_interior
)]
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

    pub(crate) fn new(ptr: &'a mut T) -> Self {
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

    /// Safety: GC must not run during the lifetime of the returned reference.
    pub unsafe fn as_mut<'b>(&'b mut self) -> &'b mut T
    where
        'a: 'b,
    {
        &mut *(self.ptr)
    }

    /// Creates a copy of this object, with a shorter lifetime, that holds a
    /// mutable borrow on the original object. When you write code that wants
    /// to use a `MutableHandle` more than once, you will typically need to
    /// call `reborrow` on all but the last usage. The same way that you might
    /// naively clone a type to allow it to be passed to multiple functions.
    ///
    /// This is the same thing that happens with regular mutable references,
    /// except there the compiler implicitly inserts the reborrow calls. Until
    /// rust gains a feature to implicitly reborrow other types, we have to do
    /// it by hand.
    pub fn reborrow<'b>(&'b mut self) -> MutableHandle<'b, T>
    where
        'a: 'b,
    {
        MutableHandle {
            ptr: self.ptr,
            anchor: PhantomData,
        }
    }

    pub(crate) fn raw(&mut self) -> RawMutableHandle<T> {
        unsafe { RawMutableHandle::from_marked_location(self.ptr) }
    }
}

impl<'a, T> MutableHandle<'a, Option<T>> {
    pub fn take(&mut self) -> Option<T> {
        // Safety: No GC occurs during take call
        unsafe { self.as_mut().take() }
    }
}

impl<'a, T> Deref for MutableHandle<'a, T> {
    type Target = T;

    fn deref(&self) -> &T {
        unsafe { &*self.ptr }
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
