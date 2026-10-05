/*
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

#include "wasm/WasmPI.h"

#include "jsfriendapi.h"
#include "builtin/Promise.h"
#include "debugger/DebugAPI.h"
#include "debugger/Debugger.h"
#include "jit/MIRGenerator.h"
#include "js/CallAndConstruct.h"
#include "js/Printf.h"
#include "js/Wrapper.h"
#include "vm/Compartment.h"
#include "vm/Iteration.h"
#include "vm/JSContext.h"
#include "vm/JSFunction.h"
#include "vm/JSObject.h"
#include "vm/NativeObject.h"
#include "vm/PromiseObject.h"
#include "wasm/WasmAnyRef.h"
#include "wasm/WasmConstants.h"
#include "wasm/WasmContext.h"
#include "wasm/WasmFeatures.h"
#include "wasm/WasmGcObject.h"
#include "wasm/WasmGenerator.h"
#include "wasm/WasmIonCompile.h"  // IonPlatformSupport
#include "wasm/WasmJS.h"
#include "wasm/WasmStacks.h"
#include "wasm/WasmValidate.h"

#include "vm/Compartment-inl.h"
#include "vm/JSObject-inl.h"
#include "wasm/WasmGcObject-inl.h"
#include "wasm/WasmInstance-inl.h"

using namespace js;
using namespace js::jit;

#ifdef ENABLE_WASM_JSPI
namespace js::wasm {

// Slot that is used in WasmPromisingFunction
const size_t WRAPPED_FN_SLOT = 0;

// Slots that are used in the WasmPromiseReaction
const size_t CONT_SLOT = 0;
const size_t REACTION_SLOT = 1;
const size_t PROMISING_PROMISE_SLOT = 2;

// Suspending

// Builds a wasm module with following structure:
// (module
//   (type $results (struct (field ..)*)))
//   (import "" "tag" (tag $on-suspend))
//   (import "" "wrapped" (func $suspending.wrappedfn ..))
//   (func $suspending.exported .. )
//   (export "" (func $suspending.exported))
// )
//
class SuspendingFunctionModuleFactory {
 public:
  // Type indices, relative to baseTypeIndex_.
  enum TypeIdx {
    ResultsTypeIndex,
    TagFuncTypeIndex,
    Count,
  };

  enum TagIdx {
    OnSuspendTagIndex,
  };

  enum FnIdx {
    WrappedFnIndex,
    ExportedFnIndex,
  };

  uint32_t baseTypeIndex_ = 0;

 private:
  // Builds function that will be imported to wasm module:
  // (func $suspending.exported
  //   (param ..)* (result ..)*
  //
  //   (local $promise externref)
  //   (local $results (ref $results))
  //
  //   ;; ensure that there is a WebAssembly.promising function on the stack
  //   guard-suspending
  //
  //   ;; call the wrapped fn
  //   (local.get $param)*
  //   call $suspending.wrappedfn
  //
  //   ;; call Promise.resolve on the result of wrapped fn
  //   call $builtin.promise-resolve
  //   ;; save the promise for when we resume
  //   local.tee $promise
  //   suspend $on-suspend
  //
  //   ;; get the results from the promise as a struct
  //   local.get $promise
  //   call $builtin.get-promise-results
  //   ref.cast $results
  //   local.set $results
  //
  //   ;; unbox the struct and return the results
  //   (struct.get $results i local.get $results)*
  //   return
  // )
  bool encodeExportedFunction(CodeMetadata& codeMeta, uint32_t paramsSize,
                              uint32_t resultSize, uint32_t paramsOffset,
                              RefType resultType, Bytes& bytecode) {
    Encoder encoder(bytecode, *codeMeta.types);

    const uint32_t promiseIndex = paramsSize;
    const uint32_t resultsIndex = paramsSize + 1;

    ValTypeVector locals;
    if (!locals.emplaceBack(RefType::extern_()) ||
        !locals.emplaceBack(resultType)) {
      return false;
    }
    if (!EncodeLocalEntries(encoder, locals)) {
      return false;
    }

    if (!encoder.writeOp(Opcode(MozOp::GuardSuspending)) ||
        !encoder.writeVarU32(OnSuspendTagIndex)) {
      return false;
    }

    // (local.get $param)* call $suspending.wrappedfn
    for (uint32_t i = 0; i < paramsSize; i++) {
      if (!encoder.writeOp(Op::LocalGet) ||
          !encoder.writeVarU32(i + paramsOffset)) {
        return false;
      }
    }
    if (!encoder.writeOp(Op::Call) || !encoder.writeVarU32(WrappedFnIndex)) {
      return false;
    }

    // call $builtin.promise-resolve
    if (!encoder.writeOp(MozOp::CallBuiltinModuleFunc) ||
        !encoder.writeVarU32((uint32_t)BuiltinModuleFuncId::PromiseResolve)) {
      return false;
    }

    // local.tee $promise
    if (!encoder.writeOp(Op::LocalTee) || !encoder.writeVarU32(promiseIndex)) {
      return false;
    }

    // suspend $on-suspend
    if (!encoder.writeOp(Op::Suspend) ||
        !encoder.writeVarU32(OnSuspendTagIndex)) {
      return false;
    }

    // local.get $promise
    // i32.const (baseTypeIndex_ + ResultsTypeIndex)
    // call $builtin.get-promise-results
    if (!encoder.writeOp(Op::LocalGet) || !encoder.writeVarU32(promiseIndex)) {
      return false;
    }
    if (!encoder.writeOp(Op::I32Const) ||
        !encoder.writeVarS32(int32_t(baseTypeIndex_ + ResultsTypeIndex))) {
      return false;
    }
    if (!encoder.writeOp(MozOp::CallBuiltinModuleFunc) ||
        !encoder.writeVarU32(
            (uint32_t)BuiltinModuleFuncId::GetPromiseResults)) {
      return false;
    }

    // ref.cast $results
    // local.set $results
    if (!encoder.writeOp(GcOp::RefCast) ||
        !encoder.writeVarS32(baseTypeIndex_ + ResultsTypeIndex) ||
        !encoder.writeOp(Op::LocalSet) || !encoder.writeVarU32(resultsIndex)) {
      return false;
    }

    // (struct.get $results i (local.get $results))*
    for (uint32_t i = 0; i < resultSize; i++) {
      if (!encoder.writeOp(Op::LocalGet) ||
          !encoder.writeVarU32(resultsIndex) ||
          !encoder.writeOp(GcOp::StructGet) ||
          !encoder.writeVarU32(baseTypeIndex_ + ResultsTypeIndex) ||
          !encoder.writeVarU32(i)) {
        return false;
      }
    }

    return encoder.writeOp(Op::End);
  }

 public:
  SharedModule build(JSContext* cx, HandleObject func,
                     const SharedTypeContext& foreignTypes,
                     uint32_t funcTypeIndex) {
    FeatureOptions options;
    // Builtin modules can use special opcodes and get stack switching enabled.
    options.isBuiltinModule = true;

    SharedCompileArgs compileArgs = CompileArgs::buildAndReport(
        cx, ScriptedCaller::selfHosted(cx), options);
    if (!compileArgs) {
      return nullptr;
    }

    MutableModuleMetadata moduleMeta = js_new<ModuleMetadata>();
    if (!moduleMeta || !moduleMeta->init(*compileArgs)) {
      ReportOutOfMemory(cx);
      return nullptr;
    }
    MutableCodeMetadata codeMeta = moduleMeta->codeMeta;

    // If the function we're wrapping is WebAssembly, treat it as if it was JS.
    // This is required by the specification so that type mismatches don't
    // surface as link errors. It also ensures that we don't need a suspend
    // barrier instruction when calling the wrapped function.
    codeMeta->funcImportsAreJS = true;

    MOZ_ASSERT(IonPlatformSupport());
    CompilerEnvironment compilerEnv(CompileMode::Once, Tier::Optimized,
                                    DebugEnabled::False);
    compilerEnv.computeParameters();

    // Copy all RecGroups from the foreign module so that ValTypes in params
    // and results that reference concrete type defs resolve correctly.
    if (!codeMeta->types->clone(*foreignTypes)) {
      ReportOutOfMemory(cx);
      return nullptr;
    }
    baseTypeIndex_ = codeMeta->types->length();

    // If we don't have room to add our types with the wrapped function's
    // module's types, then fail and treat this as an OOM. This is extremely
    // unlikely.
    if (codeMeta->types->length() > MaxTypes - TypeIdx::Count) {
      ReportOutOfMemory(cx);
      return nullptr;
    }

    // Pull the params and results from the import's declared type.
    const FuncType& importFuncType =
        codeMeta->types->type(funcTypeIndex).funcType();
    ValTypeVector params, results;
    if (!params.append(importFuncType.args().begin(),
                       importFuncType.args().end()) ||
        !results.append(importFuncType.results().begin(),
                        importFuncType.results().end())) {
      ReportOutOfMemory(cx);
      return nullptr;
    }

    const size_t resultsSize = results.length();
    const size_t paramsSize = params.length();
    const size_t paramsOffset = 0;

    // Type baseTypeIndex_ + 0: $results struct
    StructType boxedResultType;
    if (!StructType::createImmutable(results, &boxedResultType)) {
      ReportOutOfMemory(cx);
      return nullptr;
    }
    MOZ_ASSERT(codeMeta->types->length() == baseTypeIndex_ + ResultsTypeIndex);
    if (!codeMeta->types->addType(std::move(boxedResultType))) {
      ReportOutOfMemory(cx);
      return nullptr;
    }

    // Type baseTypeIndex_ + 1: tag func type (param externref)
    ValTypeVector tagParams, tagResults;
    if (!tagParams.emplaceBack(RefType::extern_())) {
      ReportOutOfMemory(cx);
      return nullptr;
    }
    MOZ_ASSERT(codeMeta->types->length() == baseTypeIndex_ + TagFuncTypeIndex);
    if (!codeMeta->types->addType(
            FuncType(std::move(tagParams), std::move(tagResults)))) {
      ReportOutOfMemory(cx);
      return nullptr;
    }

    // Tag 0: $on-suspend
    MutableTagType tagType = js_new<TagType>();
    if (!tagType || !tagType->initialize(&(
                        *codeMeta->types)[baseTypeIndex_ + TagFuncTypeIndex])) {
      ReportOutOfMemory(cx);
      return nullptr;
    }
    if (!codeMeta->tags.emplaceBack(TagKind::Exception, tagType)) {
      ReportOutOfMemory(cx);
      return nullptr;
    }

    // Func 0: $suspending.wrappedfn (imported) - params -> (externref)
    ValTypeVector wrappedParams, wrappedResults;
    if (!wrappedParams.append(params.begin(), params.end()) ||
        !wrappedResults.emplaceBack(RefType::extern_())) {
      ReportOutOfMemory(cx);
      return nullptr;
    }
    MOZ_ASSERT(codeMeta->funcs.length() == WrappedFnIndex);
    if (!moduleMeta->addDefinedFunc(std::move(wrappedParams),
                                    std::move(wrappedResults))) {
      ReportOutOfMemory(cx);
      return nullptr;
    }

    codeMeta->numFuncImports = codeMeta->funcs.length();

    // Func 1: $suspending.exported (defined, exported) - params -> results
    //
    // Give the wrapper the import's actual declared type so that it has the
    // correct identity, subtyping, and final attributes.
    MOZ_ASSERT(codeMeta->funcs.length() == ExportedFnIndex);
    MOZ_ASSERT(funcTypeIndex < baseTypeIndex_);
    MOZ_ASSERT((*codeMeta->types)[funcTypeIndex].isFuncType());
    if (!moduleMeta->addDefinedFuncWithType(funcTypeIndex,
                                            /*declareForRef = */ true,
                                            mozilla::Some(CacheableName()))) {
      ReportOutOfMemory(cx);
      return nullptr;
    }

    if (!moduleMeta->prepareForCompile(compilerEnv.mode())) {
      ReportOutOfMemory(cx);
      return nullptr;
    }

    ModuleGenerator mg(*codeMeta, compilerEnv, compilerEnv.initialState(),
                       nullptr, nullptr, nullptr);
    if (!mg.initializeCompleteTier()) {
      ReportOutOfMemory(cx);
      return nullptr;
    }

    uint32_t funcBytecodeOffset = CallSite::FIRST_VALID_BYTECODE_OFFSET;
    Bytes bytecode;
    if (!encodeExportedFunction(
            *codeMeta, paramsSize, resultsSize, paramsOffset,
            RefType::fromTypeDef(
                &(*codeMeta->types)[baseTypeIndex_ + ResultsTypeIndex], false),
            bytecode)) {
      ReportOutOfMemory(cx);
      return nullptr;
    }
    if (!mg.compileFuncDef(ExportedFnIndex, funcBytecodeOffset,
                           bytecode.begin(),
                           bytecode.begin() + bytecode.length())) {
      ReportOutOfMemory(cx);
      return nullptr;
    }

    if (!mg.finishFuncDefs()) {
      ReportOutOfMemory(cx);
      return nullptr;
    }

    SharedModule module =
        mg.finishModule(BytecodeBufferOrSource(), *moduleMeta,
                        /*maybeCompleteTier2Listener=*/nullptr);
    if (!module) {
      ReportOutOfMemory(cx);
      return nullptr;
    }
    return module;
  }
};

