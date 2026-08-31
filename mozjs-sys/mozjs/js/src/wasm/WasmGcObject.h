/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef wasm_WasmGcObject_h
#define wasm_WasmGcObject_h

#include "mozilla/Attributes.h"
#include "mozilla/CheckedInt.h"
#include "mozilla/Maybe.h"

#include "gc/GCProbes.h"
#include "gc/Pretenuring.h"
#include "gc/ZoneAllocator.h"  // AddCellMemory
#include "vm/JSContext.h"
#include "vm/JSObject.h"
#include "vm/Probes.h"
#include "wasm/WasmInstanceData.h"
#include "wasm/WasmMemory.h"
#include "wasm/WasmTypeDef.h"
#include "wasm/WasmValType.h"

namespace js {

//=========================================================================
// WasmGcObject

class WasmGcObject : public JSObject {
 protected:
  const wasm::SuperTypeVector* superTypeVector_;

  static const ObjectOps objectOps_;

  [[nodiscard]] static bool obj_lookupProperty(JSContext* cx, HandleObject obj,
                                               HandleId id,
                                               MutableHandleObject objp,
                                               PropertyResult* propp);

  [[nodiscard]] static bool obj_defineProperty(JSContext* cx, HandleObject obj,
                                               HandleId id,
                                               Handle<PropertyDescriptor> desc,
                                               ObjectOpResult& result);

  [[nodiscard]] static bool obj_hasProperty(JSContext* cx, HandleObject obj,
                                            HandleId id, bool* foundp);

  [[nodiscard]] static bool obj_getProperty(JSContext* cx, HandleObject obj,
                                            HandleValue receiver, HandleId id,
                                            MutableHandleValue vp);

  [[nodiscard]] static bool obj_setProperty(JSContext* cx, HandleObject obj,
                                            HandleId id, HandleValue v,
                                            HandleValue receiver,
                                            ObjectOpResult& result);

  [[nodiscard]] static bool obj_getOwnPropertyDescriptor(
      JSContext* cx, HandleObject obj, HandleId id,
      MutableHandle<mozilla::Maybe<PropertyDescriptor>> desc);

  [[nodiscard]] static bool obj_deleteProperty(JSContext* cx, HandleObject obj,
                                               HandleId id,
                                               ObjectOpResult& result);

  // PropOffset is a uint32_t that is used to carry information about the
  // location of an value from WasmGcObject::lookupProperty to
  // WasmGcObject::loadValue.  It is distinct from a normal uint32_t to
  // emphasise the fact that it cannot be interpreted as an offset in any
  // single contiguous area of memory:
  //
  // * If the object in question is a WasmStructObject, it is the index of
  //   the relevant field.
  //
  // * If the object in question is a WasmArrayObject, then
  //   - u32 == UINT32_MAX (0xFFFF'FFFF) means the "length" property
  //     is requested
  //   - u32 < UINT32_MAX means the array element starting at that byte
  //     offset in WasmArrayObject::data_.  It is not an array index value.
  //   See WasmGcObject::lookupProperty for details.
  class PropOffset {
    uint32_t u32_;

   public:
    PropOffset() : u32_(0) {}
    uint32_t get() const { return u32_; }
    void set(uint32_t u32) { u32_ = u32; }
  };

  [[nodiscard]] static bool lookUpProperty(JSContext* cx,
                                           Handle<WasmGcObject*> obj, jsid id,
                                           PropOffset* offset,
                                           wasm::StorageType* type);

 public:
  [[nodiscard]] static bool loadValue(JSContext* cx, Handle<WasmGcObject*> obj,
                                      jsid id, MutableHandleValue vp);

  const wasm::SuperTypeVector& superTypeVector() const {
    return *superTypeVector_;
  }

  static constexpr size_t offsetOfSuperTypeVector() {
    return offsetof(WasmGcObject, superTypeVector_);
  }

  // These are both expensive in that they involve a double indirection.
  // Avoid them if possible.
  const wasm::TypeDef& typeDef() const { return *superTypeVector().typeDef(); }
  wasm::TypeDefKind kind() const { return superTypeVector().typeDef()->kind(); }

  [[nodiscard]] bool isRuntimeSubtypeOf(
      const wasm::TypeDef* parentTypeDef) const;

