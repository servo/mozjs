/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#![cfg(feature = "debugmozjs")]

use std::ptr;

use mozjs::context::JSContext;
use mozjs::gc::{HandleObject, HandleValue};
use mozjs::jsapi::SetGCZeal;
use mozjs::jsapi::{GCReason, OnNewGlobalHookOption};
use mozjs::jsval::ObjectValue;
use mozjs::realm::AutoRealm;
use mozjs::rooted;
use mozjs::rust::wrappers2::{JS_NewGlobalObject, JS_NewPlainObject, JS_GC};
use mozjs::rust::{JSEngine, RealmOptions, Runtime, SIMPLE_GLOBAL_CLASS};

#[test]
fn handle_gc() {
    let engine = JSEngine::init().unwrap();
    let mut runtime = Runtime::new(engine.handle());
    let context = runtime.cx();
    let h_option = OnNewGlobalHookOption::FireOnNewGlobalHook;
    let c_option = RealmOptions::default();

    unsafe {
        SetGCZeal(context.raw_cx(), 2, 1);
        rooted!(&in(context) let global = JS_NewGlobalObject(
            context,
            &SIMPLE_GLOBAL_CLASS,
            ptr::null_mut(),
            h_option,
            &*c_option,
        ));
        let mut realm = AutoRealm::new_from_handle(context, global.handle());
        let context = &mut realm;

        rooted!(&in(context) let object = JS_NewPlainObject(context));
        rooted!(&in(context) let value = ObjectValue(object.get()));
        compare(context, object.handle(), value.handle());
    }
}

#[inline(never)]
fn compare(context: &mut JSContext, object: HandleObject<'_>, value: HandleValue<'_>) {
    let ptr = object.get();
    assert_eq!(ptr, value.get().to_object());
    unsafe { JS_GC(context, GCReason::API) };
    assert_eq!(object.get(), value.get().to_object());
    assert_ne!(ptr, object.get());
}
