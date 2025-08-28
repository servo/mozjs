/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::ptr::{self, NonNull};
use std::sync::{LazyLock, Mutex};

use mozjs::conversions::jsstr_to_string;
use mozjs::gc::StackGCVector;
use mozjs::jsapi::{
    CompilationType, Handle, HandleString, HandleValue, JSContext, JSSecurityCallbacks, JSString,
    JS_NewGlobalObject, JS_SetSecurityCallbacks, OnNewGlobalHookOption, RuntimeCode,
};
use mozjs::jsval::{JSVal, UndefinedValue};
use mozjs::rooted;
use mozjs::rust::{Handle as SafeHandle, JSEngine, RealmOptions, Runtime, SIMPLE_GLOBAL_CLASS};

static SECURITY_CALLBACKS: JSSecurityCallbacks = JSSecurityCallbacks {
    contentSecurityPolicyAllows: Some(content_security_policy_allows),
    codeForEvalGets: None,
    subsumes: None,
};

unsafe extern "C" fn content_security_policy_allows(
    cx: *mut JSContext,
    _runtime_code: RuntimeCode,
    _code_string: HandleString,
    _compilation_type: CompilationType,
    parameter_strings: Handle<StackGCVector<*mut JSString>>,
    _body_string: HandleString,
    parameter_args: Handle<StackGCVector<JSVal>>,
    _body_arg: HandleValue,
    can_compile_strings: *mut bool,
) -> bool {
    let parameter_strings = SafeHandle::from_raw(parameter_strings);
    assert_eq!(parameter_strings.len(), 1);
    let string0 = parameter_strings.at(0).expect("should have a value");
    let string0 = NonNull::new(*string0).expect("should be non-null");
    assert_eq!(jsstr_to_string(cx, string0), "a".to_string());

    let parameter_args = SafeHandle::from_raw(parameter_args);
    assert_eq!(parameter_args.len(), 1);
    let arg0 = parameter_args.at(0).expect("should have a value");
    let string0 = arg0.to_string();
    let string0 = NonNull::new(string0).expect("should be non-null");
    assert_eq!(jsstr_to_string(cx, string0), "a".to_string());

    *RAN_CSP_CALLBACK.lock().unwrap() = true;
    *can_compile_strings = true;
    true
}

static RAN_CSP_CALLBACK: LazyLock<Mutex<bool>> = LazyLock::new(|| Mutex::new(false));

#[test]
fn csp_allow_arguments() {
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
        JS_SetSecurityCallbacks(context, &SECURITY_CALLBACKS);

        rooted!(in(context) let global = JS_NewGlobalObject(
            context,
            &SIMPLE_GLOBAL_CLASS,
            ptr::null_mut(),
            h_option,
            &*c_option,
        ));

        rooted!(in(context) let mut rval = UndefinedValue());
        let options = runtime.new_compile_options("test", 1);
        assert!(runtime
            .evaluate_script(
                global.handle(),
                "Function(\"a\", \"return a\")",
                rval.handle_mut(),
                options
            )
            .is_ok());
        assert!(rval.get().is_object());

        assert!(*RAN_CSP_CALLBACK.lock().unwrap());
    }
}
