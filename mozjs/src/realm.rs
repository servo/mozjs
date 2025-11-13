use std::marker::PhantomData;
use std::ops::{Deref, DerefMut};
use std::ptr::NonNull;

use crate::jsapi::JS::Realm;
use crate::jsapi::{JSAutoRealm, JSObject};

use crate::context::JSContext;
use crate::gc::Handle;
use crate::rust::wrappers2::{GetCurrentRealmOrNull, CurrentGlobal};

/// Safe wrapper around [JSAutoRealm].
///
/// On creation it enters the realm of the target object,
/// realm becomes current (it's on top of the realm stack).
/// Drop exits realm.
///
/// While creating [AutoRealm] will not trigger GC,
/// it still takes `&mut JSContext`, because it can be used in place of [JSContext] (by [Deref]/[DerefMut]).
/// with additional information of entered/current realm:
/// ```compile_fail
/// use mozjs::context::JSContext;
/// use mozjs::jsapi::JSObject;
/// use mozjs::realm::AutoRealm;
/// use std::ptr::NonNull;
///
/// fn f(cx: &mut JSContext, target: NonNull<JSObject>) {
///     let realm = AutoRealm::new(cx, target);
///     f(cx, target); // one cannot use JSContext here,
///                   // because that could allow out of order realm drops.
/// }
/// ```
/// instead do this:
/// ```
/// use mozjs::context::JSContext;
/// use mozjs::jsapi::JSObject;
/// use mozjs::realm::AutoRealm;
/// use std::ptr::NonNull;
///
/// fn f(cx: &mut JSContext, target: NonNull<JSObject>) {
///     let mut realm = AutoRealm::new(cx, target);
///     let cx = &mut realm; // this JSContext is bounded to AutoRealm
///                              // which in turn is bounded to original JSContext
///     f(cx, target);
/// }
/// ```
///
/// This also enforces LIFO entering/exiting realms, which is not enforced by [JSAutoRealm]:
/// ```compile_fail
/// use mozjs::context::JSContext;
/// use mozjs::jsapi::JSObject;
/// use mozjs::realm::AutoRealm;
/// use std::ptr::NonNull;
///
/// fn f(cx: &mut JSContext, t1: NonNull<JSObject>, t2: NonNull<JSObject>) {
///     let mut realm1 = AutoRealm::new(cx, t1);
///     let cx = &mut realm1;
///     let realm2 = AutoRealm::new(cx, t2);
///     drop(realm1); // it's not possible to drop realm1 before realm2
/// }
/// ```
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
        CurrentRealm::assert(self)
    }

    /// Obtain the handle to the global object of the this realm.
    /// Because the handle is bounded with lifetime to realm, you cannot do this:
    ///
    /// ```compile_fail
    /// use mozjs::context::JSContext;
    /// use mozjs::jsapi::JSObject;
    /// use mozjs::realm::AutoRealm;
    /// use std::ptr::NonNull;
    /// use mozjs::rust::Handle;
    ///
    /// fn g(realm: &'_ mut AutoRealm, global: Handle<'_, *mut JSObject>) {
    /// }
    ///
    /// fn f(realm: &mut AutoRealm) {
    ///     let global = realm.global();
    ///     g(realm, global);
    /// }
    /// ```
    ///
    /// instead use [AutoRealm::global_and_reborrow].
    pub fn global(&'_ self) -> Handle<'_, *mut JSObject> {
        // SAFETY: object is rooted by realm
        unsafe { Handle::from_marked_location(CurrentGlobal(self)) }
    }

    /// Obtain the handle to the global object of the this realm and reborrow the realm.
    ///
    /// ```
    /// use mozjs::context::JSContext;
    /// use mozjs::jsapi::JSObject;
    /// use mozjs::realm::AutoRealm;
    /// use std::ptr::NonNull;
    /// use mozjs::rust::Handle;
    ///
    /// fn g(realm: &'_ mut AutoRealm, global: Handle<'_, *mut JSObject>) {
    /// }
    ///
    /// fn f(realm: &mut AutoRealm) {
    ///     let (global, realm) = realm.global_and_reborrow();
    ///     g(realm, global);
    /// }
    /// ```
    pub fn global_and_reborrow(&'_ mut self) -> (Handle<'_, *mut JSObject>, &'_ mut Self) {
        // SAFETY: This is ok because we bound handle will still be bounded to original lifetime
        (unsafe { std::mem::transmute(self.global()) }, self)
    }

    /// Erase the lifetime of this [AutoRealm].
    ///
    /// # Safety
    /// - The caller must ensure that the [AutoRealm] does not outlive the [JSContext] it was created with.
    pub unsafe fn erase_lifetime(self) -> AutoRealm<'static> {
        std::mem::transmute(self)
    }

    pub fn realm(&self) -> &JSAutoRealm {
        &self.realm
    }
}

