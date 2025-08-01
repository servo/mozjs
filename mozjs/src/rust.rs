/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

//! Rust wrappers around the raw JS apis

use std::cell::Cell;
use std::char;
use std::default::Default;
use std::ffi;
use std::ffi::CStr;
use std::marker::PhantomData;
use std::mem::MaybeUninit;
use std::ops::{Deref, DerefMut};
use std::ptr::{self, NonNull};
use std::slice;
use std::str;
use std::sync::atomic::{AtomicU32, Ordering};
use std::sync::{Arc, Mutex, RwLock};

use crate::consts::{JSCLASS_GLOBAL_SLOT_COUNT, JSCLASS_RESERVED_SLOTS_MASK};
use crate::consts::{JSCLASS_IS_DOMJSCLASS, JSCLASS_IS_GLOBAL};
use crate::conversions::jsstr_to_string;
use crate::default_heapsize;
pub use crate::gc::*;
use crate::glue::AppendToRootedObjectVector;
use crate::glue::{CreateRootedIdVector, CreateRootedObjectVector};
use crate::glue::{
    DeleteCompileOptions, DeleteRootedObjectVector, DescribeScriptedCaller, DestroyRootedIdVector,
};
use crate::glue::{DeleteJSAutoStructuredCloneBuffer, NewJSAutoStructuredCloneBuffer};
use crate::glue::{
    GetIdVectorAddress, GetObjectVectorAddress, NewCompileOptions, SliceRootedIdVector,
};
use crate::jsapi;
use crate::jsapi::glue::{DeleteRealmOptions, JS_Init, JS_NewRealmOptions};
use crate::jsapi::js::frontend::CompilationStencil;
use crate::jsapi::mozilla::Utf8Unit;
use crate::jsapi::shadow::BaseShape;
use crate::jsapi::HandleObjectVector as RawHandleObjectVector;
use crate::jsapi::HandleValue as RawHandleValue;
use crate::jsapi::JS_AddExtraGCRootsTracer;
use crate::jsapi::MutableHandleIdVector as RawMutableHandleIdVector;
use crate::jsapi::{already_AddRefed, jsid};
use crate::jsapi::{BuildStackString, CaptureCurrentStack, StackFormat};
use crate::jsapi::{Evaluate2, HandleValueArray, StencilRelease};
use crate::jsapi::{InitSelfHostedCode, IsWindowSlow};
use crate::jsapi::{
    JSAutoRealm, JS_SetGCParameter, JS_SetNativeStackQuota, JS_WrapObject, JS_WrapValue,
};
use crate::jsapi::{JSAutoStructuredCloneBuffer, JSStructuredCloneCallbacks, StructuredCloneScope};
use crate::jsapi::{JSClass, JSClassOps, JSContext, Realm, JSCLASS_RESERVED_SLOTS_SHIFT};
use crate::jsapi::{JSErrorReport, JSFunctionSpec, JSGCParamKey};
use crate::jsapi::{JSObject, JSPropertySpec, JSRuntime};
use crate::jsapi::{JSString, Object, PersistentRootedIdVector};
use crate::jsapi::{JS_DefineFunctions, JS_DefineProperties, JS_DestroyContext, JS_ShutDown};
use crate::jsapi::{JS_EnumerateStandardClasses, JS_GetRuntime, JS_GlobalObjectTraceHook};
use crate::jsapi::{JS_MayResolveStandardClass, JS_NewContext, JS_ResolveStandardClass};
use crate::jsapi::{JS_RequestInterruptCallback, JS_RequestInterruptCallbackCanWait};
use crate::jsapi::{JS_StackCapture_AllFrames, JS_StackCapture_MaxFrames};
use crate::jsapi::{PersistentRootedObjectVector, ReadOnlyCompileOptions, RootingContext};
use crate::jsapi::{SetWarningReporter, SourceText, ToBooleanSlow};
use crate::jsapi::{ToInt32Slow, ToInt64Slow, ToNumberSlow, ToStringSlow, ToUint16Slow};
use crate::jsapi::{ToUint32Slow, ToUint64Slow, ToWindowProxyIfWindowSlow};
use crate::jsval::ObjectValue;
use crate::panic::maybe_resume_unwind;
use log::{debug, warn};
use mozjs_sys::jsapi::JS::SavedFrameResult;
pub use mozjs_sys::jsgc::{GCMethods, IntoHandle, IntoMutableHandle};
pub use mozjs_sys::trace::Traceable as Trace;

use crate::rooted;

// From Gecko:
// Our "default" stack is what we use in configurations where we don't have a compelling reason to
// do things differently. This is effectively 1MB on 64-bit platforms.
const STACK_QUOTA: usize = 128 * 8 * 1024;

// From Gecko:
// The JS engine permits us to set different stack limits for system code,
// trusted script, and untrusted script. We have tests that ensure that
// we can always execute 10 "heavy" (eval+with) stack frames deeper in
// privileged code. Our stack sizes vary greatly in different configurations,
// so satisfying those tests requires some care. Manual measurements of the
// number of heavy stack frames achievable gives us the following rough data,
// ordered by the effective categories in which they are grouped in the
// JS_SetNativeStackQuota call (which predates this analysis).
//
// (NB: These numbers may have drifted recently - see bug 938429)
// OSX 64-bit Debug: 7MB stack, 636 stack frames => ~11.3k per stack frame
// OSX64 Opt: 7MB stack, 2440 stack frames => ~3k per stack frame
//
// Linux 32-bit Debug: 2MB stack, 426 stack frames => ~4.8k per stack frame
// Linux 64-bit Debug: 4MB stack, 455 stack frames => ~9.0k per stack frame
//
// Windows (Opt+Debug): 900K stack, 235 stack frames => ~3.4k per stack frame
//
// Linux 32-bit Opt: 1MB stack, 272 stack frames => ~3.8k per stack frame
// Linux 64-bit Opt: 2MB stack, 316 stack frames => ~6.5k per stack frame
//
// We tune the trusted/untrusted quotas for each configuration to achieve our
// invariants while attempting to minimize overhead. In contrast, our buffer
// between system code and trusted script is a very unscientific 10k.
const SYSTEM_CODE_BUFFER: usize = 10 * 1024;

