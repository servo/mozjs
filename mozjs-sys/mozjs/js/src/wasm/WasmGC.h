/*
 * Copyright 2019 Mozilla Foundation
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

#ifndef wasm_gc_h
#define wasm_gc_h

#include "jit/ABIArgGenerator.h"  // For ABIArgIter
#include "js/AllocPolicy.h"
#include "js/Vector.h"
#include "util/Memory.h"
#include "wasm/WasmBuiltins.h"
#include "wasm/WasmFrame.h"
#include "wasm/WasmSerialize.h"

namespace js {

namespace jit {
class Label;
class MacroAssembler;
}  // namespace jit

namespace wasm {

class ArgTypeVector;
class BytecodeOffset;

// Definitions for stackmaps.

using ExitStubMapVector = Vector<bool, 32, SystemAllocPolicy>;

struct StackMapHeader {
  explicit StackMapHeader(uint32_t numMappedWords = 0)
      : numMappedWords(numMappedWords),
#ifdef DEBUG
        numExitStubWords(0),
#endif
        frameOffsetFromTop(0),
        hasDebugFrameWithLiveRefs(0) {
    MOZ_ASSERT(numMappedWords <= maxMappedWords);
  }

  // The total number of stack words covered by the map ..
  static constexpr size_t MappedWordsBits = 18;
  static_assert(((1 << MappedWordsBits) - 1) * sizeof(void*) >= MaxFrameSize);
  uint32_t numMappedWords : MappedWordsBits;

  // .. of which this many are "exit stub" extras
  static constexpr size_t ExitStubWordsBits = 6;
#ifdef DEBUG
  uint32_t numExitStubWords : ExitStubWordsBits;
#endif

  // Where is Frame* relative to the top?  This is an offset in words.  On every
  // platform, FrameOffsetBits needs to be at least
  // ceil(log2(MaxParams*sizeof-biggest-param-type-in-words)).  The most
  // constraining platforms are 32-bit with SIMD support, currently x86-32.
  static constexpr size_t FrameOffsetBits = 12;
  uint32_t frameOffsetFromTop : FrameOffsetBits;

  // Notes the presence of a DebugFrame with possibly-live references.  A
  // DebugFrame may or may not contain GC-managed data; in situations when it is
  // possible that any pointers in the DebugFrame are non-null, the DebugFrame
  // gets a stackmap.
  uint32_t hasDebugFrameWithLiveRefs : 1;

  WASM_CHECK_CACHEABLE_POD(numMappedWords,
#ifdef DEBUG
                           numExitStubWords,
#endif
                           frameOffsetFromTop, hasDebugFrameWithLiveRefs);

  static constexpr uint32_t maxMappedWords = (1 << MappedWordsBits) - 1;
  static constexpr uint32_t maxExitStubWords = (1 << ExitStubWordsBits) - 1;
  static constexpr uint32_t maxFrameOffsetFromTop = (1 << FrameOffsetBits) - 1;

  static constexpr size_t MaxParamSize =
      std::max(sizeof(jit::FloatRegisters::RegisterContent),
               sizeof(jit::Registers::RegisterContent));

  // Add 16 words to account for the size of FrameWithInstances including any
  // shadow stack (at worst 8 words total), and then a little headroom in case
  // the argument area had to be aligned.
  static_assert(sizeof(FrameWithInstances) / sizeof(void*) <= 8);
  static_assert(maxFrameOffsetFromTop >=
                    (MaxParams * MaxParamSize / sizeof(void*)) + 16,
                "limited size of the offset field");

  bool operator==(const StackMapHeader& rhs) const {
    return numMappedWords == rhs.numMappedWords &&
#ifdef DEBUG
           numExitStubWords == rhs.numExitStubWords &&
#endif
           frameOffsetFromTop == rhs.frameOffsetFromTop &&
           hasDebugFrameWithLiveRefs == rhs.hasDebugFrameWithLiveRefs;
  }
  bool operator!=(const StackMapHeader& rhs) const { return !(*this == rhs); }
};

WASM_DECLARE_CACHEABLE_POD(StackMapHeader);

#ifndef DEBUG
// This is the expected size for the header, when in release builds
static_assert(sizeof(StackMapHeader) == 4,
              "wasm::StackMapHeader has unexpected size");
#endif

// A StackMap is a bit-array containing numMappedWords*2 bits, two bits per
// word of stack. Index zero is for the lowest addressed word in the range.
//
// This is a variable-length structure whose size must be known at creation
// time.
//
// Users of the map will know the address of the wasm::Frame that is covered
// by this map. In order that they can calculate the exact address range
// covered by the map, the map also stores the offset, from the highest
// addressed word of the map, of the embedded wasm::Frame. This is an offset
// down from the highest address, rather than up from the lowest, so as to
// limit its range to FrameOffsetBits bits.
//
// The stackmap may also cover a DebugFrame (all DebugFrames which may
// potentially contain live pointers into the JS heap get a map). If so, that
// can be noted, since users of the map need to trace pointers in a
// DebugFrame.
//
// Finally, for sanity checking only, for stackmaps associated with a wasm
// trap exit stub, the number of words used by the trap exit stub save area
// is also noted.  This is used in Instance::traceFrame to check that the
// TrapExitDummyValue is in the expected place in the frame.
struct StackMap final {
  friend class StackMaps;

  // The header contains the constant-sized fields before the variable-sized
  // bitmap that follows.
  StackMapHeader header;

  enum Kind : uint32_t {
    POD = 0,
    AnyRef = 1,

    // The data pointer for a WasmStructObject that requires OOL storage.
    StructDataPointer = 2,

    // The data pointer for a WasmArrayObject, which is either an interior
    // pointer to the object itself, or a pointer to OOL storage managed by
    // BufferAllocator. See WasmArrayObject::data_/inlineStorage.
    ArrayDataPointer = 3,

    Limit,
  };

 private:
  // The variable-sized bitmap.
  uint32_t bitmap[1];

  explicit StackMap(uint32_t numMappedWords) : header(numMappedWords) {
    const uint32_t nBitmap = calcBitmapNumElems(header.numMappedWords);
    memset(bitmap, 0, nBitmap * sizeof(bitmap[0]));
  }

 public:
  // Returns the size of a `StackMap` allocated with `numMappedWords`.
  static size_t allocationSizeInBytes(uint32_t numMappedWords) {
    uint32_t nBitmap = calcBitmapNumElems(numMappedWords);
    return sizeof(StackMap) + (nBitmap - 1) * sizeof(bitmap[0]);
  }

  // Returns the allocated size of this `StackMap`.
  size_t allocationSizeInBytes() const {
    return allocationSizeInBytes(header.numMappedWords);
  }

  // Record the number of words in the map used as a wasm trap exit stub
  // save area.  See comment above.
  void setExitStubWords(uint32_t nWords) {
    MOZ_RELEASE_ASSERT(nWords <= header.maxExitStubWords);
#ifdef DEBUG
    MOZ_ASSERT(header.numExitStubWords == 0);
    MOZ_ASSERT(nWords <= header.numMappedWords);
    header.numExitStubWords = nWords;
#endif
  }

  // Record the offset from the highest-addressed word of the map, that the
  // wasm::Frame lives at.  See comment above.
  void setFrameOffsetFromTop(uint32_t nWords) {
    MOZ_ASSERT(header.frameOffsetFromTop == 0);
    MOZ_RELEASE_ASSERT(nWords <= StackMapHeader::maxFrameOffsetFromTop);
    MOZ_ASSERT(header.frameOffsetFromTop < header.numMappedWords);
    header.frameOffsetFromTop = nWords;
  }

  // If the frame described by this StackMap includes a DebugFrame, call here to
  // record that fact.
  void setHasDebugFrameWithLiveRefs() {
    MOZ_ASSERT(header.hasDebugFrameWithLiveRefs == 0);
    header.hasDebugFrameWithLiveRefs = 1;
  }

  inline void set(uint32_t index, Kind kind) {
    MOZ_ASSERT(index < header.numMappedWords);
    MOZ_ASSERT(kind < Kind::Limit);
    // Because we don't zero out the field before writing it ..
    MOZ_ASSERT(get(index) == (Kind)0);
    uint32_t wordIndex = index / mappedWordsPerBitmapElem;
    uint32_t wordOffset = index % mappedWordsPerBitmapElem * bitsPerMappedWord;
    bitmap[wordIndex] |= (kind << wordOffset);
  }

  inline Kind get(uint32_t index) const {
    MOZ_ASSERT(index < header.numMappedWords);
    uint32_t wordIndex = index / mappedWordsPerBitmapElem;
    uint32_t wordOffset = index % mappedWordsPerBitmapElem * bitsPerMappedWord;
    Kind result = Kind((bitmap[wordIndex] >> wordOffset) & valueMask);
    return result;
  }

  inline uint8_t* rawBitmap() { return (uint8_t*)&bitmap; }
  inline const uint8_t* rawBitmap() const { return (const uint8_t*)&bitmap; }
  inline size_t rawBitmapLengthInBytes() const {
    return calcBitmapNumElems(header.numMappedWords) * sizeof(bitmap[0]);
  }

  inline uint32_t numMappedWords() const { return header.numMappedWords; }

#ifdef JS_JITSPEW
  // Dumps a summary of the stackmap to the JitSpew_Codegen channel.
  // `codeOffset` is the intended assembler buffer offset for the map.
  void show(uint32_t codeOffset) const;
#endif

 private:
  static constexpr uint32_t bitsPerMappedWord = 2;
  static constexpr uint32_t mappedWordsPerBitmapElem =
      sizeof(bitmap[0]) * CHAR_BIT / bitsPerMappedWord;
  static constexpr uint32_t valueMask = js::BitMask(bitsPerMappedWord);
  static_assert(8 % bitsPerMappedWord == 0);
  static_assert(Kind::Limit - 1 <= valueMask);

  static uint32_t calcBitmapNumElems(uint32_t numMappedWords) {
    MOZ_RELEASE_ASSERT(numMappedWords <= StackMapHeader::maxMappedWords);
    uint32_t nBitmap = js::HowMany(numMappedWords, mappedWordsPerBitmapElem);
    return nBitmap == 0 ? 1 : nBitmap;
  }

 public:
  bool operator==(const StackMap& rhs) const {
    // Check the header first, as it determines the bitmap length
    if (header != rhs.header) {
      return false;
    }
    // Compare the bitmap data
    return memcmp(bitmap, rhs.bitmap, rawBitmapLengthInBytes()) == 0;
  }
};

#ifndef DEBUG
// This is the expected size for a map that covers 32 or fewer words.
static_assert(sizeof(StackMap) == 8, "wasm::StackMap has unexpected size");
#endif

// A map from an offset relative to the beginning of a code block to a StackMap
using StackMapHashMap =
    HashMap<uint32_t, StackMap*, DefaultHasher<uint32_t>, SystemAllocPolicy>;

class StackMaps {
 private:
  // The primary allocator for stack maps. The LifoAlloc will malloc chunks of
  // memory to be linearly allocated as stack maps, giving us pointer stability
  // while avoiding lock contention from malloc across compilation threads. It
  // also allows us to undo a stack map allocation.
  LifoAlloc stackMaps_;
  // Map for finding a stack map at a specific code offset.
  StackMapHashMap codeOffsetToStackMap_;

  // The StackMap most recently finalized. Used for deduplication.
  StackMap* lastAdded_ = nullptr;
  // A LifoAlloc marker before the most recently allocated StackMap. Will be set
  // by create() and cleared by finalize().
  LifoAlloc::Mark beforeLastCreated_;
#ifdef DEBUG
  // The StackMap that will be undone by `beforeLastCreated_`. Used to validate
  // correct usage of this class.
  StackMap* createdButNotFinalized_ = nullptr;
#endif

 public:
  StackMaps() : stackMaps_(4096, js::BackgroundMallocArena) {}

  // Allocates a new empty stack map. After configuring the stack map to your
  // liking, you must call finalize().
  [[nodiscard]] StackMap* create(uint32_t numMappedWords) {
    MOZ_ASSERT(!createdButNotFinalized_,
               "a previous StackMap has been created but not finalized");

    beforeLastCreated_ = stackMaps_.mark();
    void* mem =
        stackMaps_.alloc(StackMap::allocationSizeInBytes(numMappedWords));
    if (!mem) {
      return nullptr;
    }
    StackMap* newMap = new (mem) StackMap(numMappedWords);
#ifdef DEBUG
    createdButNotFinalized_ = newMap;
#endif
    return newMap;
  }

  // Allocates a new stack map with a given header, e.g. one that had been
  // previously serialized. After configuring the stack map to your liking, you
  // must call finalize().
  [[nodiscard]] StackMap* create(const StackMapHeader& header) {
    StackMap* map = create(header.numMappedWords);
    if (!map) {
      return nullptr;
    }
    map->header = header;
    return map;
  }

  // Finalizes a stack map allocated by create(). The `map` is no longer valid
  // to access as it may have been deduplicated. The returned stack map must be
  // used instead. This operation is infallible.
  [[nodiscard]] StackMap* finalize(StackMap* map) {
#ifdef DEBUG
    MOZ_ASSERT(
        map == createdButNotFinalized_,
        "the provided stack map was not from the most recent call to create()");
    createdButNotFinalized_ = nullptr;
#endif

    if (lastAdded_ && *map == *lastAdded_) {
      // This stack map is a duplicate of the last one we added. Unwind the
      // allocation that created the new map and add the existing one to the
      // hash map.
      stackMaps_.release(beforeLastCreated_);
      return lastAdded_;
    }

    // This stack map is new.
    lastAdded_ = map;
    stackMaps_.cancelMark(beforeLastCreated_);
    return map;
  }

  // Add a finalized stack map with a given code offset.
  [[nodiscard]] bool add(uint32_t codeOffset, StackMap* map) {
#ifdef JS_JITSPEW
    if (JitSpewEnabled(jit::JitSpew_Codegen)) {
      map->show(codeOffset);
    }
#endif
    MOZ_ASSERT(!createdButNotFinalized_);
    MOZ_ASSERT(stackMaps_.contains(map));
    return codeOffsetToStackMap_.put(codeOffset, map);
  }

  // Finalizes a stack map created by create() and adds it to the given code
  // offset. The `map` is no longer valid to use as it may be deduplicated and
  // freed.
  [[nodiscard]] bool finalize(uint32_t codeOffset, StackMap* map) {
    return add(codeOffset, finalize(map));
  }

  void clear() {
    MOZ_ASSERT(!createdButNotFinalized_);
    codeOffsetToStackMap_.clear();
    stackMaps_.freeAll();
    lastAdded_ = nullptr;
  }
  bool empty() const { return length() == 0; }
  // Return the number of stack maps contained in this.
  size_t length() const { return codeOffsetToStackMap_.count(); }

  // Add all the stack maps from the other collection to this collection.
  // Apply an optional offset while adding the stack maps.
  [[nodiscard]] bool appendAll(StackMaps& other, uint32_t offsetInModule) {
    MOZ_ASSERT(!other.createdButNotFinalized_);

    // Reserve space for the new mappings so that we don't have to handle
    // failure in the loop below.
    if (!codeOffsetToStackMap_.reserve(codeOffsetToStackMap_.count() +
                                       other.codeOffsetToStackMap_.count())) {
      return false;
    }

    // Transfer chunks from other LifoAlloc for ownership. Pointers will stay
    // stable. We must not fail from this point onward.
    stackMaps_.transferFrom(&other.stackMaps_);

    // Copy hash map entries. This is safe because we took ownership of the
    // underlying storage.
    for (auto iter = other.codeOffsetToStackMap_.modIter(); !iter.done();
         iter.next()) {
      uint32_t newOffset = iter.get().key() + offsetInModule;
      StackMap* stackMap = iter.get().value();
      codeOffsetToStackMap_.putNewInfallible(newOffset, stackMap);
    }

    other.clear();
    return true;
  }

  const StackMap* lookup(uint32_t codeOffset) const {
    auto ptr = codeOffsetToStackMap_.readonlyThreadsafeLookup(codeOffset);
    if (!ptr) {
      return nullptr;
    }

    return ptr->value();
  }

  size_t sizeOfExcludingThis(mozilla::MallocSizeOf mallocSizeOf) const {
    return codeOffsetToStackMap_.shallowSizeOfExcludingThis(mallocSizeOf) +
           stackMaps_.sizeOfExcludingThis(mallocSizeOf);
  }

  void checkInvariants(const uint8_t* base) const;

  WASM_DECLARE_FRIEND_SERIALIZE(StackMaps);
};

// Supporting code for creation of stackmaps.

// StackArgAreaSizeUnaligned returns the size, in bytes, of the stack arg area
// size needed to pass |argTypes|, excluding any alignment padding beyond the
// size of the area as a whole.  The size is as determined by the platforms
// native ABI.
//
// StackArgAreaSizeAligned returns the same, but rounded up to the nearest 16
// byte boundary.
//
// Note, StackArgAreaSize{Unaligned,Aligned}() must process all the arguments
// in order to take into account all necessary alignment constraints.  The
// signature must include any receiver argument -- in other words, it must be
// the complete native-ABI-level call signature.
template <class T>
static inline size_t StackArgAreaSizeUnaligned(const T& argTypes,
                                               jit::ABIKind kind) {
  jit::ABIArgIter<const T> i(argTypes, kind);
  while (!i.done()) {
    i++;
  }
  return i.stackBytesConsumedSoFar();
}

static inline size_t StackArgAreaSizeUnaligned(
    const SymbolicAddressSignature& saSig, jit::ABIKind kind) {
  // WasmABIArgIter::ABIArgIter wants the items to be iterated over to be
  // presented in some type that has methods length() and operator[].  So we
  // have to wrap up |saSig|'s array of types in this API-matching class.
  class MOZ_STACK_CLASS ItemsAndLength {
    const jit::MIRType* items_;
    size_t length_;

   public:
    ItemsAndLength(const jit::MIRType* items, size_t length)
        : items_(items), length_(length) {}
    size_t length() const { return length_; }
    jit::MIRType operator[](size_t i) const { return items_[i]; }
  };

  // Assert, at least crudely, that we're not accidentally going to run off
  // the end of the array of types, nor into undefined parts of it, while
  // iterating.
  MOZ_ASSERT(saSig.numArgs <
             sizeof(saSig.argTypes) / sizeof(saSig.argTypes[0]));
  MOZ_ASSERT(saSig.argTypes[saSig.numArgs] ==
             jit::MIRType::None /*the end marker*/);

  ItemsAndLength itemsAndLength(saSig.argTypes, saSig.numArgs);
  return StackArgAreaSizeUnaligned(itemsAndLength, kind);
}

