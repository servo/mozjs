/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef js_Exception_h
#define js_Exception_h

#include "mozilla/Attributes.h"
#include "mozilla/Maybe.h"

#include "jstypes.h"

#include "js/RootingAPI.h"  // JS::{Handle,Rooted}
#include "js/TypeDecls.h"
#include "js/Value.h"  // JS::Value, JS::Handle<JS::Value>

class JSErrorReport;

namespace JS {
enum class ExceptionStackBehavior : bool {
  // Do not capture any stack.
  DoNotCapture,

  // Capture the current JS stack when setting the exception. It may be
  // retrieved by JS::GetPendingExceptionStack.
  Capture
};

// Represents a |JSErrorReport*| borrowed from an ErrorObject. The object root
// ensures the error report won't be freed in the scope of this class.
//
// Typical usage:
//
//   BorrowedErrorReport report(cx);
//   if (JS_ErrorFromException(cx, obj, report)) {
//     // ... Use report->exnType, report.get(), etc.
//   }
class MOZ_RAII BorrowedErrorReport {
  Rooted<JSObject*> owner_;
  JSErrorReport* report_ = nullptr;

 public:
  explicit BorrowedErrorReport(JSContext* cx) : owner_(cx) {}

  void init(JSObject* owner, JSErrorReport* report) {
    MOZ_ASSERT(owner);
    MOZ_ASSERT(report);
    owner_ = owner;
    report_ = report;
  }

  JSErrorReport* get() const {
    MOZ_ASSERT(report_);
    return report_;
  }
  const JSErrorReport* operator->() const { return get(); }
};

}  // namespace JS

extern JS_PUBLIC_API bool JS_IsExceptionPending(JSContext* cx);

// [SMDOC] Out Of Memory (OOM) Handling
//
// Many functions in SpiderMonkey can throw exceptions, and sometimes
// the exception thrown is out of memory. Unlike other exceptions,
// this is not an object, but rather the literal string "out of memory".
//
// **Out of Memory handling in SpiderMonkey is best-effort!**
//
// While the developers of SpiderMonkey do attempt to convert various scenarios
// into OutOfMemory calls, such that embedders can attempt to do some sort of
// recovery, we do not guarantee this in all cases.
//
// There are some places where attempting to convey OOM is challenging
// or where it would leave the engine in a state with invariants
// no longer holding. In those cases **the process will crash**.
//
// An example, though not comprehensive, signal of this to a curious
// reader would be AutoEnterOOMUnsafeRegion, which flags various
// places developers have indicated that crashing is better than
// throwing OOM.
//
// Currently we endeavour to always throw out-of-memory when we
// encounter GC heap limits. We also will sometimes throw OOM
// exceptions for things which are not really OOM: For example
// our executable code limits.
//
// It is important to not rely on OOM generation in SpiderMonkey
// as your only reliability measure, as it is not guaranteed.

// Check for pending out of memory exception.
extern JS_PUBLIC_API bool JS_IsThrowingOutOfMemory(JSContext* cx);

// Try and get the pending exception. This can return false
// if there is no pending exception -or- if there is a problem
// attempting to produce the exception (for example if wrapping
// the exception fails.)
extern JS_PUBLIC_API bool JS_GetPendingException(JSContext* cx,
                                                 JS::MutableHandleValue vp);

extern JS_PUBLIC_API void JS_SetPendingException(
    JSContext* cx, JS::HandleValue v,
    JS::ExceptionStackBehavior behavior = JS::ExceptionStackBehavior::Capture);

extern JS_PUBLIC_API void JS_ClearPendingException(JSContext* cx);

/**
 * If the given object is an exception object, the exception will have (or be
 * able to lazily create) an error report struct, and this function will
 * populate |errorReport| with it and return true. Otherwise, returns false.
 *
 * See |BorrowedErrorReport| for a usage example.
 */
extern JS_PUBLIC_API bool JS_ErrorFromException(
    JSContext* cx, JS::HandleObject obj, JS::BorrowedErrorReport& errorReport);

namespace JS {

// When propagating an exception up the call stack, we store the underlying
// reason on the JSContext as one of the following enum values.
//
// TODO: Track uncatchable exceptions explicitly.
enum class ExceptionStatus {
  // No exception status.
  None,

  // Used by debugger when forcing an early return from a frame. This uses
  // exception machinery, but at the right time is turned back into a normal
  // non-error completion.
  ForcedReturn,

