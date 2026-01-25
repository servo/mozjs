/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::ptr;

use mozjs::conversions::{
    ConversionBehavior, ConversionResult, FromJSValConvertible, ToJSValConvertible,
};
use mozjs::jsapi::OnNewGlobalHookOption;
use mozjs::jsval::UndefinedValue;
use mozjs::realm::AutoRealm;
use mozjs::rooted;
use mozjs::rust::wrappers2::{InitRealmStandardClasses, JS_NewGlobalObject};
use mozjs::rust::{evaluate_script, CompileOptionsWrapper};
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
        let mut realm = AutoRealm::new_from_handle(context, global.handle());
        let context = &mut realm;
        assert!(InitRealmStandardClasses(context));

        rooted!(&in(context) let mut rval = UndefinedValue());

        let orig_vec: Vec<f32> = vec![1.0, 2.9, 3.0];
        orig_vec.safe_to_jsval(context, rval.handle_mut());
        let converted = Vec::<f32>::safe_from_jsval(context, rval.handle(), ()).unwrap();

        assert_eq!(&orig_vec, converted.get_success_value().unwrap());

        let orig_vec: Vec<i32> = vec![1, 2, 3];
        orig_vec.safe_to_jsval(context, rval.handle_mut());
        let converted =
            Vec::<i32>::safe_from_jsval(context, rval.handle(), ConversionBehavior::Default)
                .unwrap();

        assert_eq!(&orig_vec, converted.get_success_value().unwrap());

        let options = CompileOptionsWrapper::new(&context, c"test".to_owned(), 1);
        assert!(evaluate_script(
            context,
            global.handle(),
            "new Set([1, 2, 3])",
            rval.handle_mut(),
            options,
        )
        .is_ok());

        let converted =
            Vec::<i32>::safe_from_jsval(context, rval.handle(), ConversionBehavior::Default)
                .unwrap();

        assert_eq!(&orig_vec, converted.get_success_value().unwrap());

        let options = CompileOptionsWrapper::new(&context, c"test".to_owned(), 1);
        assert!(
            evaluate_script(context, global.handle(), "({})", rval.handle_mut(), options).is_ok()
        );

        let converted =
            Vec::<i32>::safe_from_jsval(context, rval.handle(), ConversionBehavior::Default);
        assert!(match converted {
            Ok(ConversionResult::Failure(_)) => true,
            _ => false,
        });
    }
}
