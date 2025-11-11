/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::ptr::{self, NonNull};

use mozjs::context::{JSContext, RawJSContext};
use mozjs::gc::HandleValue;
use mozjs::jsapi::{ExceptionStackBehavior, OnNewGlobalHookOption, Value};
use mozjs::jsval::UndefinedValue;
use mozjs::panic::wrap_panic;
use mozjs::realm::AutoRealm;
use mozjs::rooted;
use mozjs::rust::wrappers2::{JS_DefineFunction, JS_NewGlobalObject, JS_SetPendingException};
use mozjs::rust::{evaluate_script, CompileOptionsWrapper};
use mozjs::rust::{JSEngine, RealmOptions, Runtime, SIMPLE_GLOBAL_CLASS};

#[test]
#[should_panic]
fn test_panic() {
    let engine = JSEngine::init().unwrap();
    let mut runtime = Runtime::new(engine.handle());
    let context = runtime.cx();
    #[cfg(feature = "debugmozjs")]
    unsafe {
        mozjs::jsapi::SetGCZeal(context.raw_cx(), 2, 1);
    }
    let h_option = OnNewGlobalHookOption::FireOnNewGlobalHook;
    let c_option = RealmOptions::default();

    unsafe {
        rooted!(&in(context) let global = JS_NewGlobalObject(
            context,
            &SIMPLE_GLOBAL_CLASS,
            ptr::null_mut(),
            h_option,
            &*c_option,
        ));
        let mut realm = AutoRealm::new_from_handle(context, global.handle());
        let context = realm.cx();

        let function = JS_DefineFunction(
            context,
            global.handle().into(),
            c"test".as_ptr(),
            Some(test),
            0,
            0,
        );
        assert!(!function.is_null());

        rooted!(&in(context) let mut rval = UndefinedValue());
        let options = CompileOptionsWrapper::new(&context, "test.js", 0);
        let _ = evaluate_script(
            context,
            global.handle(),
            "test();",
            rval.handle_mut(),
            options,
        );
    }
}

unsafe extern "C" fn test(cx: *mut RawJSContext, _argc: u32, _vp: *mut Value) -> bool {
    let mut cx = JSContext::from_ptr(NonNull::new(cx).unwrap());
    let mut result = false;
    wrap_panic(&mut || {
        panic!();
        #[allow(unreachable_code)]
        {
            result = true
        }
    });
    if !result {
        JS_SetPendingException(
            &mut cx,
            HandleValue::null(),
            ExceptionStackBehavior::Capture,
        );
    }
    result
}
