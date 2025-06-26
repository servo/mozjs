use criterion::{criterion_group, criterion_main, Criterion};
use std::ffi::c_void;
use std::{iter, ptr};

use mozjs::conversions::jsstr_to_string;
use mozjs::glue::{CreateJSExternalStringCallbacks, JSExternalStringCallbacksTraps};
use mozjs::jsapi::{
    JSAutoRealm, JS_NewExternalStringLatin1, JS_NewGlobalObject, OnNewGlobalHookOption,
};
use mozjs::rooted;
use mozjs::rust::{JSEngine, RealmOptions, Runtime, SIMPLE_GLOBAL_CLASS};

fn external_string(c: &mut Criterion) {
    unsafe {
        let engine = JSEngine::init().unwrap();
        let runtime = Runtime::new(engine.handle());
        let context = runtime.cx();
        let h_option = OnNewGlobalHookOption::FireOnNewGlobalHook;
        let c_option = RealmOptions::default();
        rooted!(in(context) let global = JS_NewGlobalObject(
            context,
            &SIMPLE_GLOBAL_CLASS,
            ptr::null_mut(),
            h_option,
            &*c_option,
        ));
        let _ac = JSAutoRealm::new(context, global.get());

        let latin1_base =
            iter::repeat_n("test latin-1 test", 1_000_000).fold(String::new(), |mut acc, x| {
                acc.push_str(x);
                acc
            });

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
        c.bench_function("external_string_latin1", |b| {
            b.iter(|| {
                jsstr_to_string(context, latin1_jsstr.get());
            })
        });
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

criterion_group!(benches, external_string);
criterion_main!(benches);
