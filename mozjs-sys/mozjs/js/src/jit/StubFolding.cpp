/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "jit/StubFolding.h"

#include "mozilla/Maybe.h"

#include "gc/GC.h"
#include "jit/BaselineCacheIRCompiler.h"
#include "jit/BaselineIC.h"
#include "jit/CacheIR.h"
#include "jit/CacheIRCloner.h"
#include "jit/CacheIRCompiler.h"
#include "jit/CacheIRSpewer.h"
#include "jit/CacheIRWriter.h"
#include "jit/JitScript.h"
#include "jit/ShapeList.h"

#include "vm/List-inl.h"

using namespace js;
using namespace js::jit;

static bool TryFoldingGuardShapes(JSContext* cx, ICFallbackStub* fallback,
                                  JSScript* script, ICScript* icScript,
                                  gc::AutoMarkingLock& lock) {
  // Try folding similar stubs with GuardShapes
  // into GuardMultipleShapes or GuardMultipleShapesToOffset

  ICEntry* icEntry = icScript->icEntryForStub(fallback);
  ICStub* entryStub = icEntry->firstStub();
  ICCacheIRStub* firstStub = entryStub->toCacheIRStub();

  // The caller guarantees that there are at least two stubs.
  MOZ_ASSERT(entryStub != fallback);
  MOZ_ASSERT(!firstStub->next()->isFallback());

  const uint8_t* firstStubData = firstStub->stubDataStart();
  const CacheIRStubInfo* stubInfo = firstStub->stubInfo();

  // Check to see if:
  //   a) all of the stubs in this chain have the exact same code.
  //   b) all of the stubs have the same stub field data, except for a single
  //      GuardShape (and/or consecutive RawInt32) where they differ.
  //   c) at least one stub after the first has a non-zero entry count.
  //   d) All shapes in the GuardShape have the same realm.
  //
  // If all of these conditions hold, then we generate a single stub
  // that covers all the existing cases by
  // 1) replacing GuardShape with GuardMultipleShapes.
  // 2) replacing Load/Store with equivalent LoadToOffset/StoreToOffset

  uint32_t numActive = 0;
  mozilla::Maybe<uint32_t> foldableShapeOffset;
  mozilla::Maybe<uint32_t> foldableOffsetOffset;
  GCVector<Value, 8> shapeList(cx);
  GCVector<Value, 8> offsetList(cx);

  // Helper function: Keep list of different shapes.
  // Can fail on OOM or for cross-realm shapes.
  // Returns true if the shape was successfully added to the list, and false
  // (with no pending exception) otherwise.
  auto addShape = [&shapeList, cx](uintptr_t rawShape) -> bool {
    Shape* shape = reinterpret_cast<Shape*>(rawShape);

    // Only add same realm shapes.
    if (shape->realm() != cx->realm()) {
      return false;
    }

    gc::ReadBarrier(shape);

    if (!shapeList.append(PrivateValue(shape))) {
      cx->recoverFromOutOfMemory();
      return false;
    }
    return true;
  };

  // Helper function: Keep list of "possible" different offsets (slotOffset).
  // At this stage we don't know if they differ. Therefore only keep track
  // of the first offset until we see a different offset and fill list equal to
  // shapeList if that happens.
  auto lazyAddOffset = [&offsetList, &shapeList, cx](uintptr_t slotOffset) {
    Value v = PrivateUint32Value(static_cast<uint32_t>(slotOffset));
    if (offsetList.length() == 1) {
      if (v == offsetList[0]) return true;

      while (offsetList.length() + 1 < shapeList.length()) {
        if (!offsetList.append(offsetList[0])) {
          cx->recoverFromOutOfMemory();
          return false;
        }
      }
    }

    if (!offsetList.append(v)) {
      cx->recoverFromOutOfMemory();
      return false;
    }
    return true;
  };

#ifdef JS_JITSPEW
  JitSpew(JitSpew_StubFolding, "Trying to fold stubs at offset %u @ %s:%u:%u",
          fallback->pcOffset(), script->filename(), script->lineno(),
          script->column().oneOriginValue());

  if (JitSpewEnabled(JitSpew_StubFoldingDetails)) {
    uint32_t i = 0;
    for (ICCacheIRStub* stub = firstStub; stub; stub = stub->nextCacheIR()) {
      JitSpew(JitSpew_StubFoldingDetails, "- stub %d (enteredCount: %d)", i,
              stub->enteredCount());

#  ifdef JS_CACHEIR_SPEW
      AutoJitSpewMessage msg(JitSpew_StubFoldingDetails);
      ICCacheIRStub* cache_stub = stub->toCacheIRStub();
      SpewCacheIROps(msg.printer(), "  ", cache_stub->stubInfo());
#  endif
      i++;
    }
  }
#endif

  // Find the offset of the first Shape that differs.
  // Also see if the next field is RawInt32, which is
  // the case for a Fixed/Dynamic slot if it follows the ShapeGuard.
  for (ICCacheIRStub* other = firstStub->nextCacheIR(); other;
       other = other->nextCacheIR()) {
    // Verify that the stubs share the same code.
    if (other->stubInfo() != stubInfo) {
      return true;
    }

    if (other->enteredCount() > 0) {
      numActive++;
    }

    if (foldableShapeOffset.isSome()) {
      // Already found.
      // Continue through all stubs to run above code.
      continue;
    }

    const uint8_t* otherStubData = other->stubDataStart();
    uint32_t fieldIndex = 0;
    size_t offset = 0;
    while (stubInfo->fieldType(fieldIndex) != StubField::Type::Limit) {
      StubField::Type fieldType = stubInfo->fieldType(fieldIndex);

      // Continue if the fields have same value.
      if (StubField::sizeIsInt64(fieldType)) {
        if (stubInfo->getStubRawInt64(firstStubData, offset) ==
            stubInfo->getStubRawInt64(otherStubData, offset)) {
          offset += StubField::sizeInBytes(fieldType);
          fieldIndex++;
          continue;
        }
      } else {
        MOZ_ASSERT(StubField::sizeIsWord(fieldType));
        if (stubInfo->getStubRawWord(firstStubData, offset) ==
            stubInfo->getStubRawWord(otherStubData, offset)) {
          offset += StubField::sizeInBytes(fieldType);
          fieldIndex++;
          continue;
        }
      }

      // Early abort if it is a non-shape field that differs.
      if (fieldType != StubField::Type::WeakShape) {
        return true;
      }

      // Save the offset
      foldableShapeOffset.emplace(offset);

      // Test if the consecutive field is potentially Load{Fixed|Dynamic}Slot
      offset += StubField::sizeInBytes(fieldType);
      fieldIndex++;
      if (stubInfo->fieldType(fieldIndex) == StubField::Type::RawInt32) {
        foldableOffsetOffset.emplace(offset);
      }

      break;
    }
  }

  if (foldableShapeOffset.isNothing()) {
    return true;
  }

  if (numActive == 0) {
    return true;
  }

  uint32_t totalEnteredCount = 0;

  // Make sure the shape and offset is the only value that differ.
  // Collect the shape and offset values at the same time.
  for (ICCacheIRStub* stub = firstStub; stub; stub = stub->nextCacheIR()) {
    totalEnteredCount += stub->enteredCount();
    const uint8_t* stubData = stub->stubDataStart();
    uint32_t fieldIndex = 0;
    size_t offset = 0;

    while (stubInfo->fieldType(fieldIndex) != StubField::Type::Limit) {
      StubField::Type fieldType = stubInfo->fieldType(fieldIndex);
      if (offset == *foldableShapeOffset) {
        // Save the shapes of all stubs.
        MOZ_ASSERT(fieldType == StubField::Type::WeakShape);
        uintptr_t raw = stubInfo->getStubRawWord(stubData, offset);
        if (!addShape(raw)) {
          return true;
        }
      } else if (foldableOffsetOffset.isSome() &&
                 offset == *foldableOffsetOffset) {
        // Save the offsets of all stubs.
        MOZ_ASSERT(fieldType == StubField::Type::RawInt32);
        uintptr_t raw = stubInfo->getStubRawWord(stubData, offset);
        if (!lazyAddOffset(raw)) {
          return true;
        }
      } else {
        // Check all other fields are the same.
        if (StubField::sizeIsInt64(fieldType)) {
          if (stubInfo->getStubRawInt64(firstStubData, offset) !=
              stubInfo->getStubRawInt64(stubData, offset)) {
            return true;
          }
        } else {
          MOZ_ASSERT(StubField::sizeIsWord(fieldType));
          if (stubInfo->getStubRawWord(firstStubData, offset) !=
              stubInfo->getStubRawWord(stubData, offset)) {
            return true;
          }
        }
      }

      offset += StubField::sizeInBytes(fieldType);
      fieldIndex++;
    }
  }

  // Clone the CacheIR and replace
  // - specific GuardShape with GuardMultipleShapes.
  // or
  // (multiple distinct values in offsetList)
  // - specific GuardShape with GuardMultipleShapesToOffset.
  // - subsequent Load / Store with LoadToOffset / StoreToOffset
  CacheIRWriter writer(cx);
  CacheIRReader reader(stubInfo);
  CacheIRCloner cloner(firstStub);
  bool hasSlotOffsets = offsetList.length() > 1;

  if (JitOptions.disableStubFoldingLoadsAndStores && hasSlotOffsets) {
    return true;
  }

  // Initialize the operands.
  CacheKind cacheKind = stubInfo->kind();
  for (uint32_t i = 0; i < NumInputsForCacheKind(cacheKind); i++) {
    writer.setInputOperandId(i);
  }

  // Create the shapeList to bake in the new stub.
  Rooted<ListObject*> shapeObj(cx);
  {
    gc::AutoSuppressGC suppressGC(cx);

    if (!hasSlotOffsets) {
      shapeObj.set(ShapeListObject::create(cx));
    } else {
      shapeObj.set(ShapeListWithOffsetsObject::create(cx));
    }

    if (!shapeObj) {
      return false;
    }

    MOZ_ASSERT_IF(hasSlotOffsets, shapeList.length() == offsetList.length());

    for (uint32_t i = 0; i < shapeList.length(); i++) {
      if (hasSlotOffsets) {
        if (!shapeObj->append(cx, shapeList[i], offsetList[i])) {
          return false;
        }
      } else {
        if (!shapeObj->append(cx, shapeList[i])) {
          return false;
        }
      }

      MOZ_ASSERT(static_cast<Shape*>(shapeList[i].toPrivate())->realm() ==
                 shapeObj->realm());
    }
  }

  mozilla::Maybe<Int32OperandId> offsetId;
  bool shapeSuccess = false;
  bool offsetSuccess = false;
  while (reader.more()) {
    CacheOp op = reader.readOp();
    switch (op) {
      case CacheOp::GuardShape: {
        auto [objId, shapeOffset] = reader.argsForGuardShape();
        if (shapeOffset != *foldableShapeOffset) {
          // Unrelated GuardShape.
          WeakHeapPtr<Shape*>& ptr =
              stubInfo->getStubField<StubField::Type::WeakShape>(firstStub,
                                                                 shapeOffset);
          writer.guardShape(objId, ptr.unbarrieredGet());
          break;
        }

        if (hasSlotOffsets) {
          offsetId.emplace(writer.guardMultipleShapesToOffset(objId, shapeObj));
        } else {
          writer.guardMultipleShapes(objId, shapeObj);
        }
        if (shapeSuccess) {
          // If a stub contains duplicate GuardShape ops that share a stub field
          // because of stub field deduplication, then we could reach this point
          // more than once. We could technically support this case, but it is
          // rare enough, and hard enough to reason about, that it is simplest
          // to give up here.
          JitSpew(JitSpew_StubFolding,
                  "Shape field at offset %u was used by multiple GuardShapes "
                  "(icScript: %p) with %zu shapes (%s:%u:%u)",
                  fallback->pcOffset(), icScript, shapeList.length(),
                  script->filename(), script->lineno(),
                  script->column().oneOriginValue());
          return true;
        }
        shapeSuccess = true;
        break;
      }
      case CacheOp::LoadFixedSlotResult: {
        auto [objId, offsetOffset] = reader.argsForLoadFixedSlotResult();
        if (!hasSlotOffsets || offsetOffset != *foldableOffsetOffset) {
          // Unrelated LoadFixedSlotResult
          uint32_t offset = stubInfo->getStubRawWord(firstStub, offsetOffset);
          writer.loadFixedSlotResult(objId, offset);
          break;
        }

        MOZ_ASSERT(offsetId.isSome());
        writer.loadFixedSlotFromOffsetResult(objId, offsetId.value());
        offsetSuccess = true;
        break;
      }
      case CacheOp::StoreFixedSlot: {
        auto [objId, offsetOffset, rhsId] = reader.argsForStoreFixedSlot();
        if (!hasSlotOffsets || offsetOffset != *foldableOffsetOffset) {
          // Unrelated StoreFixedSlot
          uint32_t offset = stubInfo->getStubRawWord(firstStub, offsetOffset);
          writer.storeFixedSlot(objId, offset, rhsId);
          break;
        }

        MOZ_ASSERT(offsetId.isSome());
        writer.storeFixedSlotFromOffset(objId, offsetId.value(), rhsId);
        offsetSuccess = true;
        break;
      }
      case CacheOp::StoreDynamicSlot: {
        auto [objId, offsetOffset, rhsId] = reader.argsForStoreDynamicSlot();
        if (!hasSlotOffsets || offsetOffset != *foldableOffsetOffset) {
          // Unrelated StoreDynamicSlot
          uint32_t offset = stubInfo->getStubRawWord(firstStub, offsetOffset);
          writer.storeDynamicSlot(objId, offset, rhsId);
          break;
        }

        MOZ_ASSERT(offsetId.isSome());
        writer.storeDynamicSlotFromOffset(objId, offsetId.value(), rhsId);
        offsetSuccess = true;
        break;
      }
      case CacheOp::LoadDynamicSlotResult: {
        auto [objId, offsetOffset] = reader.argsForLoadDynamicSlotResult();
        if (!hasSlotOffsets || offsetOffset != *foldableOffsetOffset) {
          // Unrelated LoadDynamicSlotResult
          uint32_t offset = stubInfo->getStubRawWord(firstStub, offsetOffset);
          writer.loadDynamicSlotResult(objId, offset);
          break;
        }

        MOZ_ASSERT(offsetId.isSome());
        writer.loadDynamicSlotFromOffsetResult(objId, offsetId.value());
        offsetSuccess = true;
        break;
      }
      default:
        cloner.cloneOp(op, reader, writer);
        break;
    }
  }

  if (!shapeSuccess) {
    // If the shape field that differed was not part of a GuardShape,
    // we can't fold these stubs together.
    JitSpew(JitSpew_StubFolding,
            "Foldable shape field at offset %u was not a GuardShape "
            "(icScript: %p) with %zu shapes (%s:%u:%u)",
            fallback->pcOffset(), icScript, shapeList.length(),
            script->filename(), script->lineno(),
            script->column().oneOriginValue());
    return true;
  }

  if (hasSlotOffsets && !offsetSuccess) {
    // If we found a differing offset field but it was not part of the
    // Load{Fixed | Dynamic}SlotResult then we can't fold these stubs
    // together.
    JitSpew(JitSpew_StubFolding,
            "Failed to fold GuardShape into GuardMultipleShapesToOffset at "
            "offset %u (icScript: %p) with %zu shapes (%s:%u:%u)",
            fallback->pcOffset(), icScript, shapeList.length(),
            script->filename(), script->lineno(),
            script->column().oneOriginValue());
    return true;
  }

  if (writer.tooLarge()) {
    JitSpew(JitSpew_StubFolding,
            "Folded stub at offset %u too large (icScript: %p) with %zu shapes "
            "(%s:%u:%u)",
            fallback->pcOffset(), icScript, shapeList.length(),
            script->filename(), script->lineno(),
            script->column().oneOriginValue());
    cx->runtime()->setUseCounter(cx->global(), JSUseCounter::IC_STUB_TOO_LARGE);
    return true;
  }

  // Replace the existing stubs with the new folded stub.
  fallback->discardStubs(cx->zone(), icEntry);

  ICAttachResult result = AttachBaselineCacheIRStubLocked(
      cx, writer, cacheKind, script, icScript, fallback, "StubFold", lock);
  if (result == ICAttachResult::OOM) {
    ReportOutOfMemory(cx);
    return false;
  }
  MOZ_RELEASE_ASSERT(result == ICAttachResult::Attached);

  // We preserve the total entry count while folding stubs to help guide
  // inlining heuristics.
  icEntry->firstStub()->setEnteredCount(totalEnteredCount);

  JitSpew(JitSpew_StubFolding,
          "Folded stub at offset %u (icScript: %p) with %zu shapes (%s:%u:%u)",
          fallback->pcOffset(), icScript, shapeList.length(),
          script->filename(), script->lineno(),
          script->column().oneOriginValue());

#ifdef JS_JITSPEW
  if (JitSpewEnabled(JitSpew_StubFoldingDetails)) {
    ICStub* newEntryStub = icEntry->firstStub();

    JitSpew(JitSpew_StubFoldingDetails, "- stub 0 (enteredCount: %d)",
            newEntryStub->enteredCount());
#  ifdef JS_CACHEIR_SPEW
    AutoJitSpewMessage msg(JitSpew_StubFoldingDetails);
    ICCacheIRStub* newStub = newEntryStub->toCacheIRStub();
    SpewCacheIROps(msg.printer(), "  ", newStub->stubInfo());
#  endif
  }
#endif

  fallback->setMayHaveFoldedStub();

  return true;
}

