/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: set ts=8 sts=2 et sw=2 tw=80:
 *
 * Copyright 2021 Mozilla Foundation
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

#include "wasm/WasmCodegenTypes.h"

#include "wasm/WasmExprType.h"
#include "wasm/WasmStubs.h"
#include "wasm/WasmTypeDef.h"
#include "wasm/WasmValidate.h"
#include "wasm/WasmValue.h"

using mozilla::MakeEnumeratedRange;
using mozilla::PodZero;

using namespace js;
using namespace js::wasm;

ArgTypeVector::ArgTypeVector(const FuncType& funcType)
    : args_(funcType.args()),
      hasStackResults_(ABIResultIter::HasStackResults(
          ResultType::Vector(funcType.results()))) {}

bool TrapSiteVectorArray::empty() const {
  for (Trap trap : MakeEnumeratedRange(Trap::Limit)) {
    if (!(*this)[trap].empty()) {
      return false;
    }
  }

  return true;
}

void TrapSiteVectorArray::clear() {
  for (Trap trap : MakeEnumeratedRange(Trap::Limit)) {
    (*this)[trap].clear();
  }
}

void TrapSiteVectorArray::swap(TrapSiteVectorArray& rhs) {
  for (Trap trap : MakeEnumeratedRange(Trap::Limit)) {
    (*this)[trap].swap(rhs[trap]);
  }
}

void TrapSiteVectorArray::shrinkStorageToFit() {
  for (Trap trap : MakeEnumeratedRange(Trap::Limit)) {
    (*this)[trap].shrinkStorageToFit();
  }
}

size_t TrapSiteVectorArray::sizeOfExcludingThis(
    MallocSizeOf mallocSizeOf) const {
  size_t ret = 0;
  for (Trap trap : MakeEnumeratedRange(Trap::Limit)) {
    ret += (*this)[trap].sizeOfExcludingThis(mallocSizeOf);
  }
  return ret;
}

CodeRange::CodeRange(Kind kind, Offsets offsets)
    : begin_(offsets.begin), ret_(0), end_(offsets.end), kind_(kind) {
  MOZ_ASSERT(begin_ <= end_);
  PodZero(&u);
#ifdef DEBUG
  switch (kind_) {
    case FarJumpIsland:
    case TrapExit:
    case Throw:
      break;
    default:
      MOZ_CRASH("should use more specific constructor");
  }
#endif
}

CodeRange::CodeRange(Kind kind, uint32_t funcIndex, Offsets offsets)
    : begin_(offsets.begin), ret_(0), end_(offsets.end), kind_(kind) {
  u.funcIndex_ = funcIndex;
  u.func.lineOrBytecode_ = 0;
  u.func.beginToUncheckedCallEntry_ = 0;
  u.func.beginToTierEntry_ = 0;
  MOZ_ASSERT(isEntry());
  MOZ_ASSERT(begin_ <= end_);
}

CodeRange::CodeRange(Kind kind, CallableOffsets offsets)
    : begin_(offsets.begin), ret_(offsets.ret), end_(offsets.end), kind_(kind) {
  MOZ_ASSERT(begin_ < ret_);
  MOZ_ASSERT(ret_ < end_);
  PodZero(&u);
#ifdef DEBUG
  switch (kind_) {
    case DebugTrap:
    case BuiltinThunk:
      break;
    default:
      MOZ_CRASH("should use more specific constructor");
  }
#endif
}

CodeRange::CodeRange(Kind kind, uint32_t funcIndex, CallableOffsets offsets)
    : begin_(offsets.begin), ret_(offsets.ret), end_(offsets.end), kind_(kind) {
  MOZ_ASSERT(isImportExit() || isJitEntry());
  MOZ_ASSERT(begin_ < ret_);
  MOZ_ASSERT(ret_ < end_);
  u.funcIndex_ = funcIndex;
  u.func.lineOrBytecode_ = 0;
  u.func.beginToUncheckedCallEntry_ = 0;
  u.func.beginToTierEntry_ = 0;
}

