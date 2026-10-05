/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef vm_Iteration_h
#define vm_Iteration_h

/*
 * JavaScript iterators.
 */

#include "mozilla/MemoryReporting.h"

#include "builtin/SelfHostingDefines.h"
#include "gc/Barrier.h"
#include "util/Memory.h"
#include "vm/NativeObject.h"
#include "vm/TypedArrayObject.h"

/*
 * [SMDOC] For-in enumeration
 *
 * A for-in loop in JS iterates over the string-valued, enumerable
 * property keys of an object and its prototype chain. The order in
 * which keys appear is specified to the extent that implementations
 * historically agreed, and implementation-defined beyond that. See
 * https://tc39.es/ecma262/#sec-enumerate-object-properties for the
 * gory details. Each key appears only once in the enumeration.
 *
 * We enumerate properties using PropertyEnumerator, which creates an
 * ordered list of PropertyKeys, using ShapePropertyIter for native
 * objects and calling enumerate hooks where necessary. This list is
 * used to create a NativeIterator, which contains (among other
 * things) a trailing array of strings representing the property keys
 * of the object, and a cursor pointing into that array. This
 * NativeIterator is wrapped in a PropertyIteratorObject, which is
 * pushed by JSOp::Iter and used by JSOp::MoreIter and JSOp::EndIter.
 *
 * While active, a NativeIterator is registered in a doubly linked
 * list, rooted in the compartment. When any property is deleted from
 * an object, this list is used to remove the deleted property from
 * any active enumerations. See SuppressDeletedProperty. This slows
 * down deletion but speeds up enumeration, which is generally a good
 * tradeoff.
 *
 * In many cases, objects with the same shape will have the same set
 * of property keys. (The most common exception is objects with dense
 * elements, which can be added or removed without changing the shape
 * of the object.) In such cases, we can reuse an existing iterator by
 * storing a pointer to the PropertyIteratorObject in the shape's
 * |cache_| pointer. Before reusing an iterator, we have to verify
 * that the prototype chain has not changed and no dense elements have
 * been added, which is done by storing a trailing array of prototype
 * shapes in the NativeIterator and comparing it against the shapes of
 * the prototype chain.
 *
 * One of the most frequent uses of for-in loops is in loops that look
 * like this, which iterate over each property of an object and do
 * something with those values:
 *   for (var key in obj) {
 *     if (obj.hasOwnProperty(key)) {
 *       doSomethingWith(obj[key]);
 *     }
 *   }
 * Most objects don't have any enumerable properties on the prototype
 * chain. In such cases, we can speed up property access inside the
 * loop by precomputing some information and storing it in the
 * iterator.  When we see a pattern like this in Ion, we generate a
 * call to GetIteratorWithIndices instead of GetIterator. In this
 * case, in addition to the list of property keys, PropertyEnumerator
 * will try to generate a list of corresponding PropertyIndex values,
 * which represent the location of the own property key in the object
 * (fixed slot/dynamic slot/dense element + offset). This list will be
 * stored in NativeIterator as yet another trailing array. When
 * present, it can be used by Ion code to speed up property access
 * inside for-in loops. See OptimizeIteratorIndices in
 * IonAnalysis.cpp.
 */

namespace js {

class ArrayObject;
class PlainObject;
class PropertyIteratorObject;

// A PropertyIndex stores information about the location of an own data
// property in a format that can be stored in a NativeIterator and consumed by
// jitcode to access properties without needing to use the megamorphic cache.
struct PropertyIndex {
 private:
  uint32_t asBits_;

 public:
  enum class Kind : uint32_t { DynamicSlot, FixedSlot, Element, Invalid };

  PropertyIndex(Kind kind, uint32_t index) : asBits_(encode(kind, index)) {}

  static PropertyIndex Invalid() { return PropertyIndex(Kind::Invalid, 0); }

  static PropertyIndex ForElement(uint32_t index) {
    return PropertyIndex(Kind::Element, index);
  }

  static PropertyIndex ForSlot(NativeObject* obj, uint32_t index) {
    if (index < obj->numFixedSlots()) {
      return PropertyIndex(Kind::FixedSlot, index);
    } else {
      return PropertyIndex(Kind::DynamicSlot, index - obj->numFixedSlots());
    }
  }

  static constexpr uint32_t KindBits = 2;