// Gecko's value on 64-bit.
const TRUSTED_SCRIPT_BUFFER: usize = 8 * 12800;

trait ToResult {
    fn to_result(self) -> Result<(), ()>;
}

impl ToResult for bool {
    fn to_result(self) -> Result<(), ()> {
        if self {
            Ok(())
        } else {
            Err(())
        }
    }
}

// ___________________________________________________________________________
// friendly Rustic API to runtimes

pub struct RealmOptions(*mut jsapi::RealmOptions);

impl Deref for RealmOptions {
    type Target = jsapi::RealmOptions;
    fn deref(&self) -> &Self::Target {
        unsafe { &*self.0 }
    }
}

impl DerefMut for RealmOptions {
    fn deref_mut(&mut self) -> &mut Self::Target {
        unsafe { &mut *self.0 }
    }
}

impl Default for RealmOptions {
    fn default() -> RealmOptions {
        RealmOptions(unsafe { JS_NewRealmOptions() })
    }
}

impl Drop for RealmOptions {
    fn drop(&mut self) {
        unsafe { DeleteRealmOptions(self.0) }
    }
}

thread_local!(static CONTEXT: Cell<*mut JSContext> = Cell::new(ptr::null_mut()));

#[derive(PartialEq)]
enum EngineState {
    Uninitialized,
    InitFailed,
    Initialized,
    ShutDown,
}

static ENGINE_STATE: Mutex<EngineState> = Mutex::new(EngineState::Uninitialized);

#[derive(Debug)]
pub enum JSEngineError {
    AlreadyInitialized,
    AlreadyShutDown,
    InitFailed,
}

/// A handle that must be kept alive in order to create new Runtimes.
/// When this handle is dropped, the engine is shut down and cannot
/// be reinitialized.
pub struct JSEngine {
    /// The count of alive handles derived from this initialized instance.
    outstanding_handles: Arc<AtomicU32>,
    // Ensure this type cannot be sent between threads.
    marker: PhantomData<*mut ()>,
}

pub struct JSEngineHandle(Arc<AtomicU32>);

impl Clone for JSEngineHandle {
    fn clone(&self) -> JSEngineHandle {
        self.0.fetch_add(1, Ordering::SeqCst);
        JSEngineHandle(self.0.clone())
    }
}

impl Drop for JSEngineHandle {
    fn drop(&mut self) {
        self.0.fetch_sub(1, Ordering::SeqCst);
    }
}

impl JSEngine {
    /// Initialize the JS engine to prepare for creating new JS runtimes.
    pub fn init() -> Result<JSEngine, JSEngineError> {
        let mut state = ENGINE_STATE.lock().unwrap();
        match *state {
            EngineState::Initialized => return Err(JSEngineError::AlreadyInitialized),
            EngineState::InitFailed => return Err(JSEngineError::InitFailed),
            EngineState::ShutDown => return Err(JSEngineError::AlreadyShutDown),
            EngineState::Uninitialized => (),
        }
        if unsafe { !JS_Init() } {
            *state = EngineState::InitFailed;
            Err(JSEngineError::InitFailed)
        } else {
            *state = EngineState::Initialized;
            Ok(JSEngine {
                outstanding_handles: Arc::new(AtomicU32::new(0)),
                marker: PhantomData,
            })
        }
    }

    pub fn can_shutdown(&self) -> bool {
        self.outstanding_handles.load(Ordering::SeqCst) == 0
    }

    /// Create a handle to this engine.
    pub fn handle(&self) -> JSEngineHandle {
        self.outstanding_handles.fetch_add(1, Ordering::SeqCst);
        JSEngineHandle(self.outstanding_handles.clone())
    }
}

/// Shut down the JS engine, invalidating any existing runtimes and preventing
/// any new ones from being created.
impl Drop for JSEngine {
    fn drop(&mut self) {
        let mut state = ENGINE_STATE.lock().unwrap();
        if *state == EngineState::Initialized {
            assert_eq!(
                self.outstanding_handles.load(Ordering::SeqCst),
                0,
                "There are outstanding JS engine handles"
            );
            *state = EngineState::ShutDown;
            unsafe {
                JS_ShutDown();
            }
        }
    }
}

pub fn transform_str_to_source_text(source: &str) -> SourceText<Utf8Unit> {
    SourceText {
        units_: source.as_ptr() as *const _,
        length_: source.len() as u32,
        ownsUnits_: false,
        _phantom_0: PhantomData,
    }
}

pub fn transform_u16_to_source_text(source: &[u16]) -> SourceText<u16> {
    SourceText {
        units_: source.as_ptr() as *const _,
        length_: source.len() as u32,
        ownsUnits_: false,
        _phantom_0: PhantomData,
    }
}

/// A handle to a Runtime that will be used to create a new runtime in another
/// thread. This handle and the new runtime must be destroyed before the original
/// runtime can be dropped.
pub struct ParentRuntime {
    /// Raw pointer to the underlying SpiderMonkey runtime.
    parent: *mut JSRuntime,
    /// Handle to ensure the JS engine remains running while this handle exists.
    engine: JSEngineHandle,
    /// The number of children of the runtime that created this ParentRuntime value.
    children_of_parent: Arc<()>,
}
unsafe impl Send for ParentRuntime {}