impl<'cx> Deref for AutoRealm<'cx> {
    type Target = JSContext;

    fn deref(&'_ self) -> &'_ Self::Target {
        &self.cx
    }
}

impl<'cx> DerefMut for AutoRealm<'cx> {
    fn deref_mut(&'_ mut self) -> &'_ mut Self::Target {
        &mut self.cx
    }
}

impl<'cx> Drop for AutoRealm<'cx> {
    // if we do not implement this rust  can shorten lifetime of cx,
    // without effecting JSAutoRealm (realm drops after we lose lifetime of cx)
    fn drop(&mut self) {}
}

/// Represents the current realm of [JSContext] (top realm on realm stack).
///
/// Similarly to [AutoRealm], while you can access this type via `&mut`/`&mut`
/// we know that this realm is current (on top of realm stack).
///
/// ```compile_fail
/// use mozjs::context::JSContext;
/// use mozjs::jsapi::JSObject;
/// use mozjs::realm::{AutoRealm, CurrentRealm};
/// use std::ptr::NonNull;
///
/// fn f(current_realm: &mut CurrentRealm, target: NonNull<JSObject>) {
///     let mut realm = AutoRealm::new(current_realm, target);
///     let cx: &mut JSContext = &mut *current_realm; // we cannot use current realm while it's not current
/// }
/// ```
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

    /// Obtain the handle to the global object of the this realm.
    /// Because the handle is bounded with lifetime to realm, you cannot do this:
    ///
    /// ```compile_fail
    /// use mozjs::context::JSContext;
    /// use mozjs::jsapi::JSObject;
    /// use mozjs::realm::CurrentRealm;
    /// use std::ptr::NonNull;
    /// use mozjs::rust::Handle;
    ///
    /// fn g(realm: &'_ mut CurrentRealm, global: Handle<'_, *mut JSObject>) {
    /// }
    ///
    /// fn f(realm: &mut CurrentRealm) {
    ///     let global = realm.global();
    ///     g(realm, global);
    /// }
    /// ```
    ///
    /// instead use [CurrentRealm::global_and_reborrow].
    pub fn global(&'_ self) -> Handle<'_, *mut JSObject> {
        // SAFETY: object is rooted by realm
        unsafe { Handle::from_marked_location(CurrentGlobal(self)) }
    }

    /// Obtain the handle to the global object of the this realm and reborrow the realm.
    ///
    /// ```
    /// use mozjs::context::JSContext;
    /// use mozjs::jsapi::JSObject;
    /// use mozjs::realm::CurrentRealm;
    /// use std::ptr::NonNull;
    /// use mozjs::rust::Handle;
    ///
    /// fn g(realm: &'_ mut CurrentRealm, global: Handle<'_, *mut JSObject>) {
    /// }
    ///
    /// fn f(realm: &mut CurrentRealm) {
    ///     let (global, realm) = realm.global_and_reborrow();
    ///     g(realm, global);
    /// }
    /// ```
    pub fn global_and_reborrow(&'_ mut self) -> (Handle<'_, *mut JSObject>, &'_ mut Self) {
        // SAFETY: This is ok because we bound handle will still be bounded to original lifetime
        (unsafe { std::mem::transmute(self.global()) }, self)
    }

    pub fn realm(&self) -> &NonNull<Realm> {
        &self.realm
    }
}

impl<'cx> Deref for CurrentRealm<'cx> {
    type Target = JSContext;

    fn deref(&'_ self) -> &'_ Self::Target {
        &self.cx
    }
}

impl<'cx> DerefMut for CurrentRealm<'cx> {
    fn deref_mut(&'_ mut self) -> &'_ mut Self::Target {
        &mut self.cx
    }
}