JSFunction* WasmSuspendingFunctionCreate(JSContext* cx, HandleObject func,
                                         uint32_t funcTypeIndex,
                                         const SharedTypeContext& typeContext) {
  if (!JSPromiseIntegrationAvailable(cx)) {
    JS_ReportErrorASCII(cx, "JS-PI is not enabled");
    return nullptr;
  }

  MOZ_ASSERT(IsCallable(ObjectValue(*func)) &&
             !IsCrossCompartmentWrapper(func));

  SuspendingFunctionModuleFactory moduleFactory;
  SharedModule module =
      moduleFactory.build(cx, func, typeContext, funcTypeIndex);
  if (!module) {
    return nullptr;
  }

  // Instantiate the module.
  Rooted<ImportValues> imports(cx);

  // Add $suspending.wrappedfn to imports.
  if (!imports.get().funcs.append(func)) {
    ReportOutOfMemory(cx);
    return nullptr;
  }

  // Add $on-suspend to imports.
  Rooted<WasmNamespaceObject*> wasmNamespace(
      cx, WasmNamespaceObject::getOrCreate(cx));
  if (!wasmNamespace) {
    ReportOutOfMemory(cx);
    return nullptr;
  }
  if (!imports.get().tagObjs.append(wasmNamespace->jsPromiseTag())) {
    ReportOutOfMemory(cx);
    return nullptr;
  }

  Rooted<WasmInstanceObject*> instance(cx);
  if (!module->instantiate(cx, imports.get(), nullptr, &instance)) {
    // Can also trap on invalid input function.
    return nullptr;
  }

  // Returns the $suspending.exported function.
  RootedFunction wasmFunc(cx);
  if (!WasmInstanceObject::getExportedFunction(
          cx, instance, SuspendingFunctionModuleFactory::ExportedFnIndex,
          &wasmFunc)) {
    return nullptr;
  }
  return wasmFunc;
}

