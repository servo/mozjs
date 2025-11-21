/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::ptr;
use std::sync::mpsc::channel;
use std::thread;

use mozjs::jsapi::GCContext;
use mozjs::jsapi::JSCLASS_FOREGROUND_FINALIZE;
use mozjs::jsapi::{JSClass, JSClassOps, JSObject, OnNewGlobalHookOption};
use mozjs::realm::AutoRealm;
use mozjs::rooted;
use mozjs::rust::wrappers2::{JS_NewGlobalObject, JS_NewObject};
use mozjs::rust::{JSEngine, RealmOptions, Runtime, SIMPLE_GLOBAL_CLASS};

fn main() {
    println!("Hello, world!");
    let engine = JSEngine::init().expect("Could not init JSEngine");
    println!("JSEngine initialized");
    let mut runtime = Runtime::new(engine.handle());
    println!("Runtime initialized");
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
        let mut realm = AutoRealm::new_from_handle(context, global.handle());
        let context = &mut realm;
        rooted!(&in(context) let _object = JS_NewObject(context, &CLASS as *const _));
    }

    let parent = runtime.prepare_for_new_child();
    let (sender, receiver) = channel();
    thread::spawn(move || {
        let runtime = unsafe { Runtime::create_with_parent(parent) };
        assert!(Runtime::get().is_some());
        drop(runtime);
        let _ = sender.send(());
    });
    let _ = receiver.recv();
    println!("Example ran without issues...");
}

unsafe extern "C" fn finalize(_fop: *mut GCContext, _object: *mut JSObject) {
    assert!(Runtime::get().is_some());
}

static CLASS_OPS: JSClassOps = JSClassOps {
    addProperty: None,
    delProperty: None,
    enumerate: None,
    newEnumerate: None,
    resolve: None,
    mayResolve: None,
    finalize: Some(finalize),
    call: None,
    construct: None,
    trace: None,
};

static CLASS: JSClass = JSClass {
    name: c"EventTargetPrototype".as_ptr(),
    flags: JSCLASS_FOREGROUND_FINALIZE,
    cOps: &CLASS_OPS as *const JSClassOps,
    spec: ptr::null(),
    ext: ptr::null(),
    oOps: ptr::null(),
};
