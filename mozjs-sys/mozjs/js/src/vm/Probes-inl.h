/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef vm_Probes_inl_h
#define vm_Probes_inl_h

#include "vm/Probes.h"

#include "vm/JSContext.h"
#include "vm/JSScript.h"

namespace js {

/*
 * Many probe handlers are implemented inline for minimal performance impact,
 * especially important when no backends are enabled.
 */

inline bool probes::EnterScript(JSContext* cx, JSScript* script,
                                JSFunction* maybeFun, InterpreterFrame* fp) {
  JSRuntime* rt = cx->runtime();
  if (rt->geckoProfiler().enabled()) {
    if (!cx->geckoProfiler().enter(cx, script)) {
      return false;
    }
    MOZ_ASSERT(!fp->hasPushedGeckoProfilerFrame());
    fp->setPushedGeckoProfilerFrame();
  }

  return true;
}

inline void probes::ExitScript(JSContext* cx, JSScript* script,
                               JSFunction* maybeFun, bool popProfilerFrame) {
  if (popProfilerFrame) {
    cx->geckoProfiler().exit(cx, script);
  }
}

} /* namespace js */

#endif /* vm_Probes_inl_h */