static inline size_t AlignStackArgAreaSize(size_t unalignedSize) {
  return AlignBytes(unalignedSize, jit::WasmStackAlignment);
}

// Generate a stackmap for a function's stack-overflow-at-entry trap, with
// the structure:
//
//    <reg dump area>
//    |       ++ <space reserved before trap, if any>
//    |               ++ <space for Frame>
//    |                       ++ <inbound arg area>
//    |                                           |
//    Lowest Addr                                 Highest Addr
//
// The caller owns the resulting stackmap.  This assumes a grow-down stack.
//
// For non-debug builds, if the stackmap would contain no pointers, no
// stackmap is created, and nullptr is returned.  For a debug build, a
// stackmap is always created and returned.
//
// The "space reserved before trap" is the space reserved by
// MacroAssembler::wasmReserveStackChecked, in the case where the frame is
// "small", as determined by that function.
[[nodiscard]] bool CreateStackMapForFunctionEntryTrap(
    const ArgTypeVector& argTypes, const jit::RegisterOffsets& trapExitLayout,
    size_t trapExitLayoutWords, size_t nBytesReservedBeforeTrap,
    size_t nInboundStackArgBytes, wasm::StackMaps& stackMaps,
    wasm::StackMap** result);

// At a resumable wasm trap, the machine's registers are saved on the stack by
// (code generated by) GenerateTrapExit().  This function writes into |args| a
// vector of booleans describing the ref-ness of the saved integer registers.
// |args[0]| corresponds to the low addressed end of the described section of
// the save area.
[[nodiscard]] bool GenerateStackmapEntriesForTrapExit(
    const ArgTypeVector& args, const jit::RegisterOffsets& trapExitLayout,
    const size_t trapExitLayoutNumWords, ExitStubMapVector* extras);

