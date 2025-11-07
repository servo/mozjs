/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::ptr;

use mozjs::jsapi::{OnNewGlobalHookOption, JSITER_OWNONLY};
use mozjs::jsval::UndefinedValue;
use mozjs::rooted;
use mozjs::rust::wrappers2::{GetPropertyKeys, JS_NewGlobalObject, JS_StringEqualsAscii};
use mozjs::rust::{IdVector, JSEngine, RealmOptions, Runtime, SIMPLE_GLOBAL_CLASS};

#[test]
fn enumerate() {
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

        rooted!(&in(context) let mut rval = UndefinedValue());
        let options = runtime.new_compile_options("test", 1);
        assert!(runtime
            .evaluate_script(global.handle(), "({ 'a': 7 })", rval.handle_mut(), options,)
            .is_ok());
        let context = runtime.cx();
        assert!(rval.is_object());

        rooted!(&in(context) let object = rval.to_object());
        let mut ids = IdVector::new(context.raw_cx());
        assert!(GetPropertyKeys(
            context,
            object.handle().into(),
            JSITER_OWNONLY,
            ids.handle_mut(),
        ));

        assert_eq!(ids.len(), 1);
        rooted!(&in(context) let id = ids[0]);

        assert!(id.is_string());
        rooted!(&in(context) let id = id.to_string());

        let mut matches = false;
        assert!(JS_StringEqualsAscii(
            context,
            id.get(),
            c"a".as_ptr(),
            &mut matches
        ));
        assert!(matches);
    }
}