/// A wrapper for the `JSContext` structure in SpiderMonkey.
pub struct Runtime {
    /// Raw pointer to the underlying SpiderMonkey context.
    cx: *mut JSContext,
    /// The engine that this runtime is associated with.
    engine: JSEngineHandle,
    /// If this Runtime was created with a parent, this member exists to ensure
    /// that that parent's count of outstanding children (see [outstanding_children])
    /// remains accurate and will be automatically decreased when this Runtime value
    /// is dropped.
    _parent_child_count: Option<Arc<()>>,
    /// The strong references to this value represent the number of child runtimes
    /// that have been created using this Runtime as a parent. Since Runtime values
    /// must be associated with a particular thread, we cannot simply use Arc<Runtime>
    /// to represent the resulting ownership graph and risk destroying a Runtime on
    /// the wrong thread.
    outstanding_children: Arc<()>,
    /// An `Option` that holds the same pointer as `cx`.
    /// This is shared with all [`ThreadSafeJSContext`]s, so
    /// they can detect when it's destroyed on the main thread.
    thread_safe_handle: Arc<RwLock<Option<*mut JSContext>>>,
}

impl Runtime {
    /// Get the `JSContext` for this thread.
    pub fn get() -> Option<NonNull<JSContext>> {
        let cx = CONTEXT.with(|context| context.get());
        NonNull::new(cx)
    }

    /// Create a [`ThreadSafeJSContext`] that can detect when this `Runtime` is destroyed.
    pub fn thread_safe_js_context(&self) -> ThreadSafeJSContext {
        ThreadSafeJSContext(self.thread_safe_handle.clone())
    }

    /// Creates a new `JSContext`.
    pub fn new(engine: JSEngineHandle) -> Runtime {
        unsafe { Self::create(engine, None) }
    }

    /// Signal that a new child runtime will be created in the future, and ensure
    /// that this runtime will not allow itself to be destroyed before the new
    /// child runtime. Returns a handle that can be passed to `create_with_parent`
    /// in order to create a new runtime on another thread that is associated with
    /// this runtime.
    pub fn prepare_for_new_child(&self) -> ParentRuntime {
        ParentRuntime {
            parent: self.rt(),
            engine: self.engine.clone(),
            children_of_parent: self.outstanding_children.clone(),
        }
    }

    /// Creates a new `JSContext` with a parent runtime. If the parent does not outlive
    /// the new runtime, its destructor will assert.
    ///
    /// Unsafety:
    /// If panicking does not abort the program, any threads with child runtimes will
    /// continue executing after the thread with the parent runtime panics, but they
    /// will be in an invalid and undefined state.
    pub unsafe fn create_with_parent(parent: ParentRuntime) -> Runtime {
        Self::create(parent.engine.clone(), Some(parent))
    }

    unsafe fn create(engine: JSEngineHandle, parent: Option<ParentRuntime>) -> Runtime {
        let parent_runtime = parent.as_ref().map_or(ptr::null_mut(), |r| r.parent);
        let js_context = JS_NewContext(default_heapsize + (ChunkSize as u32), parent_runtime);
        assert!(!js_context.is_null());

        // Unconstrain the runtime's threshold on nominal heap size, to avoid
        // triggering GC too often if operating continuously near an arbitrary
        // finite threshold. This leaves the maximum-JS_malloc-bytes threshold
        // still in effect to cause periodical, and we hope hygienic,
        // last-ditch GCs from within the GC's allocator.
        JS_SetGCParameter(js_context, JSGCParamKey::JSGC_MAX_BYTES, u32::MAX);

        JS_AddExtraGCRootsTracer(js_context, Some(trace_traceables), ptr::null_mut());

        JS_SetNativeStackQuota(
            js_context,
            STACK_QUOTA,
            STACK_QUOTA - SYSTEM_CODE_BUFFER,
            STACK_QUOTA - SYSTEM_CODE_BUFFER - TRUSTED_SCRIPT_BUFFER,
        );

        CONTEXT.with(|context| {
            assert!(context.get().is_null());
            context.set(js_context);
        });

        #[cfg(target_pointer_width = "64")]
        let cache = crate::jsapi::__BindgenOpaqueArray::<u64, 2>::default();
        #[cfg(target_pointer_width = "32")]
        let cache = crate::jsapi::__BindgenOpaqueArray::<u32, 2>::default();

        InitSelfHostedCode(js_context, cache, None);

        SetWarningReporter(js_context, Some(report_warning));

        Runtime {
            engine,
            _parent_child_count: parent.map(|p| p.children_of_parent),
            cx: js_context,
            outstanding_children: Arc::new(()),
            thread_safe_handle: Arc::new(RwLock::new(Some(js_context))),
        }
    }

    /// Returns the `JSRuntime` object.
    pub fn rt(&self) -> *mut JSRuntime {
        unsafe { JS_GetRuntime(self.cx) }
    }

    /// Returns the `JSContext` object.
    pub fn cx(&self) -> *mut JSContext {
        self.cx
    }

    pub fn evaluate_script(
        &self,
        glob: HandleObject,
        script: &str,
        filename: &str,
        line_num: u32,
        rval: MutableHandleValue,
    ) -> Result<(), ()> {
        debug!(
            "Evaluating script from {} with content {}",
            filename, script
        );

        let _ac = JSAutoRealm::new(self.cx(), glob.get());
        let options = unsafe { CompileOptionsWrapper::new(self.cx(), filename, line_num) };

        unsafe {
            let mut source = transform_str_to_source_text(&script);
            if !Evaluate2(self.cx(), options.ptr, &mut source, rval.into()) {
                debug!("...err!");
                maybe_resume_unwind();
                Err(())
            } else {
                // we could return the script result but then we'd have
                // to root it and so forth and, really, who cares?
                debug!("...ok!");
                Ok(())
            }
        }
    }
}

impl Drop for Runtime {
    fn drop(&mut self) {
        self.thread_safe_handle.write().unwrap().take();
        assert!(
            Arc::get_mut(&mut self.outstanding_children).is_some(),
            "This runtime still has live children."
        );
        unsafe {
            JS_DestroyContext(self.cx);

            CONTEXT.with(|context| {
                assert_eq!(context.get(), self.cx);
                context.set(ptr::null_mut());
            });
        }
    }
}

