/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::ptr;

use mozjs::jsapi::OnNewGlobalHookOption;
use mozjs::jsval::UndefinedValue;
use mozjs::rooted;
use mozjs::rust::wrappers2::JS_NewGlobalObject;
use mozjs::rust::{evaluate_script, CompileOptionsWrapper};
use mozjs::rust::{JSEngine, RealmOptions, Runtime, SIMPLE_GLOBAL_CLASS};

#[test]
fn evaluate() {
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
        let options = CompileOptionsWrapper::new(&context, c"test".to_owned(), 1);
        assert!(evaluate_script(
            context,
            global.handle(),
            "1 + 1",
            rval.handle_mut(),
            options
        )
        .is_ok());
        assert_eq!(rval.get().to_int32(), 2);
    }
}
