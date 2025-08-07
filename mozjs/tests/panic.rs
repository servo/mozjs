/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::ptr;

use mozjs::gc::HandleValue;
use mozjs::jsapi::{ExceptionStackBehavior, JSAutoRealm, JSContext, OnNewGlobalHookOption, Value};
use mozjs::jsapi::{JS_DefineFunction, JS_NewGlobalObject};
use mozjs::jsval::UndefinedValue;
use mozjs::panic::wrap_panic;
use mozjs::rooted;
use mozjs::rust::wrappers::JS_SetPendingException;
use mozjs::rust::{JSEngine, RealmOptions, Runtime, SIMPLE_GLOBAL_CLASS};

#[test]
#[should_panic]
fn test_panic() {
    let engine = JSEngine::init().unwrap();
    let runtime = Runtime::new(engine.handle());
    let context = runtime.cx();
    #[cfg(feature = "debugmozjs")]
    unsafe {
        mozjs::jsapi::SetGCZeal(context, 2, 1);
    }
    let h_option = OnNewGlobalHookOption::FireOnNewGlobalHook;
    let c_option = RealmOptions::default();

    unsafe {
        rooted!(in(context) let global = JS_NewGlobalObject(
            context,
            &SIMPLE_GLOBAL_CLASS,
            ptr::null_mut(),
            h_option,
            &*c_option,
        ));
        let _ac = JSAutoRealm::new(context, global.get());

        let function = JS_DefineFunction(
            context,
            global.handle().into(),
            c"test".as_ptr(),
            Some(test),
            0,
            0,
        );
        assert!(!function.is_null());

        rooted!(in(context) let mut rval = UndefinedValue());
        let options = runtime.new_compile_options("test.js", 0);
        let _ = runtime.evaluate_script(global.handle(), "test();", rval.handle_mut(), options);
    }
}

unsafe extern "C" fn test(cx: *mut JSContext, _argc: u32, _vp: *mut Value) -> bool {
    let mut result = false;
    wrap_panic(&mut || {
        panic!();
        #[allow(unreachable_code)]
        {
            result = true
        }
    });
    if !result {
        JS_SetPendingException(cx, HandleValue::null(), ExceptionStackBehavior::Capture);
    }
    result
}