/// A version of the [`JSContext`] that can be used from other threads and is thus
/// `Send` and `Sync`. This should only ever expose operations that are marked as
/// thread-safe by the SpiderMonkey API, ie ones that only atomic fields in JSContext.
#[derive(Clone)]
pub struct ThreadSafeJSContext(Arc<RwLock<Option<*mut JSContext>>>);

unsafe impl Send for ThreadSafeJSContext {}
unsafe impl Sync for ThreadSafeJSContext {}

impl ThreadSafeJSContext {
    /// Call `JS_RequestInterruptCallback` from the SpiderMonkey API.
    /// This is thread-safe according to
    /// <https://searchfox.org/mozilla-central/rev/7a85a111b5f42cdc07f438e36f9597c4c6dc1d48/js/public/Interrupt.h#19>
    pub fn request_interrupt_callback(&self) {
        if let Some(&cx) = self.0.read().unwrap().as_ref() {
            unsafe {
                JS_RequestInterruptCallback(cx);
            }
        }
    }

    /// Call `JS_RequestInterruptCallbackCanWait` from the SpiderMonkey API.
    /// This is thread-safe according to
    /// <https://searchfox.org/mozilla-central/rev/7a85a111b5f42cdc07f438e36f9597c4c6dc1d48/js/public/Interrupt.h#19>
    pub fn request_interrupt_callback_can_wait(&self) {
        if let Some(&cx) = self.0.read().unwrap().as_ref() {
            unsafe {
                JS_RequestInterruptCallbackCanWait(cx);
            }
        }
    }
}

const ChunkShift: usize = 20;
const ChunkSize: usize = 1 << ChunkShift;

#[cfg(target_pointer_width = "32")]
const ChunkLocationOffset: usize = ChunkSize - 2 * 4 - 8;

// ___________________________________________________________________________
// Wrappers around things in jsglue.cpp

pub struct RootedObjectVectorWrapper {
    pub ptr: *mut PersistentRootedObjectVector,
}

impl RootedObjectVectorWrapper {
    pub fn new(cx: *mut JSContext) -> RootedObjectVectorWrapper {
        RootedObjectVectorWrapper {
            ptr: unsafe { CreateRootedObjectVector(cx) },
        }
    }

    pub fn append(&self, obj: *mut JSObject) -> bool {
        unsafe { AppendToRootedObjectVector(self.ptr, obj) }
    }

    pub fn handle(&self) -> RawHandleObjectVector {
        RawHandleObjectVector {
            ptr: unsafe { GetObjectVectorAddress(self.ptr) },
        }
    }
}

impl Drop for RootedObjectVectorWrapper {
    fn drop(&mut self) {
        unsafe { DeleteRootedObjectVector(self.ptr) }
    }
}

pub struct CompileOptionsWrapper {
    pub ptr: *mut ReadOnlyCompileOptions,
}

impl CompileOptionsWrapper {
    pub unsafe fn new(cx: *mut JSContext, filename: &str, line: u32) -> Self {
        let filename_cstr = ffi::CString::new(filename.as_bytes()).unwrap();
        let ptr = NewCompileOptions(cx, filename_cstr.as_ptr(), line);
        assert!(!ptr.is_null());
        Self { ptr }
    }
}

impl Drop for CompileOptionsWrapper {
    fn drop(&mut self) {
        unsafe { DeleteCompileOptions(self.ptr) }
    }
}

pub struct JSAutoStructuredCloneBufferWrapper {
    ptr: NonNull<JSAutoStructuredCloneBuffer>,
}

impl JSAutoStructuredCloneBufferWrapper {
    pub unsafe fn new(
        scope: StructuredCloneScope,
        callbacks: *const JSStructuredCloneCallbacks,
    ) -> Self {
        let raw_ptr = NewJSAutoStructuredCloneBuffer(scope, callbacks);
        Self {
            ptr: NonNull::new(raw_ptr).unwrap(),
        }
    }

    pub fn as_raw_ptr(&self) -> *mut JSAutoStructuredCloneBuffer {
        self.ptr.as_ptr()
    }
}

impl Drop for JSAutoStructuredCloneBufferWrapper {
    fn drop(&mut self) {
        unsafe {
            DeleteJSAutoStructuredCloneBuffer(self.ptr.as_ptr());
        }
    }
}

pub struct Stencil {
    inner: already_AddRefed<CompilationStencil>,
}

/*unsafe impl Send for Stencil {}
unsafe impl Sync for Stencil {}*/

impl Drop for Stencil {
    fn drop(&mut self) {
        if self.is_null() {
            return;
        }
        unsafe {
            StencilRelease(self.inner.mRawPtr);
        }
    }
}

impl Deref for Stencil {
    type Target = *mut CompilationStencil;

    fn deref(&self) -> &Self::Target {
        &self.inner.mRawPtr
    }
}

impl Stencil {
    pub fn is_null(&self) -> bool {
        self.inner.mRawPtr.is_null()
    }
}

// ___________________________________________________________________________
// Fast inline converters

#[inline]
pub unsafe fn ToBoolean(v: HandleValue) -> bool {
    let val = *v.ptr;

    if val.is_boolean() {
        return val.to_boolean();
    }

    if val.is_int32() {
        return val.to_int32() != 0;
    }

    if val.is_null_or_undefined() {
        return false;
    }

    if val.is_double() {
        let d = val.to_double();
        return !d.is_nan() && d != 0f64;
    }

    if val.is_symbol() {
        return true;
    }

    ToBooleanSlow(v.into())
}

#[inline]
pub unsafe fn ToNumber(cx: *mut JSContext, v: HandleValue) -> Result<f64, ()> {
    let val = *v.ptr;
    if val.is_number() {
        return Ok(val.to_number());
    }

    let mut out = Default::default();
    if ToNumberSlow(cx, v.into_handle(), &mut out) {
        Ok(out)
    } else {
        Err(())
    }
}

