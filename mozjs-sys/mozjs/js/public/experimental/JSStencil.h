/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef js_experimental_JSStencil_h
#define js_experimental_JSStencil_h

/* The `JS::Stencil` type holds the output of the JS Parser before it is
 * allocated on the GC heap as a `JSScript`. This form may be serialized as
 * part of building a bytecode cache. This `Stencil` is not associated with any
 * particular Realm and may be generated off-main-thread, making it useful for
 * building script loaders.
 */

#include "mozilla/MemoryReporting.h"  // mozilla::MallocSizeOf
#include "mozilla/RefPtr.h"           // RefPtr, already_AddRefed
#include "mozilla/Utf8.h"             // mozilla::Utf8Unit
#include "mozilla/Vector.h"           // mozilla::Vector

#include <stddef.h>  // size_t

#include "jstypes.h"  // JS_PUBLIC_API

#include "js/CompileOptions.h"  // JS::ReadOnlyCompileOptions, JS::InstantiateOptions, JS::ReadOnlyDecodeOptions
#include "js/RootingAPI.h"   // JS::MutableHandle
#include "js/SourceText.h"   // JS::SourceText
#include "js/Transcoding.h"  // JS::TranscodeBuffer, JS::TranscodeRange
#include "js/Value.h"        // JS::Value

struct JS_PUBLIC_API JSContext;
class JS_PUBLIC_API JSTracer;

// Underlying opaque type.
namespace js {
class FrontendContext;
namespace frontend {
struct CompilationStencil;
struct CompilationGCOutput;
struct CompilationInput;
struct PreallocatedCompilationGCOutput;
struct InitialStencilAndDelazifications;
}  // namespace frontend
}  // namespace js

// ************************************************************************
//   Types
// ************************************************************************

namespace JS {

using Stencil = js::frontend::InitialStencilAndDelazifications;
using FrontendContext = js::FrontendContext;

// Temporary storage used during instantiating Stencil.
//
// Off-thread APIs can allocate this instance off main thread, and pass it back
// to the main thread, in order to reduce the main thread allocation.
struct JS_PUBLIC_API InstantiationStorage {
 private:
  // Owned CompilationGCOutput.
  //
  // This uses raw pointer instead of UniquePtr because
  // PreallocatedCompilationGCOutput is opaque.
  js::frontend::PreallocatedCompilationGCOutput* gcOutput_ = nullptr;

  friend JS_PUBLIC_API JSScript* InstantiateGlobalStencil(
      JSContext* cx, const InstantiateOptions& options, Stencil* stencil,
      InstantiationStorage* storage);

  friend JS_PUBLIC_API JSObject* InstantiateModuleStencil(
      JSContext* cx, const InstantiateOptions& options, Stencil* stencil,
      InstantiationStorage* storage);

  friend JS_PUBLIC_API bool PrepareForInstantiate(
      JS::FrontendContext* fc, JS::Stencil& stencil,
      JS::InstantiationStorage& storage);

 public:
  InstantiationStorage() = default;
  InstantiationStorage(InstantiationStorage&& other)
      : gcOutput_(other.gcOutput_) {
    other.gcOutput_ = nullptr;
  }

  ~InstantiationStorage();

  void operator=(InstantiationStorage&& other) {
    gcOutput_ = other.gcOutput_;
    other.gcOutput_ = nullptr;
  }

 private:
  InstantiationStorage(const InstantiationStorage& other) = delete;
  void operator=(const InstantiationStorage& aOther) = delete;

 public:
  bool isValid() const { return !!gcOutput_; }
};

}  // namespace JS

// ************************************************************************
//   Reference Count
// ************************************************************************

namespace JS {

// These non-member functions let us manipulate the ref counts of the opaque
// Stencil type. The RefPtrTraits below calls these for use when using the
// RefPtr type.
JS_PUBLIC_API void StencilAddRef(Stencil* stencil);
JS_PUBLIC_API void StencilRelease(Stencil* stencil);

}  // namespace JS

namespace mozilla {
template <>
struct RefPtrTraits<JS::Stencil> {
  static void AddRef(JS::Stencil* stencil) { JS::StencilAddRef(stencil); }
  static void Release(JS::Stencil* stencil) { JS::StencilRelease(stencil); }
};
}  // namespace mozilla

// ************************************************************************
//   Properties
// ************************************************************************

namespace JS {

// Return true if the stencil relies on external data as a result of XDR
// decoding.
extern JS_PUBLIC_API bool StencilIsBorrowed(Stencil* stencil);

extern JS_PUBLIC_API size_t SizeOfStencil(Stencil* stencil,
                                          mozilla::MallocSizeOf mallocSizeOf);

}  // namespace JS

// ************************************************************************
//   Compilation
// ************************************************************************

namespace JS {

// Compile the source text into a JS::Stencil using the provided options. The
// resulting stencil may be instantiated into any Realm on the current runtime
// and may be used multiple times.
//
// NOTE: On error, a null will be returned and an exception will be set on the
//       JSContext.
extern JS_PUBLIC_API already_AddRefed<Stencil> CompileGlobalScriptToStencil(
    JSContext* cx, const ReadOnlyCompileOptions& options,
    SourceText<mozilla::Utf8Unit>& srcBuf);
extern JS_PUBLIC_API already_AddRefed<Stencil> CompileGlobalScriptToStencil(
    JSContext* cx, const ReadOnlyCompileOptions& options,
    SourceText<char16_t>& srcBuf);

// Compile the source text into a JS::Stencil using "module" parse goal. The
// ECMAScript spec defines special semantics so we use a seperate entry point
// here for clarity. The result is still a JS::Stencil, but should use the
// appropriate instantiate API below.
extern JS_PUBLIC_API already_AddRefed<Stencil> CompileModuleScriptToStencil(
    JSContext* cx, const ReadOnlyCompileOptions& options,
    SourceText<mozilla::Utf8Unit>& srcBuf);
extern JS_PUBLIC_API already_AddRefed<Stencil> CompileModuleScriptToStencil(
    JSContext* cx, const ReadOnlyCompileOptions& options,
    SourceText<char16_t>& srcBuf);

}  // namespace JS

