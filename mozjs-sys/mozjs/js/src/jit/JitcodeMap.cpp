/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "jit/JitcodeMap.h"

#include "mozilla/ArrayUtils.h"
#include "mozilla/Maybe.h"

#include "gc/Marking.h"
#include "gc/Zone.h"
#include "jit/BaselineJIT.h"
#include "jit/InlineScriptTree.h"
#include "jit/JitRuntime.h"
#include "jit/JitSpewer.h"
#include "js/JitCodeAPI.h"
#include "js/ProfilingFrameIterator.h"
#include "js/Vector.h"
#include "vm/BytecodeLocation.h"  // for BytecodeLocation
#include "vm/GeckoProfiler.h"

#include "vm/GeckoProfiler-inl.h"
#include "vm/JSScript-inl.h"

using mozilla::Maybe;

namespace js {
namespace jit {

JitcodeGlobalEntry::JitcodeGlobalEntry(Kind kind, JitCode* code,
                                       void* nativeStartAddr,
                                       void* nativeEndAddr)
    : JitCodeRange(nativeStartAddr, nativeEndAddr),
      jitcode_(code),
      zone_(code->zone()),
      kind_(kind) {
  MOZ_ASSERT(code);
  MOZ_ASSERT(nativeStartAddr);
  MOZ_ASSERT(nativeEndAddr);
}

static void GetLineInfoFromJitCodeRecord(uint64_t addr, uint32_t* line,
                                         uint32_t* column) {
  JS::JitCodeRecord* record = JS::LookupJitCodeRecord(addr);
  if (!record || record->sourceInfo.empty()) {
    *line = 0;
    *column = 0;
    return;
  }

  // Calculate offset from the base address
  uint32_t codeOffset = addr - record->code_addr;

  // Binary search for the largest offset <= codeOffset
  // We know for sure that sourceInfo is sorted by offset.
  auto* it = std::upper_bound(
      record->sourceInfo.begin(), record->sourceInfo.end(), codeOffset,
      [](uint32_t offset, const JS::JitCodeSourceInfo& info) {
        return offset < info.offset;
      });

  // Upper_bound returns first element > codeOffset, so go back one.
  if (it != record->sourceInfo.begin()) {
    --it;
  }

  *line = it->lineno;
  *column = it->colno.oneOriginValue();
}

static inline JitcodeRegionEntry RegionAtAddr(const IonEntry& entry, void* ptr,
                                              uint32_t* ptrOffset) {
  MOZ_ASSERT(entry.containsPointer(ptr));
  *ptrOffset = reinterpret_cast<uint8_t*>(ptr) -
               reinterpret_cast<uint8_t*>(entry.nativeStartAddr());

  uint32_t regionIdx = entry.regionTable()->findRegionEntry(*ptrOffset);
  MOZ_ASSERT(regionIdx < entry.regionTable()->numRegions());

  return entry.regionTable()->regionEntry(regionIdx);
}

void* IonEntry::canonicalNativeAddrFor(void* ptr) const {
  uint32_t ptrOffset;
  JitcodeRegionEntry region = RegionAtAddr(*this, ptr, &ptrOffset);
  return (void*)(((uint8_t*)nativeStartAddr()) + region.nativeOffset());
}

uint32_t IonEntry::callStackAtAddr(void* ptr, CallStackFrameInfo* results,
                                   uint32_t maxResults) const {
  MOZ_ASSERT(maxResults >= 1);

  uint32_t ptrOffset;
  JitcodeRegionEntry region = RegionAtAddr(*this, ptr, &ptrOffset);

  JitcodeRegionEntry::ScriptPcIterator locationIter = region.scriptPcIterator();
  MOZ_ASSERT(locationIter.hasMore());
  uint32_t count = 0;
  while (locationIter.hasMore()) {
    uint32_t scriptIdx, pcOffset;

    locationIter.readNext(&scriptIdx, &pcOffset);
    MOZ_ASSERT(getStr(scriptIdx));

    results[count].label = getStr(scriptIdx);
    results[count].sourceId = getScriptKey(scriptIdx).scriptSource->id();

    // Calculate line numbers during sampling
    // For the first entry (innermost frame), use precise PC offset from
    // delta-run
    if (count == 0) {
      pcOffset = region.findPcOffset(ptrOffset, pcOffset);
    }

    const IonScriptData& scriptData = getScriptData(scriptIdx);
    ImmutableScriptData* isd = scriptData.scriptKey.sharedData->get();
    jsbytecode* code = isd->code();
    jsbytecode* pc = code + pcOffset;
    MOZ_ASSERT(pcOffset < isd->codeLength());

    SrcNote* notes = isd->notes();
    SrcNote* notesEnd = notes + isd->noteLength();

    JS::LimitedColumnNumberOneOrigin col;
    uint32_t line = PCToLineNumber(scriptData.lineno, scriptData.column, notes,
                                   notesEnd, code, pc, &col);
    results[count].line = line;
    results[count].column = col.oneOriginValue();

    count++;
    if (count >= maxResults) {
      break;
    }
  }

  return count;
}

IonEntry::~IonEntry() {
  // The region table is stored at the tail of the compacted data,
  // which means the start of the region table is a pointer to
  // the _middle_ of the memory space allocated for it.
  //
  // When freeing it, obtain the payload start pointer first.
  MOZ_ASSERT(regionTable_);
  js_free((void*)(regionTable_->payloadStart()));
  regionTable_ = nullptr;
}

void* IonICEntry::canonicalNativeAddrFor(void* ptr) const { return ptr; }

uint32_t IonICEntry::callStackAtAddr(void* ptr, CallStackFrameInfo* results,
                                     uint32_t maxResults) const {
  return ionEntry().callStackAtAddr(rejoinAddr(), results, maxResults);
}

uint64_t IonICEntry::realmID() const { return ionEntry().realmID(); }

void* BaselineEntry::canonicalNativeAddrFor(void* ptr) const {
  // TODO: We can't yet normalize Baseline addresses until we unify
  // BaselineScript's PCMappingEntries with JitcodeGlobalTable.
  return ptr;
}

uint32_t BaselineEntry::callStackAtAddr(void* ptr, CallStackFrameInfo* results,
                                        uint32_t maxResults) const {
  MOZ_ASSERT(containsPointer(ptr));
  MOZ_ASSERT(maxResults >= 1);

  results[0].label = str();
  results[0].sourceId = scriptKey().scriptSource->id();
  uint64_t addr = reinterpret_cast<uint64_t>(ptr);

  GetLineInfoFromJitCodeRecord(addr, &results[0].line, &results[0].column);

  return 1;
}

void* BaselineInterpreterEntry::canonicalNativeAddrFor(void* ptr) const {
  return ptr;
}

uint32_t BaselineInterpreterEntry::callStackAtAddr(void* ptr,
                                                   CallStackFrameInfo* results,
                                                   uint32_t maxResults) const {
  MOZ_CRASH("shouldn't be called for BaselineInterpreter entries");
}

uint64_t BaselineInterpreterEntry::realmID() const {
  MOZ_CRASH("shouldn't be called for BaselineInterpreter entries");
}

void* RealmIndependentSharedEntry::canonicalNativeAddrFor(void* ptr) const {
  // TODO: We can't yet normalize Baseline addresses until we unify
  // BaselineScript's PCMappingEntries with JitcodeGlobalTable.
  return ptr;
}

bool RealmIndependentSharedEntry::callStackAtAddr(
    void* ptr, BytecodeLocationVector& results, uint32_t* depth) const {
  JitSpew(JitSpew_Profiling,
          "Unexpected call - without a script, what can we do here?");
  return true;
}

uint32_t RealmIndependentSharedEntry::callStackAtAddr(
    void* ptr, CallStackFrameInfo* results, uint32_t maxResults) const {
  MOZ_ASSERT(containsPointer(ptr));
  MOZ_ASSERT(maxResults >= 1);

  results[0].label = str();
  results[0].sourceId = 0;
  results[0].line = 0;
  results[0].column = 0;
  return 1;
}

uint64_t RealmIndependentSharedEntry::realmID() const { return 0; }

const JitcodeGlobalEntry* JitcodeGlobalTable::lookupForSampler(
    void* ptr, uint64_t samplePosInBuffer) {
  JitcodeGlobalEntry* entry = lookupInternal(ptr);
  if (!entry) {
    return nullptr;
  }

  entry->setSamplePositionInBuffer(samplePosInBuffer);

  // IonIC entries must keep their corresponding Ion entries alive.
  if (entry->isIonIC()) {
    entry->asIonIC().ionEntry().setSamplePositionInBuffer(samplePosInBuffer);
  }

  // The jitcode_ edge is weak, but no read barrier is needed here because the
  // profiler never lets GC thing pointers escape: it only uses the entry to
  // resolve addresses to call-stack info that has already been baked in at
  // entry-creation time (see callStackAtAddr and realmID).

  return entry;
}

JitcodeGlobalEntry* JitcodeGlobalTable::lookupInternal(void* ptr) {
  // Search for an entry containing the one-byte range starting at |ptr|.
  JitCodeRange range(ptr, static_cast<uint8_t*>(ptr) + 1);

  if (JitCodeRange** entry = tree_.maybeLookup(&range)) {
    MOZ_ASSERT((*entry)->containsPointer(ptr));
    return static_cast<JitcodeGlobalEntry*>(*entry);
  }

  return nullptr;
}

bool JitcodeGlobalTable::addEntry(UniqueJitcodeGlobalEntry entry) {
  MOZ_ASSERT(entry->isIon() || entry->isIonIC() || entry->isBaseline() ||
             entry->isBaselineInterpreter() || entry->isDummy() ||
             entry->isRealmIndependentShared());

  // Suppress profiler sampling while data structures are being mutated.
  AutoSuppressProfilerSampling suppressSampling(TlsContext.get());

  // Old entries whose JitCode was collected but were kept alive for profiler
  // buffer processing may still occupy overlapping address ranges. Remove them
  // from the AVL tree so the new entry can take their place. They are left in
  // entries_ and will be swept from the vector by traceWeak at the next GC.
  // There may be multiple (e.g. several small IonIC stubs whose memory was
  // reused by a larger JitCode).
  while (JitCodeRange** existing = tree_.maybeLookup(entry.get())) {
    auto* oldEntry = static_cast<JitcodeGlobalEntry*>(*existing);
    MOZ_ASSERT(!oldEntry->hasJitcode());
    tree_.remove(oldEntry);
    oldEntry->setInTree(false);
  }

  if (!entries_.append(std::move(entry))) {
    return false;
  }
  if (!tree_.insert(entries_.back().get())) {
    entries_.popBack();
    return false;
  }
  entries_.back()->setInTree(true);

  return true;
}

void JitcodeGlobalTable::setAllEntriesAsExpired() {
  AutoSuppressProfilerSampling suppressSampling(TlsContext.get());
  for (EntryVector::Range r(entries_.all()); !r.empty(); r.popFront()) {
    auto& entry = r.front();
    entry->setAsExpired();
  }
}

void JitcodeGlobalTable::traceWeak(JSRuntime* rt, JSTracer* trc) {
  AutoSuppressProfilerSampling suppressSampling(rt->mainContextFromOwnThread());

  // If the profiler is off, rangeStart will be Nothing() and all entries are
  // considered to be expired.
  Maybe<uint64_t> rangeStart = rt->profilerSampleBufferRangeStart();

  entries_.eraseIf([&](auto& entry) {
    // Expire entries no longer referenced from the profiler buffer.
    if (!entry->isReferencedByProfiler(rangeStart)) {
      entry->setAsExpired();
    }

    // Entries whose JitCode was collected in a previous cycle are kept alive
    // only while still referenced from the profiler buffer. We skip the zone
    // check here because the zone may have been destroyed after the JitCode
    // was collected. The entry may have already been removed from the tree by
    // addEntry if a new JitCode was compiled at the same address.
    if (!entry->hasJitcode()) {
      if (entry->isReferencedByProfiler(rangeStart)) {
        return false;
      }
      if (entry->isInTree()) {
        tree_.remove(entry.get());
      }
      return true;
    }

    if (!entry->zone()->isCollecting() || entry->zone()->isGCFinished()) {
      return false;
    }

    if (TraceManuallyBarrieredWeakEdge(
            trc, entry->jitcodePtr(),
            "JitcodeGlobalTable::JitcodeGlobalEntry::jitcode_")) {
      return false;
    }

    // JitCode is being collected. If there are still unprocessed samples
    // in the profiler buffer referencing this entry, keep the entry alive
    // (including in the AVL tree) so that the profiler can still resolve
    // those addresses via callStackAtAddr during streaming. The entry no
    // longer needs the JitCode pointer since all the information it
    // provides (call stack strings, realmId) is already baked in.
    if (entry->isReferencedByProfiler(rangeStart)) {
      *entry->jitcodePtr() = nullptr;
      return false;
    }

    tree_.remove(entry.get());
    return true;
  });

  MOZ_ASSERT_IF(entries_.empty(), tree_.empty());
}

uint32_t JitcodeGlobalEntry::callStackAtAddr(JSRuntime* rt, void* ptr,
                                             CallStackFrameInfo* results,
                                             uint32_t maxResults) const {
  switch (kind()) {
    case Kind::Ion:
      return asIon().callStackAtAddr(ptr, results, maxResults);
    case Kind::IonIC:
      return asIonIC().callStackAtAddr(ptr, results, maxResults);
    case Kind::Baseline:
      return asBaseline().callStackAtAddr(ptr, results, maxResults);
    case Kind::BaselineInterpreter:
      return asBaselineInterpreter().callStackAtAddr(ptr, results, maxResults);
    case Kind::Dummy:
      return asDummy().callStackAtAddr(rt, ptr, results, maxResults);
    case Kind::RealmIndependentShared:
      return asRealmIndependentShared().callStackAtAddr(ptr, results,
                                                        maxResults);
  }
  MOZ_CRASH("Invalid kind");
}

uint64_t JitcodeGlobalEntry::realmID(JSRuntime* rt) const {
  switch (kind()) {
    case Kind::Ion:
      return asIon().realmID();
    case Kind::IonIC:
      return asIonIC().realmID();
    case Kind::Baseline:
      return asBaseline().realmID();
    case Kind::Dummy:
      return asDummy().realmID();
    case Kind::RealmIndependentShared:
      return asRealmIndependentShared().realmID();
    case Kind::BaselineInterpreter:
      break;
  }
  MOZ_CRASH("Invalid kind");
}

void* JitcodeGlobalEntry::canonicalNativeAddrFor(JSRuntime* rt,
                                                 void* ptr) const {
  switch (kind()) {
    case Kind::Ion:
      return asIon().canonicalNativeAddrFor(ptr);
    case Kind::IonIC:
      return asIonIC().canonicalNativeAddrFor(ptr);
    case Kind::Baseline:
      return asBaseline().canonicalNativeAddrFor(ptr);
    case Kind::Dummy:
      return asDummy().canonicalNativeAddrFor(rt, ptr);
    case Kind::RealmIndependentShared:
      return asRealmIndependentShared().canonicalNativeAddrFor(ptr);
    case Kind::BaselineInterpreter:
      break;
  }
  MOZ_CRASH("Invalid kind");
}

// static
void JitcodeGlobalEntry::DestroyPolicy::operator()(JitcodeGlobalEntry* entry) {
  switch (entry->kind()) {
    case JitcodeGlobalEntry::Kind::Ion:
      js_delete(&entry->asIon());
      break;
    case JitcodeGlobalEntry::Kind::IonIC:
      js_delete(&entry->asIonIC());
      break;
    case JitcodeGlobalEntry::Kind::Baseline:
      js_delete(&entry->asBaseline());
      break;
    case JitcodeGlobalEntry::Kind::BaselineInterpreter:
      js_delete(&entry->asBaselineInterpreter());
      break;
    case JitcodeGlobalEntry::Kind::Dummy:
      js_delete(&entry->asDummy());
      break;
    case JitcodeGlobalEntry::Kind::RealmIndependentShared:
      js_delete(&entry->asRealmIndependentShared());
      break;
  }
}

/* static */
void JitcodeRegionEntry::WriteHead(CompactBufferWriter& writer,
                                   uint32_t nativeOffset, uint8_t scriptDepth) {
  writer.writeUnsigned(nativeOffset);
  writer.writeByte(scriptDepth);
}

/* static */
void JitcodeRegionEntry::ReadHead(CompactBufferReader& reader,
                                  uint32_t* nativeOffset,
                                  uint8_t* scriptDepth) {
  *nativeOffset = reader.readUnsigned();
  *scriptDepth = reader.readByte();
}

/* static */
void JitcodeRegionEntry::WriteScriptPc(CompactBufferWriter& writer,
                                       uint32_t scriptIdx, uint32_t pcOffset) {
  writer.writeUnsigned(scriptIdx);
  writer.writeUnsigned(pcOffset);
}

/* static */
void JitcodeRegionEntry::ReadScriptPc(CompactBufferReader& reader,
                                      uint32_t* scriptIdx, uint32_t* pcOffset) {
  *scriptIdx = reader.readUnsigned();
  *pcOffset = reader.readUnsigned();
}

/* static */
void JitcodeRegionEntry::WriteDelta(CompactBufferWriter& writer,
                                    uint32_t nativeDelta, int32_t pcDelta) {
  if (pcDelta >= 0) {
    // 1 and 2-byte formats possible.

    //  NNNN-BBB0
    if (pcDelta <= ENC1_PC_DELTA_MAX && nativeDelta <= ENC1_NATIVE_DELTA_MAX) {
      uint8_t encVal = ENC1_MASK_VAL | (pcDelta << ENC1_PC_DELTA_SHIFT) |
                       (nativeDelta << ENC1_NATIVE_DELTA_SHIFT);
      writer.writeByte(encVal);
      return;
    }

    //  NNNN-NNNN BBBB-BB01
    if (pcDelta <= ENC2_PC_DELTA_MAX && nativeDelta <= ENC2_NATIVE_DELTA_MAX) {
      uint16_t encVal = ENC2_MASK_VAL | (pcDelta << ENC2_PC_DELTA_SHIFT) |
                        (nativeDelta << ENC2_NATIVE_DELTA_SHIFT);
      writer.writeByte(encVal & 0xff);
      writer.writeByte((encVal >> 8) & 0xff);
      return;
    }
  }

  //  NNNN-NNNN NNNB-BBBB BBBB-B011
  if (pcDelta >= ENC3_PC_DELTA_MIN && pcDelta <= ENC3_PC_DELTA_MAX &&
      nativeDelta <= ENC3_NATIVE_DELTA_MAX) {
    uint32_t encVal =
        ENC3_MASK_VAL |
        ((uint32_t(pcDelta) << ENC3_PC_DELTA_SHIFT) & ENC3_PC_DELTA_MASK) |
        (nativeDelta << ENC3_NATIVE_DELTA_SHIFT);
    writer.writeByte(encVal & 0xff);
    writer.writeByte((encVal >> 8) & 0xff);
    writer.writeByte((encVal >> 16) & 0xff);
    return;
  }

  //  NNNN-NNNN NNNN-NNNN BBBB-BBBB BBBB-B111
  if (pcDelta >= ENC4_PC_DELTA_MIN && pcDelta <= ENC4_PC_DELTA_MAX &&
      nativeDelta <= ENC4_NATIVE_DELTA_MAX) {
    uint32_t encVal =
        ENC4_MASK_VAL |
        ((uint32_t(pcDelta) << ENC4_PC_DELTA_SHIFT) & ENC4_PC_DELTA_MASK) |
        (nativeDelta << ENC4_NATIVE_DELTA_SHIFT);
    writer.writeByte(encVal & 0xff);
    writer.writeByte((encVal >> 8) & 0xff);
    writer.writeByte((encVal >> 16) & 0xff);
    writer.writeByte((encVal >> 24) & 0xff);
    return;
  }

  // Should never get here.
  MOZ_CRASH("pcDelta/nativeDelta values are too large to encode.");
}

/* static */
void JitcodeRegionEntry::ReadDelta(CompactBufferReader& reader,
                                   uint32_t* nativeDelta, int32_t* pcDelta) {
  // NB:
  // It's possible to get nativeDeltas with value 0 in two cases:
  //
  // 1. The last region's run.  This is because the region table's start
  // must be 4-byte aligned, and we must insert padding bytes to align the
  // payload section before emitting the table.
  //
  // 2. A zero-offset nativeDelta with a negative pcDelta.
  //
  // So if nativeDelta is zero, then pcDelta must be <= 0.

  //  NNNN-BBB0
  const uint32_t firstByte = reader.readByte();
  if ((firstByte & ENC1_MASK) == ENC1_MASK_VAL) {
    uint32_t encVal = firstByte;
    *nativeDelta = encVal >> ENC1_NATIVE_DELTA_SHIFT;
    *pcDelta = (encVal & ENC1_PC_DELTA_MASK) >> ENC1_PC_DELTA_SHIFT;
    MOZ_ASSERT_IF(*nativeDelta == 0, *pcDelta <= 0);
    return;
  }

  //  NNNN-NNNN BBBB-BB01
  const uint32_t secondByte = reader.readByte();
  if ((firstByte & ENC2_MASK) == ENC2_MASK_VAL) {
    uint32_t encVal = firstByte | secondByte << 8;
    *nativeDelta = encVal >> ENC2_NATIVE_DELTA_SHIFT;
    *pcDelta = (encVal & ENC2_PC_DELTA_MASK) >> ENC2_PC_DELTA_SHIFT;
    MOZ_ASSERT(*pcDelta != 0);
    MOZ_ASSERT_IF(*nativeDelta == 0, *pcDelta <= 0);
    return;
  }

  //  NNNN-NNNN NNNB-BBBB BBBB-B011
  const uint32_t thirdByte = reader.readByte();
  if ((firstByte & ENC3_MASK) == ENC3_MASK_VAL) {
    uint32_t encVal = firstByte | secondByte << 8 | thirdByte << 16;
    *nativeDelta = encVal >> ENC3_NATIVE_DELTA_SHIFT;

    uint32_t pcDeltaU = (encVal & ENC3_PC_DELTA_MASK) >> ENC3_PC_DELTA_SHIFT;
    // Fix sign if necessary.
    if (pcDeltaU > static_cast<uint32_t>(ENC3_PC_DELTA_MAX)) {
      pcDeltaU |= ~ENC3_PC_DELTA_MAX;
    }
    *pcDelta = pcDeltaU;
    MOZ_ASSERT(*pcDelta != 0);
    MOZ_ASSERT_IF(*nativeDelta == 0, *pcDelta <= 0);
    return;
  }

  //  NNNN-NNNN NNNN-NNNN BBBB-BBBB BBBB-B111
  MOZ_ASSERT((firstByte & ENC4_MASK) == ENC4_MASK_VAL);
  const uint32_t fourthByte = reader.readByte();
  uint32_t encVal =
      firstByte | secondByte << 8 | thirdByte << 16 | fourthByte << 24;
  *nativeDelta = encVal >> ENC4_NATIVE_DELTA_SHIFT;

  uint32_t pcDeltaU = (encVal & ENC4_PC_DELTA_MASK) >> ENC4_PC_DELTA_SHIFT;
  // fix sign if necessary
  if (pcDeltaU > static_cast<uint32_t>(ENC4_PC_DELTA_MAX)) {
    pcDeltaU |= ~ENC4_PC_DELTA_MAX;
  }
  *pcDelta = pcDeltaU;

  MOZ_ASSERT(*pcDelta != 0);
  MOZ_ASSERT_IF(*nativeDelta == 0, *pcDelta <= 0);
}

/* static */
uint32_t JitcodeRegionEntry::ExpectedRunLength(const NativeToBytecode* entry,
                                               const NativeToBytecode* end) {
  MOZ_ASSERT(entry < end);

  // We always use the first entry, so runLength starts at 1
  uint32_t runLength = 1;

  uint32_t curNativeOffset = entry->nativeOffset.offset();
  uint32_t curBytecodeOffset = entry->tree->script()->pcToOffset(entry->pc);

  for (auto nextEntry = entry + 1; nextEntry != end; nextEntry += 1) {
    // If the next run moves to a different inline site, stop the run.
    if (nextEntry->tree != entry->tree) {
      break;
    }

    uint32_t nextNativeOffset = nextEntry->nativeOffset.offset();
    uint32_t nextBytecodeOffset =
        nextEntry->tree->script()->pcToOffset(nextEntry->pc);
    MOZ_ASSERT(nextNativeOffset >= curNativeOffset);

    uint32_t nativeDelta = nextNativeOffset - curNativeOffset;
    int32_t bytecodeDelta =
        int32_t(nextBytecodeOffset) - int32_t(curBytecodeOffset);

    // If deltas are too large (very unlikely), stop the run.
    if (!IsDeltaEncodeable(nativeDelta, bytecodeDelta)) {
      break;
    }

    runLength++;

    // If the run has grown to its maximum length, stop the run.
    if (runLength == MAX_RUN_LENGTH) {
      break;
    }

    curNativeOffset = nextNativeOffset;
    curBytecodeOffset = nextBytecodeOffset;
  }

  return runLength;
}

struct JitcodeMapBufferWriteSpewer {
#ifdef JS_JITSPEW
  CompactBufferWriter* writer;
  uint32_t startPos;