  static constexpr uint32_t IndexBits = 32 - KindBits;
  static constexpr uint32_t IndexLimit = 1 << IndexBits;
  static constexpr uint32_t IndexMask = (1 << IndexBits) - 1;

  static constexpr uint32_t KindShift = IndexBits;

  static_assert(NativeObject::MAX_FIXED_SLOTS < IndexLimit);
  static_assert(NativeObject::MAX_SLOTS_COUNT < IndexLimit);
  static_assert(NativeObject::MAX_DENSE_ELEMENTS_COUNT < IndexLimit);

 private:
  uint32_t encode(Kind kind, uint32_t index) {
    MOZ_ASSERT(index < IndexLimit);
    return (uint32_t(kind) << KindShift) | index;
  }

 public:
  Kind kind() const { return Kind(asBits_ >> KindShift); }
  uint32_t index() const { return asBits_ & IndexMask; }
};

using PropertyIndexVector = js::Vector<PropertyIndex, 8, js::TempAllocPolicy>;

struct NativeIterator;

class NativeIteratorListNode {
 protected:
  // While in compartment->enumerators, these form a doubly linked list.
  NativeIteratorListNode* prev_ = nullptr;
  NativeIteratorListNode* next_ = nullptr;

 public:
  NativeIteratorListNode* prev() { return prev_; }
  NativeIteratorListNode* next() { return next_; }

  void setPrev(NativeIteratorListNode* prev) { prev_ = prev; }
  void setNext(NativeIteratorListNode* next) { next_ = next; }

  static constexpr size_t offsetOfNext() {
    return offsetof(NativeIteratorListNode, next_);
  }

  static constexpr size_t offsetOfPrev() {
    return offsetof(NativeIteratorListNode, prev_);
  }

 private:
  NativeIterator* asNativeIterator() {
    return reinterpret_cast<NativeIterator*>(this);
  }

  friend class NativeIteratorListIter;
};

class NativeIteratorListHead : public NativeIteratorListNode {
 private:
  // Initialize a |Compartment::enumerators| sentinel.
  NativeIteratorListHead() { prev_ = next_ = this; }
  friend class JS::Compartment;
};

class NativeIteratorListIter {
 private:
  NativeIteratorListHead* head_;
  NativeIteratorListNode* curr_;

 public:
  explicit NativeIteratorListIter(NativeIteratorListHead* head)
      : head_(head), curr_(head->next()) {}

  bool done() const { return curr_ == head_; }

  NativeIterator* next() {
    MOZ_ASSERT(!done());
    NativeIterator* result = curr_->asNativeIterator();
    curr_ = curr_->next();
    return result;
  }
};

class IteratorProperty {
  uintptr_t raw_ = 0;

 public:
  static constexpr uintptr_t DeletedBit = 0x1;

  // The copy constructor is deleted as a safeguard against writing code which
  // would overwrite existing IteratorProperties on NativeIterators. They are
  // intended to be written once and never changed, outside of being moved in
  // GC callbacks.
  IteratorProperty(const IteratorProperty&) = delete;

  IteratorProperty() = default;
  explicit IteratorProperty(JSLinearString* str) : raw_(uintptr_t(str)) {}
  IteratorProperty(JSLinearString* str, bool deleted)
      : raw_(uintptr_t(str) | (deleted ? DeletedBit : 0)) {}

  JSLinearString* asString() const {
    return reinterpret_cast<JSLinearString*>(raw_ & ~DeletedBit);
  }

  bool deleted() const { return raw_ & DeletedBit; }
  void markDeleted() { raw_ |= DeletedBit; }
  void clearDeleted() { raw_ &= ~DeletedBit; }

  void traceString(JSTracer* trc);
} JS_HAZ_GC_POINTER;

struct NativeIterator : public NativeIteratorListNode {
 private:
  // Object being iterated.  Non-null except in NativeIterator sentinels,
  // the empty iterator singleton (for iterating |null| or |undefined|), and
  // inactive iterators.
  GCPtr<JSObject*> objectBeingIterated_ = {};

  // Internal iterator object.
  const GCPtr<JSObject*> iterObj_ = {};
  const GCPtr<Shape*> objShape_ = {};
  uint32_t propertyCount_ = 0;
  uint32_t propertyCursor_;    // initialized by constructor
  uint32_t ownPropertyCount_;  // initialized by constructor
  HashNumber shapesHash_;      // initialized by constructor
  uint16_t protoShapeCount_ = 0;
  uint8_t flags_ = 0;

