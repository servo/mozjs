use criterion::measurement::WallTime;
use criterion::{
    criterion_group, criterion_main, BenchmarkGroup, BenchmarkId, Criterion, Throughput,
};
use mozjs::context::JSContext;
use mozjs::conversions::jsstr_to_string;
use mozjs::glue::{CreateJSExternalStringCallbacks, JSExternalStringCallbacksTraps};
use mozjs::jsapi::OnNewGlobalHookOption;
use mozjs::realm::AutoRealm;
use mozjs::rooted;
use mozjs::rust::wrappers2::{JS_NewExternalStringLatin1, JS_NewGlobalObject};
use mozjs::rust::{JSEngine, RealmOptions, Runtime, SIMPLE_GLOBAL_CLASS};
use std::ffi::c_void;
use std::ptr::NonNull;
use std::{iter, ptr};

// TODO: Create a trait for creating a latin1 string of a required length, so that we can
// try different kinds of content.
fn bench_str_repetition(
    group: &mut BenchmarkGroup<WallTime>,
    context: &mut JSContext,
    variant_name: &str,
    latin1str_16_bytes: &[u8],
) {
    assert_eq!(latin1str_16_bytes.len(), 16);
    for repetitions in [1, 4, 16, 64, 256, 1024, 4096].iter() {
        let str_len = repetitions * latin1str_16_bytes.len();
        let latin1_base = iter::repeat_n(latin1str_16_bytes, *repetitions).fold(
            Vec::with_capacity(str_len),
            |mut acc, x| {
                acc.extend_from_slice(x);
                acc
            },
        );
        let latin1_boxed = latin1_base.into_boxed_slice();
        let latin1_chars = Box::into_raw(latin1_boxed).cast::<u8>();
        let callbacks = unsafe {
            CreateJSExternalStringCallbacks(
                &EXTERNAL_STRING_CALLBACKS_TRAPS,
                str_len as *mut c_void,
            )
        };
        rooted!(&in(context) let latin1_jsstr = unsafe { JS_NewExternalStringLatin1(
            context,
            latin1_chars,
            str_len,
            callbacks
        )});
        group.throughput(Throughput::Bytes(str_len as u64));
        group.bench_with_input(
            BenchmarkId::new(variant_name, str_len),
            &latin1_jsstr,
            |b, js_str| {
                b.iter(|| {
                    unsafe {
                        jsstr_to_string(context.raw_cx(), NonNull::new(js_str.get()).unwrap())
                    };
                })
            },
        );
    }
}
fn external_string(c: &mut Criterion) {
    let engine = JSEngine::init().unwrap();
    let mut runtime = Runtime::new(engine.handle());
    let context = runtime.cx();
    let h_option = OnNewGlobalHookOption::FireOnNewGlobalHook;
    let c_option = RealmOptions::default();
    rooted!(&in(context) let global = unsafe { JS_NewGlobalObject(
        context,
        &SIMPLE_GLOBAL_CLASS,
        ptr::null_mut(),
        h_option,
        &*c_option,
    )});
    let mut realm = AutoRealm::new_from_handle(context, global.handle());
    let context = realm.cx();

    let mut group = c.benchmark_group("Latin1 conversion");

    let ascii_example = b"test latin-1 tes";
    bench_str_repetition(&mut group, context, "ascii a-z", ascii_example);
    // fastpath for the first few characters, then slowpath for the remaining (long part)
    // TODO: make generator functions, so we can define at which percentage of the size
    // the first high byte shows up (which forces the slow path).
    let ascii_with_high = b"test latin-1 \xD6\xC0\xFF";
    bench_str_repetition(&mut group, context, "ascii with high", ascii_with_high);
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
