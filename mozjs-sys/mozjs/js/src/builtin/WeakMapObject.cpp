/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "builtin/WeakMapObject-inl.h"

#include "builtin/WeakSetObject.h"
#include "gc/GC.h"
#include "gc/GCContext.h"
#include "jit/InlinableNatives.h"
#include "js/friend/ErrorMessages.h"  // JSMSG_*
#include "js/PropertySpec.h"
#include "js/WeakMap.h"
#include "vm/Compartment.h"
#include "vm/JSContext.h"
#include "vm/SelfHosting.h"

#include "builtin/MapObject-inl.h"
#include "gc/GCContext-inl.h"
#include "gc/WeakMap-inl.h"
#include "vm/NativeObject-inl.h"

using namespace js;

/* static */ MOZ_ALWAYS_INLINE bool WeakMapObject::is(HandleValue v) {
  return v.isObject() && v.toObject().is<WeakMapObject>();
}

/* static */ MOZ_ALWAYS_INLINE bool WeakMapObject::has_impl(
    JSContext* cx, const CallArgs& args) {
  MOZ_ASSERT(is(args.thisv()));

  if (!CanBeHeldWeakly(args.get(0))) {
    args.rval().setBoolean(false);
    return true;
  }

  if (Map* map = args.thisv().toObject().as<WeakMapObject>().getMap()) {
    Value key = args[0];
    if (map->has(key)) {
      args.rval().setBoolean(true);
      return true;
    }
  }

  args.rval().setBoolean(false);
  return true;
}

/* static */
bool WeakMapObject::has(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  return CallNonGenericMethod<WeakMapObject::is, WeakMapObject::has_impl>(cx,
                                                                          args);
}

// static
bool WeakMapObject::hasObject(WeakMapObject* weakMap, JSObject* obj) {
  AutoUnsafeCallWithABI unsafe;
  Map* map = weakMap->getMap();
  return map && map->has(ObjectValue(*obj));
}

/* static */ MOZ_ALWAYS_INLINE bool WeakMapObject::get_impl(
    JSContext* cx, const CallArgs& args) {
  MOZ_ASSERT(WeakMapObject::is(args.thisv()));

  if (!CanBeHeldWeakly(args.get(0))) {
    args.rval().setUndefined();
    return true;
  }

  if (Map* map = args.thisv().toObject().as<WeakMapObject>().getMap()) {
    Value key = args[0];
    if (Map::Ptr ptr = map->lookup(key)) {
      args.rval().set(ptr->value());
      return true;
    }
  }

  args.rval().setUndefined();
  return true;
}

/* static */
bool WeakMapObject::get(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  return CallNonGenericMethod<WeakMapObject::is, WeakMapObject::get_impl>(cx,
                                                                          args);
}

// static
void WeakMapObject::getObject(WeakMapObject* weakMap, JSObject* obj,
                              Value* result) {
  AutoUnsafeCallWithABI unsafe;
  if (Map* map = weakMap->getMap()) {
    if (Map::Ptr ptr = map->lookup(ObjectValue(*obj))) {
      *result = ptr->value();
      return;
    }
  }
  *result = UndefinedValue();
}

/* static */ MOZ_ALWAYS_INLINE bool WeakMapObject::delete_impl(
    JSContext* cx, const CallArgs& args) {
  MOZ_ASSERT(WeakMapObject::is(args.thisv()));

  if (!CanBeHeldWeakly(args.get(0))) {
    args.rval().setBoolean(false);
    return true;
  }

  if (Map* map = args.thisv().toObject().as<WeakMapObject>().getMap()) {
    Value key = args[0];
    // The lookup here is only used for the removal, so we can skip the read
    // barrier. This is not very important for performance, but makes it easier
    // to test nonbarriered removal from internal weakmaps (eg Debugger maps.)
    if (Map::Ptr ptr = map->lookupUnbarriered(key)) {
      map->remove(ptr);
      args.rval().setBoolean(true);
      return true;
    }
  }

  args.rval().setBoolean(false);
  return true;
}

