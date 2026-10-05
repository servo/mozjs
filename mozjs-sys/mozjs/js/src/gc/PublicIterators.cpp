/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gc/PublicIterators.h"

#include "gc/GCInternals.h"
#include "gc/GCLock.h"
#include "vm/Realm.h"
#include "vm/Runtime.h"

#include "gc/GC-inl.h"
#include "gc/PrivateIterators-inl.h"

using namespace js;
using namespace js::gc;

static void IterateRealmsArenasCellsUnbarriered(
    JSContext* cx, Zone* zone, void* data,
    JS::IterateRealmCallback realmCallback, IterateArenaCallback arenaCallback,
    IterateCellCallback cellCallback, const JS::AutoRequireNoGC& nogc) {
  {
    Rooted<Realm*> realm(cx);
    for (RealmsInZoneIter r(zone); !r.done(); r.next()) {
      realm = r;
      (*realmCallback)(cx, data, realm, nogc);
    }
  }

  for (auto thingKind : AllAllocKinds()) {
    JS::TraceKind traceKind = MapAllocToTraceKind(thingKind);
    size_t thingSize = Arena::thingSize(thingKind);

    for (ArenaIter aiter(zone, thingKind); !aiter.done(); aiter.next()) {
      Arena* arena = aiter.get();
      (*arenaCallback)(cx->runtime(), data, arena, traceKind, thingSize, nogc);
      for (ArenaCellIter cell(arena); !cell.done(); cell.next()) {
        (*cellCallback)(cx->runtime(), data, JS::GCCellPtr(cell, traceKind),
                        thingSize, nogc);
      }
    }
  }
}

void js::IterateHeapUnbarriered(JSContext* cx, void* data,
                                IterateZoneCallback zoneCallback,
                                JS::IterateRealmCallback realmCallback,
                                IterateArenaCallback arenaCallback,
                                IterateCellCallback cellCallback,
                                const js::gc::AutoTraceSession& session) {
  auto iterateZone = [&](Zone* zone) -> void {
    JS::AutoSuppressGCAnalysis callingSafeCallback(cx);
    (*zoneCallback)(cx->runtime(), data, zone, session);
    IterateRealmsArenasCellsUnbarriered(cx, zone, data, realmCallback,
                                        arenaCallback, cellCallback, session);
  };

  // Include the shared atoms zone if present.
  if (Zone* zone = cx->runtime()->gc.maybeSharedAtomsZone()) {
    iterateZone(zone);
  }

  for (ZonesIter zone(cx->runtime(), WithAtoms); !zone.done(); zone.next()) {
    iterateZone(zone);
  }
}

void js::IterateHeapUnbarrieredForZone(
    JSContext* cx, Zone* zone, void* data, IterateZoneCallback zoneCallback,
    JS::IterateRealmCallback realmCallback, IterateArenaCallback arenaCallback,
    IterateCellCallback cellCallback, const js::gc::AutoTraceSession& session) {
  JS::AutoSuppressGCAnalysis callingSafeCallback(cx);
  (*zoneCallback)(cx->runtime(), data, zone, session);
  IterateRealmsArenasCellsUnbarriered(cx, zone, data, realmCallback,
                                      arenaCallback, cellCallback, session);
}

void js::IterateChunks(JSContext* cx, void* data,
                       IterateChunkCallback chunkCallback,
                       const js::gc::AutoTraceSession& session) {
  AutoLockGC lock(cx->runtime());

  cx->runtime()->gc.forEachNonEmptyChunk(lock, [&](ArenaChunk* chunk) {
    JS::AutoSuppressGCAnalysis nogc(cx);
    chunkCallback(cx->runtime(), data, chunk, session);
  });
}

static void TraverseInnerLazyScriptsForLazyScript(
    JSContext* cx, void* data, BaseScript* enclosingScript,
    IterateScriptCallback lazyScriptCallback, const JS::AutoRequireNoGC& nogc) {
  for (JS::GCCellPtr gcThing : enclosingScript->gcthings()) {
    if (!gcThing.is<JSObject>()) {
      continue;
    }
    JSObject* obj = &gcThing.as<JSObject>();

    MOZ_ASSERT(obj->is<JSFunction>(),
               "All objects in lazy scripts should be functions");
    JSFunction* fun = &obj->as<JSFunction>();

    if (!fun->hasBaseScript()) {
      // Ignore asm.js.
      continue;
    }
    MOZ_ASSERT(fun->baseScript());
    if (!fun->baseScript()) {
      // If the function doesn't have script, ignore it.
      continue;
    }

    if (fun->hasBytecode()) {
      // Ignore non lazy function.
      continue;
    }

    // If the function is "ghost", we shouldn't expose it to the debugger.
    //
    // See GHOST_FUNCTION in FunctionFlags.h for more details.
    if (fun->isGhost()) {
      continue;
    }

    BaseScript* script = fun->baseScript();
    MOZ_ASSERT_IF(script->hasEnclosingScript(),
                  script->enclosingScript() == enclosingScript);

    {
      JS::AutoSuppressGCAnalysis callingSafeCallback(cx);
      lazyScriptCallback(cx->runtime(), data, script, nogc);
    }

    TraverseInnerLazyScriptsForLazyScript(cx, data, script, lazyScriptCallback,
                                          nogc);
  }
}