 public:
  // For cacheable native iterators, whether the iterator is currently
  // active.  Not serialized by XDR.
  struct Flags {
    // This flag is set when all shapes and properties associated with this
    // NativeIterator have been initialized, such that |shapesEnd_|, in
    // addition to being the end of shapes, is also the beginning of
    // properties.
    //
    // This flag is only *not* set when a NativeIterator is in the process
    // of being constructed.  At such time |shapesEnd_| accounts only for
    // shapes that have been initialized -- potentially none of them.
    // Instead, |propertyCursor_| is initialized to the ultimate/actual
    // start of properties and must be used instead of |propertiesBegin()|,
    // which asserts that this flag is present to guard against misuse.
    static constexpr uint32_t Initialized = 0x1;

    // This flag indicates that this NativeIterator is currently being used
    // to enumerate an object's properties and has not yet been closed.
    static constexpr uint32_t Active = 0x2;

    // This flag indicates that the object being enumerated by this
    // |NativeIterator| had a property deleted from it before it was
    // visited, forcing the properties array in this to be mutated to
    // remove it.
    static constexpr uint32_t HasUnvisitedPropertyDeletion = 0x4;

    // Whether this is the shared empty iterator object used for iterating over
    // null/undefined.
    static constexpr uint32_t IsEmptyIteratorSingleton = 0x8;

    // NOTE: the three flags below pertain to iterator indices optimizations.
    // If an object only has own data properties, we can store a list of
    // PropertyIndex that can be used in Ion to more efficiently access those
    // properties in cases like `for (var key in obj) { ...obj[key]... }`.

    // Whether the object supports indices, in the event that they are
    // requested. Note that this is exclusive with IndicesAvailable
    static constexpr uint32_t IndicesSupported = 0x10;

    // Whether space was initially reserved for indices for this iterator.
    static constexpr uint32_t IndicesAllocated = 0x20;

    // Whether indices are actually valid in the reserved area
    static constexpr uint32_t IndicesAvailable = 0x40;

    // If this iterator was created only for own properties
    static constexpr uint32_t OwnPropertiesOnly = 0x80;

    // If any of these bits are set on a |NativeIterator|, it isn't
    // currently reusable.  (An active |NativeIterator| can't be stolen
    // *right now*; a |NativeIterator| that's had its properties mutated
    // can never be reused, because it would give incorrect results.)
    static constexpr uint32_t NotReusable =
        Active | HasUnvisitedPropertyDeletion | OwnPropertiesOnly;
  };

  // We have a full u32 for this, but due to the way we compute the address
  // of indices in the MacroAssembler, we want to have a few extra bits of
  // wiggle room for shifting
  static constexpr uint32_t PropCountLimit = 1 << 30;

  // If it's really important we can increase the size of protoShapeCount_,
  // but increasing it to 32 bits would add another word.
  static constexpr uint32_t ShapeCountLimit = 1 << 16;

 private:
#ifdef DEBUG
  // If true, this iterator may contain indexed properties that came from
  // objects on the prototype chain. This is used by certain debug assertions.
  bool maybeHasIndexedPropertiesFromProto_ = false;
#endif

  // END OF PROPERTIES

  // No further fields appear after here *in NativeIterator*, but this class is
  // always allocated with space tacked on immediately after |this| to store
  // propertyCount_ IteratorProperty values, optionally propertyCount_
  // PropertyIndex values, and protoShapeCount_ GCPtr<Shape*> values.
 public:
  /**
   * Initialize a NativeIterator properly allocated for |props.length()|
   * properties and |numShapes| shapes. If |indices| is non-null, also
   * allocates room for |indices.length()| PropertyIndex values. In this case,
   * |indices.length()| must equal |props.length()|.
   *
   * Despite being a constructor, THIS FUNCTION CAN REPORT ERRORS.  Users
   * MUST set |*hadError = false| on entry and consider |*hadError| on return
   * to mean this function failed.
   */
  NativeIterator(JSContext* cx, Handle<PropertyIteratorObject*> propIter,
                 Handle<JSObject*> objBeingIterated, HandleIdVector props,
                 bool supportsIndices, PropertyIndexVector* indices,
                 uint32_t numShapes, uint32_t ownPropertyCount,
                 bool forObjectKeys, bool* hadError);

  JSObject* objectBeingIterated() const { return objectBeingIterated_; }

