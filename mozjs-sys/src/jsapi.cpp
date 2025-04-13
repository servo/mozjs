/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "jsapi.h"

#include "js/ArrayBuffer.h"
#include "js/ArrayBufferMaybeShared.h"
#include "js/BigInt.h"
#include "js/BuildId.h"
#include "js/ColumnNumber.h"
#include "js/CompilationAndEvaluation.h"
#include "js/ContextOptions.h"
#include "js/Conversions.h"
#include "js/Date.h"
#include "js/Equality.h"
#include "js/ForOfIterator.h"
#include "js/Id.h"
#include "js/Initialization.h"
#include "js/JSON.h"
#include "js/MemoryMetrics.h"
#include "js/Modules.h"
#include "js/Object.h"
#include "js/Promise.h"
#include "js/PropertySpec.h"
#include "js/Proxy.h"
#include "js/Realm.h"
#include "js/RegExp.h"
#include "js/SavedFrameAPI.h"
#include "js/ScalarType.h"
#include "js/SharedArrayBuffer.h"
#include "js/SourceText.h"
#include "js/String.h"
#include "js/StructuredClone.h"
#include "js/Symbol.h"
#include "js/UniquePtr.h"
#include "js/Utility.h"
#include "js/Warnings.h"
#include "js/WasmModule.h"
#include "js/experimental/JSStencil.h"
#include "js/experimental/JitInfo.h"
#include "js/experimental/TypedData.h"
#include "js/friend/DOMProxy.h"
#include "js/friend/ErrorMessages.h"
#include "js/friend/WindowProxy.h"
#include "js/shadow/Object.h"
#include "js/shadow/Shape.h"
#include "jsfriendapi.h"

