/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jit_JitZone_h
#define jit_JitZone_h

#include "mozilla/Assertions.h"
#include "mozilla/EnumeratedArray.h"
#include "mozilla/HashFunctions.h"
#include "mozilla/LinkedList.h"
#include "mozilla/Maybe.h"
#include "mozilla/MemoryReporting.h"

#include <stddef.h>
#include <stdint.h>
#include <utility>

#include "gc/Barrier.h"
#include "gc/Marking.h"
#include "gc/WeakMap.h"
#include "jit/CacheIRAOT.h"
#include "jit/ExecutableAllocator.h"
#include "jit/ICStubSpace.h"
#include "jit/Invalidation.h"
#include "jit/JitScript.h"
#include "js/AllocPolicy.h"
#include "js/GCHashTable.h"
#include "js/HashTable.h"
#include "js/TracingAPI.h"
#include "js/TypeDecls.h"
#include "js/UniquePtr.h"
#include "js/Utility.h"
#include "threading/ProtectedData.h"

namespace JS {
struct CodeSizes;
}

namespace js {

class BaseScript;

namespace jit {

enum class CacheKind : uint8_t;
class CacheIRStubInfo;
class JitCode;
class JitScript;

/*
 *  The EntryTrampolineMap is used to cache the trampoline code for each script
 *  as they are created.  These trampolines are created only under
 *  --emit-interpreter-entry and are used to identify which script is being
 *  interpeted when profiling with external profilers such as perf.
 *
 *  This is a weak map keyed by script. The map keeps the JitCode values alive
 *  as long as the script is alive. When a script is collected, its entry is
 *  automatically removed.
 */
using EntryTrampolineMap = WeakMap<BaseScript*, JitCode*, ZoneAllocPolicy>;

enum class ICStubEngine : uint8_t {
  // Baseline IC, see BaselineIC.h.
  Baseline = 0,

  // Ion IC, see IonIC.h.
  IonIC
};

struct CacheIRStubKey : public DefaultHasher<CacheIRStubKey> {
  struct Lookup {
    CacheKind kind;
    ICStubEngine engine;
    const uint8_t* code;
    uint32_t length;

    Lookup(CacheKind kind, ICStubEngine engine, const uint8_t* code,
           uint32_t length)
        : kind(kind), engine(engine), code(code), length(length) {}
  };

  static HashNumber hash(const Lookup& l);
  static bool match(const CacheIRStubKey& entry, const Lookup& l);

  UniquePtr<CacheIRStubInfo, JS::FreePolicy> stubInfo;

  explicit CacheIRStubKey(CacheIRStubInfo* info) : stubInfo(info) {}
  CacheIRStubKey(CacheIRStubKey&& other)
      : stubInfo(std::move(other.stubInfo)) {}

  void operator=(CacheIRStubKey&& other) {
    stubInfo = std::move(other.stubInfo);
  }
};

struct BaselineCacheIRStubCodeMapGCPolicy {
  static bool traceWeak(JSTracer* trc, CacheIRStubKey*,
                        WeakHeapPtr<JitCode*>* value) {
    return TraceWeakEdge(trc, value, "traceWeak");
  }
};

enum JitScriptFilter : bool { SkipDyingScripts, IncludeDyingScripts };

class JitZone {
 public:
  enum class StubKind : uint32_t {
    StringConcat = 0,
    RegExpMatcher,
    RegExpSearcher,
    RegExpExecMatch,
    RegExpExecTest,
    Count
  };
  template <typename Code>
  using Stubs =
      mozilla::EnumeratedArray<StubKind, Code, size_t(StubKind::Count)>;

 private:
  // Allocated space for CacheIR stubs.
  ICStubSpace stubSpace_;

  // Set of CacheIRStubInfo instances used by Ion stubs in this Zone.
  using IonCacheIRStubInfoSet =
      HashSet<CacheIRStubKey, CacheIRStubKey, SystemAllocPolicy>;
  IonCacheIRStubInfoSet ionCacheIRStubInfoSet_;