// Promising

// Builds a wasm module with following structure:
// (module
//   (type $params (struct (field ..)*))
//   (type $results (struct (field ..)*))
//   (type $cont (cont))
//   (import "" "wrapped" (func $promising.wrappedfn ..))
//   (import "" "tag" (tag $on-suspend))
//   ;; globals for passing values from exported to trampoline.
//   ;; works around work-in-progress stack switching implementation.
//   (global $promisingPromise (mut externref))
//   (global $params (mut (ref null $params)))
//   (func $promising.exported .. )
//   (func $promising.trampoline ..)
//   (func $promising.reaction ..)
//   (export "" (func $promising.exported))
// )
//
// The module provides logic for the Invoke Promising Import state transition
// via $promising.exported and $promising.trampoline (see the SMDOC).
//
class PromisingFunctionModuleFactory {
  uint32_t baseTypeIndex_ = 0;

 public:
  // Type indices, relative to baseTypeIndex_. Types added by addDefinedFunc
  // are interleaved.
  enum TypeIdx {
    ParamsTypeIndex = 0,
    ResultsTypeIndex = 1,
    // Type 2: exported fn func type (added by addDefinedFunc for Exported)
    TrampolineFuncTypeIndex = 3,
    // Type 4: trampoline fn func type (added by addDefinedFunc, same as 3)
    ContTypeIndex = 5,
    TagFuncTypeIndex = 6,
    SuspendBlockTypeIndex = 7,
    // Type 8: reaction fn func type (added by addDefinedFunc for Reaction)
    Count = 9,
  };

  enum TagIdx {
    OnSuspendTagIndex,
  };

  enum GlobalIdx {
    PromisingPromiseGlobalIndex,
    ParamsGlobalIndex,
  };

  enum FnIdx {
    WrappedFnIndex,
    ExportedFnIndex,
    TrampolineFnIndex,
    ReactionFnIndex,
  };

 private:
  // Builds function that will be exported for JS:
  // (func $promising.exported
  //   (param ..)* (result externref)
  //   (local $promisingPromise externref)
  //
  //   call $builtin.create-promise
  //   local.set $promisingPromise
  //
  //   block $suspend (result externref (ref $cont))
  //     local.get *
  //     struct.new $params
  //     global.set $params
  //
  //     local.get $promisingPromise
  //     global.set $promisingPromise
  //
  //     ref.func $promising.trampoline
  //     cont.new $cont
  //     resume (on $on-suspend $suspend)
  //
  //     local.get $promisingPromise
  //     return
  //   end
  //
  //   ;; implicitly passing suspending's promise and continuation here
  //   ref.func $promising.reaction
  //   local.get $promisingPromise
  //   call $builtin.add-promise-reactions
  //
  //   local.get $promisingPromise
  // )
  bool encodeExportedFunction(CodeMetadata& codeMeta, uint32_t paramsSize,
                              Bytes& bytecode) {
    Encoder encoder(bytecode, *codeMeta.types);

    const uint32_t promisingPromiseIndex = paramsSize;

    ValTypeVector locals;
    if (!locals.emplaceBack(RefType::extern_())) {
      return false;
    }
    if (!EncodeLocalEntries(encoder, locals)) {
      return false;
    }

#  ifdef DEBUG
    // Trap if the global promise is non-null. This would only happen if we
    // messed up our global state clearing or had unexpected re-entrance.
    if (!encoder.writeOp(Op::GlobalGet) ||
        !encoder.writeVarU32(PromisingPromiseGlobalIndex)) {
      return false;
    }
    if (!encoder.writeOp(Op::RefIsNull) || !encoder.writeOp(Op::I32Eqz)) {
      return false;
    }
    if (!encoder.writeOp(Op::If) ||
        !encoder.writeFixedU8((uint8_t)TypeCode::BlockVoid) ||
        !encoder.writeOp(Op::Unreachable) || !encoder.writeOp(Op::End)) {
      return false;
    }
#  endif

    // call $builtin.create-promise
    // local.set $promisingPromise
    if (!encoder.writeOp(MozOp::CallBuiltinModuleFunc) ||
        !encoder.writeVarU32((uint32_t)BuiltinModuleFuncId::CreatePromise)) {
      return false;
    }
    if (!encoder.writeOp(Op::LocalSet) ||
        !encoder.writeVarU32(promisingPromiseIndex)) {
      return false;
    }

    // block $suspend (result externref (ref $cont))
    if (!encoder.writeOp(Op::Block) ||
        !encoder.writeVarS32(int32_t(baseTypeIndex_ + SuspendBlockTypeIndex))) {
      return false;
    }

    // local.get * ; struct.new $params ; global.set $params
    for (uint32_t i = 0; i < paramsSize; i++) {
      if (!encoder.writeOp(Op::LocalGet) || !encoder.writeVarU32(i)) {
        return false;
      }
    }
    if (!encoder.writeOp(GcOp::StructNew) ||
        !encoder.writeVarU32(baseTypeIndex_ + ParamsTypeIndex)) {
      return false;
    }
    if (!encoder.writeOp(Op::GlobalSet) ||
        !encoder.writeVarU32(ParamsGlobalIndex)) {
      return false;
    }

    // local.get $promisingPromise ; global.set $promisingPromise
    if (!encoder.writeOp(Op::LocalGet) ||
        !encoder.writeVarU32(promisingPromiseIndex)) {
      return false;
    }
    if (!encoder.writeOp(Op::GlobalSet) ||
        !encoder.writeVarU32(PromisingPromiseGlobalIndex)) {
      return false;
    }

    // ref.func $promising.trampoline ; cont.new $cont
    if (!encoder.writeOp(Op::RefFunc) ||
        !encoder.writeVarU32(TrampolineFnIndex)) {
      return false;
    }
    if (!encoder.writeOp(Op::ContNew) ||
        !encoder.writeVarU32(baseTypeIndex_ + ContTypeIndex)) {
      return false;
    }

    // resume (on $on-suspend $suspend)
    // Handler: 1 handler, kind=Suspend(0), tag=OnSuspendTagIndex, label=0
    if (!encoder.writeOp(Op::Resume) ||
        !encoder.writeVarU32(baseTypeIndex_ + ContTypeIndex) ||
        !encoder.writeVarU32(1) ||
        !encoder.writeFixedU8(uint8_t(HandlerKind::Suspend)) ||
        !encoder.writeVarU32(OnSuspendTagIndex) || !encoder.writeVarU32(0)) {
      return false;
    }

    // local.get $promisingPromise ; return
    if (!encoder.writeOp(Op::LocalGet) ||
        !encoder.writeVarU32(promisingPromiseIndex)) {
      return false;
    }
    if (!encoder.writeOp(Op::Return)) {
      return false;
    }

    // end (block $suspend)
    if (!encoder.writeOp(Op::End)) {
      return false;
    }

    // Stack now has: externref (suspending promise), (ref $cont)
    // ref.func $promising.reaction
    if (!encoder.writeOp(Op::RefFunc) ||
        !encoder.writeVarU32(ReactionFnIndex)) {
      return false;
    }

    // local.get $promisingPromise
    if (!encoder.writeOp(Op::LocalGet) ||
        !encoder.writeVarU32(promisingPromiseIndex)) {
      return false;
    }

    // call $builtin.add-promise-reactions
    if (!encoder.writeOp(MozOp::CallBuiltinModuleFunc) ||
        !encoder.writeVarU32(
            (uint32_t)BuiltinModuleFuncId::AddPromiseReactions)) {
      return false;
    }

    // local.get $promisingPromise ; return
    if (!encoder.writeOp(Op::LocalGet) ||
        !encoder.writeVarU32(promisingPromiseIndex)) {
      return false;
    }

    return encoder.writeOp(Op::End);
  }

