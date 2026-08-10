/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef vm_Compartment_h
#define vm_Compartment_h

#include "mozilla/Maybe.h"
#include "mozilla/MemoryReporting.h"

#include <stddef.h>
#include <utility>

#include "gc/NurseryAwareHashMap.h"
#include "gc/ZoneAllocator.h"
#include "js/friend/Wrapper.h"
#include "vm/Iteration.h"
#include "vm/JSObject.h"
#include "vm/JSScript.h"

namespace js {

JSString* CopyStringPure(JSContext* cx, JSString* str);

// The data structure use to storing JSObject CCWs for a given source
// compartment. These are partitioned by target compartment so that we can
// easily select wrappers by source and target compartment. String CCWs are
// stored in a per-zone separate map.
class ObjectWrapperMap {
  static const size_t InitialInnerMapSize = 4;

  using InnerMap = NurseryAwareHashMap<JSObject*, JSObject*, ZoneAllocPolicy>;
  using OuterMap = GCHashMap<JS::Compartment*, InnerMap,
                             DefaultHasher<JS::Compartment*>, ZoneAllocPolicy>;

  OuterMap map;
  Zone* zone;

 public:
  class ModIterator {
    void goToNext() {
      if (outer.isNothing()) {
        return;
      }
      for (; !outer->done(); outer->next()) {
        JS::Compartment* c = outer->get().key();
        MOZ_ASSERT(c);
        if (filter && !filter->match(c)) {
          continue;
        }
        InnerMap& m = outer->get().value();
        if (!m.empty()) {
          if (inner.isSome()) {
            inner.reset();
          }
          inner.emplace(m.modIter());
          outer->next();
          return;
        }
      }
      inner.reset();
    }

    mozilla::Maybe<OuterMap::Iterator> outer;
    mozilla::Maybe<InnerMap::ModIterator> inner;
    const CompartmentFilter* filter;

   public:
    explicit ModIterator(ObjectWrapperMap& m) : filter(nullptr) {
      outer.emplace(m.map.iter());
      goToNext();
    }

    ModIterator(ObjectWrapperMap& m, const CompartmentFilter& f) : filter(&f) {
      outer.emplace(m.map.iter());
      goToNext();
    }

    ModIterator(ObjectWrapperMap& m, JS::Compartment* target) {
      // Leave the outer map as nothing and only iterate the inner map we
      // find here.
      auto p = m.map.lookup(target);
      if (p) {
        inner.emplace(p->value().modIter());
      }
    }

    ModIterator(const ModIterator&) = delete;
    void operator=(const ModIterator&) = delete;

    bool done() const {
      return (outer.isNothing() || outer->done()) &&
             (inner.isNothing() || inner->done());
    }

    InnerMap::Entry& get() const {
      MOZ_ASSERT(inner.isSome() && !inner->done());
      return inner->get();
    }

    void next() {
      MOZ_ASSERT(!done());
      inner->next();
      if (!inner->done()) {
        return;
      }
      goToNext();
    }

    void remove() {
      MOZ_ASSERT(inner.isSome() && !inner->done());
      inner->remove();
    }
  };

  class Ptr : public InnerMap::Ptr {
    friend class ObjectWrapperMap;

    InnerMap* map;

    Ptr() : map(nullptr) {}
    Ptr(const InnerMap::Ptr& p, InnerMap& m) : InnerMap::Ptr(p), map(&m) {}
  };

  // Iterator over compartments that the ObjectWrapperMap has wrappers for.
  class WrappedCompartmentIterator {
    OuterMap::Iterator iter;

    void settle() {
      // It's possible for InnerMap to be empty after wrappers have been
      // removed, e.g. by being nuked.
      while (!iter.done() && iter.get().value().empty()) {
        iter.next();
      }
    }

   public:
    explicit WrappedCompartmentIterator(const ObjectWrapperMap& map)
        : iter(map.map.iter()) {
      settle();
    }
    bool done() const { return iter.done(); }
    JS::Compartment* get() const { return iter.get().key(); }
    void next() {
      iter.next();
      settle();
    }
    operator JS::Compartment*() const { return get(); }
    JS::Compartment* operator->() const { return get(); }
    JS::Compartment& operator*() const { return *get(); }
  };

  explicit ObjectWrapperMap(Zone* zone) : map(zone), zone(zone) {}
  ObjectWrapperMap(Zone* zone, size_t aLen) : map(zone, aLen), zone(zone) {}

  bool empty() {
    if (map.empty()) {
      return true;
    }
    for (auto iter = map.iter(); !iter.done(); iter.next()) {
      if (!iter.get().value().empty()) {
        return false;
      }
    }
    return true;
  }

