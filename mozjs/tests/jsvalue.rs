/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::ptr;

use mozjs::jsapi::OnNewGlobalHookOption;
use mozjs::jsval::{BooleanValue, DoubleValue, Int32Value, NullValue, UndefinedValue};
use mozjs::rooted;
use mozjs::rust::wrappers2::JS_NewGlobalObject;
use mozjs::rust::{evaluate_script, CompileOptionsWrapper};
use mozjs::rust::{
    HandleObject, JSEngine, RealmOptions, RootedGuard, Runtime, SIMPLE_GLOBAL_CLASS,
};
use mozjs_sys::jsval::JSVal;

unsafe fn tester<F: Fn(RootedGuard<JSVal>)>(
    rt: &mut Runtime,
    global: HandleObject,
    // js to be executed that needs to return jsval
    js: &str,
    // rust constructed jsval
    rust: JSVal,
    test: F,
) {
    let cx = rt.cx();
    rooted!(&in(cx) let mut rval = UndefinedValue());

    let options = CompileOptionsWrapper::new(&cx, c"test".to_owned(), 1);
    assert!(evaluate_script(cx, global, js, rval.handle_mut(), options).is_ok());
    test(rval);

    rooted!(&in(cx) let mut val = rust);
    test(val);
}

#[test]
fn jsvalues() {
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

        tester(
            &mut runtime,
            global.handle(),
            "undefined",
            UndefinedValue(),
            |val| {
                assert!(val.is_undefined());
                assert!(val.is_primitive());
            },
        );

        tester(&mut runtime, global.handle(), "null", NullValue(), |val| {
            assert!(val.is_null());
            assert!(val.is_null_or_undefined());
            assert!(val.is_object_or_null());

            assert!(val.to_object_or_null().is_null())
        });

        tester(
            &mut runtime,
            global.handle(),
            "true",
            BooleanValue(true),
            |val| {
                assert!(val.is_boolean());
                assert!(val.is_primitive());

                assert!(val.to_boolean());
            },
        );

        tester(
            &mut runtime,
            global.handle(),
            "false",
            BooleanValue(false),
            |val| {
                assert!(val.is_boolean());
                assert!(val.is_primitive());

                assert!(!val.to_boolean());
            },
        );

        tester(&mut runtime, global.handle(), "42", Int32Value(42), |val| {
            assert!(val.is_int32());
            assert!(val.is_primitive());
            assert!(val.is_number());

            assert_eq!(val.to_int32(), 42);
            assert_eq!(val.to_number(), 42.0);
        });

        tester(
            &mut runtime,
            global.handle(),
            "-42",
            Int32Value(-42),
            |val| {
                assert!(val.is_int32());
                assert!(val.is_primitive());
                assert!(val.is_number());

                assert_eq!(val.to_int32(), -42);
                assert_eq!(val.to_number(), -42.0);
            },
        );

        tester(
            &mut runtime,
            global.handle(),
            "42.5",
            DoubleValue(42.5),
            |val| {
                assert!(val.is_double());
                assert!(val.is_primitive());
                assert!(val.is_number());

                assert_eq!(val.to_double(), 42.5);
                assert_eq!(val.to_number(), 42.5);
            },
        );

        tester(
            &mut runtime,
            global.handle(),
            "-42.5",
            DoubleValue(-42.5),
            |val| {
                assert!(val.is_double());
                assert!(val.is_primitive());
                assert!(val.is_number());

                assert_eq!(val.to_double(), -42.5);
                assert_eq!(val.to_number(), -42.5);
            },
        );
    }
}
