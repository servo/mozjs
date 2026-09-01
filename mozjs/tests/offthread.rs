/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::ptr;
use std::sync::mpsc::channel;
use std::sync::Arc;

use mozjs::jsapi::{InstantiateOptions, OnNewGlobalHookOption};
use mozjs::jsval::UndefinedValue;
use mozjs::offthread::{compile_to_stencil_offthread, CompilationResult};
use mozjs::realm::AutoRealm;
use mozjs::rooted;
use mozjs::rust::wrappers2::{InstantiateGlobalStencil, JS_ExecuteScript, JS_NewGlobalObject};
use mozjs::rust::{CompileOptionsWrapper, JSEngine, RealmOptions, Runtime, SIMPLE_GLOBAL_CLASS};

#[test]
#[cfg_attr(target_arch = "wasm32", ignore)]
fn offthread() {
    let engine = JSEngine::init().unwrap();
    let mut runtime = Runtime::new(engine.handle());
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

        let src = Arc::new("1 + 1".to_string());
        let options = CompileOptionsWrapper::new(context, c"test".to_owned(), 1);
        let options_ptr = options.ptr as *const _;
        let (sender, receiver) = channel();
        let offthread_token = compile_to_stencil_offthread(options_ptr, src, move |result| {
            sender.send(result).unwrap();
            None
        });

        let compilation_result = receiver.recv().unwrap();

        let CompilationResult {
            stencil,
            mut storage,
            ..
        } = compilation_result;

        assert!(offthread_token.finish().is_none());

        let instantiate_options = InstantiateOptions {
            skipFilenameValidation: (*options.ptr)._base.skipFilenameValidation_,
            hideScriptFromDebugger: (*options.ptr)._base.hideScriptFromDebugger_,
            deferDebugMetadata: (*options.ptr)._base.deferDebugMetadata_,
            eagerDelazificationStrategy_: (*options.ptr)._base.eagerDelazificationStrategy_,
            eagerBaselineStrategy_: (*options.ptr)._base.eagerBaselineStrategy_,
        };

        rooted!(&in(context) let script = InstantiateGlobalStencil(
            context,
            &instantiate_options,
            *stencil,
            storage.as_mut_ptr(),
        ));

        rooted!(&in(context) let mut rval = UndefinedValue());
        let result = JS_ExecuteScript(context, script.handle(), rval.handle_mut());
        assert!(result);
        assert_eq!(rval.get().to_int32(), 2);
    }
}
