/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use crate::jsapi::JS::{
    CompileGlobalScriptToStencil2, DestroyFrontendContext, FrontendContext as RawFrontendContext,
    NewFrontendContext, ReadOnlyCompileOptions,
};
use crate::rust::{transform_str_to_source_text, OwningCompileOptionsWrapper, Stencil};
use std::ops::Deref;
use std::sync::Arc;
use std::thread::{self, JoinHandle};

pub struct FrontendContext(*mut RawFrontendContext);

unsafe impl Send for FrontendContext {}

impl FrontendContext {
    pub fn new() -> Self {
        Self(unsafe { NewFrontendContext() })
    }
}

impl Deref for FrontendContext {
    type Target = *mut RawFrontendContext;
    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl Drop for FrontendContext {
    fn drop(&mut self) {
        unsafe { DestroyFrontendContext(self.0) }
    }
}

pub struct OffThreadToken(JoinHandle<Option<Stencil>>);

impl OffThreadToken {
    /// Obtains result
    ///
    /// Blocks until completion
    pub fn finish(self) -> Option<Stencil> {
        self.0.join().ok().flatten()
    }
}

/// Creates a new thread and starts compilation there
///
/// Callback receives stencil that can either consumed or returned
pub fn compile_to_stencil_offthread<F>(
    options: *const ReadOnlyCompileOptions,
    source: Arc<String>,
    callback: F,
) -> OffThreadToken
where
    F: FnOnce(Stencil) -> Option<Stencil> + Send + 'static,
{
    let fc = FrontendContext::new();
    let options = OwningCompileOptionsWrapper::new_for_fc(&fc, options);
    OffThreadToken(
        thread::Builder::new()
            .name("OffThread Compile".to_string())
            .spawn(move || {
                callback(unsafe {
                    Stencil::from_raw(CompileGlobalScriptToStencil2(
                        *fc,
                        options.read_only(),
                        &mut transform_str_to_source_text(&source) as *mut _,
                    ))
                })
            })
            .unwrap(),
    )
}
