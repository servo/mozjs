/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use crate::jsapi::JS::{
    DestroyFrontendContext, FrontendContext as RawFrontendContext,
    InstantiationStorage as RawInstantiationStorage, NewFrontendContext, PrepareForInstantiate,
    ReadOnlyCompileOptions,
};
use crate::jsapi::{CompileGlobalScriptToStencil_C, SetNativeStackQuota, ThreadStackQuotaForSize};
use crate::rust::{transform_str_to_source_text, OwningCompileOptionsWrapper, Stencil};

use std::mem::MaybeUninit;
use std::ops::Deref;
use std::sync::Arc;
use std::thread::{self, JoinHandle};

/// We want our default stack size limit to be approximately 2MB, to be safe for
/// JS helper tasks that can use a lot of stack, but expect most threads to use
/// much less. On Linux, however, requesting a stack of 2MB or larger risks the
/// kernel allocating an entire 2MB huge page for it on first access, which we do
/// not want. To avoid this possibility, we subtract 2 standard VM page sizes
/// from our default.
///
/// <https://searchfox.org/mozilla-central/rev/cb5faf5dd5176494302068c553da97b4d08aa339/xpcom/threads/TaskController.cpp#43>
const STACK_SIZE: usize = 2048 * 1024 - 2 * 4096;

pub struct FrontendContext(*mut RawFrontendContext);

unsafe impl Send for FrontendContext {}

impl FrontendContext {
    pub fn new() -> Self {
        Self(unsafe { NewFrontendContext() })
    }

    pub fn set_stack_quota(&self, size: usize) {
        unsafe {
            SetNativeStackQuota(self.0, ThreadStackQuotaForSize(size));
        }
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

pub struct InstantiationStorage(RawInstantiationStorage);

impl InstantiationStorage {
    pub fn as_mut_ptr(&mut self) -> *mut RawInstantiationStorage {
        &mut self.0
    }
}

unsafe impl Send for InstantiationStorage {}

impl Default for InstantiationStorage {
    fn default() -> InstantiationStorage {
        unsafe { std::mem::zeroed() }
    }
}

pub struct OffThreadToken(JoinHandle<Option<(Stencil, InstantiationStorage)>>);

impl OffThreadToken {
    /// Obtains result
    ///
    /// Blocks until completion
    pub fn finish(self) -> Option<(Stencil, InstantiationStorage)> {
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
    F: FnOnce(Stencil, InstantiationStorage) -> Option<(Stencil, InstantiationStorage)>
        + Send
        + 'static,
{
    let fc = FrontendContext::new();
    let options = OwningCompileOptionsWrapper::new_for_fc(&fc, options);
    OffThreadToken(
        thread::Builder::new()
            .name("OffThread Compile".to_string())
            .stack_size(STACK_SIZE)
            .spawn(move || {
                fc.set_stack_quota(STACK_SIZE);

                let mut source = transform_str_to_source_text(&source);
                let mut uninit_stencil = MaybeUninit::uninit();

                unsafe {
                    CompileGlobalScriptToStencil_C(
                        *fc,
                        options.read_only(),
                        &mut source,
                        uninit_stencil.as_mut_ptr(),
                    )
                }

                let stencil = unsafe { uninit_stencil.assume_init() };

                let mut storage = InstantiationStorage::default();
                let ok =
                    unsafe { PrepareForInstantiate(*fc, stencil.mRawPtr, storage.as_mut_ptr()) };

                if !ok {
                    // Gecko sets stencil to nullptr
                }
                callback(unsafe { Stencil::from_raw(stencil) }, storage)
            })
            .unwrap(),
    )
}