  void initObjectBeingIterated(JSObject& obj) {
    MOZ_ASSERT(!objectBeingIterated_);
    objectBeingIterated_.init(&obj);
  }
  void clearObjectBeingIterated() {
    MOZ_ASSERT(objectBeingIterated_);
    objectBeingIterated_ = nullptr;
  }

  const GCPtr<Shape*>& objShape() const { return objShape_; }

  GCPtr<Shape*>* protoShapesBegin(size_t numProperties) const {
    uintptr_t raw = reinterpret_cast<uintptr_t>(this);
    uintptr_t propertiesStart = raw + offsetOfFirstProperty();
    uintptr_t propertiesEnd =
        propertiesStart + numProperties * sizeof(IteratorProperty);
    uintptr_t result = propertiesEnd;
    if (flags_ & Flags::IndicesAllocated) {
      result += numProperties * sizeof(PropertyIndex);
      if constexpr (sizeof(PropertyIndex) != alignof(GCPtr<Shape*>)) {
        result = AlignBytes(result, alignof(GCPtr<Shape*>));
      }
    }
    MOZ_ASSERT(result % alignof(GCPtr<Shape*>) == 0);
    return reinterpret_cast<GCPtr<Shape*>*>(result);
  }

  GCPtr<Shape*>* protoShapesBegin() const {
    return protoShapesBegin(allocatedPropertyCount());
  }

  GCPtr<Shape*>* protoShapesEnd() const {
    return protoShapesBegin() + protoShapeCount_;
  }

  uint32_t protoShapeCount() const { return protoShapeCount_; }

  IteratorProperty* propertiesBegin() const {
    static_assert(
        alignof(GCPtr<Shape*>) >= alignof(IteratorProperty),
        "IteratorPropertys for properties must be able to appear "
        "directly after any GCPtr<Shape*>s after this NativeIterator, "
        "with no padding space required for correct alignment");
    static_assert(
        alignof(NativeIterator) >= alignof(IteratorProperty),
        "IteratorPropertys for properties must be able to appear "
        "directly after this NativeIterator when no GCPtr<Shape*>s are "
        "present, with no padding space required for correct "
        "alignment");

    return reinterpret_cast<IteratorProperty*>(uintptr_t(this) + sizeof(*this));
  }

  IteratorProperty* propertiesEnd() const {
    return propertiesBegin() + propertyCount_;
  }

  IteratorProperty* nextProperty() const {
    return propertiesBegin() + propertyCursor_;
  }

  PropertyIndex* indicesBegin() const {
    // PropertyIndex must be able to be appear directly after the properties
    // array, with no padding required for correct alignment.
    static_assert(alignof(IteratorProperty) >= alignof(PropertyIndex));
    return reinterpret_cast<PropertyIndex*>(propertiesEnd());
  }

  MOZ_ALWAYS_INLINE JS::Value nextIteratedValueAndAdvance() {
    while (propertyCursor_ < propertyCount_) {
      IteratorProperty& prop = *nextProperty();
      incCursor();
      if (prop.deleted()) {
        continue;
      }
      return JS::StringValue(prop.asString());
    }

    return JS::MagicValue(JS_NO_ITER_VALUE);
  }

  void resetPropertyCursorForReuse() {
    MOZ_ASSERT(isInitialized());

    // This function is called unconditionally on IteratorClose, so
    // unvisited properties might have been deleted, so we can't assert
    // this NativeIterator is reusable.  (Should we not bother resetting
    // the cursor in that case?)

    // If properties were marked as deleted, unmark them.
    if (hasUnvisitedPropertyDeletion()) {
      for (IteratorProperty* prop = propertiesBegin(); prop < propertiesEnd();
           prop++) {
        prop->clearDeleted();
      }
      unmarkHasUnvisitedPropertyDeletion();
    }

    // Note: JIT code inlines |propertyCursor_| resetting when an iterator
    //       ends: see |MacroAssembler::iteratorClose|.
    propertyCursor_ = 0;
  }

  bool previousPropertyWas(JS::Handle<JSLinearString*> str) {
    MOZ_ASSERT(isInitialized());
    return propertyCursor_ > 0 &&
           propertiesBegin()[propertyCursor_ - 1].asString() == str;
  }

  size_t numKeys() const { return propertyCount_; }

  size_t ownPropertyCount() const { return ownPropertyCount_; }

