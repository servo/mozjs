/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::ffi::c_void;
use std::ptr;

use mozjs::conversions::jsstr_to_string;
use mozjs::glue::{CreateJSExternalStringCallbacks, JSExternalStringCallbacksTraps};
use mozjs::jsapi::{
    JSAutoRealm, JS_NewExternalStringLatin1, JS_NewExternalUCString, JS_NewGlobalObject,
    OnNewGlobalHookOption,
};
use mozjs::rooted;
use mozjs::rust::{JSEngine, RealmOptions, Runtime, SIMPLE_GLOBAL_CLASS};

#[test]
fn external_string() {
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

        let latin1_base = "test latin-1";
        let latin1_boxed = latin1_base.as_bytes().to_vec().into_boxed_slice();
        let latin1_chars = Box::into_raw(latin1_boxed).cast::<u8>();

        let callbacks = CreateJSExternalStringCallbacks(
            &EXTERNAL_STRING_CALLBACKS_TRAPS,
            latin1_base.len() as *mut c_void,
        );
        rooted!(in(context) let latin1_jsstr = JS_NewExternalStringLatin1(
            context,
            latin1_chars,
            latin1_base.len(),
            callbacks
        ));
        assert_eq!(jsstr_to_string(context, latin1_jsstr.get()), latin1_base);

        let utf16_base = "test utf-16 $€ \u{10437}\u{24B62}";
        let utf16_boxed = utf16_base
            .encode_utf16()
            .collect::<Vec<_>>()
            .into_boxed_slice();
        let utf16_len = utf16_boxed.len();
        let utf16_chars = Box::into_raw(utf16_boxed).cast::<u16>();

        let callbacks = CreateJSExternalStringCallbacks(
            &EXTERNAL_STRING_CALLBACKS_TRAPS,
            utf16_len as *mut c_void,
        );
        rooted!(in(context) let utf16_jsstr = JS_NewExternalUCString(
            context,
            utf16_chars,
            utf16_len,
            callbacks
        ));
        assert_eq!(jsstr_to_string(context, utf16_jsstr.get()), utf16_base);
    }
}

static EXTERNAL_STRING_CALLBACKS_TRAPS: JSExternalStringCallbacksTraps =
    JSExternalStringCallbacksTraps {
        latin1Finalize: Some(latin1::finalize),
        latin1SizeOfBuffer: Some(latin1::size_of),
        utf16Finalize: Some(utf16::finalize),
        utf16SizeOfBuffer: Some(utf16::size_of),
    };

mod latin1 {
    use std::ffi::c_void;
    use std::slice;

    use mozjs::jsapi::mozilla::MallocSizeOf;

    pub unsafe extern "C" fn finalize(data: *const c_void, chars: *mut u8) {
        let slice = slice::from_raw_parts_mut(chars, data as usize);
        let _ = Box::from_raw(slice);
    }

    pub unsafe extern "C" fn size_of(data: *const c_void, _: *const u8, _: MallocSizeOf) -> usize {
        data as usize
    }
}

mod utf16 {
    use std::ffi::c_void;
    use std::slice;

    use mozjs::jsapi::mozilla::MallocSizeOf;

    pub unsafe extern "C" fn finalize(data: *const c_void, chars: *mut u16) {
        let slice = slice::from_raw_parts_mut(chars, data as usize);
        let _ = Box::from_raw(slice);
    }

    pub unsafe extern "C" fn size_of(data: *const c_void, _: *const u16, _: MallocSizeOf) -> usize {
        data as usize
    }
}