  // Builds function that is called on cont stack and calls wrapped
  // function: (func $promising.trampoline
  //   (local $params (ref null $params))
  //   (local $promisingPromise externref)
  //
  //   (local.set $params global.get $params)
  //   (global.set $params ref.null $params)
  //
  //   (local.set $promisingPromise global.get $promisingPromise)
  //   (global.set $promisingPromise ref.null extern)
  //
  //   ;; destructure the params struct
  //   (struct.get $params $i (local.get $params))*
  //   ;; clear the params local
  //   (local.set $params (ref.null $params))
  //
  //   ;; call the wrapped function
  //   call $promising.wrappedfn
  //
  //   ;; box up the results
  //   struct.new $results
  //   local.get $promisingPromise
  //   ;; resolve the promise with results
  //   call $builtin.resolve-promise-with-results
  // )
  bool encodeTrampolineFunction(CodeMetadata& codeMeta, uint32_t paramsSize,
                                Bytes& bytecode) {
    Encoder encoder(bytecode, *codeMeta.types);

    const uint32_t paramsLocalIndex = 0;
    const uint32_t promisingPromiseLocalIndex = 1;

    ValTypeVector locals;
    if (!locals.emplaceBack(RefType::fromTypeDef(
            &codeMeta.types->type(baseTypeIndex_ + ParamsTypeIndex), true)) ||
        !locals.emplaceBack(RefType::extern_())) {
      return false;
    }
    if (!EncodeLocalEntries(encoder, locals)) {
      return false;
    }

    // local.set $params (global.get $params)
    if (!encoder.writeOp(Op::GlobalGet) ||
        !encoder.writeVarU32(ParamsGlobalIndex)) {
      return false;
    }
    if (!encoder.writeOp(Op::LocalSet) ||
        !encoder.writeVarU32(paramsLocalIndex)) {
      return false;
    }

    // global.set $params (ref.null $params)
    if (!encoder.writeOp(Op::RefNull) ||
        !encoder.writeVarS32(int32_t(baseTypeIndex_ + ParamsTypeIndex))) {
      return false;
    }
    if (!encoder.writeOp(Op::GlobalSet) ||
        !encoder.writeVarU32(ParamsGlobalIndex)) {
      return false;
    }

    // local.set $promisingPromise (global.get $promisingPromise)
    if (!encoder.writeOp(Op::GlobalGet) ||
        !encoder.writeVarU32(PromisingPromiseGlobalIndex)) {
      return false;
    }
    if (!encoder.writeOp(Op::LocalSet) ||
        !encoder.writeVarU32(promisingPromiseLocalIndex)) {
      return false;
    }

    // global.set $promisingPromise (ref.null extern)
    if (!encoder.writeOp(Op::RefNull) ||
        !encoder.writeFixedU8(uint8_t(TypeCode::ExternRef))) {
      return false;
    }
    if (!encoder.writeOp(Op::GlobalSet) ||
        !encoder.writeVarU32(PromisingPromiseGlobalIndex)) {
      return false;
    }

    // (struct.get $params $i (local.get $params))*
    for (uint32_t i = 0; i < paramsSize; i++) {
      if (!encoder.writeOp(Op::LocalGet) ||
          !encoder.writeVarU32(paramsLocalIndex)) {
        return false;
      }
      if (!encoder.writeOp(GcOp::StructGet) ||
          !encoder.writeVarU32(baseTypeIndex_ + ParamsTypeIndex) ||
          !encoder.writeVarU32(i)) {
        return false;
      }
    }

    // local.set $params (ref.null $params)
    if (!encoder.writeOp(Op::RefNull) ||
        !encoder.writeVarS32(int32_t(baseTypeIndex_ + ParamsTypeIndex))) {
      return false;
    }
    if (!encoder.writeOp(Op::LocalSet) ||
        !encoder.writeVarU32(paramsLocalIndex)) {
      return false;
    }

    // call $promising.wrappedfn
    if (!encoder.writeOp(Op::Call) || !encoder.writeVarU32(WrappedFnIndex)) {
      return false;
    }

    // struct.new $results
    if (!encoder.writeOp(GcOp::StructNew) ||
        !encoder.writeVarU32(baseTypeIndex_ + ResultsTypeIndex)) {
      return false;
    }

    // local.get $promisingPromise
    if (!encoder.writeOp(Op::LocalGet) ||
        !encoder.writeVarU32(promisingPromiseLocalIndex)) {
      return false;
    }

    // call $builtin.resolve-promise-with-results
    if (!encoder.writeOp(MozOp::CallBuiltinModuleFunc) ||
        !encoder.writeVarU32(
            (uint32_t)BuiltinModuleFuncId::ResolvePromiseWithResults)) {
      return false;
    }

    return encoder.writeOp(Op::End);
  }

