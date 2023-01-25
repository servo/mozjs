/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: set ts=8 sts=2 et sw=2 tw=80:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef builtin_ModuleObject_h
#define builtin_ModuleObject_h

#include "mozilla/HashTable.h"  // mozilla::{HashMap, DefaultHasher}
#include "mozilla/Maybe.h"      // mozilla::Maybe

#include <stddef.h>  // size_t
#include <stdint.h>  // int32_t, uint32_t

#include "gc/Barrier.h"        // HeapPtr
#include "gc/ZoneAllocator.h"  // CellAllocPolicy
#include "js/Class.h"          // JSClass, ObjectOpResult
#include "js/GCVector.h"
#include "js/Id.h"  // jsid
#include "js/Modules.h"
#include "js/Proxy.h"       // BaseProxyHandler
#include "js/RootingAPI.h"  // Rooted, Handle, MutableHandle
#include "js/TypeDecls.h"  // HandleValue, HandleId, HandleObject, HandleScript, MutableHandleValue, MutableHandleIdVector, MutableHandleObject
#include "js/UniquePtr.h"  // UniquePtr
#include "vm/JSObject.h"   // JSObject
#include "vm/NativeObject.h"   // NativeObject
#include "vm/ProxyObject.h"    // ProxyObject
#include "vm/SharedStencil.h"  // FunctionDeclarationVector

class JSAtom;
class JSScript;
class JSTracer;

namespace JS {
class PropertyDescriptor;
class Value;
}  // namespace JS

namespace js {

class ArrayObject;
class CyclicModuleFields;
class ListObject;
class ModuleEnvironmentObject;
class ModuleObject;
class PromiseObject;
class ScriptSourceObject;

class ModuleRequestObject : public NativeObject {
 public:
  enum { SpecifierSlot = 0, AssertionSlot, SlotCount };

  static const JSClass class_;
  static bool isInstance(HandleValue value);
  [[nodiscard]] static ModuleRequestObject* create(
      JSContext* cx, Handle<JSAtom*> specifier,
      Handle<ArrayObject*> maybeAssertions);

  JSAtom* specifier() const;
  ArrayObject* assertions() const;
};

class ImportEntry {
  const HeapPtr<ModuleRequestObject*> moduleRequest_;
  const HeapPtr<JSAtom*> importName_;
  const HeapPtr<JSAtom*> localName_;
  const uint32_t lineNumber_;
  const uint32_t columnNumber_;

 public:
  ImportEntry(Handle<ModuleRequestObject*> moduleRequest,
              Handle<JSAtom*> maybeImportName, Handle<JSAtom*> localName,
              uint32_t lineNumber, uint32_t columnNumber);

  ModuleRequestObject* moduleRequest() const { return moduleRequest_; }
  JSAtom* importName() const { return importName_; }
  JSAtom* localName() const { return localName_; }
  uint32_t lineNumber() const { return lineNumber_; }
  uint32_t columnNumber() const { return columnNumber_; }

  void trace(JSTracer* trc);
};

using ImportEntryVector = GCVector<ImportEntry, 0, SystemAllocPolicy>;

class ExportEntry {
  const HeapPtr<JSAtom*> exportName_;
  const HeapPtr<ModuleRequestObject*> moduleRequest_;
  const HeapPtr<JSAtom*> importName_;
  const HeapPtr<JSAtom*> localName_;
  const uint32_t lineNumber_;
  const uint32_t columnNumber_;

 public:
  ExportEntry(Handle<JSAtom*> maybeExportName,
              Handle<ModuleRequestObject*> maybeModuleRequest,
              Handle<JSAtom*> maybeImportName, Handle<JSAtom*> maybeLocalName,
              uint32_t lineNumber, uint32_t columnNumber);
  JSAtom* exportName() const { return exportName_; }
  ModuleRequestObject* moduleRequest() const { return moduleRequest_; }
  JSAtom* importName() const { return importName_; }
  JSAtom* localName() const { return localName_; }
  uint32_t lineNumber() const { return lineNumber_; }
  uint32_t columnNumber() const { return columnNumber_; }

  void trace(JSTracer* trc);
};

using ExportEntryVector = GCVector<ExportEntry, 0, SystemAllocPolicy>;

class RequestedModule {
  const HeapPtr<ModuleRequestObject*> moduleRequest_;
  const uint32_t lineNumber_;
  const uint32_t columnNumber_;

