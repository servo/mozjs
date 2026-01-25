/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::ffi::CStr;
use std::ptr;
use std::ptr::NonNull;
use std::str;

use mozjs::context::{JSContext, RawJSContext};
use mozjs::jsapi::{CallArgs, JS_ReportErrorASCII, OnNewGlobalHookOption, Value};
use mozjs::jsval::UndefinedValue;
use mozjs::realm::AutoRealm;
use mozjs::rooted;
use mozjs::rust::wrappers2::{EncodeStringToUTF8, JS_DefineFunction, JS_NewGlobalObject};
use mozjs::rust::{
    evaluate_script, CompileOptionsWrapper, JSEngine, RealmOptions, Runtime, SIMPLE_GLOBAL_CLASS,
};

#[test]
fn callback() {
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
        let (global_handle, realm) = realm.global_and_reborrow();
        let context = realm;

        let function =
            JS_DefineFunction(context, global_handle, c"puts".as_ptr(), Some(puts), 1, 0);
        assert!(!function.is_null());

        let javascript = "puts('Test Iñtërnâtiônàlizætiøn ┬─┬ノ( º _ ºノ) ');";
        rooted!(&in(context) let mut rval = UndefinedValue());
        let options = CompileOptionsWrapper::new(&context, c"test.js".to_owned(), 0);
        assert!(evaluate_script(
            context,
            global_handle,
            javascript,
            rval.handle_mut(),
            options
        )
        .is_ok());
    }
}

unsafe extern "C" fn puts(context: *mut RawJSContext, argc: u32, vp: *mut Value) -> bool {
    // SAFETY: This is safe because we are in callback (so this is only access to context)
    // and we shadow the ptr, so it cannot be used anymore
    let mut context = JSContext::from_ptr(NonNull::new(context).unwrap());
    let args = CallArgs::from_vp(vp, argc);

    if args.argc_ != 1 {
        JS_ReportErrorASCII(
            context.raw_cx(),
            c"puts() requires exactly 1 argument".as_ptr(),
        );
        return false;
    }

    let arg = mozjs::rust::Handle::from_raw(args.get(0));
    let js = mozjs::rust::ToString(context.raw_cx(), arg);
    rooted!(&in(context) let message_root = js);
    unsafe extern "C" fn cb(message: *const core::ffi::c_char) {
        let message = CStr::from_ptr(message);
        let message = str::from_utf8(message.to_bytes()).unwrap();
        assert_eq!(message, "Test Iñtërnâtiônàlizætiøn ┬─┬ノ( º _ ºノ) ");
        println!("{}", message);
    }
    EncodeStringToUTF8(&mut context, message_root.handle().into(), cb);

    args.rval().set(UndefinedValue());
    true
}
