/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: set ts=8 sts=2 et sw=2 tw=80:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef js_WasmComponent_h
#define js_WasmComponent_h

#ifdef ENABLE_WASM_COMPONENTS

#  include "mozilla/RefPtr.h"  // RefPtr

#  include "jstypes.h"  // JS_PUBLIC_API

#  include "js/RefCounted.h"  // AtomicRefCounted
#  include "js/TypeDecls.h"   // HandleObject

namespace JS {

/**
 * TODO(wasm-cm): Leave a descriptive comment here :)
 * For comparison, see WasmModule.h.
 */

struct WasmComponent : js::AtomicRefCounted<WasmComponent> {
  virtual ~WasmComponent() = default;
  virtual JSObject* createObject(JSContext* cx) const = 0;
};

extern JS_PUBLIC_API bool IsWasmComponentObject(HandleObject obj);

extern JS_PUBLIC_API RefPtr<WasmComponent> GetWasmComponent(HandleObject obj);

}  // namespace JS

#endif /* ENABLE_WASM_COMPONENTS */

#endif /* js_WasmComponent_h */
