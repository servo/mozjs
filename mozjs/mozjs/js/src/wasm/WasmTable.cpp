/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: set ts=8 sts=2 et sw=2 tw=80:
 *
 * Copyright 2016 Mozilla Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "wasm/WasmTable.h"

#include "mozilla/CheckedInt.h"
#include "mozilla/PodOperations.h"

#include "vm/JSContext.h"
#include "vm/Realm.h"
#include "wasm/WasmInstance.h"
#include "wasm/WasmJS.h"

#include "wasm/WasmInstance-inl.h"

using namespace js;
using namespace js::wasm;
using mozilla::CheckedInt;
using mozilla::PodZero;

Table::Table(JSContext* cx, const TableDesc& desc,
             Handle<WasmTableObject*> maybeObject, FuncRefVector&& functions)
    : maybeObject_(maybeObject),
      observers_(cx->zone()),
      functions_(std::move(functions)),
      elemType_(desc.elemType),
      isAsmJS_(desc.isAsmJS),
      length_(desc.initialLength),
      maximum_(desc.maximumLength) {
  MOZ_ASSERT(repr() == TableRepr::Func);
}

Table::Table(JSContext* cx, const TableDesc& desc,
             Handle<WasmTableObject*> maybeObject, TableAnyRefVector&& objects)
    : maybeObject_(maybeObject),
      observers_(cx->zone()),
      objects_(std::move(objects)),
      elemType_(desc.elemType),
      isAsmJS_(desc.isAsmJS),
      length_(desc.initialLength),
      maximum_(desc.maximumLength) {
  MOZ_ASSERT(repr() == TableRepr::Ref);
}

/* static */
SharedTable Table::create(JSContext* cx, const TableDesc& desc,
                          Handle<WasmTableObject*> maybeObject) {
  // We don't support non-nullable references in tables yet.
  MOZ_RELEASE_ASSERT(desc.elemType.isNullable());

  switch (desc.elemType.tableRepr()) {
    case TableRepr::Func: {
      FuncRefVector functions;
      if (!functions.resize(desc.initialLength)) {
        ReportOutOfMemory(cx);
        return nullptr;
      }
      return SharedTable(
          cx->new_<Table>(cx, desc, maybeObject, std::move(functions)));
    }
    case TableRepr::Ref: {
      TableAnyRefVector objects;
      if (!objects.resize(desc.initialLength)) {
        ReportOutOfMemory(cx);
        return nullptr;
      }
      return SharedTable(
          cx->new_<Table>(cx, desc, maybeObject, std::move(objects)));
    }
  }
  MOZ_CRASH("switch is exhaustive");
}

void Table::tracePrivate(JSTracer* trc) {
  // If this table has a WasmTableObject, then this method is only called by
  // WasmTableObject's trace hook so maybeObject_ must already be marked.
  // TraceEdge is called so that the pointer can be updated during a moving
  // GC.
  TraceNullableEdge(trc, &maybeObject_, "wasm table object");

  switch (repr()) {
    case TableRepr::Func: {
      if (isAsmJS_) {
#ifdef DEBUG
        for (uint32_t i = 0; i < length_; i++) {
          MOZ_ASSERT(!functions_[i].instance);
        }
#endif
        break;
      }

      for (uint32_t i = 0; i < length_; i++) {
        if (functions_[i].instance) {
          functions_[i].instance->trace(trc);
        } else {
          MOZ_ASSERT(!functions_[i].code);
        }
      }
      break;
    }
    case TableRepr::Ref: {
      objects_.trace(trc);
      break;
    }
  }
}

void Table::trace(JSTracer* trc) {
  // The trace hook of WasmTableObject will call Table::tracePrivate at
  // which point we can mark the rest of the children. If there is no
  // WasmTableObject, call Table::tracePrivate directly. Redirecting through
  // the WasmTableObject avoids marking the entire Table on each incoming
  // edge (once per dependent Instance).
  if (maybeObject_) {
    TraceEdge(trc, &maybeObject_, "wasm table object");
  } else {
    tracePrivate(trc);
  }
}

uint8_t* Table::instanceElements() const {
  if (repr() == TableRepr::Ref) {
    return (uint8_t*)objects_.begin();
  }
  return (uint8_t*)functions_.begin();
}

const FunctionTableElem& Table::getFuncRef(uint32_t index) const {
  MOZ_ASSERT(isFunction());
  return functions_[index];
}