#[inline]
unsafe fn convert_from_int32<T: Default + Copy>(
    cx: *mut JSContext,
    v: HandleValue,
    conv_fn: unsafe extern "C" fn(*mut JSContext, RawHandleValue, *mut T) -> bool,
) -> Result<T, ()> {
    let val = *v.ptr;
    if val.is_int32() {
        let intval: i64 = val.to_int32() as i64;
        // TODO: do something better here that works on big endian
        let intval = *(&intval as *const i64 as *const T);
        return Ok(intval);
    }

    let mut out = Default::default();
    if conv_fn(cx, v.into(), &mut out) {
        Ok(out)
    } else {
        Err(())
    }
}

#[inline]
pub unsafe fn ToInt32(cx: *mut JSContext, v: HandleValue) -> Result<i32, ()> {
    convert_from_int32::<i32>(cx, v, ToInt32Slow)
}

#[inline]
pub unsafe fn ToUint32(cx: *mut JSContext, v: HandleValue) -> Result<u32, ()> {
    convert_from_int32::<u32>(cx, v, ToUint32Slow)
}

#[inline]
pub unsafe fn ToUint16(cx: *mut JSContext, v: HandleValue) -> Result<u16, ()> {
    convert_from_int32::<u16>(cx, v, ToUint16Slow)
}

#[inline]
pub unsafe fn ToInt64(cx: *mut JSContext, v: HandleValue) -> Result<i64, ()> {
    convert_from_int32::<i64>(cx, v, ToInt64Slow)
}

#[inline]
pub unsafe fn ToUint64(cx: *mut JSContext, v: HandleValue) -> Result<u64, ()> {
    convert_from_int32::<u64>(cx, v, ToUint64Slow)
}

#[inline]
pub unsafe fn ToString(cx: *mut JSContext, v: HandleValue) -> *mut JSString {
    let val = *v.ptr;
    if val.is_string() {
        return val.to_string();
    }

    ToStringSlow(cx, v.into())
}

pub unsafe fn ToWindowProxyIfWindow(obj: *mut JSObject) -> *mut JSObject {
    if is_window(obj) {
        ToWindowProxyIfWindowSlow(obj)
    } else {
        obj
    }
}

pub unsafe extern "C" fn report_warning(_cx: *mut JSContext, report: *mut JSErrorReport) {
    fn latin1_to_string(bytes: &[u8]) -> String {
        bytes
            .iter()
            .map(|c| char::from_u32(*c as u32).unwrap())
            .collect()
    }

    let fnptr = (*report)._base.filename.data_;
    let fname = if !fnptr.is_null() {
        let c_str = CStr::from_ptr(fnptr);
        latin1_to_string(c_str.to_bytes())
    } else {
        "none".to_string()
    };

    let lineno = (*report)._base.lineno;
    let column = (*report)._base.column._base;

    let msg_ptr = (*report)._base.message_.data_ as *const u8;
    let msg_len = (0usize..)
        .find(|&i| *msg_ptr.offset(i as isize) == 0)
        .unwrap();
    let msg_slice = slice::from_raw_parts(msg_ptr, msg_len);
    let msg = str::from_utf8_unchecked(msg_slice);

    warn!("Warning at {}:{}:{}: {}\n", fname, lineno, column, msg);
}

pub struct IdVector(*mut PersistentRootedIdVector);

impl IdVector {
    pub unsafe fn new(cx: *mut JSContext) -> IdVector {
        let vector = CreateRootedIdVector(cx);
        assert!(!vector.is_null());
        IdVector(vector)
    }

    pub fn handle_mut(&mut self) -> RawMutableHandleIdVector {
        RawMutableHandleIdVector {
            ptr: unsafe { GetIdVectorAddress(self.0) },
        }
    }
}

impl Drop for IdVector {
    fn drop(&mut self) {
        unsafe { DestroyRootedIdVector(self.0) }
    }
}

impl Deref for IdVector {
    type Target = [jsid];

    fn deref(&self) -> &[jsid] {
        unsafe {
            let mut length = 0;
            let pointer = SliceRootedIdVector(self.0, &mut length);
            slice::from_raw_parts(pointer, length)
        }
    }
}

/// Defines methods on `obj`. The last entry of `methods` must contain zeroed
/// memory.
///
/// # Failures
///
/// Returns `Err` on JSAPI failure.
///
/// # Panics
///
/// Panics if the last entry of `methods` does not contain zeroed memory.
///
/// # Safety
///
/// - `cx` must be valid.
/// - This function calls into unaudited C++ code.
pub unsafe fn define_methods(
    cx: *mut JSContext,
    obj: HandleObject,
    methods: &'static [JSFunctionSpec],
) -> Result<(), ()> {
    assert!({
        match methods.last() {
            Some(&JSFunctionSpec {
                name,
                call,
                nargs,
                flags,
                selfHostedName,
            }) => {
                name.string_.is_null()
                    && call.is_zeroed()
                    && nargs == 0
                    && flags == 0
                    && selfHostedName.is_null()
            }
            None => false,
        }
    });

    JS_DefineFunctions(cx, obj.into(), methods.as_ptr()).to_result()
}

/// Defines attributes on `obj`. The last entry of `properties` must contain
/// zeroed memory.
///
/// # Failures
///
/// Returns `Err` on JSAPI failure.
///
/// # Panics
///
/// Panics if the last entry of `properties` does not contain zeroed memory.
///
/// # Safety
///
/// - `cx` must be valid.
/// - This function calls into unaudited C++ code.
pub unsafe fn define_properties(
    cx: *mut JSContext,
    obj: HandleObject,
    properties: &'static [JSPropertySpec],
) -> Result<(), ()> {
    assert!({
        match properties.last() {
            Some(spec) => spec.is_zeroed(),
            None => false,
        }
    });

    JS_DefineProperties(cx, obj.into(), properties.as_ptr()).to_result()
}

static SIMPLE_GLOBAL_CLASS_OPS: JSClassOps = JSClassOps {
    addProperty: None,
    delProperty: None,
    enumerate: Some(JS_EnumerateStandardClasses),
    newEnumerate: None,
    resolve: Some(JS_ResolveStandardClass),
    mayResolve: Some(JS_MayResolveStandardClass),
    finalize: None,
    call: None,
    construct: None,
    trace: Some(JS_GlobalObjectTraceHook),
};