  static const uint32_t DumpMaxBytes = 50;

  explicit JitcodeMapBufferWriteSpewer(CompactBufferWriter& w)
      : writer(&w), startPos(writer->length()) {}

  void spewAndAdvance(const char* name) {
    if (writer->oom()) {
      return;
    }

    uint32_t curPos = writer->length();
    const uint8_t* start = writer->buffer() + startPos;
    const uint8_t* end = writer->buffer() + curPos;
    const char* MAP = "0123456789ABCDEF";
    uint32_t bytes = end - start;

    char buffer[DumpMaxBytes * 3];
    for (uint32_t i = 0; i < bytes; i++) {
      buffer[i * 3] = MAP[(start[i] >> 4) & 0xf];
      buffer[i * 3 + 1] = MAP[(start[i] >> 0) & 0xf];
      buffer[i * 3 + 2] = ' ';
    }
    if (bytes >= DumpMaxBytes) {
      buffer[DumpMaxBytes * 3 - 1] = '\0';
    } else {
      buffer[bytes * 3 - 1] = '\0';
    }

    JitSpew(JitSpew_Profiling, "%s@%d[%d bytes] - %s", name, int(startPos),
            int(bytes), buffer);

    // Move to the end of the current buffer.
    startPos = writer->length();
  }
#else   // !JS_JITSPEW
  explicit JitcodeMapBufferWriteSpewer(CompactBufferWriter& w) {}
  void spewAndAdvance(const char* name) {}
#endif  // JS_JITSPEW
};

// Write a run, starting at the given NativeToBytecode entry, into the given
// buffer writer.
/* static */
bool JitcodeRegionEntry::WriteRun(CompactBufferWriter& writer,
                                  const IonEntry::ScriptList& scriptList,
                                  uint32_t runLength,
                                  const NativeToBytecode* entry) {
  MOZ_ASSERT(runLength > 0);
  MOZ_ASSERT(runLength <= MAX_RUN_LENGTH);

  // Calculate script depth.
  MOZ_ASSERT(entry->tree->depth() <= 0xff);
  uint8_t scriptDepth = entry->tree->depth();
  uint32_t regionNativeOffset = entry->nativeOffset.offset();

  JitcodeMapBufferWriteSpewer spewer(writer);

  // Write the head info.
  JitSpew(JitSpew_Profiling, "    Head Info: nativeOffset=%d scriptDepth=%d",
          int(regionNativeOffset), int(scriptDepth));
  WriteHead(writer, regionNativeOffset, scriptDepth);
  spewer.spewAndAdvance("      ");

  // Write each script/pc pair.
  {
    InlineScriptTree* curTree = entry->tree;
    jsbytecode* curPc = entry->pc;
    for (uint8_t i = 0; i < scriptDepth; i++) {
      // Find the index of the script within the list.
      // NB: scriptList is guaranteed to contain curTree->script()
      uint32_t scriptIdx = 0;
      for (; scriptIdx < scriptList.length(); scriptIdx++) {
        if (scriptList[scriptIdx].scriptData.scriptKey.matches(
                curTree->script())) {
          break;
        }
      }
      MOZ_ASSERT(scriptIdx < scriptList.length());

      uint32_t pcOffset = curTree->script()->pcToOffset(curPc);

      JitSpew(JitSpew_Profiling, "    Script/PC %d: scriptIdx=%d pcOffset=%d",
              int(i), int(scriptIdx), int(pcOffset));
      WriteScriptPc(writer, scriptIdx, pcOffset);
      spewer.spewAndAdvance("      ");

      MOZ_ASSERT_IF(i < scriptDepth - 1, curTree->hasCaller());
      curPc = curTree->callerPc();
      curTree = curTree->caller();
    }
  }

  // Start writing runs.
  uint32_t curNativeOffset = entry->nativeOffset.offset();
  uint32_t curBytecodeOffset = entry->tree->script()->pcToOffset(entry->pc);

  JitSpew(JitSpew_Profiling,
          "  Writing Delta Run from nativeOffset=%d bytecodeOffset=%d",
          int(curNativeOffset), int(curBytecodeOffset));

  // Skip first entry because it is implicit in the header.  Start at subsequent
  // entry.
  for (uint32_t i = 1; i < runLength; i++) {
    MOZ_ASSERT(entry[i].tree == entry->tree);

    uint32_t nextNativeOffset = entry[i].nativeOffset.offset();
    uint32_t nextBytecodeOffset =
        entry[i].tree->script()->pcToOffset(entry[i].pc);
    MOZ_ASSERT(nextNativeOffset >= curNativeOffset);

    uint32_t nativeDelta = nextNativeOffset - curNativeOffset;
    int32_t bytecodeDelta =
        int32_t(nextBytecodeOffset) - int32_t(curBytecodeOffset);
    MOZ_ASSERT(IsDeltaEncodeable(nativeDelta, bytecodeDelta));

    JitSpew(JitSpew_Profiling,
            "    RunEntry native: %d-%d [%d]  bytecode: %d-%d [%d]",
            int(curNativeOffset), int(nextNativeOffset), int(nativeDelta),
            int(curBytecodeOffset), int(nextBytecodeOffset),
            int(bytecodeDelta));
    WriteDelta(writer, nativeDelta, bytecodeDelta);

    // Spew the bytecode in these ranges.
    if (curBytecodeOffset < nextBytecodeOffset) {
      AutoJitSpewMessage msg(JitSpew_Profiling, "      OPS: ");
      uint32_t curBc = curBytecodeOffset;
      while (curBc < nextBytecodeOffset) {
        jsbytecode* pc = entry[i].tree->script()->offsetToPC(curBc);
#ifdef JS_JITSPEW
        JSOp op = JSOp(*pc);
        msg.append("%s ", CodeName(op));
#endif
        curBc += GetBytecodeLength(pc);
      }
    }
    spewer.spewAndAdvance("      ");

    curNativeOffset = nextNativeOffset;
    curBytecodeOffset = nextBytecodeOffset;
  }

  if (writer.oom()) {
    return false;
  }

  return true;
}

void JitcodeRegionEntry::unpack() {
  CompactBufferReader reader(data_, end_);
  ReadHead(reader, &nativeOffset_, &scriptDepth_);
  MOZ_ASSERT(scriptDepth_ > 0);

  scriptPcStack_ = reader.currentPosition();
  // Skip past script/pc stack
  for (unsigned i = 0; i < scriptDepth_; i++) {
    uint32_t scriptIdx, pcOffset;
    ReadScriptPc(reader, &scriptIdx, &pcOffset);
  }

  deltaRun_ = reader.currentPosition();
}

uint32_t JitcodeRegionEntry::findPcOffset(uint32_t queryNativeOffset,
                                          uint32_t startPcOffset) const {
  DeltaIterator iter = deltaIterator();
  uint32_t curNativeOffset = nativeOffset();
  uint32_t curPcOffset = startPcOffset;
  while (iter.hasMore()) {
    uint32_t nativeDelta;
    int32_t pcDelta;
    iter.readNext(&nativeDelta, &pcDelta);

    // The start address of the next delta-run entry is counted towards
    // the current delta-run entry, because return addresses should
    // associate with the bytecode op prior (the call) not the op after.
    if (queryNativeOffset <= curNativeOffset + nativeDelta) {
      break;
    }
    curNativeOffset += nativeDelta;
    curPcOffset += pcDelta;
  }
  return curPcOffset;
}

uint32_t JitcodeIonTable::findRegionEntry(uint32_t nativeOffset) const {
  static const uint32_t LINEAR_SEARCH_THRESHOLD = 8;
  uint32_t regions = numRegions();
  MOZ_ASSERT(regions > 0);

  // For small region lists, just search linearly.
  if (regions <= LINEAR_SEARCH_THRESHOLD) {
    JitcodeRegionEntry previousEntry = regionEntry(0);
    for (uint32_t i = 1; i < regions; i++) {
      JitcodeRegionEntry nextEntry = regionEntry(i);
      MOZ_ASSERT(nextEntry.nativeOffset() >= previousEntry.nativeOffset());

      // See note in binary-search code below about why we use '<=' here
      // instead of '<'.  Short explanation: regions are closed at their
      // ending addresses, and open at their starting addresses.
      if (nativeOffset <= nextEntry.nativeOffset()) {
        return i - 1;
      }

      previousEntry = nextEntry;
    }
    // If nothing found, assume it falls within last region.
    return regions - 1;
  }

  // For larger ones, binary search the region table.
  uint32_t idx = 0;
  uint32_t count = regions;
  while (count > 1) {
    uint32_t step = count / 2;
    uint32_t mid = idx + step;
    JitcodeRegionEntry midEntry = regionEntry(mid);

    // A region memory range is closed at its ending address, not starting
    // address.  This is because the return address for calls must associate
    // with the call's bytecode PC, not the PC of the bytecode operator after
    // the call.
    //
    // So a query is < an entry if the query nativeOffset is <= the start
    // address of the entry, and a query is >= an entry if the query
    // nativeOffset is > the start address of an entry.
    if (nativeOffset <= midEntry.nativeOffset()) {
      // Target entry is below midEntry.
      count = step;
    } else {  // if (nativeOffset > midEntry.nativeOffset())
      // Target entry is at midEntry or above.
      idx = mid;
      count -= step;
    }
  }
  return idx;
}

/* static */
bool JitcodeIonTable::WriteIonTable(CompactBufferWriter& writer,
                                    const IonEntry::ScriptList& scriptList,
                                    const NativeToBytecode* start,
                                    const NativeToBytecode* end,
                                    uint32_t* tableOffsetOut,
                                    uint32_t* numRegionsOut) {
  MOZ_ASSERT(tableOffsetOut != nullptr);
  MOZ_ASSERT(numRegionsOut != nullptr);
  MOZ_ASSERT(writer.length() == 0);
  MOZ_ASSERT(scriptList.length() > 0);

  JitSpew(JitSpew_Profiling,
          "Writing native to bytecode map for %s (offset %u-%u) (%zu entries)",
          scriptList[0].scriptData.scriptKey.scriptSource->filename(),
          scriptList[0].scriptData.scriptKey.toStringStart,
          scriptList[0].scriptData.scriptKey.toStringEnd,
          mozilla::PointerRangeSize(start, end));

  JitSpew(JitSpew_Profiling, "  ScriptList of size %u",
          unsigned(scriptList.length()));
  for (uint32_t i = 0; i < scriptList.length(); i++) {
    JitSpew(JitSpew_Profiling, "  Script %u - %s (offset %u-%u)", i,
            scriptList[i].scriptData.scriptKey.scriptSource->filename(),
            scriptList[i].scriptData.scriptKey.toStringStart,
            scriptList[i].scriptData.scriptKey.toStringEnd);
  }

  // Write out runs first.  Keep a vector tracking the positive offsets from
  // payload start to the run.
  const NativeToBytecode* curEntry = start;
  js::Vector<uint32_t, 32, SystemAllocPolicy> runOffsets;

  while (curEntry != end) {
    // Calculate the length of the next run.
    uint32_t runLength = JitcodeRegionEntry::ExpectedRunLength(curEntry, end);
    MOZ_ASSERT(runLength > 0);
    MOZ_ASSERT(runLength <= uintptr_t(end - curEntry));
    JitSpew(JitSpew_Profiling, "  Run at entry %d, length %d, buffer offset %d",
            int(curEntry - start), int(runLength), int(writer.length()));

    // Store the offset of the run.
    if (!runOffsets.append(writer.length())) {
      return false;
    }

    // Encode the run.
    if (!JitcodeRegionEntry::WriteRun(writer, scriptList, runLength,
                                      curEntry)) {
      return false;
    }

    curEntry += runLength;
  }

  // Done encoding regions.  About to start table.  Ensure we are aligned to 4
  // bytes since table is composed of uint32_t values.
  uint32_t padding = sizeof(uint32_t) - (writer.length() % sizeof(uint32_t));
  if (padding == sizeof(uint32_t)) {
    padding = 0;
  }
  JitSpew(JitSpew_Profiling, "  Padding %d bytes after run @%d", int(padding),
          int(writer.length()));
  for (uint32_t i = 0; i < padding; i++) {
    writer.writeByte(0);
  }

  // Now at start of table.
  uint32_t tableOffset = writer.length();

  // The table being written at this point will be accessed directly via
  // uint32_t pointers, so all writes below use native endianness.

  // Write out numRegions
  JitSpew(JitSpew_Profiling, "  Writing numRuns=%d", int(runOffsets.length()));
  writer.writeNativeEndianUint32_t(runOffsets.length());

  // Write out region offset table.  The offsets in |runOffsets| are currently
  // forward offsets from the beginning of the buffer.  We convert them to
  // backwards offsets from the start of the table before writing them into
  // their table entries.
  for (uint32_t i = 0; i < runOffsets.length(); i++) {
    JitSpew(JitSpew_Profiling, "  Run %d offset=%d backOffset=%d @%d", int(i),
            int(runOffsets[i]), int(tableOffset - runOffsets[i]),
            int(writer.length()));
    writer.writeNativeEndianUint32_t(tableOffset - runOffsets[i]);
  }

  if (writer.oom()) {
    return false;
  }

  *tableOffsetOut = tableOffset;
  *numRegionsOut = runOffsets.length();
  return true;
}

}  // namespace jit
}  // namespace js

