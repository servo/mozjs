use crate::jsapi::{Heap, JSObject, JSTracer};
use crate::rust::{Runtime, Stencil};
use mozjs_sys::trace::Traceable;
use std::cell::RefCell;
use std::ffi::c_void;

use crate::typedarray::{TypedArray, TypedArrayElement};

unsafe impl<T: TypedArrayElement> Traceable for TypedArray<T, Box<Heap<*mut JSObject>>> {
    unsafe fn trace(&self, trc: *mut JSTracer) {
        self.underlying_object().trace(trc);
    }
}

unsafe impl Traceable for Runtime {
    #[inline]
    unsafe fn trace(&self, _: *mut JSTracer) {}
}

unsafe impl Traceable for Stencil {
    #[inline]
    unsafe fn trace(&self, _: *mut JSTracer) {}
}

/// Holds a set of JSTraceables that need to be rooted
pub struct RootedTraceableSet {
    set: Vec<*const dyn Traceable>,
}

thread_local!(
    static ROOTED_TRACEABLES: RefCell<RootedTraceableSet>  = RefCell::new(RootedTraceableSet::new())
);

impl RootedTraceableSet {
    fn new() -> RootedTraceableSet {
        RootedTraceableSet { set: Vec::new() }
    }

    pub unsafe fn add(traceable: *const dyn Traceable) {
        ROOTED_TRACEABLES.with(|traceables| {
            traceables.borrow_mut().set.push(traceable);
        });
    }

    pub unsafe fn remove(traceable: *const dyn Traceable) {
        ROOTED_TRACEABLES.with(|traceables| {
            let mut traceables = traceables.borrow_mut();
            let idx = match traceables
                .set
                .iter()
                .rposition(|x| *x as *const () == traceable as *const ())
            {
                Some(idx) => idx,
                None => return,
            };
            traceables.set.remove(idx);
        });
    }

    pub(crate) unsafe fn trace(&self, trc: *mut JSTracer) {
        for traceable in &self.set {
            (**traceable).trace(trc);
        }
    }
}

pub unsafe extern "C" fn trace_traceables(trc: *mut JSTracer, _: *mut c_void) {
    ROOTED_TRACEABLES.with(|traceables| {
        traceables.borrow().trace(trc);
    });
}
