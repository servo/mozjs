/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gc/AtomMarking-inl.h"

#include <type_traits>

#include "gc/GCLock.h"
#include "gc/PublicIterators.h"

#include "gc/GC-inl.h"
#include "gc/Heap-inl.h"
#include "gc/PrivateIterators-inl.h"

namespace js {
namespace gc {

// [SMDOC] GC Atom Marking
//
// Things in the atoms zone (which includes atomized strings, symbols, and other
// things, all of which we will refer to as 'atoms' here) may be pointed to
// freely by things in other zones. To avoid the need to perform garbage
// collections of the entire runtime to collect atoms, we compute a separate
// atom mark bitmap for each zone that is always an overapproximation of the
// atoms that zone is using. When an atom is not in the mark bitmap for any
// zone, it can be destroyed.
//
// (These bitmaps can only be calculated exactly if we collect a single zone
// since they are based on the marking state at the end of a GC which may have
// marked multiple zones.)
//
// To minimize interference with the rest of the GC, atom marking and sweeping
// is done by manipulating the mark bitmaps in the chunks used for the atoms.
// When the atoms zone is being collected, the mark bitmaps for the chunk(s)
// used by the atoms are updated normally during marking.
//
// After marking has finished and before sweeping begins, two things happen:
//
//  1) The atom marking bitmaps for collected zones are updated to remove atoms
//     that GC marking has found are not referenced by any collected zone (see
//     refineZoneBitmapsForCollectedZones). This improves our approximation.
//
//  2) The chunk mark bitmaps are updated with any atoms that might be
//     referenced by zones which weren't collected (see
//     markAtomsUsedByUncollectedZones).
//
// GC sweeping will then release all atoms which are not marked by any zone.
//
// The representation of atom mark bitmaps is as follows:
//
// Each arena in the atoms zone has an atomBitmapStart() value indicating the
// word index into the bitmap of the first thing in the arena. Each arena uses
// ArenaBitmapWords of data to store its bitmap, which uses the same
// representation as chunk mark bitmaps: at least two bits per cell (see
// CellBytesPerMarkBit and MarkBitsPerCell).

size_t AtomMarkingRuntime::allocateIndex(GCRuntime* gc) {
  // We need to find a range of bits from the atoms bitmap for this arena.

  // Try to merge background swept free indexes if necessary.
  if (freeArenaIndexes.ref().empty()) {
    mergePendingFreeArenaIndexes(gc);
  }

  // Look for a free range of bits compatible with this arena.
  if (!freeArenaIndexes.ref().empty()) {
    return freeArenaIndexes.ref().popCopy();
  }

  // Allocate a range of bits from the end for this arena.
  size_t index = allocatedWords;
  allocatedWords += ArenaBitmapWords;
  return index;
}

void AtomMarkingRuntime::freeIndex(size_t index, const AutoLockGC& lock) {
  MOZ_ASSERT((index % ArenaBitmapWords) == 0);
  MOZ_ASSERT(index < allocatedWords);

  bool wasEmpty = pendingFreeArenaIndexes.ref().empty();
  MOZ_ASSERT_IF(wasEmpty, !hasPendingFreeArenaIndexes);

  if (!pendingFreeArenaIndexes.ref().append(index)) {
    // Leak these atom bits if we run out of memory.
    return;
  }

  if (wasEmpty) {
    hasPendingFreeArenaIndexes = true;
  }
}

void AtomMarkingRuntime::mergePendingFreeArenaIndexes(GCRuntime* gc) {
  MOZ_ASSERT(CurrentThreadCanAccessRuntime(gc->rt));
  if (!hasPendingFreeArenaIndexes) {
    return;
  }

  AutoLockGC lock(gc);
  MOZ_ASSERT(!pendingFreeArenaIndexes.ref().empty());

  hasPendingFreeArenaIndexes = false;

  if (freeArenaIndexes.ref().empty()) {
    std::swap(freeArenaIndexes.ref(), pendingFreeArenaIndexes.ref());
    return;
  }

  // Leak these atom bits if we run out of memory.
  (void)freeArenaIndexes.ref().appendAll(pendingFreeArenaIndexes.ref());
  pendingFreeArenaIndexes.ref().clear();
}

// Return whether more than one zone is being collected.
static bool MultipleNonAtomZonesAreBeingCollected(GCRuntime* gc) {
  size_t count = 0;
  for (GCZonesIter zone(gc); !zone.done(); zone.next()) {
    if (!zone->isAtomsZone()) {
      count++;
      if (count == 2) {
        return true;
      }
    }
  }

  return false;
}

void AtomMarkingRuntime::refineZoneBitmapsForCollectedZones(GCRuntime* gc) {
  // If there is more than one zone to update, it's more efficient to copy the
  // chunk mark bits from each arena into a single dense bitmap and then use
  // that to refine the atom marking bitmap for each zone.
  DenseBitmap marked;
  if (MultipleNonAtomZonesAreBeingCollected(gc) &&
      computeBitmapFromChunkMarkBits(gc, marked)) {
    for (GCZonesIter zone(gc, SkipAtoms); !zone.done(); zone.next()) {
      refineZoneBitmapForCollectedZone(zone, marked);
    }
    return;
  }

  // If there's only one zone (or on OOM), refine the mark bits for each arena
  // with the zones' atom marking bitmaps directly.
  for (GCZonesIter zone(gc, SkipAtoms); !zone.done(); zone.next()) {
    for (auto thingKind : AllAllocKinds()) {
      for (ArenaIterInGC aiter(gc->atomsZone(), thingKind); !aiter.done();
           aiter.next()) {
        refineZoneBitmapForCollectedZone(zone, aiter);
      }
    }
  }
}

// Refining atom marking bitmaps:
//
// The atom marking bitmap for a zone records an overapproximation of the mark
// color for each atom referenced by that zone. After collection we refine this
// based on the actual final mark state. The final mark state is the maximum of
// the mark colours of all references for each atom. Therefore we refine the
// bitmap by setting it to the minimum of itself and the actual mark state for
// each atom.
//
// To find the minimum we use bitwise AND. For trace kinds that can only be
// marked black this works on its own. For kinds that can be marked gray we must
// preprocess the mark bitmap so that both mark bits are set for black cells.

// Masks of bit positions in a mark bitmap word that can be ColorBit::BlackBit
// or GrayOrBlackBit, which alternative throughout the word.
#if JS_BITS_PER_WORD == 32
static constexpr uintptr_t BlackBitMask = 0x55555555;
#else
static constexpr uintptr_t BlackBitMask = 0x5555555555555555;
#endif
static constexpr uintptr_t GrayOrBlackBitMask = ~BlackBitMask;

static void PropagateBlackBitsToGrayOrBlackBits(DenseBitmap& bitmap,
                                                Arena* arena) {
  // This only works if the gray bit and black bits are in the same word,
  // which is true for symbols.
  MOZ_ASSERT(
      TraceKindCanBeMarkedGray(MapAllocToTraceKind(arena->getAllocKind())));
  MOZ_ASSERT((arena->getThingSize() / CellBytesPerMarkBit) % 2 == 0);

  bitmap.forEachWord(
      arena->atomBitmapStart(), ArenaBitmapWords,
      [](uintptr_t& word) { word |= (word & BlackBitMask) << 1; });
}

static void PropagateBlackBitsToGrayOrBlackBits(
    uintptr_t (&words)[ArenaBitmapWords]) {
  for (uintptr_t& word : words) {
    word |= (word & BlackBitMask) << 1;
  }
}

static void PropagateGrayOrBlackBitsToBlackBits(SparseBitmap& bitmap,
                                                Arena* arena) {
  // This only works if the gray bit and black bits are in the same word,
  // which is true for symbols.
  MOZ_ASSERT(
      TraceKindCanBeMarkedGray(MapAllocToTraceKind(arena->getAllocKind())));
  MOZ_ASSERT((arena->getThingSize() / CellBytesPerMarkBit) % 2 == 0);

  bitmap.forEachWord(
      arena->atomBitmapStart(), ArenaBitmapWords,
      [](uintptr_t& word) { word |= (word & GrayOrBlackBitMask) >> 1; });
}

#ifdef DEBUG
static bool ArenaContainsGrayCells(Arena* arena) {
  for (ArenaCellIter cell(arena); !cell.done(); cell.next()) {
    if (cell->isMarkedGray()) {
      return true;
    }
  }
  return false;
}
#endif

bool AtomMarkingRuntime::computeBitmapFromChunkMarkBits(GCRuntime* gc,
                                                        DenseBitmap& bitmap) {
  MOZ_ASSERT(CurrentThreadIsPerformingGC());

  if (!bitmap.ensureSpace(allocatedWords)) {
    return false;
  }

  Zone* atomsZone = gc->atomsZone();
  for (auto thingKind : AllAllocKinds()) {
    for (ArenaIterInGC aiter(atomsZone, thingKind); !aiter.done();
         aiter.next()) {
      Arena* arena = aiter.get();
      AtomicBitmapWord* chunkWords = arena->chunk()->markBits.arenaBits(arena);
      bitmap.copyBitsFrom(arena->atomBitmapStart(), ArenaBitmapWords,
                          chunkWords);

      if (thingKind == AllocKind::JITCODE) {
        // The atoms zone can contain JitCode used for compiled self-hosted JS,
        // however these cells are never marked gray so we can skip this step.
        MOZ_ASSERT(!ArenaContainsGrayCells(arena));
      } else if (TraceKindCanBeMarkedGray(MapAllocToTraceKind(thingKind))) {
        // Ensure both mark bits are set for black cells so we can compute the
        // minimum of each mark color by bitwise AND.
        PropagateBlackBitsToGrayOrBlackBits(bitmap, arena);
      }
    }
  }

  return true;
}

void AtomMarkingRuntime::refineZoneBitmapForCollectedZone(
    Zone* zone, const DenseBitmap& bitmap) {
  MOZ_ASSERT(zone->isCollectingFromAnyThread());
  MOZ_ASSERT(!zone->isAtomsZone());

  // Take the bitwise and between the two mark bitmaps to get the best new
  // overapproximation we can. |bitmap| might include bits that are not in
  // the zone's mark bitmap, if additional zones were collected by the GC.
  zone->markedAtoms().bitwiseAndWith(bitmap);
}

void AtomMarkingRuntime::refineZoneBitmapForCollectedZone(Zone* zone,
                                                          Arena* arena) {
  MOZ_ASSERT(zone->isCollectingFromAnyThread());
  MOZ_ASSERT(!zone->isAtomsZone());

  AtomicBitmapWord* chunkWords = arena->chunk()->markBits.arenaBits(arena);

  AllocKind kind = arena->getAllocKind();
  if (kind == AllocKind::JITCODE) {
    // The atoms zone can contain JitCode used for compiled self-hosted JS,
    // however these cells are never marked gray so we can skip this step.
    MOZ_ASSERT(!ArenaContainsGrayCells(arena));
  } else if (TraceKindCanBeMarkedGray(MapAllocToTraceKind(kind))) {
    uintptr_t words[ArenaBitmapWords];
    memcpy(words, chunkWords, sizeof(words));
    PropagateBlackBitsToGrayOrBlackBits(words);
    zone->markedAtoms().bitwiseAndRangeWith(arena->atomBitmapStart(),
                                            ArenaBitmapWords, words);
    return;
  }

  zone->markedAtoms().bitwiseAndRangeWith(arena->atomBitmapStart(),
                                          ArenaBitmapWords, chunkWords);
}

// Set any bits in the chunk mark bitmaps for atoms which are marked in bitmap.
template <typename Bitmap>
static void BitwiseOrIntoChunkMarkBits(Zone* atomsZone, Bitmap& bitmap) {
  // Make sure that by copying the mark bits for one arena in word sizes we
  // do not affect the mark bits for other arenas.
  static_assert(ArenaBitmapBits == ArenaBitmapWords * JS_BITS_PER_WORD,
                "ArenaBitmapWords must evenly divide ArenaBitmapBits");

  for (auto thingKind : AllAllocKinds()) {
    for (ArenaIterInGC aiter(atomsZone, thingKind); !aiter.done();
         aiter.next()) {
      Arena* arena = aiter.get();
      AtomicBitmapWord* chunkWords = arena->chunk()->markBits.arenaBits(arena);
      bitmap.bitwiseOrRangeInto(arena->atomBitmapStart(), ArenaBitmapWords,
                                chunkWords);
    }
  }
}

UniquePtr<DenseBitmap> AtomMarkingRuntime::getOrMarkAtomsUsedByUncollectedZones(
    GCRuntime* gc) {
  MOZ_ASSERT(CurrentThreadIsPerformingGC());

  UniquePtr<DenseBitmap> markedUnion = MakeUnique<DenseBitmap>();
  if (!markedUnion || !markedUnion->ensureSpace(allocatedWords)) {
    // On failure, mark the atoms immediately.
    for (ZonesIter zone(gc, SkipAtoms); !zone.done(); zone.next()) {
      if (!zone->isCollecting()) {
        BitwiseOrIntoChunkMarkBits(gc->atomsZone(), zone->markedAtoms());
      }
    }
    return nullptr;
  }

  for (ZonesIter zone(gc, SkipAtoms); !zone.done(); zone.next()) {
    if (!zone->isCollecting()) {
      zone->markedAtoms().bitwiseOrInto(*markedUnion);
    }
  }

  return markedUnion;
}

void AtomMarkingRuntime::markAtomsUsedByUncollectedZones(
    GCRuntime* gc, UniquePtr<DenseBitmap> markedUnion) {
  BitwiseOrIntoChunkMarkBits(gc->atomsZone(), *markedUnion);
}

void AtomMarkingRuntime::unmarkAllGrayReferences(GCRuntime* gc) {
  for (ZonesIter sourceZone(gc, SkipAtoms); !sourceZone.done();
       sourceZone.next()) {
    MOZ_ASSERT(!sourceZone->isAtomsZone());
    auto& bitmap = sourceZone->markedAtoms();
    for (ArenaIter arena(gc->atomsZone(), AllocKind::SYMBOL); !arena.done();
         arena.next()) {
      PropagateGrayOrBlackBitsToBlackBits(bitmap, arena);
    }
#ifdef DEBUG
    for (auto cell = gc->atomsZone()->cellIter<JS::Symbol>(); !cell.done();
         cell.next()) {
      MOZ_ASSERT(getAtomMarkColor(sourceZone, cell.get()) != CellColor::Gray);
    }
#endif
  }
}

template <typename T>
void AtomMarkingRuntime::markAtom(JSContext* cx, T* thing) {
  // Trigger a read barrier on the atom, in case there is an incremental
  // GC in progress. This is necessary if the atom is being marked
  // because a reference to it was obtained from another zone which is
  // not being collected by the incremental GC.
  ReadBarrier(thing);

  return inlinedMarkAtom(cx->zone(), thing);
}

template void AtomMarkingRuntime::markAtom(JSContext* cx, JSAtom* thing);
template void AtomMarkingRuntime::markAtom(JSContext* cx, JS::Symbol* thing);

void AtomMarkingRuntime::markId(JSContext* cx, jsid id) {
  if (id.isAtom()) {
    markAtom(cx, id.toAtom());
    return;
  }
  if (id.isSymbol()) {
    markAtom(cx, id.toSymbol());
    return;
  }
  MOZ_ASSERT(!id.isGCThing());
}

void AtomMarkingRuntime::markAtomValue(JSContext* cx, const Value& value) {
  if (value.isString()) {
    if (value.toString()->isAtom()) {
      markAtom(cx, &value.toString()->asAtom());
    }
    return;
  }
  if (value.isSymbol()) {
    markAtom(cx, value.toSymbol());
    return;
  }
  MOZ_ASSERT_IF(value.isGCThing(), value.isObject() ||
                                       value.isPrivateGCThing() ||
                                       value.isBigInt());
}

template <typename T>
CellColor AtomMarkingRuntime::getAtomMarkColor(Zone* zone, T* thing) {
  static_assert(std::is_same_v<T, JSAtom> || std::is_same_v<T, JS::Symbol>,
                "Should only be called with JSAtom* or JS::Symbol* argument");

  MOZ_ASSERT(thing);
  MOZ_ASSERT(!IsInsideNursery(thing));
  MOZ_ASSERT(thing->zoneFromAnyThread()->isAtomsZone());

  if (!zone->runtimeFromAnyThread()->permanentAtomsPopulated()) {
    return CellColor::Black;
  }

  if (thing->isPermanentAndMayBeShared()) {
    return CellColor::Black;
  }

  if constexpr (std::is_same_v<T, JSAtom>) {
    if (thing->isPinned()) {
      return CellColor::Black;
    }
  }

  size_t bit = getAtomBit(&thing->asTenured());

  size_t blackBit = bit + size_t(ColorBit::BlackBit);
  size_t grayOrBlackBit = bit + size_t(ColorBit::GrayOrBlackBit);

  SparseBitmap& bitmap = zone->markedAtoms();

  MOZ_ASSERT_IF((std::is_same_v<T, JSAtom>),
                !bitmap.readonlyThreadsafeGetBit(grayOrBlackBit));
  MOZ_ASSERT_IF((std::is_same_v<T, JS::Symbol>) &&
                    bitmap.readonlyThreadsafeGetBit(blackBit),
                bitmap.readonlyThreadsafeGetBit(grayOrBlackBit));

  if (bitmap.readonlyThreadsafeGetBit(blackBit)) {
    return CellColor::Black;
  }

  if constexpr (std::is_same_v<T, JS::Symbol>) {
    if (bitmap.readonlyThreadsafeGetBit(grayOrBlackBit)) {
      return CellColor::Gray;
    }
  }

  return CellColor::White;
}

template CellColor AtomMarkingRuntime::getAtomMarkColor(Zone* zone,
                                                        JSAtom* thing);
template CellColor AtomMarkingRuntime::getAtomMarkColor(Zone* zone,
                                                        JS::Symbol* thing);

CellColor AtomMarkingRuntime::getAtomMarkColorForIndex(Zone* zone,
                                                       size_t bitIndex) {
  MOZ_ASSERT(zone->runtimeFromAnyThread()->permanentAtomsPopulated());

  size_t blackBit = bitIndex + size_t(ColorBit::BlackBit);
  size_t grayOrBlackBit = bitIndex + size_t(ColorBit::GrayOrBlackBit);

  SparseBitmap& bitmap = zone->markedAtoms();
  bool blackBitSet = bitmap.readonlyThreadsafeGetBit(blackBit);
  bool grayOrBlackBitSet = bitmap.readonlyThreadsafeGetBit(grayOrBlackBit);

  if (blackBitSet) {
    return CellColor::Black;
  }

  if (grayOrBlackBitSet) {
    return CellColor::Gray;
  }

  return CellColor::White;
}

#ifdef DEBUG

template <>
CellColor AtomMarkingRuntime::getAtomMarkColor(Zone* zone, TenuredCell* thing) {
  MOZ_ASSERT(thing);
  MOZ_ASSERT(thing->zoneFromAnyThread()->isAtomsZone());

  if (thing->is<JSString>()) {
    JSString* str = thing->as<JSString>();
    return getAtomMarkColor(zone, &str->asAtom());
  }

  if (thing->is<JS::Symbol>()) {
    return getAtomMarkColor(zone, thing->as<JS::Symbol>());
  }

  MOZ_CRASH("Unexpected atom kind");
}

bool AtomMarkingRuntime::idIsMarked(Zone* zone, jsid id) {
  if (id.isAtom()) {
    return atomIsMarked(zone, id.toAtom());
  }

  if (id.isSymbol()) {
    return atomIsMarked(zone, id.toSymbol());
  }

  MOZ_ASSERT(!id.isGCThing());
  return true;
}

bool AtomMarkingRuntime::valueIsMarked(Zone* zone, const Value& value) {
  if (value.isString()) {
    if (value.toString()->isAtom()) {
      return atomIsMarked(zone, &value.toString()->asAtom());
    }
    return true;
  }

  if (value.isSymbol()) {
    return atomIsMarked(zone, value.toSymbol());
  }

  MOZ_ASSERT_IF(value.isGCThing(), value.isObject() ||
                                       value.isPrivateGCThing() ||
                                       value.isBigInt());
  return true;
}

#endif  // DEBUG

}  // namespace gc

#ifdef DEBUG

bool AtomIsMarked(Zone* zone, JSAtom* atom) {
  return zone->runtimeFromAnyThread()->gc.atomMarking.atomIsMarked(zone, atom);
}

bool AtomIsMarked(Zone* zone, jsid id) {
  return zone->runtimeFromAnyThread()->gc.atomMarking.idIsMarked(zone, id);
}

bool AtomIsMarked(Zone* zone, const Value& value) {
  return zone->runtimeFromAnyThread()->gc.atomMarking.valueIsMarked(zone,
                                                                    value);
}

#endif  // DEBUG

}  // namespace js