bool js::jit::TryFoldingStubs(JSContext* cx, ICFallbackStub* fallback,
                              JSScript* script, ICScript* icScript) {
  gc::AutoMarkingLock lock(cx->zone(), icScript->markingLock());
  return TryFoldingStubsLocked(cx, fallback, script, icScript, lock);
}

bool js::jit::TryFoldingStubsLocked(JSContext* cx, ICFallbackStub* fallback,
                                    JSScript* script, ICScript* icScript,
                                    gc::AutoMarkingLock& lock) {
  ICEntry* icEntry = icScript->icEntryForStub(fallback);
  ICStub* entryStub = icEntry->firstStub();

  if (JitOptions.disableStubFolding) {
    return true;
  }

  // Don't fold unless there are at least two stubs.
  if (entryStub == fallback) {
    return true;
  }

  ICCacheIRStub* firstStub = entryStub->toCacheIRStub();
  if (firstStub->next()->isFallback()) {
    return true;
  }

  if (!TryFoldingGuardShapes(cx, fallback, script, icScript, lock)) {
    return false;
  }

  return true;
}

bool js::jit::AddToFoldedStub(JSContext* cx, const CacheIRWriter& writer,
                              ICScript* icScript, ICFallbackStub* fallback) {
  ICEntry* icEntry = icScript->icEntryForStub(fallback);
  ICStub* entryStub = icEntry->firstStub();

  // We only update folded stubs if they're the only stub in the IC.
  if (entryStub == fallback) {
    return false;
  }
  ICCacheIRStub* stub = entryStub->toCacheIRStub();
  if (!stub->next()->isFallback()) {
    return false;
  }

  const CacheIRStubInfo* stubInfo = stub->stubInfo();
  const uint8_t* stubData = stub->stubDataStart();

  mozilla::Maybe<uint32_t> shapeFieldOffset;
  mozilla::Maybe<uint32_t> offsetFieldOffset;
  RootedValue newShape(cx);
  RootedValue newOffset(cx);
  Rooted<ListObject*> shapeList(cx);

  CacheIRReader stubReader(stubInfo);
  CacheIRReader newReader(writer);
  while (newReader.more() && stubReader.more()) {
    CacheOp newOp = newReader.readOp();
    CacheOp stubOp = stubReader.readOp();
    switch (stubOp) {
      case CacheOp::GuardMultipleShapes: {
        // Check that the new stub has a corresponding GuardShape.
        if (newOp != CacheOp::GuardShape) {
          return false;
        }
        // Check that the object being guarded is the same.
        if (newReader.objOperandId() != stubReader.objOperandId()) {
          return false;
        }

        // Check that the shape offset is the same.
        uint32_t newShapeOffset = newReader.stubOffset();
        uint32_t stubShapesOffset = stubReader.stubOffset();
        if (newShapeOffset != stubShapesOffset) {
          return false;
        }

        MOZ_ASSERT(shapeList == nullptr);
        shapeFieldOffset.emplace(newShapeOffset);

        // Get the shape from the new stub
        StubField shapeField =
            writer.readStubField(newShapeOffset, StubField::Type::WeakShape);
        Shape* shape = reinterpret_cast<Shape*>(shapeField.asWord());
        newShape = PrivateValue(shape);

        // Get the shape array from the old stub.
        JSObject* obj = stubInfo->getStubField<StubField::Type::JSObject>(
            stub, stubShapesOffset);
        shapeList = &obj->as<ShapeListObject>();
        MOZ_ASSERT(shapeList->compartment() == shape->compartment());

        // Don't add a shape if it's from a different realm than the first
        // shape.
        //
        // Since the list was created in the realm which guarded all the shapes
        // added to it, we can use its realm to check and ensure we're not
        // adding a cross-realm shape.
        //
        // The assert verifies this property by checking the first element has
        // the same realm (and since everything in the list has the same realm,
        // checking the first element suffices)
        Realm* shapesRealm = shapeList->realm();
        MOZ_ASSERT_IF(
            !shapeList->isEmpty(),
            shapeList->as<ShapeListObject>().getUnbarriered(0)->realm() ==
                shapesRealm);
        if (shapesRealm != shape->realm()) {
          return false;
        }

        break;
      }
      case CacheOp::GuardMultipleShapesToOffset: {
        // Check that the new stub has a corresponding GuardShape.
        if (newOp != CacheOp::GuardShape) {
          return false;
        }
        // Check that the object being guarded is the same.
        if (newReader.objOperandId() != stubReader.objOperandId()) {
          return false;
        }

        // Check that the shape offset is the same.
        uint32_t newShapeOffset = newReader.stubOffset();
        uint32_t stubShapesOffset = stubReader.stubOffset();
        if (newShapeOffset != stubShapesOffset) {
          return false;
        }

        MOZ_ASSERT(shapeList == nullptr);
        shapeFieldOffset.emplace(newShapeOffset);

        // Get the shape from the new stub
        StubField shapeField =
            writer.readStubField(newShapeOffset, StubField::Type::WeakShape);
        Shape* shape = reinterpret_cast<Shape*>(shapeField.asWord());
        newShape = PrivateValue(shape);

        // Get the shape array from the old stub.
        JSObject* obj = stubInfo->getStubField<StubField::Type::JSObject>(
            stub, stubShapesOffset);
        shapeList = &obj->as<ShapeListWithOffsetsObject>();
        MOZ_ASSERT(shapeList->compartment() == shape->compartment());

        // Don't add a shape if it's from a different realm than the first
        // shape.
        //
        // Since the list was created in the realm which guarded all the shapes
        // added to it, we can use its realm to check and ensure we're not
        // adding a cross-realm shape.
        //
        // The assert verifies this property by checking the first element has
        // the same realm (and since everything in the list has the same realm,
        // checking the first element suffices)
        Realm* shapesRealm = shapeList->realm();
        MOZ_ASSERT_IF(
            !shapeList->isEmpty(),
            shapeList->as<ShapeListWithOffsetsObject>().getShape(0)->realm() ==
                shapesRealm);
        if (shapesRealm != shape->realm()) {
          return false;
        }

        // Consume the offsetId argument.
        stubReader.skip();
        break;
      }
      case CacheOp::LoadFixedSlotFromOffsetResult:
      case CacheOp::LoadDynamicSlotFromOffsetResult: {
        // Check that the new stub has a corresponding
        // Load{Fixed|Dynamic}SlotResult
        if (stubOp == CacheOp::LoadFixedSlotFromOffsetResult &&
            newOp != CacheOp::LoadFixedSlotResult) {
          return false;
        }
        if (stubOp == CacheOp::LoadDynamicSlotFromOffsetResult &&
            newOp != CacheOp::LoadDynamicSlotResult) {
          return false;
        }

        // Verify operand ID.
        if (newReader.objOperandId() != stubReader.objOperandId()) {
          return false;
        }

        MOZ_ASSERT(offsetFieldOffset.isNothing());
        offsetFieldOffset.emplace(newReader.stubOffset());

        // Get the offset from the new stub
        StubField offsetField =
            writer.readStubField(*offsetFieldOffset, StubField::Type::RawInt32);
        newOffset = PrivateUint32Value(offsetField.asWord());

        // Consume the offsetId argument.
        stubReader.skip();
        break;
      }
      case CacheOp::StoreFixedSlotFromOffset:
      case CacheOp::StoreDynamicSlotFromOffset: {
        // Check that the new stub has a corresponding Store{Fixed|Dynamic}Slot
        if (stubOp == CacheOp::StoreFixedSlotFromOffset &&
            newOp != CacheOp::StoreFixedSlot) {
          return false;
        }
        if (stubOp == CacheOp::StoreDynamicSlotFromOffset &&
            newOp != CacheOp::StoreDynamicSlot) {
          return false;
        }

        // Verify operand ID.
        if (newReader.objOperandId() != stubReader.objOperandId()) {
          return false;
        }

        MOZ_ASSERT(offsetFieldOffset.isNothing());
        offsetFieldOffset.emplace(newReader.stubOffset());

        // Get the offset from the new stub
        StubField offsetField =
            writer.readStubField(*offsetFieldOffset, StubField::Type::RawInt32);
        newOffset = PrivateUint32Value(offsetField.asWord());

        // Consume the offsetId argument.
        stubReader.skip();

        // Verify rhs ID.
        if (newReader.valOperandId() != stubReader.valOperandId()) {
          return false;
        }

        MOZ_ASSERT(!stubReader.more());
        MOZ_ASSERT(!newReader.more());
        break;
      }
      default: {
        // Check that the op is the same.
        if (newOp != stubOp) {
          return false;
        }

        // Check that the arguments are the same.
        uint32_t argLength = CacheIROpInfos[size_t(newOp)].argLength;
        for (uint32_t i = 0; i < argLength; i++) {
          if (newReader.readByte() != stubReader.readByte()) {
            return false;
          }
        }
      }
    }
  }
  if (newReader.more() || stubReader.more()) {
    return false;
  }

  if (shapeFieldOffset.isNothing()) {
    // The stub did not contain the GuardMultipleShapes op. This can happen if a
    // folded stub has been discarded by GC sweeping.
    return false;
  }

  if (!writer.stubDataEqualsIgnoringShapeAndOffset(stubData, *shapeFieldOffset,
                                                   offsetFieldOffset)) {
    return false;
  }

  // ShapeListWithSlotsObject uses two spaces per shape.
  uint32_t numShapes = offsetFieldOffset.isNothing() ? shapeList->length()
                                                     : shapeList->length() / 2;

  // Limit the maximum number of shapes we will add before giving up.
  // If we give up, transition the stub.
  size_t maxLength = offsetFieldOffset.isSome()
                         ? ShapeListWithOffsetsObject::MaxLength
                         : ShapeListObject::MaxLength;
  if (numShapes == maxLength) {
    MOZ_ASSERT(fallback->state().mode() != ICState::Mode::Generic);
    fallback->state().forceTransition();
    fallback->discardStubs(cx->zone(), icEntry);
    return false;
  }

  if (offsetFieldOffset.isSome()) {
    if (!shapeList->append(cx, newShape, newOffset)) {
      cx->recoverFromOutOfMemory();
      return false;
    }
  } else {
    if (!shapeList->append(cx, newShape)) {
      cx->recoverFromOutOfMemory();
      return false;
    }
  }

  JitSpew(JitSpew_StubFolding, "ShapeList%sObject %p: new length: %u",
          offsetFieldOffset.isNothing() ? "" : "WithOffset", shapeList.get(),
          shapeList->length());
  return true;
}