/// This is a simple `JSClass` for global objects, primarily intended for tests.
pub static SIMPLE_GLOBAL_CLASS: JSClass = JSClass {
    name: c"Global".as_ptr(),
    flags: JSCLASS_IS_GLOBAL
        | ((JSCLASS_GLOBAL_SLOT_COUNT & JSCLASS_RESERVED_SLOTS_MASK)
            << JSCLASS_RESERVED_SLOTS_SHIFT),
    cOps: &SIMPLE_GLOBAL_CLASS_OPS as *const JSClassOps,
    spec: ptr::null(),
    ext: ptr::null(),
    oOps: ptr::null(),
};

#[inline]
unsafe fn get_object_group(obj: *mut JSObject) -> *mut BaseShape {
    assert!(!obj.is_null());
    let obj = obj as *mut Object;
    (*(*obj).shape).base
}

#[inline]
pub unsafe fn get_object_class(obj: *mut JSObject) -> *const JSClass {
    (*get_object_group(obj)).clasp as *const _
}

#[inline]
pub unsafe fn get_object_realm(obj: *mut JSObject) -> *mut Realm {
    (*get_object_group(obj)).realm
}

#[inline]
pub unsafe fn get_context_realm(cx: *mut JSContext) -> *mut Realm {
    let cx = cx as *mut RootingContext;
    (*cx).realm_
}

#[inline]
pub fn is_dom_class(class: &JSClass) -> bool {
    class.flags & JSCLASS_IS_DOMJSCLASS != 0
}

#[inline]
pub unsafe fn is_dom_object(obj: *mut JSObject) -> bool {
    is_dom_class(&*get_object_class(obj))
}

#[inline]
pub unsafe fn is_window(obj: *mut JSObject) -> bool {
    (*get_object_class(obj)).flags & JSCLASS_IS_GLOBAL != 0 && IsWindowSlow(obj)
}

#[inline]
pub unsafe fn try_to_outerize(mut rval: MutableHandleValue) {
    let obj = rval.to_object();
    if is_window(obj) {
        let obj = ToWindowProxyIfWindowSlow(obj);
        assert!(!obj.is_null());
        rval.set(ObjectValue(&mut *obj));
    }
}

#[inline]
pub unsafe fn try_to_outerize_object(mut rval: MutableHandleObject) {
    if is_window(*rval) {
        let obj = ToWindowProxyIfWindowSlow(*rval);
        assert!(!obj.is_null());
        rval.set(obj);
    }
}

#[inline]
pub unsafe fn maybe_wrap_object(cx: *mut JSContext, mut obj: MutableHandleObject) {
    if get_object_realm(*obj) != get_context_realm(cx) {
        assert!(JS_WrapObject(cx, obj.reborrow().into()));
    }
    try_to_outerize_object(obj);
}

#[inline]
pub unsafe fn maybe_wrap_object_value(cx: *mut JSContext, rval: MutableHandleValue) {
    assert!(rval.is_object());
    let obj = rval.to_object();
    if get_object_realm(obj) != get_context_realm(cx) {
        assert!(JS_WrapValue(cx, rval.into()));
    } else if is_dom_object(obj) {
        try_to_outerize(rval);
    }
}

#[inline]
pub unsafe fn maybe_wrap_object_or_null_value(cx: *mut JSContext, rval: MutableHandleValue) {
    assert!(rval.is_object_or_null());
    if !rval.is_null() {
        maybe_wrap_object_value(cx, rval);
    }
}

#[inline]
pub unsafe fn maybe_wrap_value(cx: *mut JSContext, rval: MutableHandleValue) {
    if rval.is_string() {
        assert!(JS_WrapValue(cx, rval.into()));
    } else if rval.is_object() {
        maybe_wrap_object_value(cx, rval);
    }
}

/// Like `JSJitInfo::new_bitfield_1`, but usable in `const` contexts.
#[macro_export]
macro_rules! new_jsjitinfo_bitfield_1 {
    (
        $type_: expr,
        $aliasSet_: expr,
        $returnType_: expr,
        $isInfallible: expr,
        $isMovable: expr,
        $isEliminatable: expr,
        $isAlwaysInSlot: expr,
        $isLazilyCachedInSlot: expr,
        $isTypedMethod: expr,
        $slotIndex: expr,
    ) => {
        0 | (($type_ as u32) << 0u32)
            | (($aliasSet_ as u32) << 4u32)
            | (($returnType_ as u32) << 8u32)
            | (($isInfallible as u32) << 16u32)
            | (($isMovable as u32) << 17u32)
            | (($isEliminatable as u32) << 18u32)
            | (($isAlwaysInSlot as u32) << 19u32)
            | (($isLazilyCachedInSlot as u32) << 20u32)
            | (($isTypedMethod as u32) << 21u32)
            | (($slotIndex as u32) << 22u32)
    };
}

#[derive(Debug, Default)]
pub struct ScriptedCaller {
    pub filename: String,
    pub line: u32,
    pub col: u32,
}

pub unsafe fn describe_scripted_caller(cx: *mut JSContext) -> Result<ScriptedCaller, ()> {
    let mut buf = [0; 1024];
    let mut line = 0;
    let mut col = 0;
    if !DescribeScriptedCaller(cx, buf.as_mut_ptr(), buf.len(), &mut line, &mut col) {
        return Err(());
    }
    let filename = CStr::from_ptr((&buf) as *const _ as *const _);
    Ok(ScriptedCaller {
        filename: String::from_utf8_lossy(filename.to_bytes()).into_owned(),
        line,
        col,
    })
}

pub struct CapturedJSStack<'a> {
    cx: *mut JSContext,
    stack: RootedGuard<'a, *mut JSObject>,
}