  // Map CacheIRStubKey to shared JitCode objects.
  using BaselineCacheIRStubCodeMap =
      GCHashMap<CacheIRStubKey, WeakHeapPtr<JitCode*>, CacheIRStubKey,
                SystemAllocPolicy, BaselineCacheIRStubCodeMapGCPolicy>;
  BaselineCacheIRStubCodeMap baselineCacheIRStubCodes_;

  // Executable allocator for all code except wasm code.
  MainThreadData<ExecutableAllocator> execAlloc_;

  // HashMap that maps scripts to compilations inlining those scripts.
  using InlinedScriptMap =
      GCHashMap<WeakHeapPtr<BaseScript*>, IonScriptKeyVector,
                StableCellHasher<WeakHeapPtr<BaseScript*>>, SystemAllocPolicy>;
  InlinedScriptMap inlinedCompilations_;

  mozilla::LinkedList<JitScript> jitScripts_;

  // The following two fields are a pair of associated scripts. If they are
  // non-null, we have bailed out from MGuardMultipleShapes. The inner and outer
  // scripts are either the same script (when no inlining happened) or else the
  // inner script was inlined into the outer script.
  //
  // This is used to distinguish a bailout from MGuardShapeList vs
  // MGuardMultipleShapes, and for monomorphic inlining we need to track the
  // outer script that inlined the inner script.
  WeakHeapPtr<JSScript*> lastStubFoldingBailoutInner_;
  WeakHeapPtr<JSScript*> lastStubFoldingBailoutOuter_;

  // The JitZone stores stubs to concatenate strings inline and perform RegExp
  // calls inline. These bake in zone specific pointers and can't be stored in
  // JitRuntime. They also are dependent on the value of 'initialStringHeap' and
  // must be flushed when its value changes.
  //
  // These are weak pointers. Ion compilations store strong references to stubs
  // they depend on in WarpSnapshot.
  Stubs<WeakHeapPtr<JitCode*>> stubs_;

  // Map used to cache entry trampolines for scripts, for external profiling to
  // identify which functions are being interpreted.
  js::UniquePtr<EntryTrampolineMap> interpreterEntryMap;

  mozilla::Maybe<IonCompilationId> currentCompilationId_;
  bool keepJitScripts_ = false;

  // Whether AOT IC loading failed due to OOM; if so, disable
  // enforcing-AOT checks.
  bool incompleteAOTICs_ = false;

  gc::Heap initialStringHeap = gc::Heap::Tenured;

  JitCode* generateStringConcatStub(JSContext* cx);
  JitCode* generateRegExpMatcherStub(JSContext* cx);
  JitCode* generateRegExpSearcherStub(JSContext* cx);
  JitCode* generateRegExpExecMatchStub(JSContext* cx);
  JitCode* generateRegExpExecTestStub(JSContext* cx);

 public:
  explicit JitZone(JSContext* cx, bool zoneHasNurseryStrings) {
    setStringsCanBeInNursery(zoneHasNurseryStrings);
#ifdef ENABLE_JS_AOT_ICS
    js::jit::FillAOTICs(cx, this);
#endif
  }
  ~JitZone() {
    MOZ_ASSERT(jitScripts_.isEmpty());
    MOZ_ASSERT(!keepJitScripts_);
    MOZ_ASSERT_IF(interpreterEntryMap, interpreterEntryMap->empty());
  }

  void traceWeak(JSTracer* trc, Zone* zone);

  void addSizeOfIncludingThis(mozilla::MallocSizeOf mallocSizeOf,
                              JS::CodeSizes* code, size_t* jitZone,
                              size_t* cacheIRStubs) const;

  ICStubSpace* stubSpace() { return &stubSpace_; }