  // Builds function that is called by event loop when promise resolves.
  //
  // (func $promising.reaction
  //   (param $cont (ref $cont))
  //   (param $promisingPromise externref)
  //
  //   block $suspend (result externref (ref $cont))
  //     local.get $cont
  //     resume (on $on-suspend $suspend)
  //     return
  //   end
  //
  //   ;; implicitly passing suspending's promise and continuation here
  //   ref.func $promising.reaction
  //   local.get $promisingPromise
  //   call $builtin.add-promise-reactions
  // )
  bool encodeReactionFunction(CodeMetadata& codeMeta, Bytes& bytecode) {
    Encoder encoder(bytecode, *codeMeta.types);

    const uint32_t contIndex = 0;
    const uint32_t promisingPromiseIndex = 1;

    if (!EncodeLocalEntries(encoder, ValTypeVector())) {
      return false;
    }

    // block $suspend (result externref (ref $cont))
    if (!encoder.writeOp(Op::Block) ||
        !encoder.writeVarS32(int32_t(baseTypeIndex_ + SuspendBlockTypeIndex))) {
      return false;
    }

    // local.get $cont
    if (!encoder.writeOp(Op::LocalGet) || !encoder.writeVarU32(contIndex)) {
      return false;
    }

    // resume (on $on-suspend $suspend)
    if (!encoder.writeOp(Op::Resume) ||
        !encoder.writeVarU32(baseTypeIndex_ + ContTypeIndex) ||
        !encoder.writeVarU32(1) ||
        !encoder.writeFixedU8(uint8_t(HandlerKind::Suspend)) ||
        !encoder.writeVarU32(OnSuspendTagIndex) || !encoder.writeVarU32(0)) {
      return false;
    }

    // return
    if (!encoder.writeOp(Op::Return)) {
      return false;
    }

    // end (block $suspend)
    if (!encoder.writeOp(Op::End)) {
      return false;
    }

    // Stack: externref (suspending promise), (ref $cont)
    // ref.func $promising.reaction
    if (!encoder.writeOp(Op::RefFunc) ||
        !encoder.writeVarU32(ReactionFnIndex)) {
      return false;
    }

    // local.get $promisingPromise
    if (!encoder.writeOp(Op::LocalGet) ||
        !encoder.writeVarU32(promisingPromiseIndex)) {
      return false;
    }

    // call $builtin.add-promise-reactions
    if (!encoder.writeOp(MozOp::CallBuiltinModuleFunc) ||
        !encoder.writeVarU32(
            (uint32_t)BuiltinModuleFuncId::AddPromiseReactions)) {
      return false;
    }

    return encoder.writeOp(Op::End);
  }

