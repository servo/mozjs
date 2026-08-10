/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef js_friend_MicroTask_h
#define js_friend_MicroTask_h

#include "jstypes.h"

#include "js/GCPolicyAPI.h"
#include "js/RootingAPI.h"
#include "js/TypeDecls.h"
#include "js/UniquePtr.h"
#include "js/Value.h"
#include "js/ValueArray.h"

namespace JS {

// [SMDOC] MicroTasks in SpiderMonkey
//
// To enable higher performance, this header allows an embedding to work with
// a MicroTask queue stored inside the JS engine. This allows optimization of
// tasks by avoiding allocations in important cases.
//
// To make this work, we need some cooperation with the embedding.
//
// The high level thrust of this is that rather than managing the JobQueue
// themselves, embeddings assume that there's a JobQueue available to them
// inside the engine. When 'runJobs' happens, the embedding is responsible
// for pulling jobs of the queue, doing any setup required, then calling
// them.
//
// Embedding jobs are trivially supportable, since a MicroTask job is
// represented as a JS::Value, and thus an embedding job may be put on
// the queue by wrapping it in a JS::Value (e.g. using Private to store
// C++ pointers).
//
// The major requirement is that if a MicroTask identifies as a "JS"
// MicroTask, by passing the IsJSMicrotask predicate, the job must be
// run by calling RunJSMicroTask, while in the realm specified by the
// global returned by GetExecutionGlobalFromJSMicroTask, e.g
//
//    JSObject* global = JS::GetExecutionGlobalFromJSMicroTask(job);
//    if (global) {
//      AutoRealm ar(cx, global);
//      if (!JS::RunJSMicroTask(cx, job)) {
//        ...
//      }
//    }

// A MicroTask is a JS::Value. Using this MicroTask system allows
// embedders to put whatever pointer they would like into the queue.
// The task will be dequeued unchanged.
//
// The major requirement here is that if the MicroTask is a JS
// MicroTask (as determined by IsJSMicroTask), it must be run
// by calling RunJSMicroTask, while in the realm specified by
// GetExecutionGlobalFromJSMicroTask.
//
// An embedding is free to do with non-JS MicroTasks as it
// sees fit.
using GenericMicroTask = JS::Value;
using JSMicroTask = JSObject;

JS_PUBLIC_API bool IsJSMicroTask(const JS::GenericMicroTask& hv);
JS_PUBLIC_API JSMicroTask* ToUnwrappedJSMicroTask(
    const JS::GenericMicroTask& genericMicroTask);
JS_PUBLIC_API JSMicroTask* ToMaybeWrappedJSMicroTask(
    const JS::GenericMicroTask& genericMicroTask);

// Run a MicroTask that is known to be a JS MicroTask. This will crash
// if provided an invalid task kind.
//
// This will return false if an exception is thrown while processing.
JS_PUBLIC_API bool RunJSMicroTask(JSContext* cx,
                                  Handle<JS::JSMicroTask*> entry);

// Queue Management. This is done per-JSContext.
//
// Internally we maintain two queues, one for 'debugger' microtasks. These
// are expected in normal operation to either be popped off the queue first,
// or processed separately.
//
// Non-debugger MicroTasks are "regular" microtasks, and go to the regular
// microtask queue.
//
// In general, we highly recommend that most embeddings use only the regular
// microtask queue. The debugger microtask queue mostly exists to support
// patterns used by Gecko.
//
// These methods only fail for OOM.
JS_PUBLIC_API bool EnqueueMicroTask(JSContext* cx,
                                    const GenericMicroTask& entry);
JS_PUBLIC_API bool EnqueueDebugMicroTask(JSContext* cx,
                                         const GenericMicroTask& entry);
JS_PUBLIC_API bool PrependMicroTask(JSContext* cx,
                                    const GenericMicroTask& entry);

// Dequeue the next MicroTask. If there are no MicroTasks of the appropriate
// kind, each of the below API returns JS::NullValue().
//
// The generic DequeueNext will always pull a debugger microtask first,
// if one exists, then a regular microtask if one exists.
// - DequeueNextDebuggerMicroTask only pulls from the debugger queue.
// - DequeueNextRegularMicroTask only pulls from the regular queue.
//
// Internally, these basically do
//
//    if (HasXMicroTask()) { return X.popFront(); } return NullValue()
//
// so checking for emptiness before calling these is not required, and is
// very slightly less efficient.
JS_PUBLIC_API GenericMicroTask DequeueNextMicroTask(JSContext* cx);
JS_PUBLIC_API GenericMicroTask DequeueNextDebuggerMicroTask(JSContext* cx);
JS_PUBLIC_API GenericMicroTask DequeueNextRegularMicroTask(JSContext* cx);

// Peek at the next MicroTask without removing it from the queue.
// Returns JS::NullValue() if there are no MicroTasks.
// Checks debugger queue first, then regular queue.
JS_PUBLIC_API GenericMicroTask PeekNextMicroTask(JSContext* cx);

// Returns true if there are -any- microtasks pending in the queue.
JS_PUBLIC_API bool HasAnyMicroTasks(JSContext* cx);

// Returns true if there are any debugger microtasks pending in the queue.
JS_PUBLIC_API bool HasDebuggerMicroTasks(JSContext* cx);

// Returns true if there are any regular (non-debugger) microtasks pending in
// the queue.
JS_PUBLIC_API bool HasRegularMicroTasks(JSContext* cx);

// Returns the length of the regular microtask queue.
JS_PUBLIC_API size_t GetRegularMicroTaskCount(JSContext* cx);

// This is the global associated with the realm RunJSMicroTask expects to be
// in.  Returns nullptr if a dead wrapper is found.
JS_PUBLIC_API JSObject* GetExecutionGlobalFromJSMicroTask(JSMicroTask* entry);

// To handle cases where the queue needs to be set aside for some reason
// (mostly the Debugger API), we provide a Save and Restore API.
//
// When restoring the saved queue, the JSContext microtask queue must be
// empty -- you cannot drop items by restoring over a non-empty queue
// (so HasAnyMicroTasks must be false).
class SavedMicroTaskQueue {
 public:
  SavedMicroTaskQueue() = default;
  virtual ~SavedMicroTaskQueue() = default;
  SavedMicroTaskQueue(const SavedMicroTaskQueue&) = delete;
  SavedMicroTaskQueue& operator=(const SavedMicroTaskQueue&) = delete;
};

// This will return nullptr (and set OutOfMemory) if the save operation
// fails.
JS_PUBLIC_API js::UniquePtr<SavedMicroTaskQueue> SaveMicroTaskQueue(
    JSContext* cx);
JS_PUBLIC_API void RestoreMicroTaskQueue(
    JSContext* cx, js::UniquePtr<SavedMicroTaskQueue> savedQueue);

// Via the following API functions various host defined data is exposed to the
// embedder (see JobQueue::getHostDefinedData).
//
// These return true on success and false on failure. They return false if
// there are any unwrapping issues (e.g., dead wrappers), and true with nullptr
// if there just isn't any data.
//
// This disambiguates between no-data and the dead wrapper case
JS_PUBLIC_API bool MaybeGetHostDefinedDataFromJSMicroTask(
    JSMicroTask* entry, MutableHandleObject incumbentGlobal,
    MutableHandleObject optionalHostDefinedData);
JS_PUBLIC_API bool MaybeGetAllocationSiteFromJSMicroTask(
    JSMicroTask* entry, MutableHandleObject out);

JS_PUBLIC_API JSObject* MaybeGetPromiseFromJSMicroTask(JSMicroTask* entry);

// Get the flow ID from a JS microtask for profiler markers.
// This only returns false if entry has become a dead wrapper,
// in which case the microtask doesn't run anyhow.
JS_PUBLIC_API bool GetFlowIdFromJSMicroTask(JSMicroTask* entry, uint64_t* uid);

}  // namespace JS

#endif /* js_friend_MicroTask_h */
