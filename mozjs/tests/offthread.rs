/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::ptr;
use std::sync::mpsc::channel;
use std::sync::Arc;

use mozjs::jsapi::{
    InstantiateGlobalStencil, InstantiateOptions, JSAutoRealm, JS_NewGlobalObject,
    OnNewGlobalHookOption,
};
use mozjs::jsval::UndefinedValue;
use mozjs::offthread::compile_to_stencil_offthread;
use mozjs::rooted;
use mozjs::rust::{
    wrappers::JS_ExecuteScript, CompileOptionsWrapper, JSEngine, RealmOptions, Runtime,
    SIMPLE_GLOBAL_CLASS,
};

#[test]
fn offthread() {
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

        let src = Arc::new("1 + 1".to_string());
        let options = CompileOptionsWrapper::new(context, "", 1);
        let options_ptr = options.ptr as *const _;
        let (sender, receiver) = channel();
        let offthread_token = compile_to_stencil_offthread(options_ptr, src, move |stencil| {
            sender.send(stencil).unwrap();
            None
        });

        let stencil = receiver.recv().unwrap();

        assert!(offthread_token.finish().is_none());

        let options = InstantiateOptions {
            skipFilenameValidation: false,
            hideScriptFromDebugger: false,
            deferDebugMetadata: false,
        };
        rooted!(in(context) let script = InstantiateGlobalStencil(
            context,
            &options,
            *stencil,
            ptr::null_mut(),
        ));

        rooted!(in(context) let mut rval = UndefinedValue());
        let result = JS_ExecuteScript(context, script.handle(), rval.handle_mut());
        assert!(result);
        assert_eq!(rval.get().to_int32(), 2);
    }
}