  JitCode* getBaselineCacheIRStubCode(const CacheIRStubKey::Lookup& key,
                                      CacheIRStubInfo** stubInfo) {
    auto p = baselineCacheIRStubCodes_.lookup(key);
    if (p) {
      *stubInfo = p->key().stubInfo.get();
      return p->value();
    }
    *stubInfo = nullptr;
    return nullptr;
  }
  [[nodiscard]] bool putBaselineCacheIRStubCode(
      const CacheIRStubKey::Lookup& lookup, CacheIRStubKey& key,
      JitCode* stubCode) {
    auto p = baselineCacheIRStubCodes_.lookupForAdd(lookup);
    MOZ_ASSERT(!p);
    return baselineCacheIRStubCodes_.add(p, std::move(key), stubCode);
  }

  CacheIRStubInfo* getIonCacheIRStubInfo(const CacheIRStubKey::Lookup& key) {
    IonCacheIRStubInfoSet::Ptr p = ionCacheIRStubInfoSet_.lookup(key);
    return p ? p->stubInfo.get() : nullptr;
  }
  [[nodiscard]] bool putIonCacheIRStubInfo(const CacheIRStubKey::Lookup& lookup,
                                           CacheIRStubKey& key) {
    IonCacheIRStubInfoSet::AddPtr p =
        ionCacheIRStubInfoSet_.lookupForAdd(lookup);
    MOZ_ASSERT(!p);
    return ionCacheIRStubInfoSet_.add(p, std::move(key));
  }
  void purgeIonCacheIRStubInfo() { ionCacheIRStubInfoSet_.clearAndCompact(); }

  ExecutableAllocator& execAlloc() { return execAlloc_.ref(); }
  const ExecutableAllocator& execAlloc() const { return execAlloc_.ref(); }

  [[nodiscard]] bool addInlinedCompilation(const IonScriptKey& ionScriptKey,
                                           JSScript* inlined);

  IonScriptKeyVector* maybeInlinedCompilations(JSScript* inlined) {
    auto p = inlinedCompilations_.lookup(inlined);
    return p ? &p->value() : nullptr;
  }

  void removeInlinedCompilations(JSScript* inlined) {
    inlinedCompilations_.remove(inlined);
  }

  void noteStubFoldingBailout(JSScript* inner, JSScript* outer) {
    lastStubFoldingBailoutInner_ = inner;
    lastStubFoldingBailoutOuter_ = outer;
  }
  bool hasStubFoldingBailoutData(JSScript* inner) const {
    return lastStubFoldingBailoutInner_ &&
           lastStubFoldingBailoutInner_.get() == inner &&
           lastStubFoldingBailoutOuter_;
  }
  JSScript* stubFoldingBailoutOuter() const {
    MOZ_ASSERT(lastStubFoldingBailoutInner_);
    return lastStubFoldingBailoutOuter_.get();
  }
  void clearStubFoldingBailoutData() {
    lastStubFoldingBailoutInner_ = nullptr;
    lastStubFoldingBailoutOuter_ = nullptr;
  }

  void registerJitScript(JitScript* script) { jitScripts_.insertBack(script); }

  // Iterate over all JitScripts in this zone calling |f| on each, allowing |f|
  // to remove the script. The template parameter |filter| controls whether to
  // include dying JitScripts during GC sweeping. Be careful when using this not
  // to let GC things reachable from the JitScript escape - they may be gray.
  template <JitScriptFilter filter = SkipDyingScripts, typename F>
  void forEachJitScript(F&& f) {
    JitScript* script = jitScripts_.getFirst();
    while (script) {
      JitScript* next = script->getNext();
      if (filter == IncludeDyingScripts ||
          !gc::IsAboutToBeFinalizedUnbarriered(script->owningScript())) {
        f(script);
      }
      script = next;
    }
  }

