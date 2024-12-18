/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#![cfg(feature = "debugmozjs")]

use std::ptr;

use mozjs::jsapi::{GetRealmObjectPrototype, JS_NewGlobalObject, SetGCZeal};
use mozjs::jsapi::{JSAutoRealm, JSTracer, OnNewGlobalHookOption, Value};
use mozjs::jsval::ObjectValue;
use mozjs::rooted;
use mozjs::rust::{JSEngine, RealmOptions, Runtime, SIMPLE_GLOBAL_CLASS};

impl mozjs::gc::Rootable for ContainsGCValue {}
unsafe impl mozjs::gc::Traceable for ContainsGCValue {
    unsafe fn trace(&self, trc: *mut JSTracer) {
        self.val.trace(trc);
    }
}

impl mozjs::gc::Initialize for ContainsGCValue {
    unsafe fn initial() -> Option<ContainsGCValue> {
        None
    }
}

struct ContainsGCValue {
    val: Value,
}

#[test]
fn rooting() {
    let engine = JSEngine::init().unwrap();
    let runtime = Runtime::new(engine.handle());
    let context = runtime.cx();
    let h_option = OnNewGlobalHookOption::FireOnNewGlobalHook;
    let c_option = RealmOptions::default();

    unsafe {
        SetGCZeal(context, 2, 1);
        rooted!(in(context) let global = JS_NewGlobalObject(
            context,
            &SIMPLE_GLOBAL_CLASS,
            ptr::null_mut(),
            h_option,
            &*c_option,
        ));
        let _ac = JSAutoRealm::new(context, global.get());

        rooted!(in(context) let prototype_proto = GetRealmObjectPrototype(context));
        rooted!(in(context) let some_container = ContainsGCValue {
            val: ObjectValue(prototype_proto.get())
        });
        rooted!(in(context) let some_optional_container = Some(ContainsGCValue {
            val: ObjectValue(prototype_proto.get())
        }));
        assert_eq!(some_container.val.to_object(), prototype_proto.get());
        assert_eq!(
            some_container.val,
            some_optional_container.as_ref().unwrap().val
        );
    }
}
