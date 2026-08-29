/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef gc_AtomMarking_h
#define gc_AtomMarking_h

#include "mozilla/Atomics.h"

#include "NamespaceImports.h"
#include "gc/Cell.h"
#include "js/Vector.h"
#include "threading/ProtectedData.h"

namespace js {

class AutoLockGC;
class DenseBitmap;

namespace gc {

class Arena;
class GCRuntime;

// This class manages state used for marking atoms during GCs.
// See AtomMarking.cpp for details.
class AtomMarkingRuntime {
  // Unused arena atom bitmap indexes.
  js::MainThreadData<Vector<size_t, 0, SystemAllocPolicy>> freeArenaIndexes;

  // Background sweep state for |freeArenaIndexes|.
  js::GCLockData<Vector<size_t, 0, SystemAllocPolicy>> pendingFreeArenaIndexes;
  mozilla::Atomic<bool, mozilla::Relaxed> hasPendingFreeArenaIndexes;

  inline void markChildren(Zone* zone, JSAtom*);
  inline void markChildren(Zone* zone, JS::Symbol* symbol);

 public:
  // The extent of all allocated and free words in atom mark bitmaps.
  // This monotonically increases and may be read from without locking.
  mozilla::Atomic<size_t, mozilla::SequentiallyConsistent> allocatedWords;

  AtomMarkingRuntime() : allocatedWords(0) {}

  // Allocate an index in the atom marking bitmap for a new arena.
  size_t allocateIndex(GCRuntime* gc);

  // Free an index in the atom marking bitmap.
  void freeIndex(size_t index, const AutoLockGC& lock);

  void mergePendingFreeArenaIndexes(GCRuntime* gc);

  // Update the atom marking bitmaps in all collected zones according to the
  // atoms zone mark bits.
  void refineZoneBitmapsForCollectedZones(GCRuntime* gc);

  // Get a bitmap of all atoms marked in zones that are not being collected by
  // the current GC. On failure, mark the atoms instead.
  UniquePtr<DenseBitmap> getOrMarkAtomsUsedByUncollectedZones(GCRuntime* gc);

  // Set any bits in the chunk mark bitmaps for atoms which are marked in
  // uncollected zones, using the bitmap returned from the previous method.
  void markAtomsUsedByUncollectedZones(GCRuntime* gc,
                                       UniquePtr<DenseBitmap> markedUnion);

  // If gray unmarking fails or GC marking is aborted then the gray bits may end
  // up in an invalid state. This updates references to all things that could be
  // gray in the atom marking bitmaps, marking them as black if they were
  // previously (and perhaps incorrectly) considered gray. This is always safe
  // but loses information.
  void unmarkAllGrayReferences(GCRuntime* gc);

  // Get the index into the atom marking bitmaps for the first bit associated
  // with an atom.
  // This is public for testing access.
  static size_t getAtomBit(TenuredCell* thing);

 private:
  // Fill |bitmap| with an atom marking bitmap based on the things that are
  // currently marked in the chunks used by atoms zone arenas. This returns
  // false on an allocation failure (but does not report an exception).
  bool computeBitmapFromChunkMarkBits(GCRuntime* gc, DenseBitmap& bitmap);

  // Update the overapproximation of the reachable atoms in |zone| in one go
  // according to the marking state after GC.
  void refineZoneBitmapForCollectedZone(Zone* zone, const DenseBitmap& bitmap);

  // As above but called per-arena and using the mark bits directly.
  void refineZoneBitmapForCollectedZone(Zone* zone, Arena* arena);

 public:
  // Mark an atom or id as being newly reachable by the context's zone.
  template <typename T>
  void markAtom(JSContext* cx, T* thing);

  // Version of markAtom that's always inlined, for performance-sensitive
  // callers.
  template <typename T, bool Fallible>
  MOZ_ALWAYS_INLINE bool inlinedMarkAtomInternal(Zone* zone, T* thing);
  template <typename T>
  MOZ_ALWAYS_INLINE void inlinedMarkAtom(Zone* zone, T* thing);
  template <typename T>
  [[nodiscard]] MOZ_ALWAYS_INLINE bool inlinedMarkAtomFallible(Zone* zone,
                                                               T* thing);

  void markId(JSContext* cx, jsid id);
  void markAtomValue(JSContext* cx, const Value& value);

  // Get the mark color of |thing| in the atom marking bitmap for |zone|.
  template <typename T>
  CellColor getAtomMarkColor(Zone* zone, T* thing);

  // Return whether |thing/id| is in the atom marking bitmap for |zone|.
  template <typename T>
  bool atomIsMarked(Zone* zone, T* thing) {
    return getAtomMarkColor(zone, thing) != CellColor::White;
  }

  // For testing purposes, get the mark color associated with |bitIndex| in the
  // atom marking bitmap for |zone|.
  CellColor getAtomMarkColorForIndex(Zone* zone, size_t bitIndex);

  // Called during (possibly parallel) marking to unmark possibly-gray symbols.
  void maybeUnmarkGrayAtomically(Zone* zone, JS::Symbol* symbol);

#ifdef DEBUG
  bool idIsMarked(Zone* zone, jsid id);
  bool valueIsMarked(Zone* zone, const Value& value);
#endif
};

}  // namespace gc
}  // namespace js

#endif  // gc_AtomMarking_h