impl<'a> CapturedJSStack<'a> {
    pub unsafe fn new(
        cx: *mut JSContext,
        mut guard: RootedGuard<'a, *mut JSObject>,
        max_frame_count: Option<u32>,
    ) -> Option<Self> {
        let ref mut stack_capture = MaybeUninit::uninit();
        match max_frame_count {
            None => JS_StackCapture_AllFrames(stack_capture.as_mut_ptr()),
            Some(count) => JS_StackCapture_MaxFrames(count, stack_capture.as_mut_ptr()),
        };
        let ref mut stack_capture = stack_capture.assume_init();

        if !CaptureCurrentStack(cx, guard.handle_mut().raw(), stack_capture) {
            None
        } else {
            Some(CapturedJSStack { cx, stack: guard })
        }
    }

    pub fn as_string(&self, indent: Option<usize>, format: StackFormat) -> Option<String> {
        unsafe {
            let stack_handle = self.stack.handle();
            rooted!(in(self.cx) let mut js_string = ptr::null_mut::<JSString>());
            let mut string_handle = js_string.handle_mut();

            if !BuildStackString(
                self.cx,
                ptr::null_mut(),
                stack_handle.into(),
                string_handle.raw(),
                indent.unwrap_or(0),
                format,
            ) {
                return None;
            }

            Some(jsstr_to_string(self.cx, string_handle.get()))
        }
    }

    /// Executes the provided closure for each frame on the js stack
    pub fn for_each_stack_frame<F>(&self, mut f: F)
    where
        F: FnMut(Handle<*mut JSObject>),
    {
        rooted!(in(self.cx) let mut current_element = self.stack.clone());
        rooted!(in(self.cx) let mut next_element = ptr::null_mut::<JSObject>());

        loop {
            f(current_element.handle());

            unsafe {
                let result = jsapi::GetSavedFrameParent(
                    self.cx,
                    ptr::null_mut(),
                    current_element.handle().into_handle(),
                    next_element.handle_mut().into_handle_mut(),
                    jsapi::SavedFrameSelfHosted::Include,
                );

                if result != SavedFrameResult::Ok || next_element.is_null() {
                    return;
                }
            }
            current_element.set(next_element.get());
        }
    }
}

#[macro_export]
macro_rules! capture_stack {
    (in($cx:expr) let $name:ident = with max depth($max_frame_count:expr)) => {
        rooted!(in($cx) let mut __obj = ::std::ptr::null_mut());
        let $name = $crate::rust::CapturedJSStack::new($cx, __obj, Some($max_frame_count));
    };
    (in($cx:expr) let $name:ident ) => {
        rooted!(in($cx) let mut __obj = ::std::ptr::null_mut());
        let $name = $crate::rust::CapturedJSStack::new($cx, __obj, None);
    }
}

/// Wrappers for JSAPI methods that accept lifetimed Handle and MutableHandle arguments
pub mod wrappers {
    macro_rules! wrap {
        // The invocation of @inner has the following form:
        // @inner (input args) <> (accumulator) <> unparsed tokens
        // when `unparsed tokens == \eps`, accumulator contains the final result

        (@inner $saved:tt <> ($($acc:expr,)*) <> $arg:ident: *const Handle<$gentype:ty>, $($rest:tt)*) => {
            wrap!(@inner $saved <> ($($acc,)* if $arg.is_null() { std::ptr::null() } else { &(*$arg).into() },) <> $($rest)*);
        };
        (@inner $saved:tt <> ($($acc:expr,)*) <> $arg:ident: Handle<$gentype:ty>, $($rest:tt)*) => {
            wrap!(@inner $saved <> ($($acc,)* $arg.into(),) <> $($rest)*);
        };
        (@inner $saved:tt <> ($($acc:expr,)*) <> $arg:ident: MutableHandle<$gentype:ty>, $($rest:tt)*) => {
            wrap!(@inner $saved <> ($($acc,)* $arg.into(),) <> $($rest)*);
        };
        (@inner $saved:tt <> ($($acc:expr,)*) <> $arg:ident: Handle, $($rest:tt)*) => {
            wrap!(@inner $saved <> ($($acc,)* $arg.into(),) <> $($rest)*);
        };
        (@inner $saved:tt <> ($($acc:expr,)*) <> $arg:ident: MutableHandle, $($rest:tt)*) => {
            wrap!(@inner $saved <> ($($acc,)* $arg.into(),) <> $($rest)*);
        };
        (@inner $saved:tt <> ($($acc:expr,)*) <> $arg:ident: HandleFunction , $($rest:tt)*) => {
            wrap!(@inner $saved <> ($($acc,)* $arg.into(),) <> $($rest)*);
        };
        (@inner $saved:tt <> ($($acc:expr,)*) <> $arg:ident: HandleId , $($rest:tt)*) => {
            wrap!(@inner $saved <> ($($acc,)* $arg.into(),) <> $($rest)*);
        };
        (@inner $saved:tt <> ($($acc:expr,)*) <> $arg:ident: HandleObject , $($rest:tt)*) => {
            wrap!(@inner $saved <> ($($acc,)* $arg.into(),) <> $($rest)*);
        };
        (@inner $saved:tt <> ($($acc:expr,)*) <> $arg:ident: HandleScript , $($rest:tt)*) => {
            wrap!(@inner $saved <> ($($acc,)* $arg.into(),) <> $($rest)*);
        };
        (@inner $saved:tt <> ($($acc:expr,)*) <> $arg:ident: HandleString , $($rest:tt)*) => {
            wrap!(@inner $saved <> ($($acc,)* $arg.into(),) <> $($rest)*);
        };
        (@inner $saved:tt <> ($($acc:expr,)*) <> $arg:ident: HandleSymbol , $($rest:tt)*) => {
            wrap!(@inner $saved <> ($($acc,)* $arg.into(),) <> $($rest)*);
        };
        (@inner $saved:tt <> ($($acc:expr,)*) <> $arg:ident: HandleValue , $($rest:tt)*) => {
            wrap!(@inner $saved <> ($($acc,)* $arg.into(),) <> $($rest)*);
        };
        (@inner $saved:tt <> ($($acc:expr,)*) <> $arg:ident: MutableHandleFunction , $($rest:tt)*) => {
            wrap!(@inner $saved <> ($($acc,)* $arg.into(),) <> $($rest)*);
        };
        (@inner $saved:tt <> ($($acc:expr,)*) <> $arg:ident: MutableHandleId , $($rest:tt)*) => {
            wrap!(@inner $saved <> ($($acc,)* $arg.into(),) <> $($rest)*);
        };
        (@inner $saved:tt <> ($($acc:expr,)*) <> $arg:ident: MutableHandleObject , $($rest:tt)*) => {
            wrap!(@inner $saved <> ($($acc,)* $arg.into(),) <> $($rest)*);
        };
        (@inner $saved:tt <> ($($acc:expr,)*) <> $arg:ident: MutableHandleScript , $($rest:tt)*) => {
            wrap!(@inner $saved <> ($($acc,)* $arg.into(),) <> $($rest)*);
        };
        (@inner $saved:tt <> ($($acc:expr,)*) <> $arg:ident: MutableHandleString , $($rest:tt)*) => {
            wrap!(@inner $saved <> ($($acc,)* $arg.into(),) <> $($rest)*);
        };
        (@inner $saved:tt <> ($($acc:expr,)*) <> $arg:ident: MutableHandleSymbol , $($rest:tt)*) => {
            wrap!(@inner $saved <> ($($acc,)* $arg.into(),) <> $($rest)*);
        };
        (@inner $saved:tt <> ($($acc:expr,)*) <> $arg:ident: MutableHandleValue , $($rest:tt)*) => {
            wrap!(@inner $saved <> ($($acc,)* $arg.into(),) <> $($rest)*);
        };
        (@inner $saved:tt <> ($($acc:expr,)*) <> $arg:ident: $type:ty, $($rest:tt)*) => {
            wrap!(@inner $saved <> ($($acc,)* $arg,) <> $($rest)*);
        };
        (@inner ($module:tt: $func_name:ident ($($args:tt)*) -> $outtype:ty) <> ($($argexprs:expr,)*) <> ) => {
            #[inline]
            pub unsafe fn $func_name($($args)*) -> $outtype {
                $module::$func_name($($argexprs),*)
            }
        };
        ($module:tt: pub fn $func_name:ident($($args:tt)*) -> $outtype:ty) => {
            wrap!(@inner ($module: $func_name ($($args)*) -> $outtype) <> () <> $($args)* ,);
        };
        ($module:tt: pub fn $func_name:ident($($args:tt)*)) => {
            wrap!($module: pub fn $func_name($($args)*) -> ());
        }
    }