// Shared write barrier code.
//
// A barriered store looks like this:
//
//   Label skipPreBarrier;
//   EmitWasmPreBarrierGuard(..., &skipPreBarrier);
//   <COMPILER-SPECIFIC ACTIONS HERE>
//   EmitWasmPreBarrierCall(...);
//   bind(&skipPreBarrier);
//
//   <STORE THE VALUE IN MEMORY HERE>
//
//   Label skipPostBarrier;
//   <COMPILER-SPECIFIC ACTIONS HERE>
//   EmitWasmPostBarrierGuard(..., &skipPostBarrier);
//   <CALL POST-BARRIER HERE IN A COMPILER-SPECIFIC WAY>
//   bind(&skipPostBarrier);
//
// The actions are divided up to allow other actions to be placed between
// them, such as saving and restoring live registers.  The postbarrier call
// invokes C++ and will kill all live registers.

// Before storing a GC pointer value in memory, skip to `skipBarrier` if the
// prebarrier is not needed.  Will clobber `scratch`.
//
// It is OK for `instance` and `scratch` to be the same register.
//
// If `trapSiteDesc` is something, then metadata to catch a null access and
// emit a null pointer exception will be emitted. This will only catch a null
// access due to an incremental GC being in progress, the write that follows
// this pre-barrier guard must also be guarded against null.
template <class Addr>
void EmitWasmPreBarrierGuard(jit::MacroAssembler& masm, jit::Register instance,
                             jit::Register scratch, Addr addr,
                             jit::Label* skipBarrier,
                             MaybeTrapSiteDesc trapSiteDesc);