  Ptr lookup(JSObject* obj) const {
    auto op = map.lookup(obj->compartment());
    if (op) {
      auto ip = op->value().lookup(obj);
      if (ip) {
        return Ptr(ip, op->value());
      }
    }
    return Ptr();
  }

  void remove(Ptr p) {
    if (p) {
      p.map->remove(p);
    }
  }

  [[nodiscard]] bool put(JSObject* key, JSObject* value) {
    JS::Compartment* comp = key->compartment();
    auto ptr = map.lookupForAdd(comp);
    if (!ptr) {
      InnerMap m(zone, InitialInnerMapSize);
      if (!map.add(ptr, comp, std::move(m))) {
        return false;
      }
    }
    return ptr->value().put(key, value);
  }

  size_t sizeOfExcludingThis(mozilla::MallocSizeOf mallocSizeOf) {
    size_t size = map.shallowSizeOfExcludingThis(mallocSizeOf);
    for (auto iter = map.iter(); !iter.done(); iter.next()) {
      size += iter.get().value().sizeOfExcludingThis(mallocSizeOf);
    }
    return size;
  }
  size_t sizeOfIncludingThis(mozilla::MallocSizeOf mallocSizeOf) {
    return mallocSizeOf(this) + sizeOfExcludingThis(mallocSizeOf);
  }

  bool hasNurseryAllocatedWrapperEntries(const CompartmentFilter& f) {
    for (auto iter = map.iter(); !iter.done(); iter.next()) {
      JS::Compartment* c = iter.get().key();
      if (c && !f.match(c)) {
        continue;
      }
      InnerMap& m = iter.get().value();
      if (m.hasNurseryEntries()) {
        return true;
      }
    }
    return false;
  }

  void sweepAfterMinorGC(JSTracer* trc) {
    for (auto iter = map.modIter(); !iter.done(); iter.next()) {
      InnerMap& m = iter.get().value();
      m.sweepAfterMinorGC(trc);
      if (m.empty()) {
        iter.remove();
      }
    }
  }

  void traceWeak(JSTracer* trc) {
    for (auto iter = map.modIter(); !iter.done(); iter.next()) {
      InnerMap& m = iter.get().value();
      m.traceWeak(trc);
      if (m.empty()) {
        iter.remove();
      }
    }
    map.compact();
  }
};

using StringWrapperMap =
    NurseryAwareHashMap<JSString*, JSString*, ZoneAllocPolicy,
                        DuplicatesPossible>;

}  // namespace js

class JS::Compartment {
  JS::Zone* zone_;
  JSRuntime* runtime_;
  bool invisibleToDebugger_;

  js::ObjectWrapperMap crossCompartmentObjectWrappers;

  using RealmVector = js::Vector<JS::Realm*, 1, js::ZoneAllocPolicy>;
  RealmVector realms_;

 public:
  /*
   * During GC, stores the head of a list of incoming pointers from gray cells.
   *
   * The objects in the list are either cross-compartment wrappers, or
   * debugger wrapper objects.  The list link is either in the second extra
   * slot for the former, or a special slot for the latter.
   */
  JSObject* gcIncomingGrayPointers = nullptr;

  void* data = nullptr;

  // Fields set and used by the GC. Be careful, may be stale after we return
  // to the mutator.
  struct {
    // These flags help us to discover if a compartment that shouldn't be
    // alive manages to outlive a GC. Note that these flags have to be on
    // the compartment, not the realm, because same-compartment realms can
    // have cross-realm pointers without wrappers.
    bool scheduledForDestruction = false;
    bool hasMarkedCells = false;
    bool maybeAlive = true;

    // During GC, we may set this to |true| if we entered a realm in this
    // compartment. Note that (without a stack walk) we don't know exactly
    // *which* realms, because Realm::enterRealmDepthIgnoringJit_ does not
    // account for cross-Realm calls in JIT code updating cx->realm_. See
    // also the enterRealmDepthIgnoringJit_ comment.
    bool hasEnteredRealm = false;
  } gcState;

  // True if all outgoing wrappers have been nuked. This happens when all realms
  // have been nuked and NukeCrossCompartmentWrappers is called with the
  // NukeAllReferences option. This prevents us from creating new wrappers for
  // the compartment.
  bool nukedOutgoingWrappers = false;

  JS::Zone* zone() { return zone_; }
  const JS::Zone* zone() const { return zone_; }

  JSRuntime* runtimeFromMainThread() const {
    MOZ_ASSERT(js::CurrentThreadCanAccessRuntime(runtime_));
    return runtime_;
  }

  // Note: Unrestricted access to the zone's runtime from an arbitrary
  // thread can easily lead to races. Use this method very carefully.
  JSRuntime* runtimeFromAnyThread() const { return runtime_; }