  [[nodiscard]] static bool obj_newEnumerate(JSContext* cx, HandleObject obj,
                                             MutableHandleIdVector properties,
                                             bool enumerableOnly);
};

//=========================================================================
// WasmArrayObject

// [SMDOC] WasmArrayObject layout
//
// `class WasmArrayObject` represents wasm-GC arrays in the JS heap.
//
// For zero-sized and small arrays, which are common, the data is stored
// in-line (IL) immediately after the end of the WasmArrayObject.
//
// For arrays too large to represent IL, the WasmArrayObject points to an
// out-of-line (OOL) storage area that is managed by js::gc::BufferAllocator,
// which holds all of the elements.  This OOL area is sometimes referred to
// below as the "OOL block"; note however it consists of a one-word
// OOLDataHeader followed by the actual array data.
//
// Layout of WasmArrayObjects and their associated OOL areas is constrained by
// multiple requirements:
//
// (1) There must be a pointer to the storage array (the "array data pointer"
//     or just "data pointer"), that can be used as a base for indexing without
//     regard to whether the data is IL or OOL.
//
// (2) The array data areas must be 8-aligned, even on 32-bit targets, so that
//     int64/double accesses are aligned.
//
// (3) The layout must offer a way to update on-stack data pointers on minor
//     collections, without having access to the parent object.
//
// (4) The layout must be as compact as reasonably possible.
//
// (5) The IL array data must begin immediately after the end of the preceding
//     field (`data*`), with no alignment hole in between.  This is critical;
//     the scheme won't work without this.
//
// (6) [Derived from 2 and 5] The size of WasmArrayObject must be 0 % 8.
//
// (7) [Derived from 2] The size of WasmArrayObject::OOLDataHeader
//     must be 0 % 8.
//
// (8) [Derived from 5] offsetof(WasmArrayObject, data_) + sizeof(data_)
//                      == sizeof(WasmArrayObject).
//
// (9) It is assumed that the underlying JSObject and BufferAlloc allocators
//     produce memory that is at least 0 % 8.  Without that, all of the above
//     is pointless.
//
// The layout complexity starts at the data* field.  Adding extra fields before
// the data* field should have no effect, so long as the added field(s) have a
// size which is a multiple of 8 bytes.
//
// -------- 64-bit targets --------
//
// Layout is as follows:
//
// * Each `| name |` unit is an 8-aligned, 8-byte word,
//   except for the `| ..arrayData.. |` fields, which are 8-aligned and
//   a multiple of 8 bytes long (including zero).
//
// * "padding / #elems" means the 32-bit `numElements_` field and the 4 bytes
//   padding that precedes it.
//
// : 0    7 : 8    15 : 16   23  24  31 : 32    // byte offset
// : JS     : WasmGc  : WasmArray       :       // class name
// : Object : Object  : Object          :
// :        :         :                 :
// |        | Super   | padding |       |               |        (IL case)
// | Shape* | TypeVec |  #elems | data* | ..arrayData.. |
//                                 | |    |
//                                 | \-->-/
//                                 |
//                                 | | header | ..arrayData.. |  (OOL case)
//                                 |            |
//                                 \--->---->---/
//
// For the IL case, the array data area starts at offset 32, so is 8-aligned.
// The OOL allocation as a whole is 8-aligned, and the header is 1 word long,
// so the array data is also 8-aligned.
//
// -------- 32-bit targets --------
//
// Layout is as follows:
//
// * Each `| name |` unit is an 4-aligned, 4-byte word,
//   except for the `| ..arrayData.. |` fields, which have the same
//   constraints as the 64-bit layout: 8-aligned and
//   a multiple of 8 bytes long (including zero).
//
// * Both `padd/ing` fields are 4 bytes long.
//
// : 0    3   4  7 : 8    11 : 12 15  16  19   20 23 : 24  // byte offset
// : JS            : WasmGc  : WasmArray             :     // class name
// : Object        : Object  : Object                :
// :               :         :                       :
// |        | padd | Super   | padd |        |       |               |
// | Shape* |  ing | TypeVec |  ing | #elems | data* | ..arrayData.. |
//                                              | |    |         (IL case)
//                                              v \-->-/
//                                              |
//                         | padding | header | ..arrayData.. |  (OOL case)
//
// For the IL case, the array data area starts at offset 24, so is 8-aligned.
// The OOL block as a whole is 8-aligned, and the header word and padding
// together are 8 bytes, so the array data is also 8-aligned.
//
// Note 1: Don't confuse JSObject::padding (not discussed further),
//         WasmArrayObject::padding (discussed here) and
//         OOLDataHeader::padding (see below).  These are unrelated.
//
// Note 2: WasmArrayObject::padding is omitted on 32-bit Windows.  On that
//         platform, it appears that JSObject occupies offsets 0 .. 7 inclusive
//         (as shown), but WasmGcObject occupies offsets 8 .. 15 inclusive
//         (*not* as shown), which then causes the array data to start at
//         offset 28, not 24.  The reason for this is unknown.  It may be to do
//         with incomplete empty-base-class optimization in MSVC and clang-cl,
//         given that `TrailingArray` is an empty class.  However, adding
//         MOZ_EMPTY_BASES does not help.
//
//         The manual fix -- removal of the `padding` field -- restores the
//         positioning of #elems, data* and payload.
//
// -------- OOL Header words --------
//
// The OOL header word (OOLDataHeader::word) is used both for block forwarding
// and to know whether a data pointer is IL or OOL, without having access to
// its parent WasmArrayObject.
//
// This is done by looking at the word immediately preceding what the data
// pointer points at.  There are 3 possible cases:
//
// (a) The data is IL, so the word preceding what the data pointer points at is
//     the data* pointer itself (WasmArrayObject::data_).  From (2) above we
//     know this is 8-aligned, so the value stored there ends in 0b000.
//
// (b) The data is OOL and the header word holds a forwarding address.  We
//     manage forwarding addresses ourselves, and so set bit 0 to 1 to indicate
//     it's a forwarding address.
//
// (c) The data is OOL but has never been forwarded.  The header word will have
//     been initialized to OOLDataHeader_Magic, a constant that has bit 0 set
//     to 1, and, even if the bit 0 was zero, would never be a feasible
//     pointer.
//
// Hence, given a data pointer, we can determine whether the data is IL or OOL
// thusly:
//
//   data-is-IL  =  (((uintptr_t*)data_pointer)[-1] & 1) == 0
//
// If the data isn't IL, then ((uintptr_t*)data_pointer)[-1] either holds a
// forwarding pointer or not, thus:
//
//   has_forwarding_pointer
//     = !block-is-IL && ((uintptr_t*)data_pointer)[-1] != OOLDataHeader_Magic
//
// -------- Other comments --------
//
// Throughout the implementation, a value with name `arrayDataBytes` holds the
// size, in bytes, of a `| ..arrayData.. |` area.  It is exactly the number of
// bytes required to hold the stored elements.  Hence, if the data area is OOL,
// the size of the (BufferAllocator-managed) block is `arrayDataBytes +
// sizeof(OOLDataHeader)`, that is, `arrayDataBytes + 8`.
//
// -------- end --------

// Class for a wasm array.  It contains `data_`, a pointer to the array data,
// and possibly contains the data inline.  If there's not enough space inline,
// then `data_` points to byte 8 of an out-of-line area managed by
// js::gc::BufferAllocator.  In either case, `data_` points to element zero of
// the data, and so can be used as the array base when indexing, without
// knowing whether it is inline or out-of-line.

#undef WASM_ARRAY_OBJECT_NEEDS_PADDING
#if !(defined(XP_WIN) && defined(_WIN32) && !defined(__MINGW32__))
#  define WASM_ARRAY_OBJECT_NEEDS_PADDING 1
#endif

class WasmArrayObject : public WasmGcObject,
                        public TrailingArray<WasmArrayObject> {
 public:
  static const JSClass class_;

  // For both the IL and OOL cases, the array data must be 8-aligned.
  static constexpr uint32_t ArrayDataAlignment = 8;

  // ---- OOLDataHeader ----

  struct OOLDataHeader {
#ifndef JS_64BIT
    uintptr_t padding = 0;
#endif
    uintptr_t word = OOLDataHeader_Magic;
  };
  static_assert(sizeof(OOLDataHeader) == 8);

  // 0x351 has bit zero set, is unusual, and is in page 0 which is surely not
  // accessible.
  static constexpr uintptr_t OOLDataHeader_Magic = 0x351ULL;

  // ---- main layout ----

  // numElements_:
  //   The number of elements in the array.
  //
  // data_:
  //   Owned data pointer, holding `numElements_` entries. In the IL case, this
  //   points to the data array immediately after the object.  In the OOL case
  //   this points 8-bytes inside an OOL storage block that is managed by
  //   gc::BufferAllocator, and the first 8 bytes of the block is an
  //   OOLDataHeader.
  //
  //   This pointer is never null. An empty array will be stored like any
  //   other IL-storage array.

  // See the SMDOC above.
#ifdef WASM_ARRAY_OBJECT_NEEDS_PADDING
  uint32_t padding_;
#endif
  uint32_t numElements_;
  uint8_t* data_;

  // At this point, the IL data area begins.  Do not add any (C++-level) fields
  // after this point!

  // ---- methods etc ----

  // Get a pointer to the IL data area.  Because we require that there's no
  // alignment hole between the object proper and the data area, we can just
  // add the size of the object to its base pointer.
  template <typename T>
  T* inlineArrayData() {
    return offsetToPointer<T>(sizeof(WasmArrayObject));
  }

  // Get the element at index `i`.
  template <typename T>
  inline T get(uint32_t i) const {
    MOZ_ASSERT(i < numElements_);
    MOZ_ASSERT(sizeof(T) == typeDef().arrayType().elementType().size());
    return ((T*)data_)[i];
  }

  // AllocKinds for object creation
  static inline gc::AllocKind allocKindForOOL();
  static inline gc::AllocKind allocKindForIL(uint32_t arrayDataBytes);
  inline gc::AllocKind allocKind() const;

  // Calculate the byte length of the array's data storage, being careful to
  // check for overflow.  This includes the data and any extra space for
  // alignment with GC sizes, but it does not include the OOLDataHeader.  Note
  // this logic assumes that MaxArrayPayloadBytes is within uint32_t range.
  //
  // This logic is mirrored in WasmArrayObject::maxInlineElementsForElemSize
  // and MacroAssembler::wasmNewArrayObject.
  static constexpr mozilla::CheckedUint32 calcArrayDataBytesChecked(
      uint32_t elemSize, uint32_t numElements) {
    static_assert(sizeof(WasmArrayObject) % gc::CellAlignBytes == 0);
    mozilla::CheckedUint32 arrayDataBytes = elemSize;
    arrayDataBytes *= numElements;
    // Round total allocation up to gc::CellAlignBytes.  This fails when
    // `arrayDataBytes` is zero, because the `-= 1` bit produces underflow.
    // So, first add on gc::CellAlignBytes and remove it afterward.
    arrayDataBytes += gc::CellAlignBytes;
    arrayDataBytes -= 1;
    arrayDataBytes +=
        gc::CellAlignBytes - (arrayDataBytes % gc::CellAlignBytes);
    arrayDataBytes -= gc::CellAlignBytes;
    MOZ_ASSERT_IF(arrayDataBytes.isValid(),
                  arrayDataBytes.value() % gc::CellAlignBytes == 0);
    MOZ_ASSERT_IF(numElements == 0,
                  arrayDataBytes.isValid() && arrayDataBytes.value() == 0);
    return arrayDataBytes;
  }
  // The same as ::calcArrayDataBytesChecked, but does not check for overflow.
  static uint32_t calcArrayDataBytesUnchecked(uint32_t elemSize,
                                              uint32_t numElements) {
    mozilla::CheckedUint32 arrayDataBytes =
        calcArrayDataBytesChecked(elemSize, numElements);
    MOZ_ASSERT(arrayDataBytes.isValid());
    return arrayDataBytes.value();
  }
  // Compute the maximum number of elements that can be stored inline for the
  // given element size.
  static inline constexpr uint32_t maxInlineElementsForElemSize(
      uint32_t elemSize);

  size_t sizeOfExcludingThis() const;

  // Creates a new array object with out-of-line storage. Reports an error on
  // OOM. The element type, shape, class pointer, alloc site and alloc kind are
  // taken from `typeDefData`; the initial heap must be specified separately.
  // `arrayDataBytes` is the size of the storage array and does not take into
  // account the one-word OOL data header.  `arrayDataBytes` is debug-asserted
  // to be larger than WasmArrayObject_MaxInlineBytes - generally, C++ code
  // should use WasmArrayObject::createArray.
  template <bool ZeroFields>
  static MOZ_ALWAYS_INLINE WasmArrayObject* createArrayOOL(
      JSContext* cx, wasm::TypeDefInstanceData* typeDefData,
      js::gc::AllocSite* allocSite, js::gc::Heap initialHeap,
      uint32_t numElements, uint32_t arrayDataBytes);

  // Creates a new array object with inline storage. Reports an error on OOM.
  // The element type, shape, class pointer, alloc site and alloc kind are taken
  // from `typeDefData`; the initial heap must be specified separately. The size
  // of storage is debug-asserted to be within WasmArrayObject_MaxInlineBytes -
  // generally, C++ code should use WasmArrayObject::createArray.
  // `arrayDataBytes` is the size of the storage array.
  template <bool ZeroFields>
  static MOZ_ALWAYS_INLINE WasmArrayObject* createArrayIL(
      JSContext* cx, wasm::TypeDefInstanceData* typeDefData,
      js::gc::AllocSite* allocSite, js::gc::Heap initialHeap,
      uint32_t numElements, uint32_t arrayDataBytes);

  // This selects one of the above two routines, depending on how much storage
  // is required for the given type and number of elements.
  template <bool ZeroFields>
  static MOZ_ALWAYS_INLINE WasmArrayObject* createArray(
      JSContext* cx, wasm::TypeDefInstanceData* typeDefData,
      js::gc::AllocSite* allocSite, js::gc::Heap initialHeap,
      uint32_t numElements);

  // JIT accessors
  static constexpr size_t offsetOfNumElements() {
    return offsetof(WasmArrayObject, numElements_);
  }
  static constexpr size_t offsetOfData() {
    return offsetof(WasmArrayObject, data_);
  }
  static constexpr size_t offsetOfInlineArrayData() {
    static_assert((sizeof(WasmArrayObject) % ArrayDataAlignment) == 0);
    return sizeof(WasmArrayObject);
  }

  // Tracing and finalization
  static void obj_trace(JSTracer* trc, JSObject* object);
  static void obj_finalize(JS::GCContext* gcx, JSObject* object);
  static size_t obj_moved(JSObject* objNew, JSObject* objOld);

  void storeVal(const wasm::Val& val, uint32_t itemIndex);
  void fillVal(const wasm::Val& val, uint32_t itemIndex, uint32_t len);

#ifdef DEBUG
  static bool IsValidlyAlignedDataPointer(const void* v) {
    return (uintptr_t(v) & (ArrayDataAlignment - 1)) == 0;
  }
#endif
  static inline OOLDataHeader* oolDataHeaderFromDataPointer(
      const uint8_t* data) {
    MOZ_ASSERT(data);
    MOZ_ASSERT(IsValidlyAlignedDataPointer(data));
    OOLDataHeader* header = (OOLDataHeader*)data;
    header--;
    MOZ_ASSERT((header->word & 1) == 1);
    return header;
  }
  static inline uint8_t* oolDataHeaderToDataPointer(OOLDataHeader* header) {
    MOZ_ASSERT(header);
    MOZ_ASSERT(IsValidlyAlignedDataPointer(header));
    MOZ_ASSERT((header->word & 1) == 1);
    header++;
    return (uint8_t*)header;
  }
  inline OOLDataHeader* oolDataHeader() const {
    MOZ_ASSERT(!isDataInline());
    return WasmArrayObject::oolDataHeaderFromDataPointer(data_);
  }

  static inline bool isDataInline(uint8_t* data) {
    MOZ_ASSERT(data);
    MOZ_ASSERT(IsValidlyAlignedDataPointer(data));
    // Do oolDataHeaderFromDataPointer(data) without the assertions it has.
    const OOLDataHeader* header = (OOLDataHeader*)data;
    header--;
    uintptr_t headerWord = header->word;
    return (headerWord & 1) == 0;
  }
  bool isDataInline() const { return WasmArrayObject::isDataInline(data_); }

  // ::fromInlineDataPointer and ::addressOfInlineArrayData are inverses of
  // each other -- the first subtracts sizeof(WasmArrayObject) from the given
  // pointer, the second adds it back on.
  static WasmArrayObject* fromInlineDataPointer(uint8_t* data) {
    MOZ_ASSERT(isDataInline(data));
    WasmArrayObject* arrayObj =
        (WasmArrayObject*)(data - WasmArrayObject::offsetOfInlineArrayData());
    MOZ_ASSERT(WasmArrayObject::addressOfInlineArrayData(arrayObj) == data);
    return arrayObj;
  }

  static uint8_t* addressOfInlineArrayData(WasmArrayObject* base) {
    return base->offsetToPointer<uint8_t>(offsetOfInlineArrayData());
  }
};

// Some important layout constraints as specified by the SMDOC above.

// This is a requirement.
static_assert(WasmArrayObject::ArrayDataAlignment == 8);

// As is this.
static_assert((sizeof(WasmArrayObject::OOLDataHeader) %
               WasmArrayObject::ArrayDataAlignment) == 0);

// The inline payload must start on an 8-aligned boundary.
static_assert((sizeof(WasmArrayObject) % WasmArrayObject::ArrayDataAlignment) ==
              0);

// Assert that object sizes are as per the SMDOC.
#ifdef JS_64BIT
static_assert(sizeof(WasmArrayObject) == 32);
#else
static_assert(sizeof(WasmArrayObject) == 24);
#endif

// Assert that WasmArrayObject::data_ immediately precedes the IL array data,
// with no gap.
static_assert((offsetof(WasmArrayObject, data_) +
               sizeof(WasmArrayObject::data_)) == sizeof(WasmArrayObject));

// Similarly, assert that OOLDataHeader::word immediately precedes the OOL
// array data, with no gap.
static_assert((offsetof(WasmArrayObject::OOLDataHeader, word) +
               sizeof(WasmArrayObject::OOLDataHeader::word)) ==
              sizeof(WasmArrayObject::OOLDataHeader));

// `data_` must start on a word-aligned boundary.
static_assert((offsetof(WasmArrayObject, data_) % sizeof(void*)) == 0);

// It must not be possible to confuse OOLDataHeader_Magic with any word-aligned
// pointer.
static_assert((WasmArrayObject::OOLDataHeader_Magic & 1) == 1);

// OOLDataHeader_Magic is smaller than any possible valid pointer, assuming
// that page zero is never accessible.
static_assert(WasmArrayObject::OOLDataHeader_Magic < 4096);

// wasm::MaxArrayPayloadBytes must be at least 8 bytes below 2^32, so that if
// we are asked to allocate a max-sized array, adding on the OOLDataHeader
// word won't cause the total OOL block size to wrap around 2^32.  In order to
// deal with future worst-case alignment requirements, actually require 64
// bytes of margin.
static_assert(uint64_t(wasm::MaxArrayPayloadBytes) + 64 < uint64_t(UINT32_MAX));

// All of the above is pointless unless the GC's allocators provide at least
// this:
static_assert(gc::CellAlignBytes >= WasmArrayObject::ArrayDataAlignment);

// Helper to mark all locations that assume that the type of
// WasmArrayObject::numElements is uint32_t.
#define STATIC_ASSERT_WASMARRAYELEMENTS_NUMELEMENTS_IS_U32 \
  static_assert(sizeof(js::WasmArrayObject::numElements_) == sizeof(uint32_t))

//=========================================================================
// WasmStructObject

// Class for a wasm struct.  It has inline data and, if the inline area is
// insufficient, a pointer to outline data that lives in the C++ heap.
// Computing the field offsets is somewhat tricky; see SMDOC in
// WasmStructLayout.h.
//
// From a C++ viewpoint, WasmStructObject just holds two pointers, a shape
// pointer and the supertype vector pointer.  Because of class-total-size
// roundup effects, it is 16 bytes on both 64- and 32-bit targets.
//
// For our purposes a WasmStructObject is always followed immediately by an
// in-line data area, with maximum size WasmStructObject_MaxInlineBytes.  Both
// the two-word header and the inline data area have 8-aligned sizes.  The GC's
// allocation routines only guarantee 8-byte alignment.  This means a
// WasmStructObject can offer naturally aligned storage for fields of size 8,
// 4, 2 and 1, but not for fields of size 16, even though the header size is 16
// bytes.
//
// If the available inline storage is insufficient, some part of the inline
// data are will be used as a pointer to the out of line area.  This however is
// not WasmStructObject's concern: it is unaware of the in-line area layout,
// all details of which are stored in the associated StructType, and partially
// cached in TypeDefInstanceData.cached.strukt.
//
// Note that MIR alias analysis assumes the OOL-pointer field, if any, is
// readonly for the life of the object; do not change it once the object is
// created.  See MWasmLoadField::congruentTo.

class WasmStructObject : public WasmGcObject,
                         public TrailingArray<WasmStructObject> {
 public:
  static const JSClass classInline_;
  static const JSClass classOutline_;

  static const JSClass* classFromOOLness(bool needsOOLstorage) {
    return needsOOLstorage ? &classOutline_ : &classInline_;
  }

  size_t sizeOfExcludingThis() const;

  // Creates a new struct typed object, optionally initialized to zero.
  // Reports if there is an out of memory error.  The structure's type, shape,
  // class pointer, alloc site and alloc kind are taken from `typeDefData`;
  // the initial heap must be specified separately.  It is assumed and debug-
  // asserted that `typeDefData` refers to a type that does not need OOL
  // storage.
  template <bool ZeroFields>
  static MOZ_ALWAYS_INLINE WasmStructObject* createStructIL(
      JSContext* cx, wasm::TypeDefInstanceData* typeDefData,
      gc::AllocSite* allocSite, js::gc::Heap initialHeap);

  // Same as ::createStructIL, except it is assumed and debug-asserted that
  // `typeDefData` refers to a type that does need OOL storage.
  template <bool ZeroFields>
  static MOZ_ALWAYS_INLINE WasmStructObject* createStructOOL(
      JSContext* cx, wasm::TypeDefInstanceData* typeDefData,
      gc::AllocSite* allocSite, js::gc::Heap initialHeap);

  // Given the index of a field, return its actual address.
  uint8_t* fieldIndexToAddress(uint32_t fieldIndex);

  // Operations relating to the OOL block pointer.  These involve chain-chasing
  // starting from `superTypeVector_` and shouldn't be used in very hot paths.
  bool hasOOLPointer() const;
  // These will release-assert if called when `!hasOOLPointer()`.
  uint8_t** addressOfOOLPointer() const;
  uint8_t* getOOLPointer() const;
  void setOOLPointer(uint8_t* newOOLpointer);

  // Similar to the above, but find the OOL pointer by looking in the supplied
  // TypeDefInstanceData.  This requires less chain-chasing.
  uint8_t** addressOfOOLPointer(
      const wasm::TypeDefInstanceData* typeDefData) const;
  void setOOLPointer(const wasm::TypeDefInstanceData* typeDefData,
                     uint8_t* newOOLpointer);

  // Gets JS Value of the structure field.
  bool getField(JSContext* cx, uint32_t index, MutableHandle<Value> val);

  // Tracing and finalization
  static void obj_trace(JSTracer* trc, JSObject* object);
  static size_t obj_moved(JSObject* objNew, JSObject* objOld);

  void storeVal(const wasm::Val& val, uint32_t fieldIndex);
};

// This isn't specifically required.  Is merely here to make it obvious when
// the size does change.
static_assert(sizeof(WasmStructObject) == 16);

// Both `sizeof(WasmStructObject)` and WasmStructObject_MaxInlineBytes
// must be multiples of 8 for reasons described in the comment on
// `class WasmStructObject` above.
static_assert((sizeof(WasmStructObject) % 8) == 0);

const size_t WasmStructObject_MaxInlineBytes =
    ((JSObject::MAX_BYTE_SIZE - sizeof(WasmStructObject)) / 8) * 8;

static_assert((WasmStructObject_MaxInlineBytes % 8) == 0);

// These are EXTREMELY IMPORTANT.  Do not remove them.  Without them, there is
// nothing that ensures that the object layouts created by StructType::init()
// will actually be in accordance with the WasmStructObject layout constraints
// described above.  If either fails, the _ASSUMED values are wrong and will
// need to be updated.
static_assert(wasm::WasmStructObject_Size_ASSUMED == sizeof(WasmStructObject));
static_assert(wasm::WasmStructObject_MaxInlineBytes_ASSUMED ==
              WasmStructObject_MaxInlineBytes);

const size_t WasmArrayObject_MaxInlineBytes =
    ((JSObject::MAX_BYTE_SIZE - sizeof(WasmArrayObject)) / 16) * 16;

static_assert((WasmArrayObject_MaxInlineBytes % 16) == 0);

/* static */
inline constexpr uint32_t WasmArrayObject::maxInlineElementsForElemSize(
    uint32_t elemSize) {
  // This implementation inverts the logic of
  // WasmArrayObject::calcArrayDataBytes to compute numElements.
  MOZ_RELEASE_ASSERT(elemSize > 0);
  uint32_t result = WasmArrayObject_MaxInlineBytes;
  static_assert(WasmArrayObject_MaxInlineBytes % gc::CellAlignBytes == 0);
  result /= elemSize;

  MOZ_RELEASE_ASSERT(calcArrayDataBytesChecked(elemSize, result).isValid());
  return result;
}

inline bool WasmStructObject::hasOOLPointer() const {
  const wasm::SuperTypeVector* stv = superTypeVector_;
  const wasm::TypeDef* typeDef = stv->typeDef();
  MOZ_ASSERT(typeDef->superTypeVector() == stv);
  const wasm::StructType& structType = typeDef->structType();
  uint32_t offset = structType.oolPointerOffset_;
  return offset != wasm::StructType::InvalidOffset;
}

inline uint8_t** WasmStructObject::addressOfOOLPointer() const {
  const wasm::SuperTypeVector* stv = superTypeVector_;
  const wasm::TypeDef* typeDef = stv->typeDef();
  MOZ_ASSERT(typeDef->superTypeVector() == stv);
  const wasm::StructType& structType = typeDef->structType();
  uint32_t offset = structType.oolPointerOffset_;
  MOZ_RELEASE_ASSERT(offset != wasm::StructType::InvalidOffset);
  return (uint8_t**)((uint8_t*)this + offset);
}

inline uint8_t* WasmStructObject::getOOLPointer() const {
  return *addressOfOOLPointer();
}

inline void WasmStructObject::setOOLPointer(uint8_t* newOOLpointer) {
  *addressOfOOLPointer() = newOOLpointer;
}

inline uint8_t** WasmStructObject::addressOfOOLPointer(
    const wasm::TypeDefInstanceData* typeDefData) const {
  uint32_t offset = typeDefData->cached.strukt.oolPointerOffset;
  MOZ_RELEASE_ASSERT(offset != wasm::StructType::InvalidOffset);
  uint8_t** addr = (uint8_t**)((uint8_t*)this + offset);
  // Don't turn this into a release-assert; that would defeat the purpose of
  // having this method.
  MOZ_ASSERT(addr == addressOfOOLPointer());
  return addr;
}

inline void WasmStructObject::setOOLPointer(
    const wasm::TypeDefInstanceData* typeDefData, uint8_t* newOOLpointer) {
  *addressOfOOLPointer(typeDefData) = newOOLpointer;
}

// Ensure that faulting loads/stores for WasmStructObject and WasmArrayObject
// are in the NULL pointer guard page.
static_assert(WasmStructObject_MaxInlineBytes <= wasm::NullPtrGuardSize);
static_assert(sizeof(WasmArrayObject) <= wasm::NullPtrGuardSize);

}  // namespace js

//=========================================================================
// misc

namespace js {

inline bool IsWasmGcObjectClass(const JSClass* class_) {
  return class_ == &WasmArrayObject::class_ ||
         class_ == &WasmStructObject::classInline_ ||
         class_ == &WasmStructObject::classOutline_;
}

}  // namespace js

template <>
inline bool JSObject::is<js::WasmGcObject>() const {
  return js::IsWasmGcObjectClass(getClass());
}

template <>
inline bool JSObject::is<js::WasmStructObject>() const {
  const JSClass* class_ = getClass();
  return class_ == &js::WasmStructObject::classInline_ ||
         class_ == &js::WasmStructObject::classOutline_;
}

#endif /* wasm_WasmGcObject_h */