 public:
  RequestedModule(Handle<ModuleRequestObject*> moduleRequest,
                  uint32_t lineNumber, uint32_t columnNumber);
  ModuleRequestObject* moduleRequest() const { return moduleRequest_; }
  uint32_t lineNumber() const { return lineNumber_; }
  uint32_t columnNumber() const { return columnNumber_; }

  void trace(JSTracer* trc);
};

using RequestedModuleVector = GCVector<RequestedModule, 0, SystemAllocPolicy>;

class ResolvedBindingObject : public NativeObject {
 public:
  enum { ModuleSlot = 0, BindingNameSlot, SlotCount };

  static const JSClass class_;
  static bool isInstance(HandleValue value);
  static ResolvedBindingObject* create(JSContext* cx,
                                       Handle<ModuleObject*> module,
                                       Handle<JSAtom*> bindingName);
  ModuleObject* module() const;
  JSAtom* bindingName() const;
};

class IndirectBindingMap {
 public:
  void trace(JSTracer* trc);

  bool put(JSContext* cx, HandleId name,
           Handle<ModuleEnvironmentObject*> environment, HandleId targetName);

  size_t count() const { return map_ ? map_->count() : 0; }

  bool has(jsid name) const { return map_ ? map_->has(name) : false; }

  bool lookup(jsid name, ModuleEnvironmentObject** envOut,
              mozilla::Maybe<PropertyInfo>* propOut) const;

  template <typename Func>
  void forEachExportedName(Func func) const {
    if (!map_) {
      return;
    }

    for (auto r = map_->all(); !r.empty(); r.popFront()) {
      func(r.front().key());
    }
  }

 private:
  struct Binding {
    Binding(ModuleEnvironmentObject* environment, jsid targetName,
            PropertyInfo prop);
    HeapPtr<ModuleEnvironmentObject*> environment;
#ifdef DEBUG
    HeapPtr<jsid> targetName;
#endif
    PropertyInfo prop;
  };

  using Map = mozilla::HashMap<PreBarriered<jsid>, Binding,
                               mozilla::DefaultHasher<PreBarriered<jsid>>,
                               CellAllocPolicy>;

  mozilla::Maybe<Map> map_;
};

class ModuleNamespaceObject : public ProxyObject {
 public:
  enum ModuleNamespaceSlot { ExportsSlot = 0, BindingsSlot };

  static bool isInstance(HandleValue value);
  static ModuleNamespaceObject* create(JSContext* cx,
                                       Handle<ModuleObject*> module,
                                       Handle<ArrayObject*> exports,
                                       UniquePtr<IndirectBindingMap> bindings);

  ModuleObject& module();
  ArrayObject& exports();
  IndirectBindingMap& bindings();

  bool addBinding(JSContext* cx, Handle<JSAtom*> exportedName,
                  Handle<ModuleObject*> targetModule,
                  Handle<JSAtom*> targetName);

 private:
  struct ProxyHandler : public BaseProxyHandler {
    ProxyHandler();

    bool getOwnPropertyDescriptor(
        JSContext* cx, HandleObject proxy, HandleId id,
        MutableHandle<mozilla::Maybe<PropertyDescriptor>> desc) const override;
    bool defineProperty(JSContext* cx, HandleObject proxy, HandleId id,
                        Handle<PropertyDescriptor> desc,
                        ObjectOpResult& result) const override;
    bool ownPropertyKeys(JSContext* cx, HandleObject proxy,
                         MutableHandleIdVector props) const override;
    bool delete_(JSContext* cx, HandleObject proxy, HandleId id,
                 ObjectOpResult& result) const override;
    bool getPrototype(JSContext* cx, HandleObject proxy,
                      MutableHandleObject protop) const override;
    bool setPrototype(JSContext* cx, HandleObject proxy, HandleObject proto,
                      ObjectOpResult& result) const override;
    bool getPrototypeIfOrdinary(JSContext* cx, HandleObject proxy,
                                bool* isOrdinary,
                                MutableHandleObject protop) const override;
    bool setImmutablePrototype(JSContext* cx, HandleObject proxy,
                               bool* succeeded) const override;