  size_t allocatedPropertyCount() const {
    // propertyCursor_ holds the number of allocated properties until
    // the iterator is initialized. This is so we can know the proper layout
    // of the trailing bytes if we trigger a GC inside the constructor.
    if (!isInitialized()) {
      return propertyCursor_;
    }
    return propertyCount_;
  }

  JSObject* iterObj() const { return iterObj_; }

  void incCursor() {
    MOZ_ASSERT(isInitialized());
    propertyCursor_++;
  }

  HashNumber shapesHash() const { return shapesHash_; }

  bool isInitialized() const { return flags_ & Flags::Initialized; }

  size_t allocationSize() const;

#ifdef DEBUG
  void setMaybeHasIndexedPropertiesFromProto() {
    maybeHasIndexedPropertiesFromProto_ = true;
  }
  bool maybeHasIndexedPropertiesFromProto() const {
    return maybeHasIndexedPropertiesFromProto_;
  }
#endif

 private:
  bool indicesAllocated() const { return flags_ & Flags::IndicesAllocated; }

  bool isUnlinked() const { return !prev_ && !next_; }

 public:
  bool indicesAvailable() const { return flags_ & Flags::IndicesAvailable; }

  bool indicesSupported() const { return flags_ & Flags::IndicesSupported; }

  bool ownPropertiesOnly() const { return flags_ & Flags::OwnPropertiesOnly; }

  // Whether this is the shared empty iterator object used for iterating over
  // null/undefined.
  bool isEmptyIteratorSingleton() const {
    // Note: equivalent code is inlined in MacroAssembler::iteratorClose.
    bool res = flags_ & Flags::IsEmptyIteratorSingleton;
    MOZ_ASSERT_IF(
        res, flags_ == (Flags::Initialized | Flags::IsEmptyIteratorSingleton));
    MOZ_ASSERT_IF(res, !objectBeingIterated_);
    MOZ_ASSERT_IF(res, propertyCount_ == 0);
    MOZ_ASSERT_IF(res, protoShapeCount_ == 0);
    MOZ_ASSERT_IF(res, isUnlinked());
    return res;
  }
  void markEmptyIteratorSingleton() {
    flags_ |= Flags::IsEmptyIteratorSingleton;

    // isEmptyIteratorSingleton() has various debug assertions.
    MOZ_ASSERT(isEmptyIteratorSingleton());
  }

  bool isActive() const {
    MOZ_ASSERT(isInitialized());

    return flags_ & Flags::Active;
  }

  void markActive() {
    MOZ_ASSERT(isInitialized());
    MOZ_ASSERT(!isEmptyIteratorSingleton());

    flags_ |= Flags::Active;
  }

  void markInactive() {
    MOZ_ASSERT(isInitialized());
    MOZ_ASSERT(!isEmptyIteratorSingleton());

    flags_ &= ~Flags::Active;
  }

  bool isReusable() const {
    MOZ_ASSERT(isInitialized());

    if (!(flags_ & Flags::Initialized)) {
      return false;
    }
    if (flags_ & Flags::Active) {
      return false;
    }
    return true;
  }

  void markHasUnvisitedPropertyDeletion() {
    MOZ_ASSERT(isInitialized());
    MOZ_ASSERT(!isEmptyIteratorSingleton());

    flags_ |= Flags::HasUnvisitedPropertyDeletion;
  }

  void unmarkHasUnvisitedPropertyDeletion() {
    MOZ_ASSERT(isInitialized());
    MOZ_ASSERT(!isEmptyIteratorSingleton());
    MOZ_ASSERT(hasUnvisitedPropertyDeletion());

    flags_ &= ~Flags::HasUnvisitedPropertyDeletion;
  }

  bool hasUnvisitedPropertyDeletion() const {
    MOZ_ASSERT(isInitialized());

    return flags_ & Flags::HasUnvisitedPropertyDeletion;
  }

  // Indicates the native iterator may walk prototype properties.
  bool mayHavePrototypeProperties() {
    // If we can use indices for this iterator, we know it doesn't have
    // prototype properties, and so we use this as a check for prototype
    // properties.
    return !indicesAvailable() && !indicesSupported();
  }

  void disableIndices() {
    // Clear the IndicesAvailable flag so we won't use the indices on this
    // iterator, and ensure IndicesSupported is cleared as well, so we don't
    // re-request an iterator with indices. However, we leave the
    // IndicesAllocated flag because we need to free them later, and skip them
    // when looking for shapes.
    flags_ &= ~(Flags::IndicesAvailable | Flags::IndicesSupported);
  }