// Before storing a GC pointer value in memory, call out-of-line prebarrier
// code. This assumes `PreBarrierReg` contains the address that will be
// updated. On ARM64 it also assums that x20 (the PseudoStackPointer) has the
// same value as SP.  `PreBarrierReg` is preserved by the barrier function.
// Will clobber `scratch`.
//
// It is OK for `instance` and `scratch` to be the same register.
void EmitWasmPreBarrierCallImmediate(jit::MacroAssembler& masm,
                                     jit::Register instance,
                                     jit::Register scratch,
                                     jit::Register valueAddr,
                                     size_t valueOffset);
// The equivalent of EmitWasmPreBarrierCallImmediate, but for a
// jit::BaseIndex. Will clobber `scratch1` and `scratch2`.
//
// It is OK for `instance` and `scratch1` to be the same register.
void EmitWasmPreBarrierCallIndex(jit::MacroAssembler& masm,
                                 jit::Register instance, jit::Register scratch1,
                                 jit::Register scratch2, jit::BaseIndex addr);

#ifdef ENABLE_WASM_JSPI

// Before resuming a continuation, jump to a 'resume barrier' if an incremental
// GC is happening.
void EmitWasmResumeBarrierGuard(jit::MacroAssembler& masm,
                                jit::Register instance, jit::Register scratch,
                                jit::Label* enterBarrier);

