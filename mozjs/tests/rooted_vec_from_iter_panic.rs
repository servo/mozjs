/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#![cfg(feature = "debugmozjs")]

use std::panic::{catch_unwind, AssertUnwindSafe};

use mozjs::gc::{RootableVec, RootedVec};
use mozjs::jsapi::GCReason;
use mozjs::jsval::JSVal;
use mozjs::rust::wrappers2::JS_GC;
use mozjs::rust::{JSEngine, Runtime};

#[test]
fn from_iter_panic_clears_registration() {
    let engine = JSEngine::init().unwrap();
    let mut runtime = Runtime::new(engine.handle());

    let mut root = Box::new(RootableVec::<JSVal>::new_unrooted());
    let result = catch_unwind(AssertUnwindSafe(|| {
        let _rooted: RootedVec<'_, JSVal> = RootedVec::from_iter(
            &mut *root,
            std::iter::from_fn(|| -> Option<JSVal> { panic!("iterator panic") }),
        );
    }));
    assert!(result.is_err());
    drop(root); // the stale registration is still in this thread's TLS set

    let cx = runtime.cx();
    unsafe { JS_GC(cx, GCReason::API) };
}