/* static */
bool WeakMapObject::delete_(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  return CallNonGenericMethod<WeakMapObject::is, WeakMapObject::delete_impl>(
      cx, args);
}

static bool EnsureValidWeakMapKey(JSContext* cx, Handle<Value> keyVal) {
  if (MOZ_UNLIKELY(!CanBeHeldWeakly(keyVal))) {
    unsigned errorNum = GetErrorNumber(true);
    ReportValueError(cx, errorNum, JSDVG_IGNORE_STACK, keyVal, nullptr);
    return false;
  }
  return true;
}

static bool SetWeakMapEntryImpl(JSContext* cx, Handle<WeakMapObject*> mapObj,
                                Handle<Value> keyVal, Handle<Value> value) {
  if (!EnsureValidWeakMapKey(cx, keyVal)) {
    return false;
  }
  return WeakCollectionPutEntryInternal(cx, mapObj, keyVal, value);
}

/* static */ MOZ_ALWAYS_INLINE bool WeakMapObject::set_impl(
    JSContext* cx, const CallArgs& args) {
  MOZ_ASSERT(WeakMapObject::is(args.thisv()));

  Rooted<WeakMapObject*> map(cx, &args.thisv().toObject().as<WeakMapObject>());
  if (!SetWeakMapEntryImpl(cx, map, args.get(0), args.get(1))) {
    return false;
  }

  args.rval().set(args.thisv());
  return true;
}

/* static */
bool WeakMapObject::set(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  return CallNonGenericMethod<WeakMapObject::is, WeakMapObject::set_impl>(cx,
                                                                          args);
}

static bool GetOrAddWeakMapEntry(JSContext* cx, Handle<WeakMapObject*> mapObj,
                                 Handle<Value> key, Handle<Value> value,
                                 MutableHandleValue rval) {
  if (!EnsureValidWeakMapKey(cx, key)) {
    return false;
  }

  if (!EnsureObjectHasWeakMap(cx, mapObj)) {
    return false;
  }

  WeakCollectionObject::Map* map = mapObj->getMap();
  auto addPtr = map->lookupForAdd(key);
  if (!addPtr) {
    if (!PreserveReflectorAndAssertValidEntry(cx, mapObj, key, value)) {
      return false;
    }
    if (!map->add(addPtr, key, value)) {
      JS_ReportOutOfMemory(cx);
      return false;
    }
  }
  rval.set(addPtr->value());
  return true;
}

/* static */ MOZ_ALWAYS_INLINE bool WeakMapObject::getOrInsert_impl(
    JSContext* cx, const CallArgs& args) {
  MOZ_ASSERT(WeakMapObject::is(args.thisv()));

  Rooted<WeakMapObject*> map(cx, &args.thisv().toObject().as<WeakMapObject>());
  return GetOrAddWeakMapEntry(cx, map, args.get(0), args.get(1), args.rval());
}

/* static */
bool WeakMapObject::getOrInsert(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  return CallNonGenericMethod<WeakMapObject::is,
                              WeakMapObject::getOrInsert_impl>(cx, args);
}

size_t WeakCollectionObject::sizeOfExcludingThis(
    mozilla::MallocSizeOf mallocSizeOf) {
  Map* map = getMap();
  if (!map) {
    return 0;
  }

  return gc::GetAllocSize(zone(), map) +
         map->shallowSizeOfExcludingThis(mallocSizeOf);
}

size_t WeakCollectionObject::nondeterministicGetSize() {
  Map* map = getMap();
  if (!map) {
    return 0;
  }

  return map->count();
}