  // Throwing a (catchable) exception. Certain well-known exceptions are
  // explicitly tracked for convenience.
  Throwing,
  OutOfMemory,
  OverRecursed,
};

// Returns true if the status is a catchable exception. Formerly this was
// indicated by the `JSContext::throwing` flag.
static MOZ_ALWAYS_INLINE bool IsCatchableExceptionStatus(
    ExceptionStatus status) {
  return status >= ExceptionStatus::Throwing;
}

// This class encapsulates a (pending) exception and the corresponding optional
// SavedFrame stack object captured when the pending exception was set
// on the JSContext. This fuzzily correlates with a `throw` statement in JS,
// although arbitrary JSAPI consumers or VM code may also set pending exceptions
// via `JS_SetPendingException`.
//
// This is not the same stack as `e.stack` when `e` is an `Error` object.
// (That would be JS::ExceptionStackOrNull).
class MOZ_STACK_CLASS ExceptionStack {
  Rooted<Value> exception_;
  Rooted<JSObject*> stack_;

  friend JS_PUBLIC_API bool GetPendingExceptionStack(
      JSContext* cx, JS::ExceptionStack* exceptionStack);

  void init(HandleValue exception, HandleObject stack) {
    exception_ = exception;
    stack_ = stack;
  }

 public:
  explicit ExceptionStack(JSContext* cx) : exception_(cx), stack_(cx) {}

  ExceptionStack(JSContext* cx, HandleValue exception, HandleObject stack)
      : exception_(cx, exception), stack_(cx, stack) {}

  HandleValue exception() const { return exception_; }

  // |stack| can be null.
  HandleObject stack() const { return stack_; }
};

/**
 * Save and later restore the current exception state of a given JSContext.
 * This is useful for implementing behavior in C++ that's like try/catch
 * or try/finally in JS.
 *
 * Typical usage:
 *
 *     bool ok = JS::Evaluate(cx, ...);
 *     AutoSaveExceptionState savedExc(cx);
 *     ... cleanup that might re-enter JS ...
 *     return ok;
 */
class JS_PUBLIC_API AutoSaveExceptionState {
 private:
  JSContext* context;
  ExceptionStatus status;
  RootedValue exceptionValue;
  RootedObject exceptionStack;

 public:
  /*
   * Take a snapshot of cx's current exception state. Then clear any current
   * pending exception in cx.
   */
  explicit AutoSaveExceptionState(JSContext* cx);

  /*
   * If neither drop() nor restore() was called, restore the exception
   * state only if no exception is currently pending on cx.
   */
  ~AutoSaveExceptionState();

  /*
   * Discard any stored exception state.
   * If this is called, the destructor is a no-op.
   */
  void drop();

  /*
   * Replace cx's exception state with the stored exception state. Then
   * discard the stored exception state. If this is called, the
   * destructor is a no-op.
   */
  void restore();
};

// Get the current pending exception value and stack.
// This function asserts that there is a pending exception.
// If this function returns false, then retrieving the current pending exception
// failed and might have been overwritten by a new exception.
extern JS_PUBLIC_API bool GetPendingExceptionStack(
    JSContext* cx, JS::ExceptionStack* exceptionStack);

// Similar to GetPendingExceptionStack, but also clears the current
// pending exception.
extern JS_PUBLIC_API bool StealPendingExceptionStack(
    JSContext* cx, JS::ExceptionStack* exceptionStack);

// Set both the exception value and its associated stack on the context as
// the current pending exception.
extern JS_PUBLIC_API void SetPendingExceptionStack(
    JSContext* cx, const JS::ExceptionStack& exceptionStack);

/**
 * If the given object is an exception object (or an unwrappable
 * cross-compartment wrapper for one), return the stack for that exception, if
 * any.  Will return null if the given object is not an exception object
 * (including if it's null or a security wrapper that can't be unwrapped) or if
 * the exception has no stack.
 */
extern JS_PUBLIC_API JSObject* ExceptionStackOrNull(JS::HandleObject obj);

/**
 * If the given object is an exception object, return the error cause for that
 * exception, if any, or mozilla::Nothing.
 */
extern JS_PUBLIC_API mozilla::Maybe<JS::Value> GetExceptionCause(JSObject* exc);

}  // namespace JS

#endif  // js_Exception_h
