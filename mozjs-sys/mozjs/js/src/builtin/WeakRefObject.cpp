/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "builtin/WeakRefObject.h"

#include "jsapi.h"

#include "gc/FinalizationObservers.h"
#include "js/friend/ErrorMessages.h"  // js::GetErrorMessage, JSMSG_*
#include "vm/GlobalObject.h"
#include "vm/JSContext.h"

#include "gc/PrivateIterators-inl.h"
#include "gc/WeakMap-inl.h"
#include "vm/JSObject-inl.h"
#include "vm/NativeObject-inl.h"

using namespace js::gc;

namespace js {

/* static */
bool WeakRefObject::construct(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  // https://tc39.es/proposal-weakrefs/#sec-weak-ref-constructor
  // The WeakRef constructor is not intended to be called as a function and will
  // throw an exception when called in that manner.
  if (!ThrowIfNotConstructing(cx, args, "WeakRef")) {
    return false;
  }

  // https://tc39.es/proposal-weakrefs/#sec-weak-ref-target
  // 1. If NewTarget is undefined, throw a TypeError exception.
  // 2. If Type(target) is not Object, throw a TypeError exception.
  if (!CanBeHeldWeakly(args.get(0))) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_BAD_WEAKREF_TARGET);
    return false;
  }

  // 3. Let weakRef be ? OrdinaryCreateFromConstructor(NewTarget,
  //    "%WeakRefPrototype%", « [[Target]] »).
  RootedObject proto(cx);
  if (!GetPrototypeFromBuiltinConstructor(cx, args, JSProto_WeakRef, &proto)) {
    return false;
  }

  Rooted<WeakRefObject*> weakRef(
      cx, NewObjectWithClassProto<WeakRefObject>(cx, proto));
  if (!weakRef) {
    return false;
  }

  RootedValue target(cx, args[0]);
  bool isPermanent = false;
  if (target.isObject()) {
    // Fully unwrap the target to register it with the GC.
    RootedObject object(cx, CheckedUnwrapDynamic(&target.toObject(), cx));
    if (!object) {
      ReportAccessDenied(cx);
      return false;
    }

    target = ObjectValue(*object);

    // If the target is a DOM wrapper, preserve it.
    if (!preserveDOMWrapper(cx, object)) {
      return false;
    }
  } else {
    JS::Symbol* symbol = target.toSymbol();
    isPermanent = symbol->isPermanentAndMayBeShared();
  }

  // Skip the following steps for permanent targets.
  // (See the note following https://tc39.es/ecma262/#sec-canbeheldweakly)
  if (!isPermanent) {
    // 4. Perform AddToKeptObjects(target).
    if (!target.toGCThing()->zone()->addToKeptObjects(target)) {
      ReportOutOfMemory(cx);
      return false;
    };

    // Add an entry to the per-zone maps from target JS object to a list of weak
    // ref objects.
    gc::GCRuntime* gc = &cx->runtime()->gc;
    if (!gc->registerWeakRef(cx, target, weakRef)) {
      ReportOutOfMemory(cx);
      return false;
    }
  }

  // 5. Set weakRef.[[Target]] to target.
  weakRef->setReservedSlotGCThingAsPrivate(TargetSlot, target.toGCThing());

  // 6. Return weakRef.
  args.rval().setObject(*weakRef);

  return true;
}

/* static */
bool WeakRefObject::preserveDOMWrapper(JSContext* cx, HandleObject obj) {
  if (!MaybePreserveDOMWrapper(cx, obj)) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_BAD_WEAKREF_TARGET);
    return false;
  }

  return true;
}

/* static */
void WeakRefObject::trace(JSTracer* trc, JSObject* obj) {
  WeakRefObject* weakRef = &obj->as<WeakRefObject>();

  // The next and prev slots added by the ObserverListObject base class are
  // internal weak pointers and are not traced, even if requested by the tracer.

  if (trc->traceWeakEdges()) {
    Value target = weakRef->target();
    Value prior = target;
    TraceManuallyBarrieredEdge(trc, &target, "WeakRefObject::target");
    if (target != prior) {
      weakRef->setTargetUnbarriered(target);
    }
  }
}

/* static */
void WeakRefObject::finalize(JS::GCContext* gcx, JSObject* obj) {
  auto* weakRef = &obj->as<WeakRefObject>();
  weakRef->clearTargetAndUnlink();
}

const JSClassOps WeakRefObject::classOps_ = {
    .finalize = finalize,
    .trace = trace,
};

const ClassSpec WeakRefObject::classSpec_ = {
    GenericCreateConstructor<WeakRefObject::construct, 1,
                             gc::AllocKind::FUNCTION>,
    GenericCreatePrototype<WeakRefObject>,
    nullptr,
    nullptr,
    WeakRefObject::methods,
    WeakRefObject::properties,
};

