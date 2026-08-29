/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef gc_AtomMarking_inl_h
#define gc_AtomMarking_inl_h

#include "gc/AtomMarking.h"

#include "mozilla/Assertions.h"
#include "mozilla/Maybe.h"

#include <type_traits>

#include "vm/JSContext.h"
#include "vm/StringType.h"
#include "vm/SymbolType.h"

#include "gc/Heap-inl.h"

namespace js {
namespace gc {

/* static */
inline size_t AtomMarkingRuntime::getAtomBit(TenuredCell* thing) {
  MOZ_ASSERT(thing->zoneFromAnyThread()->isAtomsZone());
  Arena* arena = thing->arena();
  size_t arenaBit = (reinterpret_cast<uintptr_t>(thing) - arena->address()) /
                    CellBytesPerMarkBit;
  return arena->atomBitmapStart() * JS_BITS_PER_WORD + arenaBit;
}

template <typename T, bool Fallible>
MOZ_ALWAYS_INLINE bool AtomMarkingRuntime::inlinedMarkAtomInternal(Zone* zone,
                                                                   T* thing) {
  static_assert(std::is_same_v<T, JSAtom> || std::is_same_v<T, JS::Symbol>,
                "Should only be called with JSAtom* or JS::Symbol* argument");

  MOZ_ASSERT(zone);
  MOZ_ASSERT(!zone->isAtomsZone());

  MOZ_ASSERT(thing);
  js::gc::TenuredCell* cell = &thing->asTenured();
  MOZ_ASSERT(cell->zoneFromAnyThread()->isAtomsZone());

  if (thing->isPermanentAndMayBeShared()) {
    return true;
  }

  if constexpr (std::is_same_v<T, JSAtom>) {
    if (thing->isPinned()) {
      return true;
    }
  }

  size_t bit = getAtomBit(cell);
  size_t blackBit = bit + size_t(ColorBit::BlackBit);
  size_t grayOrBlackBit = bit + size_t(ColorBit::GrayOrBlackBit);
  MOZ_ASSERT(grayOrBlackBit / JS_BITS_PER_WORD < allocatedWords);

  {
    mozilla::Maybe<AutoEnterOOMUnsafeRegion> oomUnsafe;
    if constexpr (!Fallible) {
      oomUnsafe.emplace();
    }

    bool ok = zone->markedAtoms().setBit(blackBit);
    if constexpr (std::is_same_v<T, JS::Symbol>) {
      ok = ok && zone->markedAtoms().setBit(grayOrBlackBit);
    }

    if (!ok) {
      if constexpr (!Fallible) {
        oomUnsafe->crash("AtomMarkingRuntime::inlinedMarkAtomInternal");
      } else {
        return false;
      }
    }
  }

  // Children of the thing also need to be marked in the context's zone.
  // We don't have a JSTracer for this so manually handle the cases in which
  // an atom can reference other atoms.
  markChildren(zone, thing);

  return true;
}

inline void AtomMarkingRuntime::maybeUnmarkGrayAtomically(Zone* zone,
                                                          JS::Symbol* symbol) {
  MOZ_ASSERT(zone);
  MOZ_ASSERT(!zone->isAtomsZone());
  MOZ_ASSERT(symbol);
  MOZ_ASSERT(symbol->zoneFromAnyThread()->isAtomsZone());

  if (symbol->isPermanentAndMayBeShared()) {
    return;
  }

  // The atom is currently marked black or gray.
  MOZ_ASSERT(atomIsMarked(zone, symbol));

  // Set the black bit. This has the effect of making the mark black if it was
  // previously gray.
  size_t blackBit = getAtomBit(symbol) + size_t(ColorBit::BlackBit);
  MOZ_ASSERT(blackBit / JS_BITS_PER_WORD < allocatedWords);
  zone->markedAtoms().atomicSetExistingBit(blackBit);

  MOZ_ASSERT(getAtomMarkColor(zone, symbol) == CellColor::Black);
}

inline bool GCRuntime::isSymbolReferencedByUncollectedZone(JS::Symbol* sym,
                                                           MarkColor color) {
  MOZ_ASSERT(sym->zone()->isAtomsZone());

  if (!atomsUsedByUncollectedZones.ref()) {
    return false;
  }

  MOZ_ASSERT(atomsZone()->wasGCStarted());

  size_t bit = AtomMarkingRuntime::getAtomBit(sym);
  size_t blackBit = bit + size_t(ColorBit::BlackBit);
  size_t grayOrBlackBit = bit + size_t(ColorBit::GrayOrBlackBit);
  MOZ_ASSERT(grayOrBlackBit / JS_BITS_PER_WORD < atomMarking.allocatedWords);

  const DenseBitmap& bitmap = *atomsUsedByUncollectedZones.ref();
  if (grayOrBlackBit >= bitmap.count()) {
    return false;  // Atom created during collection.
  }

  if (bitmap.getBit(blackBit)) {
    return true;
  }

  return color == MarkColor::Gray && bitmap.getBit(grayOrBlackBit);
}

void AtomMarkingRuntime::markChildren(Zone* zone, JSAtom*) {}

void AtomMarkingRuntime::markChildren(Zone* zone, JS::Symbol* symbol) {
  if (JSAtom* description = symbol->description()) {
    inlinedMarkAtom(zone, description);
  }
}

template <typename T>
MOZ_ALWAYS_INLINE void AtomMarkingRuntime::inlinedMarkAtom(Zone* zone,
                                                           T* thing) {
  MOZ_ALWAYS_TRUE((inlinedMarkAtomInternal<T, false>(zone, thing)));
}

template <typename T>
MOZ_ALWAYS_INLINE bool AtomMarkingRuntime::inlinedMarkAtomFallible(Zone* zone,
                                                                   T* thing) {
  return inlinedMarkAtomInternal<T, true>(zone, thing);
}

}  // namespace gc
}  // namespace js

#endif  // gc_AtomMarking_inl_h
