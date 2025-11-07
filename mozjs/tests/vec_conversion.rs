/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::ptr;

use mozjs::conversions::{
    ConversionBehavior, ConversionResult, FromJSValConvertible, ToJSValConvertible,
};
use mozjs::jsapi::{JSAutoRealm, OnNewGlobalHookOption};
use mozjs::jsval::UndefinedValue;
use mozjs::rooted;
use mozjs::rust::wrappers2::{InitRealmStandardClasses, JS_NewGlobalObject};
use mozjs::rust::{JSEngine, RealmOptions, Runtime, SIMPLE_GLOBAL_CLASS};

#[test]
fn vec_conversion() {
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
        let _ac = JSAutoRealm::new(context.raw_cx(), global.get());
        assert!(InitRealmStandardClasses(context));

        rooted!(&in(context) let mut rval = UndefinedValue());

        let orig_vec: Vec<f32> = vec![1.0, 2.9, 3.0];
        orig_vec.to_jsval(context.raw_cx(), rval.handle_mut());
        let converted = Vec::<f32>::from_jsval(context.raw_cx(), rval.handle(), ()).unwrap();

        assert_eq!(&orig_vec, converted.get_success_value().unwrap());

        let orig_vec: Vec<i32> = vec![1, 2, 3];
        orig_vec.to_jsval(context.raw_cx(), rval.handle_mut());
        let converted =
            Vec::<i32>::from_jsval(context.raw_cx(), rval.handle(), ConversionBehavior::Default)
                .unwrap();

        assert_eq!(&orig_vec, converted.get_success_value().unwrap());

        let options = runtime.new_compile_options("test", 1);
        assert!(runtime
            .evaluate_script(
                global.handle(),
                "new Set([1, 2, 3])",
                rval.handle_mut(),
                options,
            )
            .is_ok());
        let context = runtime.cx();
        let converted =
            Vec::<i32>::from_jsval(context.raw_cx(), rval.handle(), ConversionBehavior::Default)
                .unwrap();

        assert_eq!(&orig_vec, converted.get_success_value().unwrap());

        let options = runtime.new_compile_options("test", 1);
        assert!(runtime
            .evaluate_script(global.handle(), "({})", rval.handle_mut(), options)
            .is_ok());
        let context = runtime.cx();
        let converted =
            Vec::<i32>::from_jsval(context.raw_cx(), rval.handle(), ConversionBehavior::Default);
        assert!(match converted {
            Ok(ConversionResult::Failure(_)) => true,
            _ => false,
        });
    }
}