    bool preventExtensions(JSContext* cx, HandleObject proxy,
                           ObjectOpResult& result) const override;
    bool isExtensible(JSContext* cx, HandleObject proxy,
                      bool* extensible) const override;
    bool has(JSContext* cx, HandleObject proxy, HandleId id,
             bool* bp) const override;
    bool get(JSContext* cx, HandleObject proxy, HandleValue receiver,
             HandleId id, MutableHandleValue vp) const override;
    bool set(JSContext* cx, HandleObject proxy, HandleId id, HandleValue v,
             HandleValue receiver, ObjectOpResult& result) const override;

    void trace(JSTracer* trc, JSObject* proxy) const override;
    void finalize(JS::GCContext* gcx, JSObject* proxy) const override;

    static const char family;
  };

  bool hasBindings() const;

 public:
  static const ProxyHandler proxyHandler;
};

// Value types of [[Status]] in a Cyclic Module Record
// https://tc39.es/ecma262/#table-cyclic-module-fields
enum class ModuleStatus : int8_t {
  Unlinked,
  Linking,
  Linked,
  Evaluating,
  EvaluatingAsync,
  Evaluated,

  // Sub-state of Evaluated with error value set.
  //
  // This is not returned from ModuleObject::status(); use hadEvaluationError()
  // to check this.
  Evaluated_Error
};

// Special values for CyclicModuleFields' asyncEvaluatingPostOrderSlot field,
// which is used as part of the implementation of the AsyncEvaluation field of
// cyclic module records.
//
// The spec requires us to be able to tell the order in which the field was set
// to true for async evaluating modules.
//
// This is arranged by using an integer to record the order. After evaluation is
// complete the value is set to ASYNC_EVALUATING_POST_ORDER_CLEARED.
//
// See https://tc39.es/ecma262/#sec-cyclic-module-records for field defintion.
// See https://tc39.es/ecma262/#sec-async-module-execution-fulfilled for sort
// requirement.

// Initial value for the runtime's counter used to generate these values.
constexpr uint32_t ASYNC_EVALUATING_POST_ORDER_INIT = 1;

// Value that the field is set to after being cleared.
constexpr uint32_t ASYNC_EVALUATING_POST_ORDER_CLEARED = 0;

class ModuleObject : public NativeObject {
 public:
  // Module fields including those for AbstractModuleRecords described by:
  // https://tc39.es/ecma262/#sec-abstract-module-records
  enum ModuleSlot {
    ScriptSlot = 0,
    EnvironmentSlot,
    NamespaceSlot,
    CyclicModuleFieldsSlot,
    SlotCount
  };

  static const JSClass class_;

  static bool isInstance(HandleValue value);

  static ModuleObject* create(JSContext* cx);

  // Initialize the slots on this object that are dependent on the script.
  void initScriptSlots(HandleScript script);

  void setInitialEnvironment(
      Handle<ModuleEnvironmentObject*> initialEnvironment);

  void initFunctionDeclarations(UniquePtr<FunctionDeclarationVector> decls);
  void initImportExportData(
      MutableHandle<RequestedModuleVector> requestedModules,
      MutableHandle<ImportEntryVector> importEntries,
      MutableHandle<ExportEntryVector> localExportEntries,
      MutableHandle<ExportEntryVector> indirectExportEntries,
      MutableHandle<ExportEntryVector> starExportEntries);
  static bool Freeze(JSContext* cx, Handle<ModuleObject*> self);
#ifdef DEBUG
  static bool AssertFrozen(JSContext* cx, Handle<ModuleObject*> self);
#endif

  JSScript* maybeScript() const;
  JSScript* script() const;
  ModuleEnvironmentObject& initialEnvironment() const;
  ModuleEnvironmentObject* environment() const;
  ModuleNamespaceObject* namespace_();
  ModuleStatus status() const;
  mozilla::Maybe<uint32_t> maybeDfsIndex() const;
  uint32_t dfsIndex() const;
  mozilla::Maybe<uint32_t> maybeDfsAncestorIndex() const;
  uint32_t dfsAncestorIndex() const;
  bool hadEvaluationError() const;
  Value maybeEvaluationError() const;
  Value evaluationError() const;
  JSObject* metaObject() const;
  ScriptSourceObject* scriptSourceObject() const;
  const RequestedModuleVector& requestedModules() const;
  const ImportEntryVector& importEntries() const;
  const ExportEntryVector& localExportEntries() const;
  const ExportEntryVector& indirectExportEntries() const;
  const ExportEntryVector& starExportEntries() const;
  IndirectBindingMap& importBindings();

