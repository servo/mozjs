/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "jsapi.h"
#include "jspubtd.h"
#include "js/Class.h"
#include "js/GCAPI.h"
#include "js/MemoryFunctions.h"
#include "js/SliceBudget.h"
#include "js/TypeDecls.h"

#include "jsapi-tests/tests.h"

static const JS::MemoryUse TestUse1 = JS::MemoryUse::XPCWrappedNative;
static const JS::MemoryUse TestUse2 = JS::MemoryUse::DOMBinding;

BEGIN_TEST(testMemoryAssociation) {
  JS::RootedObject obj(cx, JS_NewPlainObject(cx));
  CHECK(obj);

  // No association is allowed for nursery objects.
  JS_GC(cx);

  // Test object/memory association.
  JS::AddAssociatedMemory(obj, 100, TestUse1);
  JS::RemoveAssociatedMemory(obj, 100, TestUse1);

  // Test association when object moves due to compacting GC.
  void* initial = obj;
  JS::AddAssociatedMemory(obj, 300, TestUse1);
  JS::PrepareForFullGC(cx);
  JS::NonIncrementalGC(cx, JS::GCOptions::Shrink, JS::GCReason::DEBUG_GC);
  CHECK(obj != initial);
  JS::RemoveAssociatedMemory(obj, 300, TestUse1);

  // Test multiple associations.
  JS::AddAssociatedMemory(obj, 400, TestUse1);
  JS::AddAssociatedMemory(obj, 500, TestUse2);
  JS::RemoveAssociatedMemory(obj, 400, TestUse1);
  JS::RemoveAssociatedMemory(obj, 500, TestUse2);

  return true;
}
END_TEST(testMemoryAssociation)

static bool finalizerWasCalled = false;

static void BackgroundFinalizedClass_finalize(JS::GCContext*, JSObject* obj) {
  JS::RemoveAssociatedMemory(obj, 1024, TestUse1);
  MOZ_ASSERT(!js::CurrentThreadIsMainThread());
  finalizerWasCalled = true;
}

static const JSClassOps BackgroundFinalizedClassOps = {
    .finalize = &BackgroundFinalizedClass_finalize,
};

static const JSClass BackgroundFinalizedClass = {"BackgroundFinalizedClass",
                                                 JSCLASS_BACKGROUND_FINALIZE,
                                                 &BackgroundFinalizedClassOps};

BEGIN_TEST(testMemoryAssociation_BackgroundFinalized) {
  AutoLeaveZeal az{cx};
  AutoGCParameter param1{cx, JSGC_INCREMENTAL_GC_ENABLED, true};

  JSObject* obj = JS_NewObject(cx, &BackgroundFinalizedClass);
  CHECK(obj);

  JS::AddAssociatedMemory(obj, 1024, TestUse1);

  JS::PrepareForFullGC(cx);
  JS::SliceBudget budget{JS::TimeBudget{1000000}};
  JS::StartIncrementalGC(cx, JS::GCOptions::Normal, JS::GCReason::API, budget);
  while (JS::IsIncrementalGCInProgress(cx)) {
    JS::IncrementalGCSlice(cx, JS::GCReason::API, budget);
  }

  CHECK(finalizerWasCalled);
  return true;
}

END_TEST(testMemoryAssociation_BackgroundFinalized)