  // Certain compartments are implementation details of the embedding, and
  // references to them should never leak out to script. For realms belonging to
  // this compartment, onNewGlobalObject does not fire, and addDebuggee is a
  // no-op.
  bool invisibleToDebugger() const { return invisibleToDebugger_; }

  RealmVector& realms() { return realms_; }

  // Cross-compartment wrappers are shared by all realms in the compartment, but
  // are still associated with a realm. To prevent us from having multiple
  // realms, each with some cross-compartment wrappers potentially keeping the
  // realm alive longer than necessary, we always allocate CCWs in the first
  // realm.
  js::GlobalObject& firstGlobal() const;
  js::GlobalObject& globalForNewCCW() const { return firstGlobal(); }

  void assertNoCrossCompartmentWrappers() {
    MOZ_ASSERT(crossCompartmentObjectWrappers.empty());
  }

  void addSizeOfIncludingThis(mozilla::MallocSizeOf mallocSizeOf,
                              size_t* compartmentObjects,
                              size_t* crossCompartmentWrappersTables,
                              size_t* compartmentsPrivateData);

#ifdef JSGC_HASH_TABLE_CHECKS
  void checkObjectWrappersAfterMovingGC();
#endif

 private:
  bool getNonWrapperObjectForCurrentCompartment(JSContext* cx,
                                                js::HandleObject origObj,
                                                js::MutableHandleObject obj);
  bool getOrCreateWrapper(JSContext* cx, js::HandleObject existing,
                          js::MutableHandleObject obj);

 public:
  explicit Compartment(JS::Zone* zone, bool invisibleToDebugger);

  void destroy(JS::GCContext* gcx);

  [[nodiscard]] inline bool wrap(JSContext* cx, JS::MutableHandleValue vp);

  [[nodiscard]] inline bool wrap(JSContext* cx,
                                 MutableHandle<mozilla::Maybe<Value>> vp);

  [[nodiscard]] bool wrap(JSContext* cx, js::MutableHandleString strp);
  [[nodiscard]] bool wrap(JSContext* cx, js::MutableHandle<JS::BigInt*> bi);
  [[nodiscard]] bool wrap(JSContext* cx, JS::MutableHandleObject obj);
  [[nodiscard]] bool wrap(JSContext* cx,
                          JS::MutableHandle<JS::PropertyDescriptor> desc);
  [[nodiscard]] bool wrap(
      JSContext* cx,
      JS::MutableHandle<mozilla::Maybe<JS::PropertyDescriptor>> desc);
  [[nodiscard]] bool wrap(JSContext* cx,
                          JS::MutableHandle<JS::GCVector<JS::Value>> vec);
  [[nodiscard]] bool rewrap(JSContext* cx, JS::MutableHandleObject obj,
                            JS::HandleObject existing);

  [[nodiscard]] bool putWrapper(JSContext* cx, JSObject* wrapped,
                                JSObject* wrapper);

  [[nodiscard]] bool putWrapper(JSContext* cx, JSString* wrapped,
                                JSString* wrapper);

  js::ObjectWrapperMap::Ptr lookupWrapper(JSObject* obj) const {
    return crossCompartmentObjectWrappers.lookup(obj);
  }

  inline js::StringWrapperMap::Ptr lookupWrapper(JSString* str) const;

  void removeWrapper(js::ObjectWrapperMap::Ptr p);

  bool hasNurseryAllocatedObjectWrapperEntries(const js::CompartmentFilter& f) {
    return crossCompartmentObjectWrappers.hasNurseryAllocatedWrapperEntries(f);
  }

  // Iterators over |wrapped -> wrapper| entries for object CCWs in this
  // compartment. Entires can be restricted with a filter or target compartment.
  using ObjectWrapperIter = js::ObjectWrapperMap::ModIterator;
  ObjectWrapperIter objectWrapperMappings() {
    return ObjectWrapperIter(crossCompartmentObjectWrappers);
  }
  ObjectWrapperIter objectWrapperMappings(const js::CompartmentFilter& f) {
    return ObjectWrapperIter(crossCompartmentObjectWrappers, f);
  }
  ObjectWrapperIter objectWrapperMappingsTo(Compartment* target) {
    return ObjectWrapperIter(crossCompartmentObjectWrappers, target);
  }

  // Iterator over compartments that this compartment has CCWs for.
  using WrappedObjectCompartmentIterator =
      js::ObjectWrapperMap::WrappedCompartmentIterator;
  WrappedObjectCompartmentIterator wrappedObjectCompartments() const {
    return WrappedObjectCompartmentIterator(crossCompartmentObjectWrappers);
  }