const JSClass WeakRefObject::class_ = {
    "WeakRef",
    JSCLASS_HAS_RESERVED_SLOTS(SlotCount) |
        JSCLASS_HAS_CACHED_PROTO(JSProto_WeakRef) | JSCLASS_FOREGROUND_FINALIZE,
    &classOps_, &classSpec_, &classExtension_};

const JSClass WeakRefObject::protoClass_ = {
    // https://tc39.es/proposal-weakrefs/#sec-weak-ref.prototype
    // https://tc39.es/proposal-weakrefs/#sec-properties-of-the-weak-ref-prototype-object
    "WeakRef.prototype",
    JSCLASS_HAS_CACHED_PROTO(JSProto_WeakRef),
    JS_NULL_CLASS_OPS,
    &classSpec_,
};

const JSPropertySpec WeakRefObject::properties[] = {
    JS_STRING_SYM_PS(toStringTag, "WeakRef", JSPROP_READONLY),
    JS_PS_END,
};

const JSFunctionSpec WeakRefObject::methods[] = {
    JS_FN("deref", deref, 0, 0),
    JS_FS_END,
};

Value WeakRefObject::target() {
  Value value = getReservedSlot(TargetSlot);
  if (value.isUndefined()) {
    return UndefinedValue();
  }

  auto* cell = static_cast<Cell*>(value.toPrivate());
  if (cell->is<JSObject>()) {
    return ObjectValue(*cell->as<JSObject>());
  }

  return SymbolValue(cell->as<JS::Symbol>());
}

/* static */
bool WeakRefObject::deref(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  // https://tc39.es/proposal-weakrefs/#sec-weak-ref.prototype.deref
  // 1. Let weakRef be the this value.
  // 2. If Type(weakRef) is not Object, throw a TypeError exception.
  // 3. If weakRef does not have a [[Target]] internal slot, throw a TypeError
  //    exception.
  if (!args.thisv().isObject() ||
      !args.thisv().toObject().is<WeakRefObject>()) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_NOT_A_WEAK_REF,
                              "Receiver of WeakRef.deref call");
    return false;
  }

  Rooted<WeakRefObject*> weakRef(cx,
                                 &args.thisv().toObject().as<WeakRefObject>());

  // We need to perform a read barrier, which may clear the target.
  readBarrier(cx, weakRef);

  // 4. Let target be the value of weakRef.[[Target]].
  // 5. If target is not empty,
  //    a. Perform AddToKeptObjects(target).
  //    b. Return target.
  // 6. Return undefined.
  RootedValue target(cx, weakRef->target());
  if (target.isUndefined()) {
    args.rval().setUndefined();
    return true;
  }

  bool isPermanent =
      target.isSymbol() && target.toSymbol()->isPermanentAndMayBeShared();
  if (!isPermanent && !target.toGCThing()->zone()->addToKeptObjects(target)) {
    ReportOutOfMemory(cx);
    return false;
  }

  // Target should be wrapped into the current realm before returning it.
  if (!JS_WrapValue(cx, &target)) {
    return false;
  }

  args.rval().set(target);
  return true;
}

void WeakRefObject::setTarget(Value target) {
  setReservedSlotGCThingAsPrivate(TargetSlot, target.toGCThing());
}
void WeakRefObject::setTargetUnbarriered(Value target) {
  setReservedSlotGCThingAsPrivateUnbarriered(TargetSlot, target.toGCThing());
}

void WeakRefObject::clearTargetAndUnlink() {
  unlink();
  clearReservedSlotGCThingAsPrivate(TargetSlot);
}

/* static */
void WeakRefObject::readBarrier(JSContext* cx, Handle<WeakRefObject*> self) {
  RootedValue target(cx, self->target());
  if (target.isUndefined()) {
    return;
  }

  if (target.isObject() && target.toObject().getClass()->isDOMClass()) {
    // We preserved the target when the WeakRef was created. If it has since
    // been released then the DOM object it wraps has been collected, so clear
    // the target.
    RootedObject obj(cx, &target.toObject());

    // Ensure buffered wrapper preservations are committed because the DOM's
    // hasReleasedWrapperCallback checks the preserving-wrapper flag.
    cx->runtime()->commitPendingWrapperPreservations(obj->zone());

    MOZ_ASSERT(cx->runtime()->hasReleasedWrapperCallback);
    bool wasReleased = cx->runtime()->hasReleasedWrapperCallback(obj);
    if (wasReleased) {
      obj->zone()->finalizationObservers()->removeWeakRefTarget(target, self);
      return;
    }
  }

  gc::ValueReadBarrier(target);
}

namespace gc {

void GCRuntime::traceKeptObjects(JSTracer* trc) {
  for (GCZonesIter zone(this); !zone.done(); zone.next()) {
    zone->traceKeptObjects(trc);
  }
}

}  // namespace gc

}  // namespace js
