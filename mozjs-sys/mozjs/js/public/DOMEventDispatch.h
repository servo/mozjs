/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef js_DOMEventDispatch_h
#define js_DOMEventDispatch_h

#include "js/TypeDecls.h"

namespace JS {

/**
 * Callback type for DOM event dispatching from SpiderMonkey.
 *
 * Current events dispatched include JIT compilation steps during instantiation:
 * - "omt_eager_baseline_function" when functions are queued for compilation
 * - "omt_eager_baseline_dispatch" when compilation batches are sent
 *
 * Function names are appended when available (e.g.,
 * "omt_eager_baseline_function: myFunc").
 * Anonymous functions appear without names.
 */
typedef void (*DispatchDOMEventCallback)(JSContext* cx, const char* eventType);

/**
 * Set the DOM event dispatch callback for embedders.
 * Allows embedders to observe internal SpiderMonkey operations for
 * testing/debugging. Pass nullptr to clear the callback.
 */
extern JS_PUBLIC_API void SetDispatchDOMEventCallback(
    JSContext* cx, DispatchDOMEventCallback callback);

} /* namespace JS */

namespace js {

/**
 * Internal function to dispatch DOM events for testing.
 * Calls the registered DispatchDOMEventCallback if available.
 */
extern void TestingDispatchDOMEvent(JSContext* cx, const char* eventType);

/**
 * Internal function to dispatch DOM events with optional function information.
 *
 * Behavior:
 * - If script is nullptr or lacks function: dispatches basic eventType
 * - If function name extraction succeeds: dispatches "eventType: functionName"
 * - If function name extraction fails: does nothing and returns.
 */
extern void TestingDispatchDOMEvent(JSContext* cx, const char* eventType,
                                    JSScript* script);

} /* namespace js */

/**
 * Convenience macro for internal testing event dispatch.
 * Supports both basic form and optional script parameter for function names.
 */
#define TRACE_FOR_TEST_DOM(cx, str, ...)                 \
  do {                                                   \
    js::TestingDispatchDOMEvent(cx, str, ##__VA_ARGS__); \
  } while (0)

#endif /* js_DOMEventDispatch_h */