  void setStatus(ModuleStatus newStatus);
  void setDfsIndex(uint32_t index);
  void setDfsAncestorIndex(uint32_t index);
  void clearDfsIndexes();

  static PromiseObject* createTopLevelCapability(JSContext* cx,
                                                 Handle<ModuleObject*> module);
  bool hasTopLevelAwait() const;
  bool isAsyncEvaluating() const;
  void setAsyncEvaluating();
  void setEvaluationError(HandleValue newValue);
  void setPendingAsyncDependencies(uint32_t newValue);
  void setInitialTopLevelCapability(Handle<PromiseObject*> capability);
  bool hasTopLevelCapability() const;
  PromiseObject* maybeTopLevelCapability() const;
  PromiseObject* topLevelCapability() const;
  ListObject* asyncParentModules() const;
  mozilla::Maybe<uint32_t> maybePendingAsyncDependencies() const;
  uint32_t pendingAsyncDependencies() const;
  mozilla::Maybe<uint32_t> maybeAsyncEvaluatingPostOrder() const;
  uint32_t getAsyncEvaluatingPostOrder() const;
  void clearAsyncEvaluatingPostOrder();
  void setCycleRoot(ModuleObject* cycleRoot);
  ModuleObject* getCycleRoot() const;

  static void onTopLevelEvaluationFinished(ModuleObject* module);

  static bool appendAsyncParentModule(JSContext* cx, Handle<ModuleObject*> self,
                                      Handle<ModuleObject*> parent);

  [[nodiscard]] static bool topLevelCapabilityResolve(
      JSContext* cx, Handle<ModuleObject*> module);
  [[nodiscard]] static bool topLevelCapabilityReject(
      JSContext* cx, Handle<ModuleObject*> module, HandleValue error);

  void setMetaObject(JSObject* obj);

  static bool instantiateFunctionDeclarations(JSContext* cx,
                                              Handle<ModuleObject*> self);

  static bool execute(JSContext* cx, Handle<ModuleObject*> self);

  static ModuleNamespaceObject* createNamespace(JSContext* cx,
                                                Handle<ModuleObject*> self,
                                                HandleObject exports);

  static bool createEnvironment(JSContext* cx, Handle<ModuleObject*> self);

  void initAsyncSlots(JSContext* cx, bool hasTopLevelAwait,
                      Handle<ListObject*> asyncParentModules);

  static bool GatherAsyncParentCompletions(
      JSContext* cx, Handle<ModuleObject*> module,
      MutableHandle<ArrayObject*> execList);

 private:
  static const JSClassOps classOps_;

  static void trace(JSTracer* trc, JSObject* obj);
  static void finalize(JS::GCContext* gcx, JSObject* obj);

  bool hasCyclicModuleFields() const;
  CyclicModuleFields* cyclicModuleFields();
  const CyclicModuleFields* cyclicModuleFields() const;
};

JSObject* GetOrCreateModuleMetaObject(JSContext* cx, HandleObject module);

ModuleObject* CallModuleResolveHook(JSContext* cx,
                                    HandleValue referencingPrivate,
                                    HandleObject moduleRequest);

JSObject* StartDynamicModuleImport(JSContext* cx, HandleScript script,
                                   HandleValue specifier, HandleValue options);

bool OnModuleEvaluationFailure(JSContext* cx, HandleObject evaluationPromise,
                               JS::ModuleErrorBehaviour errorBehaviour);

bool FinishDynamicModuleImport(JSContext* cx, HandleObject evaluationPromise,
                               HandleValue referencingPrivate,
                               HandleObject moduleRequest,
                               HandleObject promise);

}  // namespace js

template <>
inline bool JSObject::is<js::ModuleNamespaceObject>() const {
  return js::IsDerivedProxyObject(this,
                                  &js::ModuleNamespaceObject::proxyHandler);
}

#endif /* builtin_ModuleObject_h */