 public:
  SharedModule build(JSContext* cx, HandleFunction fn) {
    const FuncType& fnType = fn->wasmTypeDef()->funcType();
    size_t paramsSize = fnType.args().length();
    uint32_t funcTypeIndex =
        fn->wasmInstance().codeMeta().funcs[fn->wasmFuncIndex()].typeIndex;

    FeatureOptions options;
    // Builtin modules can use special opcodes and get stack switching enabled.
    options.isBuiltinModule = true;

    SharedCompileArgs compileArgs = CompileArgs::buildAndReport(
        cx, ScriptedCaller::selfHosted(cx), options);
    if (!compileArgs) {
      return nullptr;
    }

    MutableModuleMetadata moduleMeta = js_new<ModuleMetadata>();
    if (!moduleMeta || !moduleMeta->init(*compileArgs)) {
      ReportOutOfMemory(cx);
      return nullptr;
    }
    MutableCodeMetadata codeMeta = moduleMeta->codeMeta;

    MOZ_ASSERT(IonPlatformSupport());
    CompilerEnvironment compilerEnv(CompileMode::Once, Tier::Optimized,
                                    DebugEnabled::False);
    compilerEnv.computeParameters();

    // Copy all RecGroups from the wrapped function's module so that ValTypes
    // referencing concrete type defs are valid in this module's TypeContext.
    const SharedTypeContext& foreignTypes = fn->wasmInstance().codeMeta().types;
    if (!codeMeta->types->clone(*foreignTypes)) {
      ReportOutOfMemory(cx);
      return nullptr;
    }
    baseTypeIndex_ = codeMeta->types->length();

    // If we don't have room to add our types with the wrapped function's
    // module's types, then fail and treat this as an OOM. This is extremely
    // unlikely.
    if (codeMeta->types->length() > MaxTypes - TypeIdx::Count) {
      ReportOutOfMemory(cx);
      return nullptr;
    }

    // Type baseTypeIndex_ + 0: $params struct
    StructType boxedParamsStruct;
    if (!StructType::createImmutable(fnType.args(), &boxedParamsStruct)) {
      ReportOutOfMemory(cx);
      return nullptr;
    }
    MOZ_ASSERT(codeMeta->types->length() == baseTypeIndex_ + ParamsTypeIndex);
    if (!codeMeta->types->addType(std::move(boxedParamsStruct))) {
      ReportOutOfMemory(cx);
      return nullptr;
    }

    // Type baseTypeIndex_ + 1: $results struct
    StructType boxedResultType;
    if (!StructType::createImmutable(fnType.results(), &boxedResultType)) {
      ReportOutOfMemory(cx);
      return nullptr;
    }
    MOZ_ASSERT(codeMeta->types->length() == baseTypeIndex_ + ResultsTypeIndex);
    if (!codeMeta->types->addType(std::move(boxedResultType))) {
      ReportOutOfMemory(cx);
      return nullptr;
    }

    // Func 0 (imported): $promising.wrappedfn
    // Use the wrapped function's actual type index from the cloned type context
    // so that there is no type mismatch during instantiation.
    MOZ_ASSERT(funcTypeIndex < baseTypeIndex_);
    MOZ_ASSERT((*codeMeta->types)[funcTypeIndex].isFuncType());
    MOZ_ASSERT(codeMeta->funcs.length() == WrappedFnIndex);
    if (!moduleMeta->addDefinedFuncWithType(funcTypeIndex)) {
      ReportOutOfMemory(cx);
      return nullptr;
    }

    codeMeta->numFuncImports = codeMeta->funcs.length();

    // Func 1 (exported): $promising.exported
    // addDefinedFunc creates Type baseTypeIndex_ + 2: exported fn func type
    ValTypeVector exportedParams, exportedResults;
    if (!exportedParams.append(fnType.args().begin(), fnType.args().end()) ||
        !exportedResults.emplaceBack(RefType::extern_())) {
      ReportOutOfMemory(cx);
      return nullptr;
    }
    MOZ_ASSERT(codeMeta->funcs.length() == ExportedFnIndex);
    if (!moduleMeta->addDefinedFunc(
            std::move(exportedParams), std::move(exportedResults),
            /* declareForRef = */ true, mozilla::Some(CacheableName()))) {
      ReportOutOfMemory(cx);
      return nullptr;
    }

    // Type baseTypeIndex_ + 3: trampoline func type () -> ()
    // This is the func type the cont type will reference.
    MOZ_ASSERT(codeMeta->types->length() ==
               baseTypeIndex_ + TrampolineFuncTypeIndex);
    if (!codeMeta->types->addType(FuncType(ValTypeVector(), ValTypeVector()))) {
      ReportOutOfMemory(cx);
      return nullptr;
    }

    // Func 2: $promising.trampoline () -> ()
    // addDefinedFunc creates Type baseTypeIndex_ + 4: trampoline func type
    ValTypeVector trampolineParams, trampolineResults;
    MOZ_ASSERT(codeMeta->funcs.length() == TrampolineFnIndex);
    if (!moduleMeta->addDefinedFunc(std::move(trampolineParams),
                                    std::move(trampolineResults),
                                    /* declareForRef = */ true)) {
      ReportOutOfMemory(cx);
      return nullptr;
    }

    // Type baseTypeIndex_ + 5: $cont = cont(TrampolineFuncTypeIndex)
    MOZ_ASSERT(codeMeta->types->length() == baseTypeIndex_ + ContTypeIndex);
    if (!codeMeta->types->addType(ContType(&codeMeta->types->type(
            baseTypeIndex_ + TrampolineFuncTypeIndex)))) {
      ReportOutOfMemory(cx);
      return nullptr;
    }

    // Type baseTypeIndex_ + 6: tag func type (param externref)
    ValTypeVector tagParams, tagResults;
    if (!tagParams.emplaceBack(RefType::extern_())) {
      ReportOutOfMemory(cx);
      return nullptr;
    }
    MOZ_ASSERT(codeMeta->types->length() == baseTypeIndex_ + TagFuncTypeIndex);
    if (!codeMeta->types->addType(
            FuncType(std::move(tagParams), std::move(tagResults)))) {
      ReportOutOfMemory(cx);
      return nullptr;
    }

    // Type baseTypeIndex_ + 7: suspend block type () -> (externref, (ref
    // $cont))
    ValTypeVector suspendBlockParams, suspendBlockResults;
    if (!suspendBlockResults.emplaceBack(RefType::extern_()) ||
        !suspendBlockResults.emplaceBack(RefType::fromTypeDef(
            &codeMeta->types->type(baseTypeIndex_ + ContTypeIndex), false))) {
      ReportOutOfMemory(cx);
      return nullptr;
    }
    MOZ_ASSERT(codeMeta->types->length() ==
               baseTypeIndex_ + SuspendBlockTypeIndex);
    if (!codeMeta->types->addType(FuncType(std::move(suspendBlockParams),
                                           std::move(suspendBlockResults)))) {
      ReportOutOfMemory(cx);
      return nullptr;
    }

    // Func 3: $promising.reaction
    // addDefinedFunc creates Type baseTypeIndex_ + 8: reaction func type
    ValTypeVector reactionParams, reactionResults;
    if (!reactionParams.emplaceBack(RefType::fromTypeDef(
            &codeMeta->types->type(baseTypeIndex_ + ContTypeIndex), true)) ||
        !reactionParams.emplaceBack(RefType::extern_())) {
      ReportOutOfMemory(cx);
      return nullptr;
    }
    MOZ_ASSERT(codeMeta->funcs.length() == ReactionFnIndex);
    if (!moduleMeta->addDefinedFunc(std::move(reactionParams),
                                    std::move(reactionResults),
                                    /* declareForRef = */ true)) {
      ReportOutOfMemory(cx);
      return nullptr;
    }

    // Tag 0: $on-suspend
    MutableTagType tagType = js_new<TagType>();
    if (!tagType || !tagType->initialize(&(
                        *codeMeta->types)[baseTypeIndex_ + TagFuncTypeIndex])) {
      ReportOutOfMemory(cx);
      return nullptr;
    }
    if (!codeMeta->tags.emplaceBack(TagKind::Exception, tagType)) {
      ReportOutOfMemory(cx);
      return nullptr;
    }

    // Global 0: $promisingPromise (mut externref)
    if (!codeMeta->globals.append(
            GlobalDesc(InitExpr(LitVal(ValType(RefType::extern_()))),
                       /* isMutable = */ true))) {
      ReportOutOfMemory(cx);
      return nullptr;
    }

    // Global 1: $params (mut (ref null $params))
    if (!codeMeta->globals.append(GlobalDesc(
            InitExpr(LitVal(ValType(RefType::fromTypeDef(
                &codeMeta->types->type(baseTypeIndex_ + ParamsTypeIndex),
                true)))),
            /* isMutable = */ true))) {
      ReportOutOfMemory(cx);
      return nullptr;
    }

    if (!moduleMeta->prepareForCompile(compilerEnv.mode())) {
      ReportOutOfMemory(cx);
      return nullptr;
    }

    ModuleGenerator mg(*codeMeta, compilerEnv, compilerEnv.initialState(),
                       nullptr, nullptr, nullptr);
    if (!mg.initializeCompleteTier()) {
      ReportOutOfMemory(cx);
      return nullptr;
    }

    uint32_t funcBytecodeOffset = CallSite::FIRST_VALID_BYTECODE_OFFSET;

    Bytes bytecode;
    if (!encodeExportedFunction(*codeMeta, paramsSize, bytecode)) {
      ReportOutOfMemory(cx);
      return nullptr;
    }
    if (!mg.compileFuncDef(ExportedFnIndex, funcBytecodeOffset,
                           bytecode.begin(),
                           bytecode.begin() + bytecode.length())) {
      ReportOutOfMemory(cx);
      return nullptr;
    }
    funcBytecodeOffset += bytecode.length();

    Bytes bytecode2;
    if (!encodeTrampolineFunction(*codeMeta, paramsSize, bytecode2)) {
      ReportOutOfMemory(cx);
      return nullptr;
    }
    if (!mg.compileFuncDef(TrampolineFnIndex, funcBytecodeOffset,
                           bytecode2.begin(),
                           bytecode2.begin() + bytecode2.length())) {
      ReportOutOfMemory(cx);
      return nullptr;
    }
    funcBytecodeOffset += bytecode2.length();

    Bytes bytecode3;
    if (!encodeReactionFunction(*codeMeta, bytecode3)) {
      ReportOutOfMemory(cx);
      return nullptr;
    }
    if (!mg.compileFuncDef(ReactionFnIndex, funcBytecodeOffset,
                           bytecode3.begin(),
                           bytecode3.begin() + bytecode3.length())) {
      ReportOutOfMemory(cx);
      return nullptr;
    }

    if (!mg.finishFuncDefs()) {
      ReportOutOfMemory(cx);
      return nullptr;
    }

    SharedModule m = mg.finishModule(BytecodeBufferOrSource(), *moduleMeta,
                                     /*maybeCompleteTier2Listener=*/nullptr);
    if (!m) {
      ReportOutOfMemory(cx);
      return nullptr;
    }
    return m;
  }
};