  // Like forEachJitScript above, but abort if |f| returns false.
  template <JitScriptFilter filter = SkipDyingScripts, typename F>
  bool forEachJitScriptFallible(F&& f) {
    JitScript* script = jitScripts_.getFirst();
    while (script) {
      JitScript* next = script->getNext();
      if (filter == IncludeDyingScripts ||
          !gc::IsAboutToBeFinalizedUnbarriered(script->owningScript())) {
        if (!f(script)) {
          return false;
        }
      }
      script = next;
    }
    return true;
  }

  bool keepJitScripts() const { return keepJitScripts_; }
  void setKeepJitScripts(bool keep) { keepJitScripts_ = keep; }

  mozilla::Maybe<IonCompilationId> currentCompilationId() const {
    return currentCompilationId_;
  }
  mozilla::Maybe<IonCompilationId>& currentCompilationIdRef() {
    return currentCompilationId_;
  }

  void setIncompleteAOTICs() { incompleteAOTICs_ = true; }
  bool isIncompleteAOTICs() const { return incompleteAOTICs_; }

  void traceWeak(JSTracer* trc, JS::Realm* realm);

  void traceScriptTableRoots(JSTracer* trc);
  void finishScriptTableRoots();

  void discardStubs() {
    for (WeakHeapPtr<JitCode*>& stubRef : stubs_) {
      stubRef = nullptr;
    }
  }

  bool hasStubs() const {
    for (const WeakHeapPtr<JitCode*>& stubRef : stubs_) {
      if (stubRef) {
        return true;
      }
    }
    return false;
  }

  void setStringsCanBeInNursery(bool allow) {
    MOZ_ASSERT(!hasStubs());
    initialStringHeap = allow ? gc::Heap::Default : gc::Heap::Tenured;
  }

  [[nodiscard]] JitCode* ensureStubExists(JSContext* cx, StubKind kind) {
    if (JitCode* code = stubs_[kind]) {
      return code;
    }
    switch (kind) {
      case StubKind::StringConcat:
        stubs_[kind] = generateStringConcatStub(cx);
        break;
      case StubKind::RegExpMatcher:
        stubs_[kind] = generateRegExpMatcherStub(cx);
        break;
      case StubKind::RegExpSearcher:
        stubs_[kind] = generateRegExpSearcherStub(cx);
        break;
      case StubKind::RegExpExecMatch:
        stubs_[kind] = generateRegExpExecMatchStub(cx);
        break;
      case StubKind::RegExpExecTest:
        stubs_[kind] = generateRegExpExecTestStub(cx);
        break;
      case StubKind::Count:
        MOZ_CRASH("Invalid kind");
    }
    return stubs_[kind];
  }

  EntryTrampolineMap* maybeInterpreterEntryMap() {
    return interpreterEntryMap.get();
  }
  EntryTrampolineMap* getOrCreateInterpreterEntryMap(JS::Zone* zone);

  static constexpr size_t offsetOfStringConcatStub() {
    return offsetof(JitZone, stubs_) +
           size_t(StubKind::StringConcat) * sizeof(uintptr_t);
  }
  static constexpr size_t offsetOfRegExpMatcherStub() {
    return offsetof(JitZone, stubs_) +
           size_t(StubKind::RegExpMatcher) * sizeof(uintptr_t);
  }
  static constexpr size_t offsetOfRegExpSearcherStub() {
    return offsetof(JitZone, stubs_) +
           size_t(StubKind::RegExpSearcher) * sizeof(uintptr_t);
  }
  static constexpr size_t offsetOfRegExpExecMatchStub() {
    return offsetof(JitZone, stubs_) +
           size_t(StubKind::RegExpExecMatch) * sizeof(uintptr_t);
  }
  static constexpr size_t offsetOfRegExpExecTestStub() {
    return offsetof(JitZone, stubs_) +
           size_t(StubKind::RegExpExecTest) * sizeof(uintptr_t);
  }
};

}  // namespace jit
}  // namespace js

#endif /* jit_JitZone_h */