  /*
   * These methods mark pointers that cross compartment boundaries. They are
   * called in per-zone GCs to prevent the wrappers' outgoing edges from
   * dangling (full GCs naturally follow pointers across compartments) and
   * when compacting to update cross-compartment pointers.
   */
  enum EdgeSelector { AllEdges, NonGrayEdges, GrayEdges, BlackEdges };
  void traceWrapperTargetsInCollectedZones(JSTracer* trc,
                                           EdgeSelector whichEdges);
  static void traceIncomingCrossCompartmentEdgesForZoneGC(
      JSTracer* trc, EdgeSelector whichEdges);

  void sweepRealms(JS::GCContext* gcx, bool keepAtleastOne,
                   bool destroyingRuntime);
  void sweepAfterMinorGC(JSTracer* trc);
  void traceCrossCompartmentObjectWrapperEdges(JSTracer* trc);

  void fixupCrossCompartmentObjectWrappersAfterMovingGC(JSTracer* trc);
  void fixupAfterMovingGC(JSTracer* trc);

  [[nodiscard]] bool findSweepGroupEdges();

 private:
  // Head node of list of active iterators that may need deleted property
  // suppression.
  js::NativeIteratorListHead enumerators_;

 public:
  js::NativeIteratorListHead* enumeratorsAddr() { return &enumerators_; }
  MOZ_ALWAYS_INLINE bool objectMaybeInIteration(JSObject* obj);

  void traceWeakNativeIterators(JSTracer* trc);
};

namespace js {

// We only set the hasMarkedCells flag for objects and scripts. It's assumed
// that, if a compartment is alive, then it will have at least some live object
// or script it in. Even if we get this wrong, the worst that will happen is
// that scheduledForDestruction will be set on the compartment, which will cause
// some extra GC activity to try to free the compartment.
template <typename T>
inline void SetCompartmentHasMarkedCells(T* thing) {}

template <>
inline void SetCompartmentHasMarkedCells(JSObject* thing) {
  thing->compartment()->gcState.hasMarkedCells = true;
}

template <>
inline void SetCompartmentHasMarkedCells(JSScript* thing) {
  thing->compartment()->gcState.hasMarkedCells = true;
}

/*
 * AutoWrapperVector and AutoWrapperRooter can be used to store wrappers that
 * are obtained from the cross-compartment map. However, these classes should
 * not be used if the wrapper will escape. For example, it should not be stored
 * in the heap.
 *
 * The AutoWrapper rooters are different from other autorooters because their
 * wrappers are marked on every GC slice rather than just the first one. If
 * there's some wrapper that we want to use temporarily without causing it to be
 * marked, we can use these AutoWrapper classes. If we get unlucky and a GC
 * slice runs during the code using the wrapper, the GC will mark the wrapper so
 * that it doesn't get swept out from under us. Otherwise, the wrapper needn't
 * be marked. This is useful in functions like JS_TransplantObject that
 * manipulate wrappers in compartments that may no longer be alive.
 */

/*
 * This class stores the data for AutoWrapperVector and AutoWrapperRooter. It
 * should not be used in any other situations.
 */
struct WrapperValue {
  /*
   * We use unbarrieredGet() in the constructors to avoid invoking a read
   * barrier on the wrapper, which may be dead (see the comment about bug
   * 803376 in gc/GC.cpp regarding this). If there is an incremental GC while
   * the wrapper is in use, the AutoWrapper rooter will ensure the wrapper gets
   * marked.
   */
  explicit WrapperValue(const ObjectWrapperMap::Ptr& ptr)
      : value(ptr->value().unbarrieredGet()) {}

  explicit WrapperValue(const ObjectWrapperMap::ModIterator& e)
      : value(e.get().value().unbarrieredGet()) {}

  JSObject*& get() { return value; }
  JSObject* get() const { return value; }
  operator JSObject*() const { return value; }

 private:
  JSObject* value;
};

class MOZ_RAII AutoWrapperVector : public JS::GCVector<WrapperValue, 8>,
                                   public JS::AutoGCRooter {
 public:
  explicit AutoWrapperVector(JSContext* cx)
      : JS::GCVector<WrapperValue, 8>(cx),
        JS::AutoGCRooter(cx, JS::AutoGCRooter::Kind::WrapperVector) {}

  void trace(JSTracer* trc);

 private:
};

class MOZ_RAII AutoWrapperRooter : public JS::AutoGCRooter {
 public:
  AutoWrapperRooter(JSContext* cx, const WrapperValue& v)
      : JS::AutoGCRooter(cx, JS::AutoGCRooter::Kind::Wrapper), value(v) {}

  operator JSObject*() const { return value; }

  void trace(JSTracer* trc);

 private:
  WrapperValue value;
};

} /* namespace js */

#endif /* vm_Compartment_h */
