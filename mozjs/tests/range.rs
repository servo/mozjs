use std::ptr;

use mozjs::jsapi::mozilla::Range;
use mozjs::jsapi::{BigIntIsUint64, JS_NewGlobalObject, StringToBigInt, StringToBigInt1};
use mozjs::jsapi::{JSAutoRealm, OnNewGlobalHookOption};
use mozjs::rooted;
use mozjs::rust::{JSEngine, RealmOptions, Runtime, SIMPLE_GLOBAL_CLASS};

#[test]
fn range() {
    let engine = JSEngine::init().unwrap();
    let runtime = Runtime::new(engine.handle());
    let context = runtime.cx();
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

        // Number.MAX_SAFE_INTEGER + 10
        let int = 9007199254741001;
        let mut string = int.to_string();
        let range = string.as_bytes_mut().as_mut_ptr_range();
        let chars = Range::new(range.start, range.end);
        rooted!(in(context) let bigint = StringToBigInt(context, chars));
        assert!(!bigint.get().is_null());

        let mut result = 0;
        assert!(BigIntIsUint64(bigint.get(), &mut result));
        assert_eq!(result, int);

        let mut chars: Vec<_> = string.encode_utf16().collect();
        let range = chars.as_mut_ptr_range();
        let chars = Range::new(range.start, range.end);
        rooted!(in(context) let bigint = StringToBigInt1(context, chars));
        assert!(!bigint.get().is_null());

        let mut result = 0;
        assert!(BigIntIsUint64(bigint.get(), &mut result));
        assert_eq!(result, int);
    }
}