CodeRange::CodeRange(uint32_t funcIndex, uint32_t funcLineOrBytecode,
                     FuncOffsets offsets)
    : begin_(offsets.begin),
      ret_(offsets.ret),
      end_(offsets.end),
      kind_(Function) {
  MOZ_ASSERT(begin_ < ret_);
  MOZ_ASSERT(ret_ < end_);
  MOZ_ASSERT(offsets.uncheckedCallEntry - begin_ <= UINT8_MAX);
  MOZ_ASSERT(offsets.tierEntry - begin_ <= UINT8_MAX);
  u.funcIndex_ = funcIndex;
  u.func.lineOrBytecode_ = funcLineOrBytecode;
  u.func.beginToUncheckedCallEntry_ = offsets.uncheckedCallEntry - begin_;
  u.func.beginToTierEntry_ = offsets.tierEntry - begin_;
}

const CodeRange* wasm::LookupInSorted(const CodeRangeVector& codeRanges,
                                      CodeRange::OffsetInCode target) {
  size_t lowerBound = 0;
  size_t upperBound = codeRanges.length();

  size_t match;
  if (!BinarySearch(codeRanges, lowerBound, upperBound, target, &match)) {
    return nullptr;
  }

  return &codeRanges[match];
}

CallIndirectId CallIndirectId::forFunc(const ModuleEnvironment& moduleEnv,
                                       uint32_t funcIndex) {
  return CallIndirectId::forFuncType(moduleEnv,
                                     moduleEnv.funcs[funcIndex].typeIndex);
}

CallIndirectId CallIndirectId::forFuncType(const ModuleEnvironment& moduleEnv,
                                           uint32_t funcTypeIndex) {
  // asm.js tables are homogenous and don't require a signature check
  if (moduleEnv.isAsmJS()) {
    return CallIndirectId();
  }

  const FuncType& funcType = moduleEnv.types->type(funcTypeIndex).funcType();
  if (funcType.hasImmediateTypeId()) {
    return CallIndirectId(CallIndirectIdKind::Immediate,
                          funcType.immediateTypeId());
  }
  return CallIndirectId(CallIndirectIdKind::Global,
                        moduleEnv.offsetOfTypeId(funcTypeIndex));
}

CalleeDesc CalleeDesc::function(uint32_t funcIndex) {
  CalleeDesc c;
  c.which_ = Func;
  c.u.funcIndex_ = funcIndex;
  return c;
}
CalleeDesc CalleeDesc::import(uint32_t globalDataOffset) {
  CalleeDesc c;
  c.which_ = Import;
  c.u.import.globalDataOffset_ = globalDataOffset;
  return c;
}
CalleeDesc CalleeDesc::wasmTable(const TableDesc& desc,
                                 CallIndirectId callIndirectId) {
  CalleeDesc c;
  c.which_ = WasmTable;
  c.u.table.globalDataOffset_ = desc.globalDataOffset;
  c.u.table.minLength_ = desc.initialLength;
  c.u.table.maxLength_ = desc.maximumLength;
  c.u.table.callIndirectId_ = callIndirectId;
  return c;
}
CalleeDesc CalleeDesc::asmJSTable(const TableDesc& desc) {
  CalleeDesc c;
  c.which_ = AsmJSTable;
  c.u.table.globalDataOffset_ = desc.globalDataOffset;
  return c;
}
CalleeDesc CalleeDesc::builtin(SymbolicAddress callee) {
  CalleeDesc c;
  c.which_ = Builtin;
  c.u.builtin_ = callee;
  return c;
}
CalleeDesc CalleeDesc::builtinInstanceMethod(SymbolicAddress callee) {
  CalleeDesc c;
  c.which_ = BuiltinInstanceMethod;
  c.u.builtin_ = callee;
  return c;
}
CalleeDesc CalleeDesc::wasmFuncRef() {
  CalleeDesc c;
  c.which_ = FuncRef;
  return c;
}