bool WeakCollectionObject::nondeterministicGetKeys(
    JSContext* cx, Handle<WeakCollectionObject*> obj, MutableHandleObject ret) {
  RootedObject arr(cx, NewDenseEmptyArray(cx));
  if (!arr) {
    return false;
  }
  if (Map* map = obj->getMap()) {
    // Prevent GC from mutating the weakmap while iterating.
    gc::AutoSuppressGC suppress(cx);
    for (auto iter = map->iter(); !iter.done(); iter.next()) {
      const auto& key = iter.get().key();
      MOZ_ASSERT(key.isObject() || key.isSymbol());
      JS::ExposeValueToActiveJS(key);
      RootedValue keyVal(cx, key);
      if (!cx->compartment()->wrap(cx, &keyVal)) {
        return false;
      }
      if (!NewbornArrayPush(cx, arr, keyVal)) {
        return false;
      }
    }
  }
  ret.set(arr);
  return true;
}

JS_PUBLIC_API bool JS_NondeterministicGetWeakMapKeys(JSContext* cx,
                                                     HandleObject objArg,
                                                     MutableHandleObject ret) {
  RootedObject obj(cx, UncheckedUnwrap(objArg));
  if (!obj || !obj->is<WeakMapObject>()) {
    ret.set(nullptr);
    return true;
  }
  return WeakCollectionObject::nondeterministicGetKeys(
      cx, obj.as<WeakCollectionObject>(), ret);
}

/* static */
void WeakCollectionObject::trace(JSTracer* trc, JSObject* obj) {
  auto* collection = &obj->as<WeakCollectionObject>();
  TraceBufferSlot(trc, collection, WeakCollectionObject::DataSlot,
                  "WeakMapObject weak map");
  if (Map* map = collection->getMap()) {
    map->trace(trc);
  }
}

JS_PUBLIC_API JSObject* JS::NewWeakMapObject(JSContext* cx) {
  JSObject* obj = NewTenuredBuiltinClassInstance<WeakMapObject>(cx);
  MOZ_ASSERT_IF(obj, obj->isTenured());
  return obj;
}

JS_PUBLIC_API bool JS::IsWeakMapObject(JSObject* obj) {
  return obj->is<WeakMapObject>();
}

JS_PUBLIC_API bool JS::GetWeakMapEntry(JSContext* cx, HandleObject mapObj,
                                       HandleValue key,
                                       MutableHandleValue rval) {
  CHECK_THREAD(cx);
  cx->check(key);
  rval.setUndefined();

  if (!CanBeHeldWeakly(key)) {
    return true;
  }

  WeakMapObject::Map* map = mapObj->as<WeakMapObject>().getMap();
  if (!map) {
    return true;
  }

  if (auto ptr = map->lookup(key)) {
    rval.set(ptr->value());
  }
  return true;
}

JS_PUBLIC_API bool JS::SetWeakMapEntry(JSContext* cx, HandleObject mapObj,
                                       HandleValue key, HandleValue val) {
  CHECK_THREAD(cx);
  cx->check(key, val);
  return SetWeakMapEntryImpl(cx, mapObj.as<WeakMapObject>(), key, val);
}

// static
bool WeakMapObject::tryOptimizeCtorWithIterable(JSContext* cx,
                                                Handle<WeakMapObject*> obj,
                                                Handle<Value> iterableVal,
                                                bool* optimized) {
  MOZ_ASSERT(!iterableVal.isNullOrUndefined());
  MOZ_ASSERT(!*optimized);

  if (!CanOptimizeMapOrSetCtorWithIterable<JSProto_WeakMap>(WeakMapObject::set,
                                                            obj, cx)) {
    return true;
  }

  if (!iterableVal.isObject()) {
    return true;
  }
  JSObject* iterable = &iterableVal.toObject();

  // Fast path for `new WeakMap(array)`.
  if (IsOptimizableArrayForMapOrSetCtor<MapOrSet::Map>(iterable, cx)) {
    RootedValue keyVal(cx);
    RootedValue value(cx);
    Rooted<ArrayObject*> array(cx, &iterable->as<ArrayObject>());
    uint32_t len = array->getDenseInitializedLength();

    for (uint32_t index = 0; index < len; index++) {
      Value element = array->getDenseElement(index);
      MOZ_ASSERT(IsPackedArray(&element.toObject()));

      auto* elementArray = &element.toObject().as<ArrayObject>();
      keyVal.set(elementArray->getDenseElement(0));
      value.set(elementArray->getDenseElement(1));
      MOZ_ASSERT(!keyVal.isMagic(JS_ELEMENTS_HOLE));
      MOZ_ASSERT(!value.isMagic(JS_ELEMENTS_HOLE));

      if (!SetWeakMapEntryImpl(cx, obj, keyVal, value)) {
        return false;
      }
    }

    *optimized = true;
    return true;
  }

  return true;
}