  void link(NativeIteratorListNode* other) {
    MOZ_ASSERT(isInitialized());

    // The shared iterator used for for-in with null/undefined is immutable and
    // shouldn't be linked.
    MOZ_ASSERT(!isEmptyIteratorSingleton());

    // A NativeIterator cannot appear in the enumerator list twice.
    MOZ_ASSERT(isUnlinked());

    setNext(other);
    setPrev(other->prev());

    other->prev()->setNext(this);
    other->setPrev(this);
  }
  void unlink() {
    MOZ_ASSERT(isInitialized());
    MOZ_ASSERT(!isEmptyIteratorSingleton());

    next()->setPrev(prev());
    prev()->setNext(next());
    setNext(nullptr);
    setPrev(nullptr);
  }

  void trace(JSTracer* trc);

  static constexpr size_t offsetOfObjectBeingIterated() {
    return offsetof(NativeIterator, objectBeingIterated_);
  }

  static constexpr size_t offsetOfProtoShapeCount() {
    return offsetof(NativeIterator, protoShapeCount_);
  }

  static constexpr size_t offsetOfPropertyCursor() {
    return offsetof(NativeIterator, propertyCursor_);
  }

  static constexpr size_t offsetOfPropertyCount() {
    return offsetof(NativeIterator, propertyCount_);
  }

  static constexpr size_t offsetOfOwnPropertyCount() {
    return offsetof(NativeIterator, ownPropertyCount_);
  }

  static constexpr size_t offsetOfFlags() {
    return offsetof(NativeIterator, flags_);
  }

  static constexpr size_t offsetOfObjectShape() {
    return offsetof(NativeIterator, objShape_);
  }

  static constexpr size_t offsetOfFirstProperty() {
    // Properties are stored directly after |this|.
    return sizeof(NativeIterator);
  }
};

class PropertyIteratorObject : public NativeObject {
  static const JSClassOps classOps_;

  enum { IteratorSlot, SlotCount };

 public:
  static const JSClass class_;

  NativeIterator* getNativeIterator() const {
    return maybePtrFromReservedSlot<NativeIterator>(IteratorSlot);
  }
  void initNativeIterator(js::NativeIterator* ni) {
    initReservedSlot(IteratorSlot, PrivateValue(ni));
  }

  size_t sizeOfMisc(mozilla::MallocSizeOf mallocSizeOf) const;

  static size_t offsetOfIteratorSlot() {
    return getFixedSlotOffset(IteratorSlot);
  }

 private:
  static void trace(JSTracer* trc, JSObject* obj);
  static void finalize(JS::GCContext* gcx, JSObject* obj);
};

class ArrayIteratorObject : public NativeObject {
 public:
  static const JSClass class_;
};

ArrayIteratorObject* NewArrayIteratorTemplate(JSContext* cx);
ArrayIteratorObject* NewArrayIterator(JSContext* cx);

class StringIteratorObject : public NativeObject {
 public:
  static const JSClass class_;
};

StringIteratorObject* NewStringIteratorTemplate(JSContext* cx);
StringIteratorObject* NewStringIterator(JSContext* cx);

class RegExpStringIteratorObject : public NativeObject {
 public:
  static const JSClass class_;
};

RegExpStringIteratorObject* NewRegExpStringIteratorTemplate(JSContext* cx);
RegExpStringIteratorObject* NewRegExpStringIterator(JSContext* cx);

#ifdef NIGHTLY_BUILD
class IteratorRangeObject : public NativeObject {
 public:
  static const JSClass class_;
};

IteratorRangeObject* NewIteratorRange(JSContext* cx);
#endif

[[nodiscard]] bool EnumerateProperties(JSContext* cx, HandleObject obj,
                                       MutableHandleIdVector props);

PropertyIteratorObject* LookupInIteratorCache(JSContext* cx, HandleObject obj);
PropertyIteratorObject* LookupInShapeIteratorCache(JSContext* cx,
                                                   HandleObject obj);

PropertyIteratorObject* GetIterator(JSContext* cx, HandleObject obj);
PropertyIteratorObject* GetIteratorWithIndices(JSContext* cx, HandleObject obj);

PropertyIteratorObject* GetIteratorForObjectKeys(JSContext* cx,
                                                 HandleObject obj);
PropertyIteratorObject* GetIteratorWithIndicesForObjectKeys(JSContext* cx,
                                                            HandleObject obj);

PropertyIteratorObject* ValueToIterator(JSContext* cx, HandleValue vp);

void CloseIterator(JSObject* obj);

bool IteratorCloseForException(JSContext* cx, HandleObject obj);

void UnwindIteratorForUncatchableException(JSObject* obj);

extern bool SuppressDeletedProperty(JSContext* cx, HandleObject obj, jsid id);

extern bool SuppressDeletedElement(JSContext* cx, HandleObject obj,
                                   uint32_t index);

#ifdef DEBUG
extern void AssertDenseElementsNotIterated(NativeObject* obj);
#else
inline void AssertDenseElementsNotIterated(NativeObject* obj) {}
#endif

/*
 * IteratorMore() returns the next iteration value. If no value is available,
 * MagicValue(JS_NO_ITER_VALUE) is returned.
 */
inline Value IteratorMore(JSObject* iterobj) {
  NativeIterator* ni =
      iterobj->as<PropertyIteratorObject>().getNativeIterator();
  return ni->nextIteratedValueAndAdvance();
}

/*
 * Create an object of the form { value: VALUE, done: DONE }.
 * ES 2017 draft 7.4.7.
 */
extern PlainObject* CreateIterResultObject(JSContext* cx, HandleValue value,
                                           bool done);

/*
 * Global Iterator constructor.
 * Iterator Helpers proposal 2.1.3.
 */
class IteratorObject : public NativeObject {
 public:
  static const JSClass class_;
  static const JSClass protoClass_;

