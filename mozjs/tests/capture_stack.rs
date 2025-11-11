/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::ptr;

use mozjs::capture_stack;
use mozjs::jsapi::{CallArgs, JSContext, OnNewGlobalHookOption, StackFormat, Value};
use mozjs::jsval::UndefinedValue;
use mozjs::realm::AutoRealm;
use mozjs::rooted;
use mozjs::rust::wrappers2::{JS_DefineFunction, JS_NewGlobalObject};
use mozjs::rust::{
    evaluate_script, CompileOptionsWrapper, JSEngine, RealmOptions, Runtime, SIMPLE_GLOBAL_CLASS,
};

#[test]
fn capture_stack() {
    unsafe extern "C" fn print_stack(context: *mut JSContext, argc: u32, vp: *mut Value) -> bool {
        let args = CallArgs::from_vp(vp, argc);

        capture_stack!(in(context) let stack);
        let str_stack = stack
            .unwrap()
            .as_string(None, StackFormat::SpiderMonkey)
            .unwrap();
        println!("{}", str_stack);
        assert_eq!(
            "bar@test.js:3:21\nfoo@test.js:5:17\n@test.js:8:16\n".to_string(),
            str_stack
        );

        args.rval().set(UndefinedValue());
        true
    }

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
            c"print_stack".as_ptr(),
            Some(print_stack),
            0,
            0,
        );
        assert!(!function.is_null());

        let javascript = "
            function foo(arg1) {
                var bar = function() {
                    print_stack();
                };
                bar();
            }

            foo(\"arg1-value\");
        ";
        rooted!(&in(context) let mut rval = UndefinedValue());
        let options = CompileOptionsWrapper::new(&context, "test.js", 0);
        assert!(evaluate_script(
            context,
            global.handle(),
            javascript,
            rval.handle_mut(),
            options
        )
        .is_ok());
    }
}
