#![cfg(feature = "debugmozjs")]

use std::ptr;

use mozjs::gc::RootedTraceableBox;
use mozjs::jsapi::SetGCZeal;
use mozjs::jsapi::{GCReason, Heap, JSTracer, OnNewGlobalHookOption, Value};
use mozjs::jsval::ObjectValue;
use mozjs::realm::AutoRealm;
use mozjs::rooted;
use mozjs::rust::wrappers2::{JS_NewGlobalObject, JS_NewPlainObject, JS_GC};
use mozjs::rust::{JSEngine, RealmOptions, Runtime, SIMPLE_GLOBAL_CLASS};

#[test]
fn rooted_traceable_box() {
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

        let something = RootedTraceableBox::new(Something {
            object: Heap::default(),
        });
        something
            .object
            .set(ObjectValue(JS_NewPlainObject(context)));
        JS_GC(context, GCReason::API);

        rooted!(&in(context) let _container = Container {
            something: something.into_box(),
        });

        JS_GC(context, GCReason::API);
    }
}

struct Something {
    object: Heap<Value>,
}

unsafe impl mozjs::rust::Trace for Something {
    unsafe fn trace(&self, trc: *mut JSTracer) {
        self.object.trace(trc);
    }
}

struct Container {
    something: Box<Something>,
}

unsafe impl mozjs::rust::Trace for Container {
    unsafe fn trace(&self, trc: *mut JSTracer) {
        self.something.trace(trc);
    }
}

impl mozjs::gc::Rootable for Container {}