  static bool finishInit(JSContext* cx, HandleObject ctor, HandleObject proto);
};

/*
 * Wrapper for iterators created via Iterator.from.
 * Iterator Helpers proposal 2.1.3.3.1.1.
 */
class WrapForValidIteratorObject : public NativeObject {
 public:
  static const JSClass class_;

  enum { IteratorSlot, NextMethodSlot, SlotCount };

  static_assert(
      IteratorSlot == WRAP_FOR_VALID_ITERATOR_ITERATOR_SLOT,
      "IteratedSlot must match self-hosting define for iterator object slot.");

  static_assert(
      NextMethodSlot == WRAP_FOR_VALID_ITERATOR_NEXT_METHOD_SLOT,
      "NextMethodSlot must match self-hosting define for next method slot.");
};

WrapForValidIteratorObject* NewWrapForValidIterator(JSContext* cx);

/*
 * Generator-esque object returned by Iterator Helper methods.
 */
class IteratorHelperObject : public NativeObject {
 public:
  static const JSClass class_;

  enum {
    // The implementation (an instance of one of the generators in
    // builtin/Iterator.js).
    // Never null.
    GeneratorSlot,

    // The [[UnderlyingIterator]] internal slot. Either an object or undefined
    // in the case of IteratorConcat. In the spec, the internal slot stores an
    // "Iterator Record", but our implementation only stores the actual iterator
    // object.
    UnderlyingIteratorSlot,

    SlotCount,
  };

  static_assert(GeneratorSlot == ITERATOR_HELPER_GENERATOR_SLOT,
                "GeneratorSlot must match self-hosting define for generator "
                "object slot.");

  static_assert(UnderlyingIteratorSlot ==
                    ITERATOR_HELPER_UNDERLYING_ITERATOR_SLOT,
                "UnderlyingIteratorSlot must match self-hosting define for "
                "underlying iterator slot.");
};

IteratorHelperObject* NewIteratorHelper(JSContext* cx);

ArrayObject* IterableToArray(JSContext* cx, HandleValue iterable);

bool HasOptimizableArrayIteratorPrototype(JSContext* cx);

enum class MustBePacked { No, Yes };

template <MustBePacked Packed>
bool IsArrayWithDefaultIterator(JSObject* obj, JSContext* cx);

bool IsMapObjectWithDefaultIterator(JSObject* obj, JSContext* cx);
bool IsSetObjectWithDefaultIterator(JSObject* obj, JSContext* cx);

// Typed arrays and classes with an enumerate hook can have extra properties not
// included in the shape's property map or the object's dense elements.
static inline bool ClassCanHaveExtraEnumeratedProperties(const JSClass* clasp) {
  return IsTypedArrayClass(clasp) || clasp->getNewEnumerate() ||
         clasp->getEnumerate();
}

} /* namespace js */

#endif /* vm_Iteration_h */