static inline void DoScriptCallback(JSContext* cx, void* data,
                                    BaseScript* script,
                                    IterateScriptCallback callback,
                                    const JS::AutoRequireNoGC& nogc) {
  // Exclude any scripts that may be the result of a failed compile. Check that
  // script either has bytecode or is ready to delazify.
  //
  // This excludes lazy scripts that do not have an enclosing scope because we
  // cannot distinguish a failed compile fragment from a lazy script with a lazy
  // parent.
  if (!script->hasBytecode() && !script->isReadyForDelazification()) {
    return;
  }

  // Invoke callback.
  {
    JS::AutoSuppressGCAnalysis callingSafeCallback(cx);
    callback(cx->runtime(), data, script, nogc);
  }

  // The check above excluded lazy scripts with lazy parents, so explicitly
  // visit inner scripts now if we are lazy with a successfully compiled parent.
  if (!script->hasBytecode()) {
    TraverseInnerLazyScriptsForLazyScript(cx, data, script, callback, nogc);
  }
}

void js::IterateScripts(JSContext* cx, Realm* realm, void* data,
                        IterateScriptCallback scriptCallback) {
  MOZ_ASSERT(!cx->suppressGC);
  AutoEmptyNurseryAndPrepareForTracing session(cx);

  if (realm) {
    Zone* zone = realm->zone();
    for (auto iter = zone->cellIter<BaseScript>(session); !iter.done();
         iter.next()) {
      if (iter->realm() != realm) {
        continue;
      }
      DoScriptCallback(cx, data, iter.get(), scriptCallback, session);
    }
  } else {
    for (ZonesIter zone(cx->runtime(), SkipAtoms); !zone.done(); zone.next()) {
      for (auto iter = zone->cellIter<BaseScript>(session); !iter.done();
           iter.next()) {
        DoScriptCallback(cx, data, iter.get(), scriptCallback, session);
      }
    }
  }
}

void js::IterateGrayObjects(Zone* zone, IterateGCThingCallback cellCallback,
                            void* data) {
  MOZ_ASSERT(!JS::RuntimeHeapIsBusy());

  JSContext* cx = TlsContext.get();
  AutoPrepareForTracing session(cx);
  JS::AutoSuppressGCAnalysis callingSafeCallback(cx);

  for (auto kind : ObjectAllocKinds()) {
    for (GrayObjectIter obj(zone, kind); !obj.done(); obj.next()) {
      if (obj->asTenured().isMarkedGray()) {
        cellCallback(data, JS::GCCellPtr(obj.get()), session);
      }
    }
  }
}

JS_PUBLIC_API void JS_IterateCompartments(
    JSContext* cx, void* data,
    JSIterateCompartmentCallback compartmentCallback) {
  AutoTraceSession session(cx->runtime());

  for (CompartmentsIter c(cx->runtime()); !c.done(); c.next()) {
    JS::AutoSuppressGCAnalysis callingSafeCallback(cx);
    if ((*compartmentCallback)(cx, data, c) ==
        JS::CompartmentIterResult::Stop) {
      break;
    }
  }
}

JS_PUBLIC_API void JS_IterateCompartmentsInZone(
    JSContext* cx, JS::Zone* zone, void* data,
    JSIterateCompartmentCallback compartmentCallback) {
  AutoTraceSession session(cx->runtime());

  for (CompartmentsInZoneIter c(zone); !c.done(); c.next()) {
    JS::AutoSuppressGCAnalysis callingSafeCallback(cx);
    if ((*compartmentCallback)(cx, data, c) ==
        JS::CompartmentIterResult::Stop) {
      break;
    }
  }
}

JS_PUBLIC_API void JS::IterateRealms(JSContext* cx, void* data,
                                     JS::IterateRealmCallback realmCallback) {
  AutoTraceSession session(cx->runtime());
  JS::AutoSuppressGCAnalysis nogc(cx);

  Rooted<Realm*> realm(cx);
  for (RealmsIter r(cx->runtime()); !r.done(); r.next()) {
    realm = r;
    (*realmCallback)(cx, data, realm, nogc);
  }
}

JS_PUBLIC_API void JS::IterateRealmsWithPrincipals(
    JSContext* cx, JSPrincipals* principals, void* data,
    JS::IterateRealmCallback realmCallback) {
  MOZ_ASSERT(principals);

  AutoTraceSession session(cx->runtime());
  JS::AutoSuppressGCAnalysis callingSafeCallback(cx);

  Rooted<Realm*> realm(cx);
  for (RealmsIter r(cx->runtime()); !r.done(); r.next()) {
    if (r->principals() != principals) {
      continue;
    }
    realm = r;
    (*realmCallback)(cx, data, realm, session);
  }
}

JS_PUBLIC_API void JS::IterateRealmsInCompartment(
    JSContext* cx, JS::Compartment* compartment, void* data,
    JS::IterateRealmCallback realmCallback) {
  AutoTraceSession session(cx->runtime());
  JS::AutoSuppressGCAnalysis callingSafeCallback(cx);

  Rooted<Realm*> realm(cx);
  for (RealmsInCompartmentIter r(compartment); !r.done(); r.next()) {
    realm = r;
    (*realmCallback)(cx, data, realm, session);
  }
}