// Wraps `$promising.exported` function so that it's not a host function as
// required by the spec.
static bool WasmPromisingFunction(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  Rooted<JSFunction*> callee(cx, &args.callee().as<JSFunction>());
  RootedFunction fn(
      cx,
      &callee->getExtendedSlot(WRAPPED_FN_SLOT).toObject().as<JSFunction>());

  if (Call(cx, UndefinedHandleValue, fn, args, args.rval())) {
    return true;
  }

  // The stack was unwound during exception. There should be no active
  // continuation.
  MOZ_RELEASE_ASSERT(!cx->wasm().currentStack());

  // Any errors from invoking the wasm function need to be converted to a
  // rejected promise.
  JSObject* newPromise = NewPromiseObject(cx, nullptr);
  if (!newPromise) {
    return false;
  }
  Rooted<PromiseObject*> promiseObject(cx, &newPromise->as<PromiseObject>());
  args.rval().setObject(*promiseObject);
  return RejectPromiseWithPendingError(cx, promiseObject);
}

JSFunction* WasmPromisingFunctionCreate(JSContext* cx, HandleObject func) {
  RootedFunction wrappedWasmFunc(cx, &func->as<JSFunction>());
  MOZ_ASSERT(wrappedWasmFunc->isWasm());

  PromisingFunctionModuleFactory moduleFactory;
  SharedModule module = moduleFactory.build(cx, wrappedWasmFunc);
  if (!module) {
    return nullptr;
  }
  // Instantiate the module.
  Rooted<ImportValues> imports(cx);

  // Add wrapped function ($promising.wrappedfn) to imports.
  if (!imports.get().funcs.append(func)) {
    ReportOutOfMemory(cx);
    return nullptr;
  }

  // Add $on-suspend to imports.
  Rooted<WasmNamespaceObject*> wasmNamespace(
      cx, WasmNamespaceObject::getOrCreate(cx));
  if (!wasmNamespace) {
    ReportOutOfMemory(cx);
    return nullptr;
  }
  if (!imports.get().tagObjs.append(wasmNamespace->jsPromiseTag())) {
    ReportOutOfMemory(cx);
    return nullptr;
  }

  Rooted<WasmInstanceObject*> instance(cx);
  if (!module->instantiate(cx, imports.get(), nullptr, &instance)) {
    MOZ_ASSERT(cx->isThrowingOutOfMemory());
    return nullptr;
  }

  // Wrap $promising.exported function for exceptions/traps handling.
  RootedFunction wasmFunc(cx);
  if (!WasmInstanceObject::getExportedFunction(
          cx, instance, PromisingFunctionModuleFactory::ExportedFnIndex,
          &wasmFunc)) {
    return nullptr;
  }

  RootedFunction wasmFuncWrapper(
      cx, NewNativeFunction(cx, WasmPromisingFunction, 0, nullptr,
                            gc::AllocKind::FUNCTION_EXTENDED, GenericObject));
  if (!wasmFuncWrapper) {
    return nullptr;
  }
  wasmFuncWrapper->initExtendedSlot(WRAPPED_FN_SLOT, ObjectValue(*wasmFunc));
  return wasmFuncWrapper;
}

// Reaction on fulfilled suspending promise. This will call $promising.reaction.
static bool WasmPromiseReaction(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  Rooted<JSFunction*> callee(cx, &args.callee().as<JSFunction>());
  RootedFunction reactionFunc(
      cx, &callee->getExtendedSlot(REACTION_SLOT).toObject().as<JSFunction>());
  Rooted<PromiseObject*> promisingPromiseObject(
      cx, &callee->getExtendedSlot(PROMISING_PROMISE_SLOT)
               .toObject()
               .as<PromiseObject>());
  JS::RootedValueArray<2> argv(cx);
  JS::Rooted<JS::Value> rval(cx);
  argv[0].set(callee->getExtendedSlot(CONT_SLOT));
  argv[1].set(ObjectValue(*promisingPromiseObject));

  if (Call(cx, UndefinedHandleValue, reactionFunc, argv, &rval)) {
    return true;
  }

  // The stack was unwound during exception.
  MOZ_RELEASE_ASSERT(!cx->wasm().currentStack());

  // Any errors from invoking the wasm function need to be converted to a
  // rejected promise.
  return RejectPromiseWithPendingError(cx, promisingPromiseObject);
}

// Creates a promise.
//
// Seen as $builtin.create-promise to wasm.
void* CreatePromise(Instance* instance) {
  MOZ_ASSERT(SASigCreatePromise.failureMode == FailureMode::FailOnNullPtr);
  JSContext* cx = instance->cx();
  JSObject* promise = NewPromiseObject(cx, nullptr);
  if (!promise) {
    MOZ_ASSERT(cx->isExceptionPending());
    return nullptr;
  }
  return AnyRef::fromJSObject(*promise).forCompiledCode();
}