/* static */
bool WeakMapObject::construct(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  // ES6 draft rev 31 (15 Jan 2015) 23.3.1.1 step 1.
  if (!ThrowIfNotConstructing(cx, args, "WeakMap")) {
    return false;
  }

  RootedObject proto(cx);
  if (!GetPrototypeFromBuiltinConstructor(cx, args, JSProto_WeakMap, &proto)) {
    return false;
  }

  Rooted<WeakMapObject*> obj(cx, NewObjectWithClassProtoAndKind<WeakMapObject>(
                                     cx, proto, TenuredObject));
  if (!obj) {
    return false;
  }

  MOZ_ASSERT(obj->isTenured());

  // Steps 5-6, 11.
  if (!args.get(0).isNullOrUndefined()) {
    Handle<Value> iterable = args[0];
    bool optimized = false;
    if (!tryOptimizeCtorWithIterable(cx, obj, iterable, &optimized)) {
      return false;
    }
    if (!optimized) {
      FixedInvokeArgs<1> args2(cx);
      args2[0].set(iterable);

      RootedValue thisv(cx, ObjectValue(*obj));
      if (!CallSelfHostedFunction(cx, cx->names().WeakMapConstructorInit, thisv,
                                  args2, args2.rval())) {
        return false;
      }
    }
  }

  args.rval().setObject(*obj);
  return true;
}

const JSClassOps WeakCollectionObject::classOps_ = {
    .trace = &trace,
};

const ClassSpec WeakMapObject::classSpec_ = {
    GenericCreateConstructor<WeakMapObject::construct, 0,
                             gc::AllocKind::FUNCTION>,
    GenericCreatePrototype<WeakMapObject>,
    nullptr,
    nullptr,
    WeakMapObject::methods,
    WeakMapObject::properties,
    GenericFinishInit<WhichHasRealmFuseProperty::Proto>,
};

const JSClass WeakMapObject::class_ = {
    "WeakMap",
    JSCLASS_HAS_RESERVED_SLOTS(SlotCount) |
        JSCLASS_HAS_CACHED_PROTO(JSProto_WeakMap),
    &WeakCollectionObject::classOps_,
    &WeakMapObject::classSpec_,
};

const JSClass WeakMapObject::protoClass_ = {
    "WeakMap.prototype",
    JSCLASS_HAS_CACHED_PROTO(JSProto_WeakMap),
    JS_NULL_CLASS_OPS,
    &WeakMapObject::classSpec_,
};

const JSPropertySpec WeakMapObject::properties[] = {
    JS_STRING_SYM_PS(toStringTag, "WeakMap", JSPROP_READONLY),
    JS_PS_END,
};

const JSFunctionSpec WeakMapObject::methods[] = {
    JS_INLINABLE_FN("has", has, 1, 0, WeakMapHas),
    JS_INLINABLE_FN("get", get, 1, 0, WeakMapGet),
    JS_FN("delete", delete_, 1, 0),
    JS_FN("set", set, 2, 0),
    JS_FN("getOrInsert", getOrInsert, 2, 0),
    JS_SELF_HOSTED_FN("getOrInsertComputed", "WeakMapGetOrInsertComputed", 2,
                      0),
    JS_FS_END,
};
