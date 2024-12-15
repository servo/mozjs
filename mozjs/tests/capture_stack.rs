/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::ptr;

use mozjs::capture_stack;
use mozjs::jsapi;
use mozjs::jsapi::{CallArgs, JSAutoRealm, JSContext, OnNewGlobalHookOption, StackFormat, Value};
use mozjs::jsapi::{JS_DefineFunction, JS_NewGlobalObject};
use mozjs::jsval::UndefinedValue;
use mozjs::rooted;
use mozjs::rust::{JSEngine, RealmOptions, Runtime, SIMPLE_GLOBAL_CLASS};

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
        rooted!(in(context) let mut rval = UndefinedValue());
        assert!(runtime
            .evaluate_script(global.handle(), javascript, "test.js", 0, rval.handle_mut())
            .is_ok());
    }
}

#[test]
fn iterate_stack_frames() {
    unsafe extern "C" fn assert_stack_state(
        context: *mut JSContext,
        argc: u32,
        vp: *mut Value,
    ) -> bool {
        let mut function_names = vec![];
        capture_stack!(in(context) let stack);
        stack.unwrap().for_each_stack_frame(|frame| {
            rooted!(in(context) let mut result: *mut jsapi::JSString = ptr::null_mut());

            // Get function name
            unsafe {
                jsapi::GetSavedFrameFunctionDisplayName(
                    context,
                    ptr::null_mut(),
                    frame.into(),
                    result.handle_mut().into(),
                    jsapi::SavedFrameSelfHosted::Include,
                );
            }
            let buffer = if !result.is_null() {
                let mut buffer = vec![0; 3];
                jsapi::JS_EncodeStringToBuffer(context, *result, buffer.as_mut_ptr(), 3);
                Some(buffer.into_iter().map(|c| c as u8).collect())
            } else {
                None
            };
            function_names.push(buffer);
        });

        assert_eq!(function_names.len(), 4);
        assert_eq!(function_names[0], Some(b"baz".to_vec()));
        assert_eq!(function_names[1], Some(b"bar".to_vec()));
        assert_eq!(function_names[2], Some(b"foo".to_vec()));
        assert_eq!(function_names[3], None);

        true
    }

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
            c"assert_stack_state".as_ptr(),
            Some(assert_stack_state),
            0,
            0,
        );
        assert!(!function.is_null());

        let javascript = "
            function foo() {
                function bar() {
                    function baz() {
                        assert_stack_state();
                    }
                    baz();
                }
                bar();
            }
            foo();
        ";
        rooted!(in(context) let mut rval = UndefinedValue());
        assert!(runtime
            .evaluate_script(global.handle(), javascript, "test.js", 0, rval.handle_mut())
            .is_ok());
    }
}
