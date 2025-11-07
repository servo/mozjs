/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::ffi::c_void;
use std::ptr;
use std::ptr::NonNull;

#[cfg(test)]
use mozjs::context::JSContext;
use mozjs::conversions::jsstr_to_string;
use mozjs::glue::{CreateJSExternalStringCallbacks, JSExternalStringCallbacksTraps};
use mozjs::jsapi::{JSAutoRealm, OnNewGlobalHookOption};
use mozjs::rooted;
use mozjs::rust::wrappers2::{
    JS_NewExternalStringLatin1, JS_NewExternalUCString, JS_NewGlobalObject,
};
use mozjs::rust::{JSEngine, RealmOptions, Runtime, SIMPLE_GLOBAL_CLASS};

#[test]
fn external_string() {
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

        test_latin1_string(context, "test latin1");
        test_latin1_string(context, "abcdefghijklmnop"); // exactly 16 bytes
        test_latin1_string(context, "abcdefghijklmnopq"); // 17 bytes
        test_latin1_string(context, "abcdefghijklmno"); // 15 bytes
        test_latin1_string(context, "abcdefghijklmnopqrstuvwxyzabcdef"); //32 bytes
        test_latin1_string(context, "abcdefghijklmnopqrstuvwxyzabcde"); //31 bytes
        test_latin1_string(context, "abcdefghijklmnopqrstuvwxyzabcdefg"); //33 bytes
                                                                          //test_latin1_string(context, "test latin-1 Ö"); //testing whole latin1 range.
                                                                          // whole latin1 table
        test_latin1_string(context, "   ! 	\" 	# 	$ 	% 	& 	' 	( 	) 	* 	+ 	, 	- 	. 	/");
        test_latin1_string(context, "0 	1 	2 	3 	4 	5 	6 	7 	8 	9 	: 	; 	< 	= 	> 	?");
        test_latin1_string(context, "@ 	A 	B 	C 	D 	E 	F 	G 	H 	I 	J 	K 	L 	M 	N 	O");
        test_latin1_string(context, "P 	Q 	R 	S 	T 	U 	V 	W 	X 	Y 	Z 	[ 	\\ 	] 	^ 	_");
        test_latin1_string(context, "` 	a 	b 	c 	d 	e 	f 	g 	h 	i 	j 	k 	l 	m 	n 	o");
        test_latin1_string(context, "p 	q 	r 	s 	t 	u 	v 	w 	x 	y 	z 	{ 	| 	} 	~");
        test_latin1_string_bytes(
            context,
            b"\xA0\xA1\xA2\xA3\xA4\xA5\xA6\xA7\xA8\xA9\xAA\xAB\xAC\xAD\xAE\xAF",
        );
        test_latin1_string_bytes(
            context,
            b"\xB0\xB1\xB2\xB3\xB4\xB5\xB6\xB7\xB8\xB9\xBA\xBB\xBC\xBD\xBE\xBF",
        );
        test_latin1_string_bytes(
            context,
            b"\xC0\xC1\xC2\xC3\xC4\xC5\xC6\xC7\xC8\xC9\xCA\xCB\xCC\xCD\xCE\xCF",
        );
        test_latin1_string_bytes(
            context,
            b"\xD0\xD1\xD2\xD3\xD4\xD5\xD6\xD7\xD8\xD9\xDA\xDB\xDC\xDD\xDE\xDF",
        );
        test_latin1_string_bytes(
            context,
            b"\xE0\xE1\xE2\xE3\xE4\xE5\xE6\xE7\xE8\xE9\xEA\xEB\xEC\xED\xEE\xEF",
        );
        test_latin1_string_bytes(
            context,
            b"\xF0\xF1\xF2\xF3\xF4\xF5\xF6\xF7\xF8\xF9\xFA\xFB\xFC\xFD\xFE\xFF",
        );

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
        rooted!(&in(context) let utf16_jsstr = JS_NewExternalUCString(
            context,
            utf16_chars,
            utf16_len,
            callbacks
        ));
        assert_eq!(
            jsstr_to_string(context.raw_cx(), NonNull::new(utf16_jsstr.get()).unwrap()),
            utf16_base
        );
    }
}

#[cfg(test)]
unsafe fn test_latin1_string(context: &mut JSContext, latin1_base: &str) {
    let latin1_boxed = latin1_base.as_bytes().to_vec().into_boxed_slice();
    let latin1_chars = Box::into_raw(latin1_boxed).cast::<u8>();

    let callbacks = CreateJSExternalStringCallbacks(
        &EXTERNAL_STRING_CALLBACKS_TRAPS,
        latin1_base.len() as *mut c_void,
    );
    rooted!(&in(context) let latin1_jsstr = JS_NewExternalStringLatin1(
        context,
        latin1_chars,
        latin1_base.len(),
        callbacks
    ));
    assert_eq!(
        jsstr_to_string(context.raw_cx(), NonNull::new(latin1_jsstr.get()).unwrap()),
        latin1_base
    );
}

#[cfg(test)]
unsafe fn test_latin1_string_bytes(context: &mut JSContext, latin1_base: &[u8]) {
    let latin1_boxed = latin1_base.to_vec().into_boxed_slice();
    let latin1_chars = Box::into_raw(latin1_boxed).cast::<u8>();

    let callbacks = CreateJSExternalStringCallbacks(
        &EXTERNAL_STRING_CALLBACKS_TRAPS,
        latin1_base.len() as *mut c_void,
    );
    rooted!(&in(context) let latin1_jsstr = JS_NewExternalStringLatin1(
        context,
        latin1_chars,
        latin1_base.len(),
        callbacks
    ));
    assert_eq!(
        jsstr_to_string(context.raw_cx(), NonNull::new(latin1_jsstr.get()).unwrap()),
        encoding_rs::mem::decode_latin1(latin1_base)
    );
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
