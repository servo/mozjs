use std::marker::PhantomData;
use std::ptr::NonNull;

use mozjs_sys::jsapi::JS::Realm;
use mozjs_sys::jsapi::{JSAutoRealm, JSObject};

use crate::context::JSContext;
use crate::gc::Handle;
use crate::rust::wrappers2::{CurrentGlobalOrNull, GetCurrentRealmOrNull};

pub struct AutoRealm<'cx> {
    cx: JSContext,
    realm: JSAutoRealm,
    phantom: PhantomData<&'cx mut ()>,
}

impl<'cx> AutoRealm<'cx> {
    /// Enters the realm of the given target object.
    /// The realm becomes the current realm (it's on top of the realm stack).
    /// The realm is exited when the [AutoRealm] is dropped.
    ///
    /// While this function will not trigger GC (it will in fact root the object)
    /// but because [AutoRealm] can act as a [JSContext] we need to take `&mut JSContext`.
    pub fn new(cx: &'cx mut JSContext, target: NonNull<JSObject>) -> AutoRealm<'cx> {
        let realm = JSAutoRealm::new(unsafe { cx.raw_cx_no_gc() }, target.as_ptr());
        AutoRealm {
            cx: unsafe { JSContext::from_ptr(NonNull::new_unchecked(cx.raw_cx())) },
            realm,
            phantom: PhantomData,
        }
    }

    /// Enters the realm of the given target object.
    /// The realm becomes the current realm (it's on top of the realm stack).
    /// The realm is exited when the [AutoRealm] is dropped.
    ///
    /// While this function will not trigger GC (it will in fact root the object)
    /// but because [AutoRealm] can act as a [JSContext] we need to take `&mut JSContext`.
    pub fn new_from_handle(
        cx: &'cx mut JSContext,
        target: Handle<*mut JSObject>,
    ) -> AutoRealm<'cx> {
        Self::new(cx, NonNull::new(target.get()).unwrap())
    }

    /// If we can get &mut AutoRealm then we are current realm,
    /// because if there existed other current realm, we couldn't get &mut AutoRealm.
    pub fn current_realm(&'cx mut self) -> CurrentRealm<'cx> {
        CurrentRealm::assert(self.cx())
    }

    /// Obtain the handle to the global object of the current realm.
    pub fn global(&'_ self) -> Handle<'_, *mut JSObject> {
        // SAFETY: object is rooted by realm
        unsafe { Handle::from_marked_location(CurrentGlobalOrNull(self.cx_no_gc()) as _) }
    }

    /// Erase the lifetime of this [AutoRealm].
    ///
    /// # Safety
    /// - The caller must ensure that the [AutoRealm] does not outlive the [JSContext] it was created with.
    pub unsafe fn erase_lifetime(self) -> AutoRealm<'static> {
        std::mem::transmute(self)
    }

    pub fn cx(&mut self) -> &mut JSContext {
        &mut self.cx
    }

    pub fn cx_no_gc(&self) -> &JSContext {
        &self.cx
    }

    pub fn realm(&self) -> &JSAutoRealm {
        &self.realm
    }
}

impl<'cx> Drop for AutoRealm<'cx> {
    // this is not trivially dropped type
    // not sure why we need to do this manually
    fn drop(&mut self) {}
}

/// Represents the current realm of [JSContext] (top realm on realm stack).
pub struct CurrentRealm<'cx> {
    cx: &'cx mut JSContext,
    realm: NonNull<Realm>,
}

impl<'cx> CurrentRealm<'cx> {
    /// Asserts that the current realm is valid and returns it.
    pub fn assert(cx: &'cx mut JSContext) -> CurrentRealm<'cx> {
        let realm = unsafe { GetCurrentRealmOrNull(cx) };
        CurrentRealm {
            cx,
            realm: NonNull::new(realm).unwrap(),
        }
    }

    /// Obtain the handle to the global object of the current realm.
    pub fn global(&'_ self) -> Handle<'_, *mut JSObject> {
        // SAFETY: object is rooted by realm
        unsafe { Handle::from_marked_location(CurrentGlobalOrNull(self.cx_no_gc()) as _) }
    }

    pub fn cx(&mut self) -> &mut JSContext {
        self.cx
    }

    pub fn cx_no_gc(&self) -> &JSContext {
        &self.cx
    }

    pub fn realm(&self) -> &NonNull<Realm> {
        &self.realm
    }
}