bool Table::getFuncRef(JSContext* cx, uint32_t index,
                       MutableHandleFunction fun) const {
  MOZ_ASSERT(isFunction());

  const FunctionTableElem& elem = getFuncRef(index);
  if (!elem.code) {
    fun.set(nullptr);
    return true;
  }

  Instance& instance = *elem.instance;
  const CodeRange& codeRange = *instance.code().lookupFuncRange(elem.code);

  Rooted<WasmInstanceObject*> instanceObj(cx, instance.object());
  return instanceObj->getExportedFunction(cx, instanceObj,
                                          codeRange.funcIndex(), fun);
}

void Table::setFuncRef(uint32_t index, void* code, Instance* instance) {
  MOZ_ASSERT(isFunction());

  FunctionTableElem& elem = functions_[index];
  if (elem.instance) {
    gc::PreWriteBarrier(elem.instance->objectUnbarriered());
  }

  if (!isAsmJS_) {
    elem.code = code;
    elem.instance = instance;
    MOZ_ASSERT(elem.instance->objectUnbarriered()->isTenured(),
               "no postWriteBarrier (Table::set)");
  } else {
    elem.code = code;
    elem.instance = nullptr;
  }
}

void Table::fillFuncRef(uint32_t index, uint32_t fillCount, FuncRef ref,
                        JSContext* cx) {
  MOZ_ASSERT(isFunction());

  if (ref.isNull()) {
    for (uint32_t i = index, end = index + fillCount; i != end; i++) {
      setNull(i);
    }
    return;
  }

  RootedFunction fun(cx, ref.asJSFunction());
  MOZ_RELEASE_ASSERT(IsWasmExportedFunction(fun));

  Rooted<WasmInstanceObject*> instanceObj(
      cx, ExportedFunctionToInstanceObject(fun));
  uint32_t funcIndex = ExportedFunctionToFuncIndex(fun);

#ifdef DEBUG
  RootedFunction f(cx);
  MOZ_ASSERT(instanceObj->getExportedFunction(cx, instanceObj, funcIndex, &f));
  MOZ_ASSERT(fun == f);
#endif

  Instance& instance = instanceObj->instance();
  Tier tier = instance.code().bestTier();
  const MetadataTier& metadata = instance.metadata(tier);
  const CodeRange& codeRange =
      metadata.codeRange(metadata.lookupFuncExport(funcIndex));
  void* code = instance.codeBase(tier) + codeRange.funcCheckedCallEntry();
  for (uint32_t i = index, end = index + fillCount; i != end; i++) {
    setFuncRef(i, code, &instance);
  }
}

AnyRef Table::getAnyRef(uint32_t index) const {
  MOZ_ASSERT(!isFunction());
  // TODO/AnyRef-boxing: With boxed immediates and strings, the write barrier
  // is going to have to be more complicated.
  ASSERT_ANYREF_IS_JSOBJECT;
  return AnyRef::fromJSObject(objects_[index]);
}

void Table::fillAnyRef(uint32_t index, uint32_t fillCount, AnyRef ref) {
  MOZ_ASSERT(!isFunction());
  // TODO/AnyRef-boxing: With boxed immediates and strings, the write barrier
  // is going to have to be more complicated.
  ASSERT_ANYREF_IS_JSOBJECT;
  for (uint32_t i = index, end = index + fillCount; i != end; i++) {
    objects_[i] = ref.asJSObject();
  }
}

bool Table::getValue(JSContext* cx, uint32_t index,
                     MutableHandleValue result) const {
  switch (repr()) {
    case TableRepr::Func: {
      MOZ_RELEASE_ASSERT(!isAsmJS());
      RootedFunction fun(cx);
      if (!getFuncRef(cx, index, &fun)) {
        return false;
      }
      result.setObjectOrNull(fun);
      return true;
    }
    case TableRepr::Ref: {
      if (!ValType(elemType_).isExposable()) {
        JS_ReportErrorNumberUTF8(cx, GetErrorMessage, nullptr,
                                 JSMSG_WASM_BAD_VAL_TYPE);
        return false;
      }
      return ToJSValue(cx, &objects_[index], ValType(elemType_), result);
    }
    default:
      MOZ_CRASH();
  }
}

void Table::setNull(uint32_t index) {
  switch (repr()) {
    case TableRepr::Func: {
      MOZ_RELEASE_ASSERT(!isAsmJS_);
      FunctionTableElem& elem = functions_[index];
      if (elem.instance) {
        gc::PreWriteBarrier(elem.instance->objectUnbarriered());
      }

      elem.code = nullptr;
      elem.instance = nullptr;
      break;
    }
    case TableRepr::Ref: {
      fillAnyRef(index, 1, AnyRef::null());
      break;
    }
  }
}

