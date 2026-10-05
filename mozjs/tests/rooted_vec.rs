/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#![cfg(feature = "debugmozjs")]

use std::ptr;

use mozjs::jsapi::HandleValueArray;
use mozjs::jsapi::OnNewGlobalHookOption;
use mozjs::jsapi::SetGCZeal;
use mozjs::jsval::{JSVal, ObjectValue};
use mozjs::realm::AutoRealm;
use mozjs::rooted;
use mozjs::rust::wrappers2::{
    GetArrayLength, JS_GetElement, JS_NewGlobalObject, JS_NewPlainObject, NewArrayObject,
};
use mozjs::rust::{JSEngine, RealmOptions, Runtime, SIMPLE_GLOBAL_CLASS};

#[test]
fn rooted_vec() {
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
        let mut context = &mut realm;
        rooted!(&in(context) let mut values = vec![]);
        for _ in 0..32 {
            values.push(ObjectValue(JS_NewPlainObject(&mut context)));
        }
        rooted!(&in(context) let array = NewArrayObject(&mut context, &HandleValueArray::from(&values)));
        let mut length = 0;
        assert!(GetArrayLength(&mut context, array.handle(), &mut length));
        assert_eq!(values.len(), length as usize);
        for index in 0..length {
            rooted!(&in(context) let mut element: JSVal);
            assert!(JS_GetElement(
                &mut context,
                array.handle(),
                index,
                element.handle_mut()
            ));
            assert_eq!(values[index as usize], element.get());
        }
    }
}