JS::ProfiledFrameHandle::ProfiledFrameHandle(
    JSRuntime* rt, js::jit::JitcodeGlobalEntry& entry, void* addr,
    const js::jit::CallStackFrameInfo& frameInfo, uint32_t depth)
    : rt_(rt),
      entry_(entry),
      addr_(addr),
      canonicalAddr_(nullptr),
      frameInfo_(frameInfo),
      depth_(depth) {
  if (!canonicalAddr_) {
    canonicalAddr_ = entry_.canonicalNativeAddrFor(rt_, addr_);
  }
}

JS_PUBLIC_API JS::ProfilingFrameIterator::FrameKind
JS::ProfiledFrameHandle::frameKind() const {
  if (entry_.isBaselineInterpreter()) {
    return JS::ProfilingFrameIterator::Frame_BaselineInterpreter;
  }
  if (entry_.isBaseline()) {
    return JS::ProfilingFrameIterator::Frame_Baseline;
  }
  if (entry_.isRealmIndependentShared()) {
    return JS::ProfilingFrameIterator::Frame_Baseline;
  }
  return JS::ProfilingFrameIterator::Frame_Ion;
}

JS_PUBLIC_API uint64_t JS::ProfiledFrameHandle::realmID() const {
  return entry_.realmID(rt_);
}