namespace glue {

// Reexport some functions that are marked inline.

bool JS_Init() { return ::JS_Init(); }

JS::RealmOptions* JS_NewRealmOptions() {
  JS::RealmOptions* result = new JS::RealmOptions;
  return result;
}

void DeleteRealmOptions(JS::RealmOptions* options) { delete options; }

JS::OwningCompileOptions* JS_NewOwningCompileOptions(JSContext* cx) {
  JS::OwningCompileOptions* result = new JS::OwningCompileOptions(cx);
  return result;
}

void DeleteOwningCompileOptions(JS::OwningCompileOptions* opts) { delete opts; }

JS::shadow::Zone* JS_AsShadowZone(JS::Zone* zone) {
  return JS::shadow::Zone::from(zone);
}

// Currently Unused, see jsimpls.rs (JS::CallArgs::from_vp)
JS::CallArgs JS_CallArgsFromVp(unsigned argc, JS::Value* vp) {
  return JS::CallArgsFromVp(argc, vp);
}

void JS_StackCapture_AllFrames(JS::StackCapture* capture) {
  JS::StackCapture all = JS::StackCapture(JS::AllFrames());
  // Since Rust can't provide a meaningful initial value for the
  // pointer, it is uninitialized memory. This means we must
  // overwrite its value, rather than perform an assignment
  // which could invoke a destructor on uninitialized memory.
  mozilla::PodAssign(capture, &all);
}

void JS_StackCapture_MaxFrames(uint32_t max, JS::StackCapture* capture) {
  JS::StackCapture maxFrames = JS::StackCapture(JS::MaxFrames(max));
  mozilla::PodAssign(capture, &maxFrames);
}

void JS_StackCapture_FirstSubsumedFrame(JSContext* cx,
                                        bool ignoreSelfHostedFrames,
                                        JS::StackCapture* capture) {
  JS::StackCapture subsumed =
      JS::StackCapture(JS::FirstSubsumedFrame(cx, ignoreSelfHostedFrames));
  mozilla::PodAssign(capture, &subsumed);
}

size_t GetLinearStringLength(JSLinearString* s) {
  return JS::GetLinearStringLength(s);
}

uint16_t GetLinearStringCharAt(JSLinearString* s, size_t idx) {
  return JS::GetLinearStringCharAt(s, idx);
}

JSLinearString* AtomToLinearString(JSAtom* atom) {
  return JS::AtomToLinearString(atom);
}

// Wrappers around UniquePtr functions

/**
 * Create a new ArrayBuffer with the given contents. The contents must not be
 * modified by any other code, internal or external.
 *
 * !!! IMPORTANT !!!
 * If and only if an ArrayBuffer is successfully created and returned,
 * ownership of |contents| is transferred to the new ArrayBuffer.
 *
 * When the ArrayBuffer is ready to be disposed of, `freeFunc(contents,
 * freeUserData)` will be called to release the ArrayBuffer's reference on the
 * contents.
 *
 * `freeFunc()` must not call any JSAPI functions that could cause a garbage
 * collection.
 *
 * The caller must keep the buffer alive until `freeFunc()` is called, or, if
 * `freeFunc` is null, until the JSRuntime is destroyed.
 *
 * The caller must not access the buffer on other threads. The JS engine will
 * not allow the buffer to be transferred to other threads. If you try to
 * transfer an external ArrayBuffer to another thread, the data is copied to a
 * new malloc buffer. `freeFunc()` must be threadsafe, and may be called from
 * any thread.
 *
 * This allows ArrayBuffers to be used with embedder objects that use reference
 * counting, for example. In that case the caller is responsible
 * for incrementing the reference count before passing the contents to this
 * function. This also allows using non-reference-counted contents that must be
 * freed with some function other than free().
 */
JSObject* NewExternalArrayBuffer(JSContext* cx, size_t nbytes, void* contents,
                                 JS::BufferContentsFreeFunc freeFunc,
                                 void* freeUserData) {
  js::UniquePtr<void, JS::BufferContentsDeleter> dataPtr{
      contents, {freeFunc, freeUserData}};
  return NewExternalArrayBuffer(cx, nbytes, std::move(dataPtr));
}

JSObject* NewArrayBufferWithContents(JSContext* cx, size_t nbytes,
                                     void* contents) {
  js::UniquePtr<void, JS::FreePolicy> dataPtr{contents};
  return JS::NewArrayBufferWithContents(cx, nbytes, std::move(dataPtr));
}

// Reexport some methods

bool JS_ForOfIteratorInit(
    JS::ForOfIterator* iterator, JS::HandleValue iterable,
    JS::ForOfIterator::NonIterableBehavior nonIterableBehavior) {
  return iterator->init(iterable, nonIterableBehavior);
}

bool JS_ForOfIteratorNext(JS::ForOfIterator* iterator,
                          JS::MutableHandleValue val, bool* done) {
  return iterator->next(val, done);
}

// These functions are only intended for use in testing,
// to make sure that the Rust implementation of JS::Value
// agrees with the C++ implementation.

void JS_ValueSetBoolean(JS::Value* value, bool x) { value->setBoolean(x); }

bool JS_ValueIsBoolean(const JS::Value* value) { return value->isBoolean(); }

bool JS_ValueToBoolean(const JS::Value* value) { return value->toBoolean(); }

void JS_ValueSetDouble(JS::Value* value, double x) { value->setDouble(x); }

bool JS_ValueIsDouble(const JS::Value* value) { return value->isDouble(); }

double JS_ValueToDouble(const JS::Value* value) { return value->toDouble(); }

void JS_ValueSetInt32(JS::Value* value, int32_t x) { value->setInt32(x); }

bool JS_ValueIsInt32(const JS::Value* value) { return value->isInt32(); }

int32_t JS_ValueToInt32(const JS::Value* value) { return value->toInt32(); }

bool JS_ValueIsNumber(const JS::Value* value) { return value->isNumber(); }

double JS_ValueToNumber(const JS::Value* value) { return value->toNumber(); }

void JS_ValueSetNull(JS::Value* value) { value->setNull(); }

bool JS_ValueIsNull(const JS::Value* value) { return value->isNull(); }

bool JS_ValueIsUndefined(const JS::Value* value) {
  return value->isUndefined();
}

// These types are using maybe so we manually unwrap them in these wrappers

bool FromPropertyDescriptor(JSContext* cx,
                            JS::Handle<JS::PropertyDescriptor> desc_,
                            JS::MutableHandleValue vp) {
  return JS::FromPropertyDescriptor(
      cx,
      JS::Rooted<mozilla::Maybe<JS::PropertyDescriptor>>(
          cx, mozilla::ToMaybe(&desc_)),
      vp);
}

bool JS_GetPropertyDescriptor(JSContext* cx, JS::Handle<JSObject*> obj,
                              const char* name,
                              JS::MutableHandle<JS::PropertyDescriptor> desc,
                              JS::MutableHandle<JSObject*> holder,
                              bool* isNone) {
  JS::Rooted<mozilla::Maybe<JS::PropertyDescriptor>> mpd(cx);
  bool result = JS_GetPropertyDescriptor(cx, obj, name, &mpd, holder);
  *isNone = mpd.isNothing();
  if (!*isNone) {
    desc.set(*mpd);
  }
  return result;
}

bool JS_GetOwnPropertyDescriptorById(
    JSContext* cx, JS::HandleObject obj, JS::HandleId id,
    JS::MutableHandle<JS::PropertyDescriptor> desc, bool* isNone) {
  JS::Rooted<mozilla::Maybe<JS::PropertyDescriptor>> mpd(cx);
  bool result = JS_GetOwnPropertyDescriptorById(cx, obj, id, &mpd);
  *isNone = mpd.isNothing();
  if (!*isNone) {
    desc.set(*mpd);
  }
  return result;
}

bool JS_GetOwnPropertyDescriptor(JSContext* cx, JS::HandleObject obj,
                                 const char* name,
                                 JS::MutableHandle<JS::PropertyDescriptor> desc,
                                 bool* isNone) {
  JS::Rooted<mozilla::Maybe<JS::PropertyDescriptor>> mpd(cx);
  bool result = JS_GetOwnPropertyDescriptor(cx, obj, name, &mpd);
  *isNone = mpd.isNothing();
  if (!*isNone) {
    desc.set(*mpd);
  }
  return result;
}

bool JS_GetOwnUCPropertyDescriptor(
    JSContext* cx, JS::HandleObject obj, const char16_t* name, size_t namelen,
    JS::MutableHandle<JS::PropertyDescriptor> desc, bool* isNone) {
  JS::Rooted<mozilla::Maybe<JS::PropertyDescriptor>> mpd(cx);
  bool result = JS_GetOwnUCPropertyDescriptor(cx, obj, name, namelen, &mpd);
  *isNone = mpd.isNothing();
  if (!*isNone) {
    desc.set(*mpd);
  }
  return result;
}

bool JS_GetPropertyDescriptorById(
    JSContext* cx, JS::HandleObject obj, JS::HandleId id,
    JS::MutableHandle<JS::PropertyDescriptor> desc,
    JS::MutableHandleObject holder, bool* isNone) {
  JS::Rooted<mozilla::Maybe<JS::PropertyDescriptor>> mpd(cx);
  bool result = JS_GetPropertyDescriptorById(cx, obj, id, &mpd, holder);
  *isNone = mpd.isNothing();
  if (!*isNone) {
    desc.set(*mpd);
  }
  return result;
}

bool JS_GetUCPropertyDescriptor(JSContext* cx, JS::HandleObject obj,
                                const char16_t* name, size_t namelen,
                                JS::MutableHandle<JS::PropertyDescriptor> desc,
                                JS::MutableHandleObject holder, bool* isNone) {
  JS::Rooted<mozilla::Maybe<JS::PropertyDescriptor>> mpd(cx);
  bool result =
      JS_GetUCPropertyDescriptor(cx, obj, name, namelen, &mpd, holder);
  *isNone = mpd.isNothing();
  if (!*isNone) {
    desc.set(*mpd);
  }
  return result;
}

bool SetPropertyIgnoringNamedGetter(
    JSContext* cx, JS::HandleObject obj, JS::HandleId id, JS::HandleValue v,
    JS::HandleValue receiver, const JS::Handle<JS::PropertyDescriptor>* ownDesc,
    JS::ObjectOpResult& result) {
  return js::SetPropertyIgnoringNamedGetter(
      cx, obj, id, v, receiver,
      JS::Rooted<mozilla::Maybe<JS::PropertyDescriptor>>(
          cx, mozilla::ToMaybe(ownDesc)),
      result);
}

bool CreateError(JSContext* cx, JSExnType type, JS::HandleObject stack,
                 JS::HandleString fileName, uint32_t lineNumber,
                 uint32_t columnNumber, JSErrorReport* report,
                 JS::HandleString message, JS::HandleValue cause,
                 JS::MutableHandleValue rval) {
  return JS::CreateError(
      cx, type, stack, fileName, lineNumber,
      JS::ColumnNumberOneOrigin(columnNumber), report, message,
      JS::Rooted<mozilla::Maybe<JS::Value>>(cx, mozilla::ToMaybe(&cause)),
      rval);
}

JSExnType GetErrorType(const JS::Value& val) {
  auto type = JS_GetErrorType(val);
  if (type.isNothing()) {
    return JSEXN_ERROR_LIMIT;
  }
  return *type;
}

void GetExceptionCause(JSObject* exc, JS::MutableHandleValue dest) {
  auto cause = JS::GetExceptionCause(exc);
  if (cause.isNothing()) {
    dest.setNull();
  } else {
    dest.set(*cause);
  }
}
}  // namespace glue