// ************************************************************************
//   Instantiation
// ************************************************************************

namespace JS {

// Instantiate the Stencil into current Realm and return the JSScript.
extern JS_PUBLIC_API JSScript* InstantiateGlobalStencil(
    JSContext* cx, const InstantiateOptions& options, Stencil* stencil,
    InstantiationStorage* storage = nullptr);

// Instantiate a module Stencil and return the associated object. Inside the
// engine this is a js::ModuleObject.
extern JS_PUBLIC_API JSObject* InstantiateModuleStencil(
    JSContext* cx, const InstantiateOptions& options, Stencil* stencil,
    InstantiationStorage* storage = nullptr);

}  // namespace JS

// ************************************************************************
//   Transcoding
// ************************************************************************

namespace JS {

// Serialize the Stencil into the transcode buffer.
// This fails if the stencil contains asm.js.
//
// If the `buffer` isn't empty, the start of the `buffer` should meet
// JS::IsTranscodingBytecodeAligned, and the length should meet
// JS::IsTranscodingBytecodeOffsetAligned.
//
// NOTE: As long as IsTranscodingBytecodeOffsetAligned is met, that means
//       there's JS::BytecodeOffsetAlignment+extra bytes in the buffer,
//       IsTranscodingBytecodeAligned should be guaranteed to meet by
//       malloc, used by MallocAllocPolicy in mozilla::Vector.
extern JS_PUBLIC_API TranscodeResult EncodeStencil(JSContext* cx,
                                                   Stencil* stencil,
                                                   TranscodeBuffer& buffer);
extern JS_PUBLIC_API TranscodeResult EncodeStencil(JS::FrontendContext* fc,
                                                   Stencil* stencil,
                                                   TranscodeBuffer& buffer);

// Deserialize data and create a new Stencil.
extern JS_PUBLIC_API TranscodeResult
DecodeStencil(JSContext* cx, const ReadOnlyDecodeOptions& options,
              const TranscodeRange& range, Stencil** stencilOut);
extern JS_PUBLIC_API TranscodeResult
DecodeStencil(JS::FrontendContext* fc, const ReadOnlyDecodeOptions& options,
              const TranscodeRange& range, Stencil** stencilOut);

// ************************************************************************
//   Collect delazifications
// ************************************************************************

enum class CollectDelazificationsResult {
  // The collection wasn't ongoing, and it's newly started.
  // The caller is responsible for either finishing or aborting later.
  NewlyStarted,

  // The collection has already started by other consumer.
  // The caller doesn't own the collection.
  AlreadyStarted,

  // The target script is not supported and the collection is not started.
  NotSupported,
};

// Start collecting delazifications for given script or module's source object.
//
// On successful case, this sets the result out-parameter to one of the above
// CollectDelazificationsResult and returns true, and returns false otherwise.
extern JS_PUBLIC_API bool StartCollectingDelazifications(
    JSContext* cx, JS::Handle<JSScript*> script, Stencil* stencil,
    CollectDelazificationsResult& result);

extern JS_PUBLIC_API bool StartCollectingDelazifications(
    JSContext* cx, JS::Handle<JSObject*> module, Stencil* stencil,
    CollectDelazificationsResult& result);

// Finish collecting delazifications and retrieve the result as a JS::Stencil
// that reflects the delazification from the execution.
//
// The returned stencil is the same thing as what is passed to
// JS::StartCollectingDelazifications.
extern JS_PUBLIC_API bool FinishCollectingDelazifications(
    JSContext* cx, Handle<JSScript*> script, JS::Stencil** stencilOut);

// Similar to |JS::FinishCollectingDelazifications|, but receives module obect.
extern JS_PUBLIC_API bool FinishCollectingDelazifications(
    JSContext* cx, Handle<JSObject*> module, JS::Stencil** stencilOut);

extern JS_PUBLIC_API void AbortCollectingDelazifications(JSScript* script);
extern JS_PUBLIC_API void AbortCollectingDelazifications(JSObject* module);

// ************************************************************************
//   Cache
// ************************************************************************

// Returns true if the stencil is compatible with caching.
// This returns false if the stencil contains asm.js.
extern JS_PUBLIC_API bool IsStencilCacheable(JS::Stencil* stencil);

// ************************************************************************
//   Script Source
// ************************************************************************

// Returns the uncompressed source text length of given stencil.
// Returns 0 if the source is discarded or somehow not accessible.
extern JS_PUBLIC_API size_t GetScriptSourceLength(JS::Stencil* stencil);

// Returns the uncompressed source text of given stencil, to the `result` out
// parameter, as a string value either in Latin1 or char16.
// `result` is set to undefined if the source is discarded or somehow not
// accessible.
extern JS_PUBLIC_API bool GetScriptSourceText(
    JSContext* cx, JS::Stencil* stencil, JS::MutableHandle<JS::Value> result);

}  // namespace JS

#endif  // js_experimental_JSStencil_h