// Converts promise results into actual function result, or exception/trap
// if rejected.
//
// Seen as $builtin.get-promise-results to wasm.
void* GetPromiseResults(Instance* instance, void* promiseRef,
                        uint32_t typeIndex) {
  MOZ_ASSERT(SASigGetPromiseResults.failureMode == FailureMode::FailOnNullPtr);
  JSContext* cx = instance->cx();

  JSObject* promiseObj = &AnyRef::fromCompiledCode(promiseRef).toJSObject();
  Rooted<PromiseObject*> promise(
      cx, UnwrapAndDowncastObject<PromiseObject>(cx, promiseObj));
  if (!promise) {
    return nullptr;
  }
  bool promiseRejected = promise->state() == JS::PromiseState::Rejected;
  RootedValue promiseReasonOrValue(cx, promise->valueOrReason());
  if (!cx->compartment()->wrap(cx, &promiseReasonOrValue)) {
    return nullptr;
  }

  if (promiseRejected) {
    cx->setPendingException(promiseReasonOrValue, ShouldCaptureStack::Maybe);
    return nullptr;
  }

  MOZ_ASSERT(promise->state() == JS::PromiseState::Fulfilled);
  RootedValue jsValue(cx, promiseReasonOrValue);

  // Construct the results object.
  Rooted<WasmStructObject*> results(
      cx, instance->constantStructNewDefault(cx, typeIndex));
  if (!results) {
    return nullptr;
  }
  const FieldTypeVector& fields = results->typeDef().structType().fields_;

  if (fields.length() > 0) {
    // The struct object is constructed based on returns of exported function.
    // It is the only way we can get ValType for Val::fromJSValue call.
    const wasm::FuncType& sig = instance->codeMeta().getFuncType(
        SuspendingFunctionModuleFactory::ExportedFnIndex);

    if (fields.length() == 1) {
      RootedVal val(cx);
      MOZ_ASSERT(sig.result(0).storageType() == fields[0].type);
      if (!Val::fromJSValue(cx, sig.result(0), jsValue, &val)) {
        return nullptr;
      }
      results->storeVal(val, 0);
    } else {
      // The multi-value result is wrapped into ArrayObject/Iterable.
      Rooted<ArrayObject*> array(cx, IterableToArray(cx, jsValue));
      if (!array) {
        return nullptr;
      }
      if (fields.length() != array->length()) {
        UniqueChars expected(JS_smprintf("%zu", fields.length()));
        UniqueChars got(JS_smprintf("%u", array->length()));
        if (!expected || !got) {
          ReportOutOfMemory(cx);
          return nullptr;
        }

        JS_ReportErrorNumberUTF8(cx, GetErrorMessage, nullptr,
                                 JSMSG_WASM_WRONG_NUMBER_OF_VALUES,
                                 expected.get(), got.get());
        return nullptr;
      }

      for (size_t i = 0; i < fields.length(); i++) {
        RootedVal val(cx);
        RootedValue v(cx, array->getDenseElement(i));
        MOZ_ASSERT(sig.result(i).storageType() == fields[i].type);
        if (!Val::fromJSValue(cx, sig.result(i), v, &val)) {
          return nullptr;
        }
        results->storeVal(val, i);
      }
    }
  }

  return AnyRef::fromCompiledCode(results).forCompiledCode();
}

// Collects returned suspending promising, and registers callbacks to
// react on it using WasmPromiseReaction.
//
// Seen as $builtin.add-promise-reactions to wasm.
int32_t AddPromiseReactions(Instance* instance, void* promiseRef, void* contRef,
                            void* reactionRef, void* promisingPromiseRef) {
  MOZ_ASSERT(SASigAddPromiseReactions.failureMode == FailureMode::FailOnNegI32);
  JSContext* cx = instance->cx();
  RootedObject promiseObject(
      cx, &AnyRef::fromCompiledCode(promiseRef).toJSObject());
  if (IsProxy(promiseObject) &&
      JS_IsDeadWrapper(UncheckedUnwrap(promiseObject))) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr, JSMSG_DEAD_OBJECT);
    return -1;
  }
  Rooted<ContObject*> contObject(
      cx, &AnyRef::fromCompiledCode(contRef).toJSObject().as<ContObject>());
  RootedFunction reactionFunc(
      cx, &AnyRef::fromCompiledCode(reactionRef).toJSObject().as<JSFunction>());
  Rooted<PromiseObject*> promisingPromise(
      cx, &AnyRef::fromCompiledCode(promisingPromiseRef)
               .toJSObject()
               .as<PromiseObject>());

  // Build a `then` function
  RootedFunction then_(
      cx, NewNativeFunction(cx, WasmPromiseReaction, 1, nullptr,
                            gc::AllocKind::FUNCTION_EXTENDED, GenericObject));
  if (!then_) {
    return -1;
  }
  then_->initExtendedSlot(CONT_SLOT, ObjectValue(*contObject));
  then_->initExtendedSlot(REACTION_SLOT, ObjectValue(*reactionFunc));
  then_->initExtendedSlot(PROMISING_PROMISE_SLOT,
                          ObjectValue(*promisingPromise));

  // Add the `then` function as a promise reaction
  if (!JS::AddPromiseReactions(cx, promiseObject, then_, then_)) {
    MOZ_ASSERT(cx->isExceptionPending());
    return -1;
  }
  return 0;
}

// Calls `Promise.resolve(value)`.
//
// Seen as $builtin.promise-resolve to wasm.
void* PromiseResolve(Instance* instance, void* valueRef) {
  MOZ_ASSERT(SASigPromiseResolve.failureMode == FailureMode::FailOnNullPtr);
  JSContext* cx = instance->cx();
  RootedObject promiseConstructor(cx, GetPromiseConstructor(cx));
  RootedValue value(cx, AnyRef::fromCompiledCode(valueRef).toJSValue());
  RootedObject promise(cx, PromiseResolve(cx, promiseConstructor, value));
  if (!promise) {
    MOZ_ASSERT(cx->isExceptionPending());
    return nullptr;
  }
  return AnyRef::fromJSObject(*promise).forCompiledCode();
}

// Resolves the promise using results packed by wasm.
//
// Seen as $builtin.resolve-promise-with-results to wasm.
int32_t ResolvePromiseWithResults(Instance* instance, void* resultsRef,
                                  void* promiseRef) {
  MOZ_ASSERT(SASigResolvePromiseWithResults.failureMode ==
             FailureMode::FailOnNegI32);
  JSContext* cx = instance->cx();
  RootedObject promise(cx, &AnyRef::fromCompiledCode(promiseRef).toJSObject());
  Rooted<WasmStructObject*> results(cx, &AnyRef::fromCompiledCode(resultsRef)
                                             .toJSObject()
                                             .as<WasmStructObject>());

  const StructType& resultType = results->typeDef().structType();

  RootedValue val(cx);
  // Unbox the result value from the struct, if any.
  switch (resultType.fields_.length()) {
    case 0:
      break;
    case 1: {
      if (!results->getField(cx, /*index=*/0, &val)) {
        return -1;
      }
    } break;
    default: {
      Rooted<ArrayObject*> array(cx, NewDenseEmptyArray(cx));
      if (!array) {
        return -1;
      }
      for (size_t i = 0; i < resultType.fields_.length(); i++) {
        RootedValue item(cx);
        if (!results->getField(cx, i, &item)) {
          return -1;
        }
        if (!NewbornArrayPush(cx, array, item)) {
          return -1;
        }
      }
      val.setObject(*array);
    } break;
  }

  if (!ResolvePromise(cx, promise, val)) {
    MOZ_ASSERT(cx->isExceptionPending());
    return -1;
  }
  return 0;
}

}  // namespace js::wasm
#endif  // ENABLE_WASM_JSPI