// Call the 'resume barrier' for a continuation. This will clobber all
// registers except for `instance`. `instance` must be InstanceReg.
//
// See [SMDOC] Wasm Stack Switching in WasmStacks.cpp for more information.
//
// This will immediately trace the continuation stack if it hasn't been already.
void EmitWasmResumeBarrier(jit::MacroAssembler& masm, jit::Register instance,
                           jit::Register cont);

#endif  // ENABLE_WASM_JSPI

// After storing a GC pointer value in memory, skip to `skipBarrier` if a
// postbarrier is not needed.  If the location being set is in an
// heap-allocated object then `object` must reference that object; otherwise
// it should be None. The value that was stored is `setValue`.  Will clobber
// `otherScratch` and will use other available scratch registers.
//
// `otherScratch` cannot be a designated scratch register.
void EmitWasmPostBarrierGuard(jit::MacroAssembler& masm,
                              const mozilla::Maybe<jit::Register>& object,
                              jit::Register otherScratch,
                              jit::Register setValue, jit::Label* skipBarrier);

// Before calling Instance::postBarrierWholeCell, we can check the object
// against the store buffer's last element cache, skipping the post barrier if
// that object had already been barriered.
//
// `instance` and `temp` can be the same register; if so, instance will be
// clobbered, otherwise instance will be preserved.
void CheckWholeCellLastElementCache(jit::MacroAssembler& masm,
                                    jit::Register instance,
                                    jit::Register object, jit::Register temp,
                                    jit::Label* skipBarrier);

#ifdef DEBUG
// Check (approximately) whether `nextPC` is a valid code address for a
// stackmap created by this compiler.  This is done by examining the
// instruction at `nextPC`.  The matching is inexact, so it may err on the
// side of returning `true` if it doesn't know.  Doing so reduces the
// effectiveness of the MOZ_ASSERTs that use this function, so at least for
// the four primary platforms we should keep it as exact as possible.

bool IsPlausibleStackMapKey(const uint8_t* nextPC);
#endif

}  // namespace wasm
}  // namespace js

#endif  // wasm_gc_h
