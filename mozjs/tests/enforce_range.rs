/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::ptr;

use mozjs::conversions::{ConversionBehavior, ConversionResult, FromJSValConvertible};
use mozjs::jsapi::{Heap, JSObject, OnNewGlobalHookOption};
use mozjs::jsval::UndefinedValue;
use mozjs::realm::AutoRealm;
use mozjs::rooted;
use mozjs::rust::wrappers2::{JS_ClearPendingException, JS_IsExceptionPending, JS_NewGlobalObject};
use mozjs::rust::{evaluate_script, CompileOptionsWrapper};
use mozjs::rust::{HandleObject, JSEngine, RealmOptions, Runtime, SIMPLE_GLOBAL_CLASS};

struct SM {
    _realm: AutoRealm<'static>,
    global: Box<Heap<*mut JSObject>>,
    rt: Runtime,
    _engine: JSEngine,
}

impl SM {
    fn new() -> Self {
        let engine = JSEngine::init().unwrap();
        let mut rt = Runtime::new(engine.handle());
        let cx = rt.cx();
        #[cfg(feature = "debugmozjs")]
        unsafe {
            mozjs::jsapi::SetGCZeal(cx.raw_cx(), 2, 1);
        }
        let h_option = OnNewGlobalHookOption::FireOnNewGlobalHook;
        let c_option = RealmOptions::default();
        rooted!(&in(cx) let global = unsafe {JS_NewGlobalObject(
            cx,
            &SIMPLE_GLOBAL_CLASS,
            ptr::null_mut(),
            h_option,
            &*c_option,
        )});
        let realm = AutoRealm::new_from_handle(cx, global.handle());
        Self {
            _engine: engine,
            // SAFETY: This is safe because the lifetime of the realm is tied to the Runtime.
            _realm: unsafe { realm.erase_lifetime() },
            rt,
            global: Heap::boxed(global.get()),
        }
    }

    /// Returns value or (Type)Error
    fn obtain<T: FromJSValConvertible<Config = ConversionBehavior>>(
        &mut self,
        js: &str,
    ) -> Result<T, ()> {
        let cx = self.rt.cx();
        rooted!(&in(cx) let mut rval = UndefinedValue());
        unsafe {
            let options = CompileOptionsWrapper::new(&cx, c"test".to_owned(), 1);
            evaluate_script(
                cx,
                HandleObject::from_raw(self.global.handle()),
                js,
                rval.handle_mut(),
                options,
            )
            .unwrap();
            assert!(!JS_IsExceptionPending(cx));
            match <T as FromJSValConvertible>::safe_from_jsval(
                cx,
                rval.handle(),
                ConversionBehavior::EnforceRange,
            ) {
                Ok(ConversionResult::Success(t)) => Ok(t),
                Ok(ConversionResult::Failure(e)) => panic!("{}", e.to_string_lossy()),
                Err(()) => {
                    assert!(JS_IsExceptionPending(cx));
                    JS_ClearPendingException(cx);
                    Err(())
                }
            }
        }
    }
}

#[test]
fn conversion() {
    let mut sm = SM::new();

    // u64 = unsigned long long
    // use `AbortSignal.timeout(u64)` to test for TypeError in browser
    assert!(sm.obtain::<u64>("Number.MIN_VALUE").is_ok());
    assert!(sm.obtain::<u64>("Number.MIN_SAFE_INTEGER").is_err());
    assert!(sm.obtain::<u64>("-1").is_err());
    assert_eq!(sm.obtain::<u64>("-0.9999"), Ok(0));
    assert_eq!(sm.obtain::<u64>("-0.9"), Ok(0));
    assert_eq!(sm.obtain::<u64>("-0.6"), Ok(0));
    assert_eq!(sm.obtain::<u64>("-0.5"), Ok(0));
    assert_eq!(sm.obtain::<u64>("-0.4"), Ok(0));
    assert_eq!(sm.obtain::<u64>("-0.1"), Ok(0));
    assert_eq!(sm.obtain::<u64>("0"), Ok(0));

    assert_eq!(
        sm.obtain::<u64>("Number.MAX_SAFE_INTEGER-1"),
        Ok((1 << 53) - 2)
    );
    assert_eq!(
        sm.obtain::<u64>("Number.MAX_SAFE_INTEGER"),
        Ok((1 << 53) - 1)
    );
    assert!(sm.obtain::<u64>("Number.MAX_SAFE_INTEGER+0.4").is_ok());
    assert!(sm.obtain::<u64>("Number.MAX_SAFE_INTEGER+0.5").is_err());
    assert!(sm.obtain::<u64>("Number.MAX_SAFE_INTEGER+1").is_err());
    assert!(sm.obtain::<u64>("Number.MAX_VALUE").is_err());
}