// There's a couple of classes from pre-57 releases of SM that bindgen can't
// deal with. https://github.com/rust-lang-nursery/rust-bindgen/issues/851
// https://bugzilla.mozilla.org/show_bug.cgi?id=1277338
// https://rust-lang-nursery.github.io/rust-bindgen/replacing-types.html

/**
 * <div rustbindgen replaces="JS::CallArgs"></div>
 */

class MOZ_STACK_CLASS CallArgsReplacement {
 protected:
  JS::Value* argv_;
  unsigned argc_;
  bool constructing_ : 1;
  bool ignoresReturnValue_ : 1;
#ifdef JS_DEBUG
  JS::detail::IncludeUsedRval wantUsedRval_;
#endif
};

/**
 * <div rustbindgen replaces="JSJitMethodCallArgs"></div>
 */

class JSJitMethodCallArgsReplacement {
 private:
  JS::Value* argv_;
  unsigned argc_;
  bool constructing_ : 1;
  bool ignoresReturnValue_ : 1;
#ifdef JS_DEBUG
  JS::detail::NoUsedRval wantUsedRval_;
#endif
};

/// <div rustbindgen replaces="JS::MutableHandleIdVector"></div>
struct MutableHandleIdVector_Simple {
  void* ptr;
};
static_assert(sizeof(JS::MutableHandleIdVector) ==
                  sizeof(MutableHandleIdVector_Simple),
              "wrong handle size");

/// <div rustbindgen replaces="JS::HandleObjectVector"></div>
struct HandleObjectVector_Simple {
  void* ptr;
};

/// <div rustbindgen replaces="JS::MutableHandleObjectVector"></div>
struct MutableHandleObjectVector_Simple {
  void* ptr;
};
