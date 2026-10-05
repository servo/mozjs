/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef vm_Probes_h
#define vm_Probes_h

#include "vm/JSObject.h"

namespace js {

class InterpreterFrame;

namespace probes {

/*
 * Static probes
 *
 * The probe points defined in this file are scattered around the SpiderMonkey
 * source tree. The presence of probes::SomeEvent() means that someEvent is
 * about to happen or has happened. To the extent possible, probes should be
 * inserted in all paths associated with a given event, regardless of the
 * active runmode (interpreter/traceJIT/methodJIT/ionJIT).
 *
 * When a probe fires, it is handled by any probe handling backends that have
 * been compiled in. By default, most probes do nothing or at least do nothing
 * expensive, so the presence of the probe should have negligible effect on
 * running time. (Probes in slow paths may do something by default, as long as
 * there is no noticeable slowdown.)
 *
 * For some probes, the mere existence of the probe is too expensive even if it
 * does nothing when called. For example, just having consistent information
 * available for a function call entry/exit probe causes the JITs to
 * de-optimize function calls. In those cases, the JITs may query at compile
 * time whether a probe is desired, and omit the probe invocation if not. If a
 * probe is runtime-disabled at compilation time, it is not guaranteed to fire
 * within a compiled function if it is later enabled.
 *
 * Not all backends handle all of the probes listed here.
 */

/*
 * Internal use only: remember whether "profiling", whatever that means, is
 * currently active. Used for state management.
 */
extern bool ProfilingActive;

/* Entering a JS function */
bool EnterScript(JSContext*, JSScript*, JSFunction*, InterpreterFrame*);

/* About to leave a JS function */
void ExitScript(JSContext*, JSScript*, JSFunction*, bool popProfilerFrame);

/*
 * Object has been created. |obj| must exist (its class and size are read)
 */
bool CreateObject(JSContext* cx, JSObject* obj);

/*
 * Object is about to be finalized. |obj| must still exist (its class is
 * read)
 */
bool FinalizeObject(JSObject* obj);
}  // namespace probes

inline bool probes::CreateObject(JSContext* cx, JSObject* obj) { return true; }

inline bool probes::FinalizeObject(JSObject* obj) { return true; }

} /* namespace js */

#endif /* vm_Probes_h */
