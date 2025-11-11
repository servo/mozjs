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
    /// While this function will not trigger GC (it will in fact root the object)
    /// but because [AutoRealm] can act as a [JSContext] we need to take via `&mut`,
    /// thus effectively preventing any out of order drops.
    pub fn new(cx: &'cx mut JSContext, target: NonNull<JSObject>) -> AutoRealm<'cx> {
        let realm = JSAutoRealm::new(unsafe { cx.raw_cx_no_gc() }, target.as_ptr());
        AutoRealm {
            cx: unsafe { JSContext::from_ptr(NonNull::new_unchecked(cx.raw_cx())) },
            realm,
            phantom: PhantomData,
        }
    }

    pub fn new_from_handle(
        cx: &'cx mut JSContext,
        target: Handle<*mut JSObject>,
    ) -> AutoRealm<'cx> {
        Self::new(cx, NonNull::new(target.get()).unwrap())
    }

    pub fn cx(&mut self) -> &mut JSContext {
        &mut self.cx
    }

    pub fn cx_no_gc(&self) -> &JSContext {
        &self.cx
    }

    pub fn in_realm(&'cx mut self) -> InRealm<'cx> {
        InRealm::Entered(self)
    }

    pub fn global(&'_ self) -> Handle<'_, *mut JSObject> {
        // SAFETY: object is rooted by realm
        unsafe { Handle::from_marked_location(CurrentGlobalOrNull(self.cx_no_gc()) as _) }
    }

    pub unsafe fn erase_lifetime(self) -> AutoRealm<'static> {
        std::mem::transmute(self)
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

pub struct AlreadyInRealm<'cx> {
    cx: &'cx mut JSContext,
    realm: NonNull<Realm>,
}

impl<'cx> AlreadyInRealm<'cx> {
    pub fn assert(cx: &'cx mut JSContext) -> AlreadyInRealm<'cx> {
        let realm = unsafe { GetCurrentRealmOrNull(cx) };
        AlreadyInRealm {
            cx,
            realm: NonNull::new(realm).unwrap(),
        }
    }

    pub fn cx(&mut self) -> &mut JSContext {
        self.cx
    }

    pub fn cx_no_gc(&self) -> &JSContext {
        &self.cx
    }

    pub fn in_realm(&'cx mut self) -> InRealm<'cx> {
        InRealm::Already(self)
    }

    pub fn global(&'_ self) -> Handle<'_, *mut JSObject> {
        // SAFETY: object is rooted by realm
        unsafe { Handle::from_marked_location(CurrentGlobalOrNull(self.cx_no_gc()) as _) }
    }

    pub fn realm(&self) -> &NonNull<Realm> {
        &self.realm
    }
}

pub enum InRealm<'cx> {
    Already(&'cx mut AlreadyInRealm<'cx>),
    Entered(&'cx mut AutoRealm<'cx>),
}

impl<'cx> InRealm<'cx> {
    pub fn cx(&mut self) -> &mut JSContext {
        match self {
            InRealm::Already(already_in_realm) => already_in_realm.cx(),
            InRealm::Entered(auto_realm) => auto_realm.cx(),
        }
    }

    pub fn cx_no_gc(&self) -> &JSContext {
        match self {
            InRealm::Already(already_in_realm) => already_in_realm.cx_no_gc(),
            InRealm::Entered(auto_realm) => auto_realm.cx_no_gc(),
        }
    }

    pub fn global(&'_ self) -> Handle<'_, *mut JSObject> {
        // SAFETY: object is rooted by realm
        unsafe { Handle::from_marked_location(CurrentGlobalOrNull(self.cx_no_gc()) as _) }
    }
}