    use super::*;
    use crate::glue;
    use crate::glue::EncodedStringCallback;
    use crate::jsapi;
    use crate::jsapi::jsid;
    use crate::jsapi::mozilla::Utf8Unit;
    use crate::jsapi::BigInt;
    use crate::jsapi::CallArgs;
    use crate::jsapi::CloneDataPolicy;
    use crate::jsapi::ColumnNumberOneOrigin;
    use crate::jsapi::CompartmentTransplantCallback;
    use crate::jsapi::JSONParseHandler;
    use crate::jsapi::Latin1Char;
    use crate::jsapi::PropertyKey;
    use crate::jsapi::TaggedColumnNumberOneOrigin;
    //use jsapi::DynamicImportStatus;
    use crate::jsapi::ESClass;
    use crate::jsapi::ExceptionStackBehavior;
    use crate::jsapi::ForOfIterator;
    use crate::jsapi::ForOfIterator_NonIterableBehavior;
    use crate::jsapi::HandleObjectVector;
    use crate::jsapi::InstantiateOptions;
    use crate::jsapi::JSClass;
    use crate::jsapi::JSErrorReport;
    use crate::jsapi::JSExnType;
    use crate::jsapi::JSFunctionSpecWithHelp;
    use crate::jsapi::JSJitInfo;
    use crate::jsapi::JSONWriteCallback;
    use crate::jsapi::JSPrincipals;
    use crate::jsapi::JSPropertySpec;
    use crate::jsapi::JSPropertySpec_Name;
    use crate::jsapi::JSProtoKey;
    use crate::jsapi::JSScript;
    use crate::jsapi::JSStructuredCloneData;
    use crate::jsapi::JSType;
    use crate::jsapi::ModuleErrorBehaviour;
    use crate::jsapi::MutableHandleIdVector;
    use crate::jsapi::PromiseState;
    use crate::jsapi::PromiseUserInputEventHandlingState;
    use crate::jsapi::ReadOnlyCompileOptions;
    use crate::jsapi::Realm;
    use crate::jsapi::RefPtr;
    use crate::jsapi::RegExpFlags;
    use crate::jsapi::ScriptEnvironmentPreparer_Closure;
    use crate::jsapi::SourceText;
    use crate::jsapi::StackCapture;
    use crate::jsapi::StructuredCloneScope;
    use crate::jsapi::Symbol;
    use crate::jsapi::SymbolCode;
    use crate::jsapi::TwoByteChars;
    use crate::jsapi::UniqueChars;
    use crate::jsapi::Value;
    use crate::jsapi::WasmModule;
    use crate::jsapi::{ElementAdder, IsArrayAnswer, PropertyDescriptor};
    use crate::jsapi::{JSContext, JSFunction, JSNative, JSObject, JSString};
    use crate::jsapi::{
        JSStructuredCloneCallbacks, JSStructuredCloneReader, JSStructuredCloneWriter,
    };
    use crate::jsapi::{MallocSizeOf, ObjectOpResult, ObjectPrivateVisitor, TabSizes};
    use crate::jsapi::{SavedFrameResult, SavedFrameSelfHosted};
    include!("jsapi_wrappers.in.rs");
    include!("glue_wrappers.in.rs");
}
