/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::ptr;

use mozjs::glue::InitializeMemoryReporter;
use mozjs::jsapi::{JSObject, OnNewGlobalHookOption};
use mozjs::jsval::UndefinedValue;
use mozjs::rooted;
use mozjs::rust::wrappers2::{CollectServoSizes, JS_NewGlobalObject};
use mozjs::rust::{
    evaluate_script, CompileOptionsWrapper, JSEngine, RealmOptions, Runtime, SIMPLE_GLOBAL_CLASS,
};

unsafe extern "C" fn measures_nothing(_obj: *mut JSObject) -> bool {
    false
}

/// The heap walk measures the null pointer, which Windows `_msize` answers by ending
/// the process rather than by returning: a failure here is an abort, not an assertion.
#[test]
fn collect_servo_sizes() {
    unsafe { InitializeMemoryReporter(Some(measures_nothing)) };

    let engine = JSEngine::init().unwrap();
    let mut runtime = Runtime::new(engine.handle());
    let context = runtime.cx();
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
            "Array.from({ length: 100 }, (_, i) => ({ i, s: 'x'.repeat(i) })).length",
            rval.handle_mut(),
            options
        )
        .is_ok());

        let mut sizes = std::mem::zeroed();
        assert!(CollectServoSizes(context, &mut sizes, None));
        assert!(sizes.gcHeapUsed > 0, "measured an empty GC heap");
    }
}