JS_PUBLIC_API JS::ProfiledFrameRange JS::GetProfiledFrames(JSContext* cx,
                                                           void* addr) {
  // Ensure ProfiledFrameRange::MaxInliningDepth matches
  // InlineScriptTree::MaxDepth. Please keep them in sync.
  static_assert(ProfiledFrameRange::MaxInliningDepth ==
                    js::jit::InlineScriptTree::MaxDepth,
                "ProfiledFrameRange::MaxInliningDepth must match "
                "InlineScriptTree::MaxDepth");

  JSRuntime* rt = cx->runtime();
  js::jit::JitcodeGlobalTable* table =
      rt->jitRuntime()->getJitcodeGlobalTable();
  js::jit::JitcodeGlobalEntry* entry = table->lookup(addr);

  ProfiledFrameRange result(rt, addr, entry);

  if (entry) {
    result.depth_ = entry->callStackAtAddr(rt, addr, result.frames_,
                                           std::size(result.frames_));
  }
  return result;
}

JS::ProfiledFrameHandle JS::ProfiledFrameRange::Iter::operator*() const {
  // The iterator iterates in high depth to low depth order. index_ goes up,
  // and the depth we need to pass to ProfiledFrameHandle goes down.
  uint32_t depth = range_.depth_ - 1 - index_;
  return ProfiledFrameHandle(range_.rt_, *range_.entry_, range_.addr_,
                             range_.frames_[depth], depth);
}