bool Table::copy(JSContext* cx, const Table& srcTable, uint32_t dstIndex,
                 uint32_t srcIndex) {
  MOZ_RELEASE_ASSERT(!srcTable.isAsmJS_);
  switch (repr()) {
    case TableRepr::Func: {
      MOZ_RELEASE_ASSERT(elemType().isFunc() && srcTable.elemType().isFunc());
      FunctionTableElem& dst = functions_[dstIndex];
      if (dst.instance) {
        gc::PreWriteBarrier(dst.instance->objectUnbarriered());
      }

      const FunctionTableElem& src = srcTable.functions_[srcIndex];
      dst.code = src.code;
      dst.instance = src.instance;

      if (dst.instance) {
        MOZ_ASSERT(dst.code);
        MOZ_ASSERT(dst.instance->objectUnbarriered()->isTenured(),
                   "no postWriteBarrier (Table::copy)");
      } else {
        MOZ_ASSERT(!dst.code);
      }
      break;
    }
    case TableRepr::Ref: {
      switch (srcTable.repr()) {
        case TableRepr::Ref: {
          fillAnyRef(dstIndex, 1, srcTable.getAnyRef(srcIndex));
          break;
        }
        case TableRepr::Func: {
          MOZ_RELEASE_ASSERT(srcTable.elemType().isFunc());
          // Upcast.
          RootedFunction fun(cx);
          if (!srcTable.getFuncRef(cx, srcIndex, &fun)) {
            // OOM, so just pass it on.
            return false;
          }
          fillAnyRef(dstIndex, 1, AnyRef::fromJSObject(fun));
          break;
        }
      }
      break;
    }
  }
  return true;
}

uint32_t Table::grow(uint32_t delta) {
  MOZ_RELEASE_ASSERT(elemType_.isNullable());

  // This isn't just an optimization: movingGrowable() assumes that
  // onMovingGrowTable does not fire when length == maximum.
  if (!delta) {
    return length_;
  }

  uint32_t oldLength = length_;

  CheckedInt<uint32_t> newLength = oldLength;
  newLength += delta;
  if (!newLength.isValid() || newLength.value() > MaxTableLength) {
    return -1;
  }

  if (maximum_ && newLength.value() > maximum_.value()) {
    return -1;
  }

  MOZ_ASSERT(movingGrowable());

  switch (repr()) {
    case TableRepr::Func: {
      MOZ_RELEASE_ASSERT(!isAsmJS_);
      if (!functions_.resize(newLength.value())) {
        return -1;
      }
      break;
    }
    case TableRepr::Ref: {
      if (!objects_.resize(newLength.value())) {
        return -1;
      }
      break;
    }
  }

  if (auto* object = maybeObject_.unbarrieredGet()) {
    RemoveCellMemory(object, gcMallocBytes(), MemoryUse::WasmTableTable);
  }

  length_ = newLength.value();

  if (auto* object = maybeObject_.unbarrieredGet()) {
    AddCellMemory(object, gcMallocBytes(), MemoryUse::WasmTableTable);
  }

  for (InstanceSet::Range r = observers_.all(); !r.empty(); r.popFront()) {
    r.front()->instance().onMovingGrowTable(this);
  }

  return oldLength;
}

bool Table::movingGrowable() const {
  return !maximum_ || length_ < maximum_.value();
}

bool Table::addMovingGrowObserver(JSContext* cx, WasmInstanceObject* instance) {
  MOZ_ASSERT(movingGrowable());

  // A table can be imported multiple times into an instance, but we only
  // register the instance as an observer once.

  if (!observers_.put(instance)) {
    ReportOutOfMemory(cx);
    return false;
  }

  return true;
}

size_t Table::sizeOfExcludingThis(MallocSizeOf mallocSizeOf) const {
  if (isFunction()) {
    return functions_.sizeOfExcludingThis(mallocSizeOf);
  }
  return objects_.sizeOfExcludingThis(mallocSizeOf);
}

size_t Table::gcMallocBytes() const {
  size_t size = sizeof(*this);
  if (isFunction()) {
    size += length() * sizeof(FunctionTableElem);
  } else {
    size += length() * sizeof(TableAnyRefVector::ElementType);
  }
  return size;
}
