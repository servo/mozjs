/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <functional>

#include "jit/shared/IonAssemblerBufferWithConstantPools.h"
#include "jsapi-tests/tests.h"

// Tests for classes in:
//
//   jit/shared/IonAssemblerBuffer.h
//   jit/shared/IonAssemblerBufferWithConstantPools.h
//
// Classes in js::jit tested:
//
//   BufferOffset
//   BufferSlice (implicitly)
//   AssemblerBuffer
//
//   BranchDeadlineSet
//   Pool (implicitly)
//   AssemblerBufferWithConstantPools
//

BEGIN_TEST(testAssemblerBuffer_BufferOffset) {
  using js::jit::BufferOffset;

  BufferOffset off1;
  BufferOffset off2(10);

  CHECK(!off1.assigned());
  CHECK(off2.assigned());
  CHECK_EQUAL(off2.getOffset(), 10);
  off1 = off2;
  CHECK(off1.assigned());
  CHECK_EQUAL(off1.getOffset(), 10);

  return true;
}
END_TEST(testAssemblerBuffer_BufferOffset)

BEGIN_TEST(testAssemblerBuffer_AssemblerBuffer) {
  using js::jit::BufferOffset;
  using AsmBuf = js::jit::AssemblerBuffer<uint32_t>;

  AsmBuf ab;
  CHECK(ab.isAligned(16));
  CHECK_EQUAL(ab.size(), 0u);
  CHECK_EQUAL(ab.nextOffset().getOffset(), 0);
  CHECK(!ab.oom());

  BufferOffset off1 = ab.putInt(1000017);
  CHECK_EQUAL(off1.getOffset(), 0);
  CHECK_EQUAL(ab.size(), 4u);
  CHECK_EQUAL(ab.nextOffset().getOffset(), 4);
  CHECK(!ab.isAligned(16));
  CHECK(ab.isAligned(4));
  CHECK(ab.isAligned(1));
  CHECK_EQUAL(*ab.getInst(off1), 1000017u);

  BufferOffset off2 = ab.putInt(1000018);
  CHECK_EQUAL(off2.getOffset(), 4);

  BufferOffset off3 = ab.putInt(1000019);
  CHECK_EQUAL(off3.getOffset(), 8);

  BufferOffset off4 = ab.putInt(1000020);
  CHECK_EQUAL(off4.getOffset(), 12);
  CHECK_EQUAL(ab.size(), 16u);
  CHECK_EQUAL(ab.nextOffset().getOffset(), 16);

  // Last one in the slice.
  BufferOffset off5 = ab.putInt(1000021);
  CHECK_EQUAL(off5.getOffset(), 16);
  CHECK_EQUAL(ab.size(), 20u);
  CHECK_EQUAL(ab.nextOffset().getOffset(), 20);

  BufferOffset off6 = ab.putInt(1000022);
  CHECK_EQUAL(off6.getOffset(), 20);
  CHECK_EQUAL(ab.size(), 24u);
  CHECK_EQUAL(ab.nextOffset().getOffset(), 24);

  // Reference previous slice. Excercise the finger.
  CHECK_EQUAL(*ab.getInst(off1), 1000017u);
  CHECK_EQUAL(*ab.getInst(off6), 1000022u);
  CHECK_EQUAL(*ab.getInst(off1), 1000017u);
  CHECK_EQUAL(*ab.getInst(off5), 1000021u);

  // Too much data for one slice.
  const uint32_t fixdata[] = {2000036, 2000037, 2000038,
                              2000039, 2000040, 2000041};

  // Split payload across multiple slices.
  CHECK_EQUAL(ab.nextOffset().getOffset(), 24);
  BufferOffset good1 = ab.putBytes(sizeof(fixdata), fixdata);
  CHECK_EQUAL(good1.getOffset(), 24);
  CHECK_EQUAL(ab.nextOffset().getOffset(), 48);
  CHECK_EQUAL(*ab.getInst(good1), 2000036u);
  CHECK_EQUAL(*ab.getInst(BufferOffset(32)), 2000038u);
  CHECK_EQUAL(*ab.getInst(BufferOffset(36)), 2000039u);
  CHECK_EQUAL(*ab.getInst(BufferOffset(40)), 2000040u);
  CHECK_EQUAL(*ab.getInst(BufferOffset(44)), 2000041u);

  return true;
}
END_TEST(testAssemblerBuffer_AssemblerBuffer)

BEGIN_TEST(testAssemblerBuffer_BranchDeadlineSet) {
  using DLSet = js::jit::BranchDeadlineSet<3>;
  using js::jit::BufferOffset;

  js::LifoAlloc alloc(1024, js::MallocArena);
  DLSet dls(alloc);

  CHECK(dls.empty());
  CHECK_EQUAL(dls.size(), 0u);
  CHECK_EQUAL(dls.maxRangeSize(), 0u);

  // Removing non-existant deadline is OK.
  dls.removeDeadline(1, BufferOffset(7));

  // Add deadlines in increasing order as intended. This is optimal.
  dls.addDeadline(1, BufferOffset(10));
  CHECK(!dls.empty());
  CHECK_EQUAL(dls.size(), 1u);
  CHECK_EQUAL(dls.maxRangeSize(), 1u);
  CHECK_EQUAL(dls.earliestDeadline().getOffset(), 10);
  CHECK_EQUAL(dls.earliestDeadlineRange(), 1u);

  // Removing non-existant deadline is OK.
  dls.removeDeadline(1, BufferOffset(7));
  dls.removeDeadline(1, BufferOffset(17));
  dls.removeDeadline(0, BufferOffset(10));
  CHECK_EQUAL(dls.size(), 1u);
  CHECK_EQUAL(dls.maxRangeSize(), 1u);

  // Two identical deadlines for different ranges.
  dls.addDeadline(2, BufferOffset(10));
  CHECK(!dls.empty());
  CHECK_EQUAL(dls.size(), 2u);
  CHECK_EQUAL(dls.maxRangeSize(), 1u);
  CHECK_EQUAL(dls.earliestDeadline().getOffset(), 10);

  // It doesn't matter which range earliestDeadlineRange() reports first,
  // but it must report both.
  if (dls.earliestDeadlineRange() == 1) {
    dls.removeDeadline(1, BufferOffset(10));
    CHECK_EQUAL(dls.earliestDeadline().getOffset(), 10);
    CHECK_EQUAL(dls.earliestDeadlineRange(), 2u);
  } else {
    CHECK_EQUAL(dls.earliestDeadlineRange(), 2u);
    dls.removeDeadline(2, BufferOffset(10));
    CHECK_EQUAL(dls.earliestDeadline().getOffset(), 10);
    CHECK_EQUAL(dls.earliestDeadlineRange(), 1u);
  }

  // Add deadline which is the front of range 0, but not the global earliest.
  dls.addDeadline(0, BufferOffset(20));
  CHECK_EQUAL(dls.earliestDeadline().getOffset(), 10);
  CHECK(dls.earliestDeadlineRange() > 0);

  // Non-optimal add to front of single-entry range 0.
  dls.addDeadline(0, BufferOffset(15));
  CHECK_EQUAL(dls.earliestDeadline().getOffset(), 10);
  CHECK(dls.earliestDeadlineRange() > 0);

  // Append to 2-entry range 0.
  dls.addDeadline(0, BufferOffset(30));
  CHECK_EQUAL(dls.earliestDeadline().getOffset(), 10);
  CHECK(dls.earliestDeadlineRange() > 0);

  // Add penultimate entry.
  dls.addDeadline(0, BufferOffset(25));
  CHECK_EQUAL(dls.earliestDeadline().getOffset(), 10);
  CHECK(dls.earliestDeadlineRange() > 0);

  // Prepend, stealing earliest from other range.
  dls.addDeadline(0, BufferOffset(5));
  CHECK_EQUAL(dls.earliestDeadline().getOffset(), 5);
  CHECK_EQUAL(dls.earliestDeadlineRange(), 0u);

  // Remove central element.
  dls.removeDeadline(0, BufferOffset(20));
  CHECK_EQUAL(dls.earliestDeadline().getOffset(), 5);
  CHECK_EQUAL(dls.earliestDeadlineRange(), 0u);

  // Remove front, giving back the lead.
  dls.removeDeadline(0, BufferOffset(5));
  CHECK_EQUAL(dls.earliestDeadline().getOffset(), 10);
  CHECK(dls.earliestDeadlineRange() > 0);

  // Remove front, giving back earliest to range 0.
  dls.removeDeadline(dls.earliestDeadlineRange(), BufferOffset(10));
  CHECK_EQUAL(dls.earliestDeadline().getOffset(), 15);
  CHECK_EQUAL(dls.earliestDeadlineRange(), 0u);

  // Remove tail.
  dls.removeDeadline(0, BufferOffset(30));
  CHECK_EQUAL(dls.earliestDeadline().getOffset(), 15);
  CHECK_EQUAL(dls.earliestDeadlineRange(), 0u);

  // Now range 0 = [15, 25].
  CHECK_EQUAL(dls.size(), 2u);
  dls.removeDeadline(0, BufferOffset(25));
  dls.removeDeadline(0, BufferOffset(15));
  CHECK(dls.empty());

  return true;
}
END_TEST(testAssemblerBuffer_BranchDeadlineSet)

// Mock Assembler class for testing the AssemblerBufferWithConstantPools
// callbacks.
namespace {

// Mock instruction set.
struct Instr {
  enum class Op : uint32_t {
    // 0x1111xxxx - align filler instructions.
    AlignFiller = 0x1111,

    // 0x2222xxxx - manually inserted 'arith' instructions.
    Arith = 0x2222,

    // 0xaaaaxxxx - noop filler instruction.
    NoopFiller = 0xaaaa,

    // 0xb0bbxxxx - branch xxxx bytes forward. (Pool guard).
    Branch = 0xb0bb,

    // 0xb1bbxxxx - branch xxxx bytes forward. (Short-range branch).
    ShortBranch = 0xb1bb,

    // 0xb2bbxxxx - branch xxxx bytes forward. (Veneer branch).
    VeneerBranch = 0xb2bb,

    // 0xb3bbxxxx - branch xxxx bytes forward. (Patched short-range branch).
    PatchedShortBranch = 0xb3bb,

    // 0xc0ccxxxx - constant pool load (uninitialized).
    PoolLoadUninit = 0xc0cc,

    // 0xc1ccxxxx - constant pool load to index xxxx.
    PoolLoadIndex = 0xc1cc,

    // 0xc2ccxxxx - constant pool load xxxx bytes ahead.
    PoolLoadPc = 0xc2cc,

    // 0xffffxxxx - pool header with xxxx bytes.
    PoolHeader = 0xffff,
  };

  // Encode operation with 16-bit payload.
  static constexpr uint32_t Encode(Op op, uint16_t bytes) {
    return (static_cast<uint32_t>(op) << 16) | bytes;
  }

  static constexpr std::pair<Op, uint16_t> Decode(uint32_t instr) {
    return {static_cast<Op>(instr >> 16), uint16_t(instr)};
  }

  static constexpr bool Is(Op op, uint32_t instr) {
    return static_cast<uint32_t>(op) == (instr >> 16);
  }

  static constexpr const char* ToName(Op op) {
    switch (op) {
      case Op::AlignFiller:
        return "AlignFiller";
      case Op::Arith:
        return "Arith";
      case Op::NoopFiller:
        return "NoopFiller";
      case Op::Branch:
        return "Branch";
      case Op::ShortBranch:
        return "ShortBranch";
      case Op::VeneerBranch:
        return "VeneerBranch";
      case Op::PatchedShortBranch:
        return "PatchedShortBranch";
      case Op::PoolLoadUninit:
        return "PoolLoadUninit";
      case Op::PoolLoadIndex:
        return "PoolLoadIndex";
      case Op::PoolLoadPc:
        return "PoolLoadPc";
      case Op::PoolHeader:
        return "PoolHeader";
    }
    return "<UNKNOWN OP>";
  }

  static constexpr uint32_t AlignFiller(uint16_t bytes) {
    return Encode(Op::AlignFiller, bytes);
  }

  static constexpr uint32_t NoopFiller(uint16_t bytes) {
    return Encode(Op::NoopFiller, bytes);
  }

  static constexpr uint32_t Arith(uint16_t bytes) {
    return Encode(Op::Arith, bytes);
  }

  static constexpr uint32_t Branch(uint16_t bytes) {
    return Encode(Op::Branch, bytes);
  }

  static constexpr uint32_t ShortBranch(uint16_t bytes) {
    return Encode(Op::ShortBranch, bytes);
  }

  static constexpr uint32_t PatchedShortBranch(uint16_t bytes) {
    return Encode(Op::PatchedShortBranch, bytes);
  }

  static constexpr uint32_t VeneerBranch(uint16_t bytes) {
    return Encode(Op::VeneerBranch, bytes);
  }

  static constexpr uint32_t PoolLoadUninit(uint16_t bytes) {
    return Encode(Op::PoolLoadUninit, bytes);
  }

  static constexpr uint32_t PoolLoadIndex(uint16_t bytes) {
    return Encode(Op::PoolLoadIndex, bytes);
  }

  static constexpr uint32_t PoolLoadPc(uint16_t bytes) {
    return Encode(Op::PoolLoadPc, bytes);
  }

  static constexpr uint32_t PoolHeader(uint16_t bytes) {
    return Encode(Op::PoolHeader, bytes);
  }
};

struct TestAssembler;

using Inst = uint32_t;
static constexpr size_t InstSize = sizeof(Inst);

// Define three short branch types.
//
// (The first type isn't currently used, cf. `TestAssembler::BranchRangeFor`.)
static constexpr unsigned NumShortBranchRanges = 3;

// `TestAssembler::BranchRange{,Short}` is smaller than the default hysteresis
// `jit::ShortRangeBranchHysteresis`. For testing purposes adjust the hysteresis
// to match non-test assemblers, where the hysteresis smaller than any branch
// range.
//
// Use 20 bytes to test the error case in ShortBranchVeneerExpiresTooFastNoPool.
// Smaller sizes break other assembler buffer parts and larger values don't make
// ShortBranchVeneerExpiresTooFastNoPool fail.
static constexpr size_t ShortRangeBranchHysteresis = 20;

static constexpr auto AsmBufSettings = js::jit::AssemblerBufferSettings{
    .instSize = InstSize,
    .guardSize = 1,
    .headerSize = 1,
    .pcBias = 0,
    .alignFillInst = Instr::AlignFiller(0),
    .nopFillInst = Instr::NoopFiller(0),
    .numShortBranchRanges = NumShortBranchRanges,
    .shortRangeBranchHysteresis = ShortRangeBranchHysteresis,
};

using AsmBufWithPool =
    js::jit::AssemblerBufferWithConstantPools<Inst, TestAssembler,
                                              AsmBufSettings>;

struct TestAsmBufWithPool : AsmBufWithPool {
  TestAsmBufWithPool()
      : AsmBufWithPool(
            /* poolMaxOffset= */ 17,
            /* nopFill= */ 0) {}

  static constexpr auto InstSize = AsmBufSettings.instSize;

  /**
   * Dump instructions to help debugging.
   */
  void dumpInstructions() {
    using js::jit::BufferOffset;

    BufferOffset cur(0);
    BufferOffset last = nextOffset();
    while (cur < last) {
      auto [op, bytes] = Instr::Decode(*getInst(cur));
      printf("%04x: %s[%04x]\n", cur.getOffset(), Instr::ToName(op), bytes);

      cur = BufferOffset(cur.getOffset() + InstSize);
    }
  }
};

struct TestAssembler {
  static const unsigned BranchRange = 36;
  static const unsigned BranchRangeShort = 28;

  static void InsertIndexIntoTag(uint8_t* load_, uint32_t index) {
    uint32_t* load = reinterpret_cast<uint32_t*>(load_);
    MOZ_ASSERT(*load == Instr::PoolLoadUninit(0),
               "Expected uninitialized constant pool load");
    MOZ_ASSERT(index < 0x10000);
    *load = Instr::PoolLoadIndex(index);
  }

  static void PatchConstantPoolLoad(void* loadAddr, void* constPoolAddr) {
    uint32_t* load = reinterpret_cast<uint32_t*>(loadAddr);
    uint32_t index = *load & 0xffff;
    MOZ_ASSERT(*load == Instr::PoolLoadIndex(index),
               "Expected constant pool load(index)");
    ptrdiff_t offset = reinterpret_cast<uint8_t*>(constPoolAddr) -
                       reinterpret_cast<uint8_t*>(loadAddr);
    offset += index * 4;
    MOZ_ASSERT(offset % 4 == 0, "Unaligned constant pool");
    MOZ_ASSERT(offset > 0 && offset < 0x10000, "Pool out of range");
    *load = Instr::PoolLoadPc(offset);
  }

  static void WritePoolGuard(js::jit::BufferOffset branch, uint32_t* dest,
                             js::jit::BufferOffset afterPool) {
    MOZ_ASSERT(branch.assigned());
    MOZ_ASSERT(afterPool.assigned());
    size_t branchOff = branch.getOffset();
    size_t afterPoolOff = afterPool.getOffset();
    MOZ_ASSERT(afterPoolOff > branchOff);
    uint32_t delta = afterPoolOff - branchOff;
    *dest = Instr::Branch(delta);
  }

  static void WritePoolHeader(void* start, js::jit::Pool* p, bool isNatural) {
    MOZ_ASSERT(!isNatural, "Natural pool guards not implemented.");
    uint32_t* hdr = reinterpret_cast<uint32_t*>(start);
    *hdr = Instr::PoolHeader(p->getPoolSize());
  }

  static unsigned BranchRangeFor(unsigned rangeIdx) {
    MOZ_ASSERT(rangeIdx < NumShortBranchRanges);

    switch (rangeIdx) {
      case 0:
        MOZ_CRASH("unused branch type");
      case 1:
        return BranchRange;
      case 2:
        return BranchRangeShort;
    }
    MOZ_CRASH("bad branch type");
  }

  static void PatchShortRangeBranchToVeneer(AsmBufWithPool* buffer,
                                            unsigned rangeIdx,
                                            js::jit::BufferOffset deadline,
                                            js::jit::BufferOffset veneer) {
    size_t branchOff = deadline.getOffset() - BranchRangeFor(rangeIdx);
    size_t veneerOff = veneer.getOffset();
    Inst* branch = buffer->getInst(js::jit::BufferOffset(branchOff));

    MOZ_ASSERT(Instr::Is(Instr::Op::ShortBranch, *branch),
               "Expected short-range branch instruction");
    // Copy branch offset to veneer. A real instruction set would require
    // some adjustment of the label linked-list.
    *buffer->getInst(veneer) = Instr::VeneerBranch(*branch & 0xffff);
    MOZ_ASSERT(veneerOff > branchOff, "Veneer should follow branch");
    *branch = Instr::PatchedShortBranch(veneerOff - branchOff);
  }
};

class AutoForbidNops {
 protected:
  AsmBufWithPool* ab_;

 public:
  explicit AutoForbidNops(AsmBufWithPool* ab) : ab_(ab) { ab_->enterNoNops(); }
  ~AutoForbidNops() { ab_->leaveNoNops(); }
};

class AutoForbidPoolsAndNops : public AutoForbidNops {
 public:
  AutoForbidPoolsAndNops(AsmBufWithPool* ab, size_t maxInst)
      : AutoForbidNops(ab) {
    ab_->enterNoPool(maxInst);
  }
  ~AutoForbidPoolsAndNops() { ab_->leaveNoPool(); }
};

}  // namespace

BEGIN_TEST(testAssemblerBuffer_AssemblerBufferWithConstantPools) {
  using js::jit::BufferOffset;

  TestAsmBufWithPool ab{};

  CHECK(ab.isAligned(16));
  CHECK_EQUAL(ab.size(), 0u);
  CHECK_EQUAL(ab.nextOffset().getOffset(), 0);
  CHECK(!ab.oom());

  // Each slice holds 5 instructions. Trigger a constant pool inside the slice.
  uint32_t poolLoad[] = {Instr::PoolLoadUninit(0)};
  uint32_t poolData[] = {0xdddd0000, 0xdddd0001, 0xdddd0002, 0xdddd0003};
  BufferOffset load =
      ab.allocEntry(1, 1, (uint8_t*)poolLoad, (uint8_t*)poolData);
  CHECK_EQUAL(load.getOffset(), 0);

  // Pool hasn't been emitted yet. Load has been patched by
  // InsertIndexIntoTag.
  CHECK_EQUAL(*ab.getInst(load), Instr::PoolLoadIndex(0));

  // Expected layout:
  //
  //   0: load [pc+16]
  //   4: arith(1)
  //   8: guard branch pc+12
  //  12: pool header
  //  16: poolData
  //  20: arith(2)
  //
  ab.putInt(Instr::Arith(1));
  // One could argue that the pool should be flushed here since there is no
  // more room. However, the current implementation doesn't dump pool until
  // asked to add data:
  ab.putInt(Instr::Arith(2));

  CHECK_EQUAL(*ab.getInst(BufferOffset(0)), Instr::PoolLoadPc(16));
  CHECK_EQUAL(*ab.getInst(BufferOffset(4)), Instr::Arith(1));
  CHECK_EQUAL(*ab.getInst(BufferOffset(8)), Instr::Branch(12));
  CHECK_EQUAL(*ab.getInst(BufferOffset(12)), Instr::PoolHeader(4));
  CHECK_EQUAL(*ab.getInst(BufferOffset(16)), 0xdddd0000u);
  CHECK_EQUAL(*ab.getInst(BufferOffset(20)), Instr::Arith(2));

  // allocEntry() overwrites the load instruction! Restore the original.
  poolLoad[0] = Instr::PoolLoadUninit(0);

  // Now try with load and pool data on separate slices.
  load = ab.allocEntry(1, 1, (uint8_t*)poolLoad, (uint8_t*)poolData);
  CHECK_EQUAL(load.getOffset(), 24);
  CHECK_EQUAL(*ab.getInst(load),
              Instr::PoolLoadIndex(0));  // Index into current pool.
  ab.putInt(Instr::Arith(1));
  ab.putInt(Instr::Arith(2));
  CHECK_EQUAL(*ab.getInst(BufferOffset(24)), Instr::PoolLoadPc(16));
  CHECK_EQUAL(*ab.getInst(BufferOffset(28)), Instr::Arith(1));
  CHECK_EQUAL(*ab.getInst(BufferOffset(32)), Instr::Branch(12));
  CHECK_EQUAL(*ab.getInst(BufferOffset(36)), Instr::PoolHeader(4));
  CHECK_EQUAL(*ab.getInst(BufferOffset(40)), 0xdddd0000u);
  CHECK_EQUAL(*ab.getInst(BufferOffset(44)), Instr::Arith(2));

  // Two adjacent loads to the same pool.
  poolLoad[0] = Instr::PoolLoadUninit(0);
  load = ab.allocEntry(1, 1, (uint8_t*)poolLoad, (uint8_t*)poolData);
  CHECK_EQUAL(load.getOffset(), 48);
  CHECK_EQUAL(*ab.getInst(load),
              Instr::PoolLoadIndex(0));  // Index into current pool.

  poolLoad[0] = Instr::PoolLoadUninit(0);
  load = ab.allocEntry(1, 1, (uint8_t*)poolLoad, (uint8_t*)(poolData + 1));
  CHECK_EQUAL(load.getOffset(), 52);
  CHECK_EQUAL(*ab.getInst(load),
              Instr::PoolLoadIndex(1));  // Index into current pool.

  ab.putInt(Instr::Arith(5));

  CHECK_EQUAL(*ab.getInst(BufferOffset(48)),
              Instr::PoolLoadPc(16));  // load pc+16.
  CHECK_EQUAL(*ab.getInst(BufferOffset(52)),
              Instr::PoolLoadPc(16));  // load pc+16.
  CHECK_EQUAL(*ab.getInst(BufferOffset(56)),
              Instr::Branch(16));  // guard branch pc+16.
  CHECK_EQUAL(*ab.getInst(BufferOffset(60)),
              Instr::PoolHeader(8));                        // header 8 bytes.
  CHECK_EQUAL(*ab.getInst(BufferOffset(64)), 0xdddd0000u);  // datum 1.
  CHECK_EQUAL(*ab.getInst(BufferOffset(68)), 0xdddd0001u);  // datum 2.
  CHECK_EQUAL(*ab.getInst(BufferOffset(72)), Instr::Arith(5));

  // Two loads as above, but the first load has an 8-byte pool entry, and the
  // second load wouldn't be able to reach its data. This must produce two
  // pools.
  poolLoad[0] = Instr::PoolLoadUninit(0);
  load = ab.allocEntry(1, 2, (uint8_t*)poolLoad, (uint8_t*)(poolData + 2));
  CHECK_EQUAL(load.getOffset(), 76);
  CHECK_EQUAL(*ab.getInst(load),
              Instr::PoolLoadIndex(0));  // Index into current pool.

  poolLoad[0] = Instr::PoolLoadUninit(0);
  load = ab.allocEntry(1, 1, (uint8_t*)poolLoad, (uint8_t*)poolData);
  CHECK_EQUAL(load.getOffset(), 96);
  CHECK_EQUAL(*ab.getInst(load),
              Instr::PoolLoadIndex(0));  // Index into current pool.

  CHECK_EQUAL(*ab.getInst(BufferOffset(76)),
              Instr::PoolLoadPc(12));  // load pc+12.
  CHECK_EQUAL(*ab.getInst(BufferOffset(80)),
              Instr::Branch(16));  // guard branch pc+16.
  CHECK_EQUAL(*ab.getInst(BufferOffset(84)),
              Instr::PoolHeader(8));                        // header 8 bytes.
  CHECK_EQUAL(*ab.getInst(BufferOffset(88)), 0xdddd0002u);  // datum 1.
  CHECK_EQUAL(*ab.getInst(BufferOffset(92)), 0xdddd0003u);  // datum 2.

  // Second pool is not flushed yet, and there is room for one instruction
  // after the load. Test the keep-together feature.
  ab.enterNoPool(2);
  ab.putInt(Instr::Arith(6));
  ab.putInt(Instr::Arith(7));
  ab.leaveNoPool();

  CHECK_EQUAL(*ab.getInst(BufferOffset(96)),
              Instr::PoolLoadPc(12));  // load pc+12.
  CHECK_EQUAL(*ab.getInst(BufferOffset(100)),
              Instr::Branch(12));  // guard branch pc+12.
  CHECK_EQUAL(*ab.getInst(BufferOffset(104)),
              Instr::PoolHeader(4));                         // header 4 bytes.
  CHECK_EQUAL(*ab.getInst(BufferOffset(108)), 0xdddd0000u);  // datum 1.
  CHECK_EQUAL(*ab.getInst(BufferOffset(112)), Instr::Arith(6));
  CHECK_EQUAL(*ab.getInst(BufferOffset(116)), Instr::Arith(7));

  return true;
}
END_TEST(testAssemblerBuffer_AssemblerBufferWithConstantPools)

BEGIN_TEST(testAssemblerBuffer_AssemblerBufferWithConstantPools_ShortBranch) {
  using js::jit::BufferOffset;

  TestAsmBufWithPool ab{};

  // Insert short-range branch.
  BufferOffset br1 = ab.putInt(Instr::ShortBranch(0xcc));
  ab.registerBranchDeadline(
      1, BufferOffset(br1.getOffset() + TestAssembler::BranchRange));

  ab.putInt(Instr::Arith(1));

  BufferOffset off = ab.putInt(Instr::Arith(2));
  ab.registerBranchDeadline(
      1, BufferOffset(off.getOffset() + TestAssembler::BranchRange));

  ab.putInt(Instr::Arith(3));
  ab.putInt(Instr::Arith(4));

  // Second short-range branch that will be swiped up by hysteresis.
  BufferOffset br2 = ab.putInt(Instr::ShortBranch(0xd2d));
  ab.registerBranchDeadline(
      1, BufferOffset(br2.getOffset() + TestAssembler::BranchRange));

  // Branch should not have been patched yet here.
  CHECK_EQUAL(*ab.getInst(br1), Instr::ShortBranch(0xcc));
  CHECK_EQUAL(*ab.getInst(br2), Instr::ShortBranch(0xd2d));

  // Cancel one of the pending branches.
  // This is what will happen to most branches as they are bound before
  // expiring by Assembler::bind().
  ab.unregisterBranchDeadline(
      1, BufferOffset(off.getOffset() + TestAssembler::BranchRange));

  off = ab.putInt(Instr::Arith(6));
  // Here we may or may not have patched the branch yet, but it is inevitable
  // now:
  //
  //  0: br1 pc+36
  //  4: arith(1)
  //  8: arith(2) (unpatched)
  // 12: arith(3)
  // 16: arith(4)
  // 20: br2 pc+20
  // 24: arith(6)
  CHECK_EQUAL(off.getOffset(), 24);
  // 28: guard branch pc+16
  // 32: pool header
  // 36: veneer1
  // 40: veneer2
  // 44: arith(7)

  off = ab.putInt(Instr::Arith(7));
  CHECK_EQUAL(off.getOffset(), 44);

  // Now the branch must have been patched.
  CHECK_EQUAL(*ab.getInst(br1),
              Instr::PatchedShortBranch(36));  // br1 pc+36 (patched)
  CHECK_EQUAL(*ab.getInst(BufferOffset(8)),
              Instr::Arith(2));  // arith(2) (unpatched)
  CHECK_EQUAL(*ab.getInst(br2),
              Instr::PatchedShortBranch(20));  // br2 pc+20 (patched)
  CHECK_EQUAL(*ab.getInst(BufferOffset(28)),
              Instr::Branch(16));  // br pc+16 (guard)
  CHECK_EQUAL(*ab.getInst(BufferOffset(32)),
              Instr::PoolHeader(0));  // pool header 0 bytes.
  CHECK_EQUAL(*ab.getInst(BufferOffset(36)),
              Instr::VeneerBranch(0xcc));  // veneer1 w/ original 'cc' offset.
  CHECK_EQUAL(*ab.getInst(BufferOffset(40)),
              Instr::VeneerBranch(0xd2d));  // veneer2 w/ original 'd2d' offset.
  CHECK_EQUAL(*ab.getInst(BufferOffset(44)), Instr::Arith(7));

  return true;
}
END_TEST(testAssemblerBuffer_AssemblerBufferWithConstantPools_ShortBranch)

BEGIN_TEST(
    testAssemblerBuffer_AssemblerBufferWithConstantPools_ShortBranchVeneerExpiresTooFast) {
  using js::jit::BufferOffset;

  // When NumShortBranchRanges > 1, it is possible for branch deadlines to
  // expire faster than we can insert veneers. Suppose branches are 4 bytes
  // each, we could have the following deadline set:
  //
  //   Range 0: 40, 44, 48
  //   Range 1: 44, 48
  //
  // It is not good enough to start inserting veneers at the 40 deadline; we
  // would not be able to create veneers for the second 44 deadline.
  // Instead, we need to start at 32:
  //
  //   32: veneer(40)
  //   36: veneer(44)
  //   40: veneer(44)
  //   44: veneer(48)
  //   48: veneer(48)

  TestAsmBufWithPool ab{};

  ab.putInt(Instr::Arith(1));

  BufferOffset br1 = ab.putInt(Instr::ShortBranch(0xaa));
  ab.registerBranchDeadline(
      1, BufferOffset(br1.getOffset() + TestAssembler::BranchRange));

  BufferOffset br2 = ab.putInt(Instr::ShortBranch(0xbb));
  ab.registerBranchDeadline(
      1, BufferOffset(br2.getOffset() + TestAssembler::BranchRange));

  BufferOffset br3 = ab.putInt(Instr::ShortBranch(0xcc));
  ab.registerBranchDeadline(
      1, BufferOffset(br3.getOffset() + TestAssembler::BranchRange));

  BufferOffset br4 = ab.putInt(Instr::ShortBranch(0xdd));
  ab.registerBranchDeadline(
      2, BufferOffset(br4.getOffset() + TestAssembler::BranchRangeShort));

  BufferOffset br5 = ab.putInt(Instr::ShortBranch(0xee));
  ab.registerBranchDeadline(
      2, BufferOffset(br5.getOffset() + TestAssembler::BranchRangeShort));

  // Branch should not have been patched yet here.
  CHECK_EQUAL(*ab.getInst(br1), Instr::ShortBranch(0xaa));
  CHECK_EQUAL(*ab.getInst(br2), Instr::ShortBranch(0xbb));
  CHECK_EQUAL(*ab.getInst(br3), Instr::ShortBranch(0xcc));
  CHECK_EQUAL(*ab.getInst(br4), Instr::ShortBranch(0xdd));
  CHECK_EQUAL(*ab.getInst(br5), Instr::ShortBranch(0xee));
  CHECK_EQUAL(br5.getOffset(), 20);

  // Instructions:
  //
  //  0: arith(1)
  //  4: br1 (deadline = 40)
  //  8: br2 (deadline = 44)
  // 12: br3 (deadline = 48)
  // 16: br4 (deadline = 44)
  // 20: br5 (deadline = 48)

  // Branch patching happens when adding arith(2).
  auto off = ab.putInt(Instr::Arith(2));
  CHECK_EQUAL(off.getOffset(), 52);

  // Instructions:
  //
  //  0: arith(1)
  //  4: br1 pc+28
  //  8: br2 pc+28
  // 12: br3 pc+32
  // 16: br4 pc+24
  // 20: br5 pc+28
  // 24: guard branch pc+28
  // 28: pool header
  // 32: veneer1
  // 36: veneer2
  // 40: veneer4
  // 44: veneer3
  // 48: veneer5
  // 52: arith(2)

  // Now the branches must have been patched.
  CHECK_EQUAL(*ab.getInst(br1),
              Instr::PatchedShortBranch(28));  // br1 pc+28 (patched)
  CHECK_EQUAL(*ab.getInst(br2),
              Instr::PatchedShortBranch(28));  // br2 pc+28 (patched)
  CHECK_EQUAL(*ab.getInst(br3),
              Instr::PatchedShortBranch(32));  // br3 pc+32 (patched)
  CHECK_EQUAL(*ab.getInst(br4),
              Instr::PatchedShortBranch(24));  // br4 pc+24 (patched)
  CHECK_EQUAL(*ab.getInst(br5),
              Instr::PatchedShortBranch(28));  // br5 pc+28 (patched)

  CHECK_EQUAL(*ab.getInst(BufferOffset(32)),
              Instr::VeneerBranch(0xaa));  // veneer1 w/ original 'aa' offset.
  CHECK_EQUAL(*ab.getInst(BufferOffset(36)),
              Instr::VeneerBranch(0xbb));  // veneer2 w/ original 'bb' offset.
  CHECK_EQUAL(*ab.getInst(BufferOffset(40)),
              Instr::VeneerBranch(0xdd));  // veneer4 w/ original 'dd' offset.
  CHECK_EQUAL(*ab.getInst(BufferOffset(44)),
              Instr::VeneerBranch(0xcc));  // veneer3 w/ original 'cc' offset.
  CHECK_EQUAL(*ab.getInst(BufferOffset(48)),
              Instr::VeneerBranch(0xee));  // veneer5 w/ original 'ee' offset.

  return true;
}
END_TEST(
    testAssemblerBuffer_AssemblerBufferWithConstantPools_ShortBranchVeneerExpiresTooFast)

BEGIN_TEST(
    testAssemblerBuffer_AssemblerBufferWithConstantPools_ShortBranchVeneerExpiresTooFastNoPool) {
  using js::jit::BufferOffset;

  TestAsmBufWithPool ab{};

  BufferOffset br1 = ab.putInt(Instr::ShortBranch(0xaa));
  ab.registerBranchDeadline(
      1, BufferOffset(br1.getOffset() + TestAssembler::BranchRange));

  BufferOffset br2 = ab.putInt(Instr::ShortBranch(0xbb));
  ab.registerBranchDeadline(
      1, BufferOffset(br2.getOffset() + TestAssembler::BranchRange));

  BufferOffset br3 = ab.putInt(Instr::ShortBranch(0xcc));
  ab.registerBranchDeadline(
      1, BufferOffset(br3.getOffset() + TestAssembler::BranchRange));

  BufferOffset br4 = ab.putInt(Instr::ShortBranch(0xdd));
  ab.registerBranchDeadline(
      2, BufferOffset(br4.getOffset() + TestAssembler::BranchRangeShort));

  // Branch should not have been patched yet here.
  CHECK_EQUAL(*ab.getInst(br1), Instr::ShortBranch(0xaa));
  CHECK_EQUAL(*ab.getInst(br2), Instr::ShortBranch(0xbb));
  CHECK_EQUAL(*ab.getInst(br3), Instr::ShortBranch(0xcc));
  CHECK_EQUAL(*ab.getInst(br4), Instr::ShortBranch(0xdd));

  // Instructions:
  //
  //  0: br1 (deadline = 36)
  //  4: br2 (deadline = 40)
  //  8: br3 (deadline = 44)
  // 12: br4 (deadline = 40)
  CHECK_EQUAL(br4.getOffset(), 12);

  // Three consecutive instructions can't be emitted at this point, because when
  // issuing the pool for the earliest dead line (br1 at offset 36), this
  // instruction sequence would be generated:
  //
  // 16: arith(1)
  // 20: arith(2)
  // 24: arith(3)
  // 28: guard branch
  // 32: pool header
  // 36: veneer1
  // 40: veneer2 + veneer4 (!!! Conflict !!!)
  //
  // The deadline for both br2 and br4 is at offset 40, so we have a conflict.
  //
  // That means the three `arith` instructions need to be emitted after the
  // pool:
  //
  //  0: br1 pc+24
  //  4: br2 pc+24
  //  8: br3 pc+28
  // 12: br4 pc+20
  // 16: guard branch pc+24
  // 20: pool header
  // 24: veneer1
  // 28: veneer2
  // 32: veneer4
  // 36: veneer3
  // 40: arith(1)
  // 44: arith(2)
  // 48: arith(3)
  {
    AutoForbidPoolsAndNops afp(&ab, 3);

    ab.putInt(Instr::Arith(1));
    ab.putInt(Instr::Arith(2));
    ab.putInt(Instr::Arith(3));
  }

  // Now the branches must have been patched.
  CHECK_EQUAL(*ab.getInst(br1),
              Instr::PatchedShortBranch(24));  // br1 pc+24 (patched)
  CHECK_EQUAL(*ab.getInst(br2),
              Instr::PatchedShortBranch(24));  // br2 pc+24 (patched)
  CHECK_EQUAL(*ab.getInst(br3),
              Instr::PatchedShortBranch(28));  // br3 pc+28 (patched)
  CHECK_EQUAL(*ab.getInst(br4),
              Instr::PatchedShortBranch(20));  // br4 pc+20 (patched)

  CHECK_EQUAL(*ab.getInst(BufferOffset(24)),
              Instr::VeneerBranch(0xaa));  // veneer1 w/ original 'aa' offset.
  CHECK_EQUAL(*ab.getInst(BufferOffset(28)),
              Instr::VeneerBranch(0xbb));  // veneer2 w/ original 'bb' offset.
  CHECK_EQUAL(*ab.getInst(BufferOffset(32)),
              Instr::VeneerBranch(0xdd));  // veneer4 w/ original 'dd' offset.
  CHECK_EQUAL(*ab.getInst(BufferOffset(36)),
              Instr::VeneerBranch(0xcc));  // veneer3 w/ original 'cc' offset.

  return true;
}
END_TEST(
    testAssemblerBuffer_AssemblerBufferWithConstantPools_ShortBranchVeneerExpiresTooFastNoPool)

// Test that everything is put together correctly in the ARM64 assembler.
#if defined(JS_CODEGEN_ARM64)

#  include "jit/MacroAssembler-inl.h"

BEGIN_TEST(testAssemblerBuffer_ARM64) {
  using namespace js::jit;

  js::LifoAlloc lifo(4096, js::MallocArena);
  TempAllocator alloc(&lifo);
  JitContext jc(cx);
  StackMacroAssembler masm(cx, alloc);
  AutoCreatedBy acb(masm, __func__);

  // Branches to an unbound label.
  Label lab1;
  masm.branch(Assembler::Equal, &lab1);
  masm.branch(Assembler::LessThan, &lab1);
  masm.bind(&lab1);
  masm.branch(Assembler::Equal, &lab1);

  CHECK_EQUAL(masm.getInstructionAt(BufferOffset(0))->InstructionBits(),
              vixl::B_cond | vixl::Assembler::ImmCondBranch(2) | vixl::eq);
  CHECK_EQUAL(masm.getInstructionAt(BufferOffset(4))->InstructionBits(),
              vixl::B_cond | vixl::Assembler::ImmCondBranch(1) | vixl::lt);
  CHECK_EQUAL(masm.getInstructionAt(BufferOffset(8))->InstructionBits(),
              vixl::B_cond | vixl::Assembler::ImmCondBranch(0) | vixl::eq);

  // Branches can reach the label, but the linked list of uses needs to be
  // rearranged. The final conditional branch cannot reach the first branch.
  Label lab2a;
  Label lab2b;
  masm.bind(&lab2a);
  masm.B(&lab2b);
  // Generate 1,100,000 bytes of NOPs.
  for (unsigned n = 0; n < 1100000; n += 4) {
    masm.Nop();
  }
  masm.branch(Assembler::LessThan, &lab2b);
  masm.bind(&lab2b);
  CHECK_EQUAL(
      masm.getInstructionAt(BufferOffset(lab2a.offset()))->InstructionBits(),
      vixl::B | vixl::Assembler::ImmUncondBranch(1100000 / 4 + 2));
  CHECK_EQUAL(masm.getInstructionAt(BufferOffset(lab2b.offset() - 4))
                  ->InstructionBits(),
              vixl::B_cond | vixl::Assembler::ImmCondBranch(1) | vixl::lt);

  // Generate a conditional branch that can't reach its label.
  Label lab3a;
  Label lab3b;
  masm.bind(&lab3a);
  masm.branch(Assembler::LessThan, &lab3b);
  for (unsigned n = 0; n < 1100000; n += 4) {
    masm.Nop();
  }
  masm.bind(&lab3b);
  masm.B(&lab3a);
  Instruction* bcond3 = masm.getInstructionAt(BufferOffset(lab3a.offset()));
  CHECK_EQUAL(bcond3->BranchType(), vixl::CondBranchType);
  ptrdiff_t delta = bcond3->ImmPCRawOffset() * 4;
  Instruction* veneer =
      masm.getInstructionAt(BufferOffset(lab3a.offset() + delta));
  CHECK_EQUAL(veneer->BranchType(), vixl::UncondBranchType);
  delta += veneer->ImmPCRawOffset() * 4;
  CHECK_EQUAL(delta, lab3b.offset() - lab3a.offset());
  Instruction* b3 = masm.getInstructionAt(BufferOffset(lab3b.offset()));
  CHECK_EQUAL(b3->BranchType(), vixl::UncondBranchType);
  CHECK_EQUAL(4 * b3->ImmPCRawOffset(), -delta);

  return true;
}
END_TEST(testAssemblerBuffer_ARM64)

// Helpers to encode ARM64 instructions.
namespace AArch64 {

// See LinkAndGetByteOffsetTo and LinkAndGetInstructionOffsetTo
auto offset(js::jit::BufferOffset branch, js::jit::BufferOffset label) {
  constexpr auto elementShift = vixl::kInstructionSizeLog2;

  ptrdiff_t branch_offset = ptrdiff_t(branch.getOffset() >> elementShift);
  ptrdiff_t label_offset = ptrdiff_t(label.getOffset() >> elementShift);
  return label_offset - branch_offset;
}

auto label_offset(js::jit::BufferOffset branch, js::jit::Label* label) {
  MOZ_ASSERT(label->bound());
  return offset(branch, js::jit::BufferOffset(label->offset()));
}

// Unbound labels use a zero offset.
constexpr inline ptrdiff_t unbound = 0;

auto nop() { return vixl::HINT | vixl::Assembler::ImmHint(vixl::NOP); }

auto b(ptrdiff_t offset) {
  return vixl::B | vixl::Assembler::ImmUncondBranch(offset);
}

auto cbz(vixl::Register rt, ptrdiff_t offset) {
  return vixl::Assembler::SF(rt) | vixl::CBZ |
         vixl::Assembler::ImmCmpBranch(offset) | vixl::Assembler::Rt(rt);
}

auto tbz(vixl::Register rt, unsigned bitPos, ptrdiff_t offset) {
  return vixl::TBZ | vixl::Assembler::ImmTestBranchBit(bitPos) |
         vixl::Assembler::ImmTestBranch(offset) | vixl::Assembler::Rt(rt);
}

auto tbnz(vixl::Register rt, unsigned bitPos, ptrdiff_t offset) {
  return vixl::TBNZ | vixl::Assembler::ImmTestBranchBit(bitPos) |
         vixl::Assembler::ImmTestBranch(offset) | vixl::Assembler::Rt(rt);
}

// "non-natural" pool header.
auto poolheader(uint16_t size) {
  MOZ_ASSERT(size < (1 << 15));
  return 0xffff'0000 | size;
}
}  // namespace AArch64

BEGIN_TEST(testAssemblerBuffer_ARM64_ShortBranchVeneerExpiresTooFast) {
  using namespace js::jit;
  using namespace AArch64;

  // Same as AssemblerBufferWithConstantPools_ShortBranchVeneerExpiresTooFast,
  // only this time test with an actual assembler and not just the mock
  // assembler.

  js::LifoAlloc lifo(4096, js::MallocArena);
  TempAllocator alloc(&lifo);
  JitContext jc(cx);
  StackMacroAssembler masm(cx, alloc);
  AutoCreatedBy acb(masm, __func__);

  auto rt = vixl::x1;

  auto cbz = std::bind_front(AArch64::cbz, rt);
  auto tbz = std::bind_front(AArch64::tbz, rt);

  // Generate the following instruction sequence:
  //
  // 0: cbz'1
  // 4: cbz'2
  // 8: cbz'3
  // 12..n: nop
  // n: tbz'1
  // n+4: tbz'2
  // n+8..k: nop
  // k: <pool>
  //
  // Branch range limits:
  //  tbz: imm14 = +/- 32KB
  //  cbz: imm19 = +/- 1MB
  //
  // Goals:
  // - cbz'1 has the earliest deadline
  // - cbz'2 and tzb'1 have the same deadline
  // - cbz'3 and tzb'2 have the same deadline

  Label cbz_lbl1, cbz_lbl2, cbz_lbl3;
  Label tbz_lbl1, tbz_lbl2;

  BufferOffset cbz1(masm.currentOffset());
  masm.Cbz(rt, &cbz_lbl1);

  BufferOffset cbz2(masm.currentOffset());
  masm.Cbz(rt, &cbz_lbl2);

  BufferOffset cbz3(masm.currentOffset());
  masm.Cbz(rt, &cbz_lbl3);

  // Number of cbz instructions which will share deadlines with tbz.
  int32_t shared_deadlines = 2;

  // Difference in branch ranges in bytes.
  int32_t range_diff =
      vixl::Instruction::ImmBranchMaxForwardOffset(vixl::CondBranchRangeType) -
      vixl::Instruction::ImmBranchMaxForwardOffset(vixl::TestBranchRangeType);
  CHECK_EQUAL(range_diff % 4, 0);

  // Number of nops to add before tbz instructions.
  int32_t nops_before_tbz = (range_diff / 4) - shared_deadlines;

  for (int32_t i = 0; i < nops_before_tbz; ++i) {
    masm.Nop();
  }

  BufferOffset tbz1(masm.currentOffset());
  unsigned tbz1_bitpos = 12;
  masm.Tbz(rt, tbz1_bitpos, &tbz_lbl1);

  BufferOffset tbz2(masm.currentOffset());
  unsigned tbz2_bitpos = 15;
  masm.Tbz(rt, tbz2_bitpos, &tbz_lbl2);

  // Compute deadlines for cbz instructions.
  BufferOffset cbz_deadline1(
      cbz1.getOffset() +
      vixl::Instruction::ImmBranchMaxForwardOffset(vixl::CondBranchRangeType));
  BufferOffset cbz_deadline2(
      cbz2.getOffset() +
      vixl::Instruction::ImmBranchMaxForwardOffset(vixl::CondBranchRangeType));
  BufferOffset cbz_deadline3(
      cbz3.getOffset() +
      vixl::Instruction::ImmBranchMaxForwardOffset(vixl::CondBranchRangeType));

  // Compute deadlines for tbz instructions.
  BufferOffset tbz_deadline1(
      tbz1.getOffset() +
      vixl::Instruction::ImmBranchMaxForwardOffset(vixl::TestBranchRangeType));
  BufferOffset tbz_deadline2(
      tbz2.getOffset() +
      vixl::Instruction::ImmBranchMaxForwardOffset(vixl::TestBranchRangeType));

  // Ensure deadlines are correctly set-up.
  CHECK(cbz_deadline1 < cbz_deadline2);
  CHECK_EQUAL(cbz_deadline2.getOffset(), tbz_deadline1.getOffset());
  CHECK_EQUAL(cbz_deadline3.getOffset(), tbz_deadline2.getOffset());

  // All branches are still unbound.
  CHECK_EQUAL(masm.getInstructionAt(cbz1)->InstructionBits(), cbz(unbound));
  CHECK_EQUAL(masm.getInstructionAt(cbz2)->InstructionBits(), cbz(unbound));
  CHECK_EQUAL(masm.getInstructionAt(cbz3)->InstructionBits(), cbz(unbound));
  CHECK_EQUAL(masm.getInstructionAt(tbz1)->InstructionBits(),
              tbz(tbz1_bitpos, unbound));
  CHECK_EQUAL(masm.getInstructionAt(tbz2)->InstructionBits(),
              tbz(tbz2_bitpos, unbound));

  // Fill up with more nops to trigger veneer construction.
  int32_t nops_before_deadline = 0;
  while (masm.currentOffset() < uint32_t(cbz_deadline1.getOffset())) {
    masm.Nop();
    nops_before_deadline++;
  }

  // Total number of pool instructions:
  // - 1 Guard branch
  // - 1 Pool header
  // - 5 veneer branches (3 cbz + 2 tbz instructions)
  constexpr int32_t pool_instructions = 1 + 1 + 5;

  // Compute offset of pool guard branch.
  int32_t pool_start_offset = tbz2.getOffset() + nops_before_deadline * 4;

  // Nop before pool
  BufferOffset nop_before_pool(pool_start_offset - 4);
  CHECK_EQUAL(masm.getInstructionAt(nop_before_pool)->InstructionBits(), nop());

  // Nop after pool
  BufferOffset nop_after_pool(pool_start_offset + pool_instructions * 4);
  CHECK_EQUAL(masm.getInstructionAt(nop_after_pool)->InstructionBits(), nop());

  // Ensure the above Nop is the last instruction.
  CHECK_EQUAL(BufferOffset(nop_after_pool.getOffset() + 4).getOffset(),
              int32_t(masm.currentOffset()));

  // Pool guard branch
  BufferOffset guard(pool_start_offset);
  CHECK_EQUAL(masm.getInstructionAt(guard)->InstructionBits(),
              b(offset(guard, nop_after_pool)));

  // Pool header
  BufferOffset header(pool_start_offset + 4);
  CHECK_EQUAL(masm.getInstructionAt(header)->InstructionBits(), poolheader(1));

  // Veneer branches
  BufferOffset veneer1(pool_start_offset + 8);
  CHECK_EQUAL(masm.getInstructionAt(veneer1)->InstructionBits(), b(unbound));

  BufferOffset veneer2(pool_start_offset + 12);
  CHECK_EQUAL(masm.getInstructionAt(veneer2)->InstructionBits(), b(unbound));

  BufferOffset veneer3(pool_start_offset + 16);
  CHECK_EQUAL(masm.getInstructionAt(veneer3)->InstructionBits(), b(unbound));

  BufferOffset veneer4(pool_start_offset + 20);
  CHECK_EQUAL(masm.getInstructionAt(veneer4)->InstructionBits(), b(unbound));

  BufferOffset veneer5(pool_start_offset + 24);
  CHECK_EQUAL(masm.getInstructionAt(veneer5)->InstructionBits(), b(unbound));

  // Finally bind all labels.
  masm.bind(&cbz_lbl1);
  masm.bind(&cbz_lbl2);
  masm.bind(&cbz_lbl3);
  masm.bind(&tbz_lbl1);
  masm.bind(&tbz_lbl2);

  // Check all veneer branches are correctly bound.
  CHECK_EQUAL(masm.getInstructionAt(cbz1)->InstructionBits(),
              cbz(offset(cbz1, veneer1)));
  CHECK_EQUAL(masm.getInstructionAt(veneer1)->InstructionBits(),
              b(label_offset(veneer1, &cbz_lbl1)));

  CHECK_EQUAL(masm.getInstructionAt(cbz2)->InstructionBits(),
              cbz(offset(cbz2, veneer3)));
  CHECK_EQUAL(masm.getInstructionAt(veneer3)->InstructionBits(),
              b(label_offset(veneer3, &cbz_lbl2)));

  CHECK_EQUAL(masm.getInstructionAt(cbz3)->InstructionBits(),
              cbz(offset(cbz3, veneer5)));
  CHECK_EQUAL(masm.getInstructionAt(veneer5)->InstructionBits(),
              b(label_offset(veneer5, &cbz_lbl3)));

  CHECK_EQUAL(masm.getInstructionAt(tbz1)->InstructionBits(),
              tbz(tbz1_bitpos, offset(tbz1, veneer2)));
  CHECK_EQUAL(masm.getInstructionAt(veneer2)->InstructionBits(),
              b(label_offset(veneer2, &tbz_lbl1)));

  CHECK_EQUAL(masm.getInstructionAt(tbz2)->InstructionBits(),
              tbz(tbz2_bitpos, offset(tbz2, veneer4)));
  CHECK_EQUAL(masm.getInstructionAt(veneer4)->InstructionBits(),
              b(label_offset(veneer4, &tbz_lbl2)));

  return true;
}
END_TEST(testAssemblerBuffer_ARM64_ShortBranchVeneerExpiresTooFast)

BEGIN_TEST(testAssemblerBuffer_ARM64_ShortBranchSecondaryVeneer) {
  using namespace js::jit;
  using namespace AArch64;

  js::LifoAlloc lifo(4096, js::MallocArena);
  TempAllocator alloc(&lifo);
  JitContext jc(cx);
  StackMacroAssembler masm(cx, alloc);
  AutoCreatedBy acb(masm, __func__);

  auto rt = vixl::x1;

  auto tbz = std::bind_front(AArch64::tbz, rt);

  Label tbz_lbl1;
  Label tbz_lbln;
  Label cbz_lbln;

  BufferOffset tbz1(masm.currentOffset());
  unsigned tbz1_bitpos = 0;
  masm.Tbz(rt, tbz1_bitpos, &tbz_lbl1);

  // Emit more instructions than covered through the default hysteresis.
  constexpr int32_t tbz_count = js::jit::ShortRangeBranchHysteresis / 4;
  for (int32_t i = 0; i < tbz_count; ++i) {
    masm.Tbz(rt, 1, &tbz_lbln);
  }

  // Create additional short branches for a different range. The total number of
  // these branches must be larger than |tbz_count|. That way all tbz branches
  // are considered as "secondary veneers" in AssemblerBufferWithConstantPools,
  // cf. |AssemblerBufferWithConstantPools::sizeOfSecondaryVeneers()|.
  constexpr int32_t cbz_count = tbz_count + 10;
  for (int32_t i = 0; i < cbz_count; ++i) {
    masm.Cbz(rt, &cbz_lbln);
  }

  // tbz1 is unbound.
  CHECK_EQUAL(masm.getInstructionAt(tbz1)->InstructionBits(),
              tbz(tbz1_bitpos, unbound));

  // Compute deadline for tbz1 instruction.
  BufferOffset tbz_deadline1(
      tbz1.getOffset() +
      vixl::Instruction::ImmBranchMaxForwardOffset(vixl::TestBranchRangeType));

  // Instructions until deadline is reached.
  int32_t current = int32_t(masm.currentOffset());
  int32_t instr_until_deadline = (tbz_deadline1.getOffset() - current) / 4;

  // Total number of tbz instructions, plus a pool guard and pool header.
  int32_t tbz_and_pool_instr = 1 + tbz_count + 1 + 1;

  // Compute how many nops to insert until deadline is reached.
  int32_t nops = instr_until_deadline - tbz_and_pool_instr;

  // Insert nops.
  for (int32_t i = 0; i < nops; ++i) {
    masm.Nop();
  }

  // tbz1 is still unbound after emitting nops.
  CHECK_EQUAL(masm.getInstructionAt(tbz1)->InstructionBits(),
              tbz(tbz1_bitpos, unbound));

  BufferOffset nop_before_pool(masm.currentOffset() - 4);
  CHECK_EQUAL(masm.getInstructionAt(nop_before_pool)->InstructionBits(), nop());

  // AutoForbidPoolsAndNops triggers pool construction.
  {
    js::jit::AutoForbidPoolsAndNops afp(&masm, 1);

    masm.Nop();
  }

  BufferOffset nop_after_pool(masm.currentOffset() - 4);
  CHECK_EQUAL(masm.getInstructionAt(nop_after_pool)->InstructionBits(), nop());

  // Pool guard branch
  BufferOffset guard(nop_before_pool.getOffset() + 4);
  CHECK_EQUAL(masm.getInstructionAt(guard)->InstructionBits(),
              b(offset(guard, nop_after_pool)));

  // Pool header
  BufferOffset header(nop_before_pool.getOffset() + 8);
  CHECK_EQUAL(masm.getInstructionAt(header)->InstructionBits(), poolheader(1));

  // Veneer branch for tbz1.
  BufferOffset veneer1(nop_before_pool.getOffset() + 12);
  CHECK_EQUAL(masm.getInstructionAt(veneer1)->InstructionBits(), b(unbound));

  // tbz1 bound to the veneer branch.
  CHECK_EQUAL(masm.getInstructionAt(tbz1)->InstructionBits(),
              tbz(tbz1_bitpos, offset(tbz1, veneer1)));

  // Finally bind all labels.
  masm.bind(&tbz_lbl1);
  masm.bind(&tbz_lbln);
  masm.bind(&cbz_lbln);

  // Veneer branch bound to label tbz_lbl1.
  CHECK_EQUAL(masm.getInstructionAt(veneer1)->InstructionBits(),
              b(label_offset(veneer1, &tbz_lbl1)));

  return true;
}
END_TEST(testAssemblerBuffer_ARM64_ShortBranchSecondaryVeneer)

BEGIN_TEST(
    testAssemblerBuffer_ARM64_ShortBranchSecondaryVeneerRegisterDeadline) {
  using namespace js::jit;
  using namespace AArch64;

  js::LifoAlloc lifo(4096, js::MallocArena);
  TempAllocator alloc(&lifo);
  JitContext jc(cx);
  StackMacroAssembler masm(cx, alloc);
  AutoCreatedBy acb(masm, __func__);

  auto rt = vixl::x1;

  auto tbz = std::bind_front(AArch64::tbz, rt);

  Label tbz_lbl1;
  Label tbz_lbl2;
  Label cbz_lbl;

  BufferOffset tbz1(masm.currentOffset());
  unsigned tbz1_bitpos = 0;

  // Emit more instructions than covered through the default hysteresis.
  constexpr int32_t tbz_count = (js::jit::ShortRangeBranchHysteresis / 4) + 1;
  for (int32_t i = 0; i < tbz_count; ++i) {
    masm.Tbz(rt, tbz1_bitpos, &tbz_lbl1);
  }

  // Create the same number of short branches for a different range.
  constexpr int32_t cbz_count = tbz_count;
  for (int32_t i = 0; i < cbz_count; ++i) {
    masm.Cbz(rt, &cbz_lbl);
  }

  // tbz1 is unbound.
  CHECK_EQUAL(masm.getInstructionAt(tbz1)->InstructionBits(),
              tbz(tbz1_bitpos, unbound));

  // Compute deadline for tbz1 instruction.
  BufferOffset tbz_deadline1(
      tbz1.getOffset() +
      vixl::Instruction::ImmBranchMaxForwardOffset(vixl::TestBranchRangeType));

  // Instructions until deadline is reached.
  int32_t current = int32_t(masm.currentOffset());
  int32_t instr_until_deadline = (tbz_deadline1.getOffset() - current) / 4;

  // Total number of secondary veneers, plus a pool guard and pool header.
  //
  // There is the same number of tbz and cbz instructions, so the total number
  // of "secondary" veneers is the same, too.
  int32_t secondary_veneers_and_pool_instr = tbz_count + 1 + 1;

  // Compute how many instructions to insert until deadline is reached.
  int32_t nops = instr_until_deadline - secondary_veneers_and_pool_instr;

  // Insert nops.
  for (int32_t i = 0; i < nops; ++i) {
    masm.Nop();
  }
  BufferOffset after_last_nop(masm.currentOffset());

  // tbz1 is still unbound after emitting nops.
  CHECK_EQUAL(masm.getInstructionAt(tbz1)->InstructionBits(),
              tbz(tbz1_bitpos, unbound));

  // Create the final tbz instruction.
  unsigned tbz2_bitpos = 1;
  masm.Tbz(rt, tbz2_bitpos, &tbz_lbl2);

  // Ensure tbz2 is the last emitted instruction.
  //
  // This will trigger a pool generation through `nextInstrOffset()`.
  BufferOffset tbz2(masm.currentOffset() - 4);
  CHECK_EQUAL(masm.getInstructionAt(tbz2)->InstructionBits(),
              tbz(tbz2_bitpos, unbound));

  // Pool was added after the last nop.
  CHECK_EQUAL(masm.getInstructionAt(after_last_nop)->InstructionBits(),
              b(offset(after_last_nop, tbz2)));

  // Pool header
  BufferOffset header(after_last_nop.getOffset() + 4);
  CHECK_EQUAL(masm.getInstructionAt(header)->InstructionBits(), poolheader(1));

  // Finally bind all labels.
  masm.bind(&tbz_lbl1);
  masm.bind(&tbz_lbl2);
  masm.bind(&cbz_lbl);

  return true;
}
END_TEST(testAssemblerBuffer_ARM64_ShortBranchSecondaryVeneerRegisterDeadline)

BEGIN_TEST(testAssemblerBuffer_ARM64_BoundLabelBranchDeadline) {
  using namespace js::jit;
  using namespace AArch64;

  js::LifoAlloc lifo(4096, js::MallocArena);
  TempAllocator alloc(&lifo);
  JitContext jc(cx);
  StackMacroAssembler masm(cx, alloc);
  AutoCreatedBy acb(masm, __func__);

  auto rt = vixl::x1;

  auto tbz = std::bind_front(AArch64::tbz, rt);
  auto tbnz = std::bind_front(AArch64::tbnz, rt);

  // Like vixl::MacroAssembler::LabelIsOutOfRange, except that currentOffset()
  // instead of nextInstrOffset() is used. That ensures we don't accidentally
  // flush the constant pool.
  auto LabelIsOutOfRange = [&](Label* label, vixl::ImmBranchType branch_type) {
    int32_t diff = int32_t(masm.currentOffset()) - label->offset();
    return !Instruction::IsValidImmPCOffset(branch_type, diff / 4);
  };

  Label tbz_lbl1, tbz_lbl2, tbz_lbl3;

  // Bind tbz_lbl3 to the start.
  masm.bind(&tbz_lbl3);

  BufferOffset tbz1(masm.currentOffset());
  unsigned tbz1_bitpos = 12;
  masm.Tbz(rt, tbz1_bitpos, &tbz_lbl1);

  // Add some additional Tbz to ensure we have enough veneers to make the pool
  // large enough that the last Tbz for |tbz_lbl3| gets out of range.
  for (int32_t i = 0; i < 10; ++i) {
    masm.Tbz(rt, 0, &tbz_lbl2);
  }

  // Compute deadline for |tbz1|.
  BufferOffset tbz_deadline1(
      tbz1.getOffset() +
      vixl::Instruction::ImmBranchMaxForwardOffset(vixl::TestBranchRangeType));

  // Instructions until deadline is reached.
  int32_t current = int32_t(masm.currentOffset());
  int32_t instr_until_deadline = (tbz_deadline1.getOffset() - current) / 4;

  // Compute how many nops to insert until deadline is reached, excluding the
  // pool guard and pool header.
  int32_t nops = instr_until_deadline - 2;

  // Insert nops.
  for (int32_t i = 0; i < nops; ++i) {
    masm.Nop();
  }
  int32_t pool_start_offset = masm.currentOffset();

  // tbz1 is unbound.
  CHECK_EQUAL(masm.getInstructionAt(tbz1)->InstructionBits(),
              tbz(tbz1_bitpos, unbound));

  // tbz_lbl3 is still in range.
  CHECK_EQUAL(LabelIsOutOfRange(&tbz_lbl3, vixl::TestBranchType), false);

  // Emit Tbz. This should trigger pool construction, because tbz1 is about to
  // get out of range.
  unsigned tbz3_bitpos = 15;
  masm.Tbz(rt, tbz3_bitpos, &tbz_lbl3);
  BufferOffset after_tbz3(masm.currentOffset());

  // Unconditional branch to |tbz_lbl3|.
  BufferOffset uncondBranch(after_tbz3.getOffset() - 4);
  CHECK_EQUAL(masm.getInstructionAt(uncondBranch)->InstructionBits(),
              b(label_offset(uncondBranch, &tbz_lbl3)));

  // Tbz was inverted to Tbnz.
  BufferOffset tbnz1(after_tbz3.getOffset() - 8);
  CHECK_EQUAL(masm.getInstructionAt(tbnz1)->InstructionBits(),
              tbnz(tbz3_bitpos, offset(tbnz1, after_tbz3)));

  // Pool guard branch
  BufferOffset guard(pool_start_offset);
  CHECK_EQUAL(masm.getInstructionAt(guard)->InstructionBits(),
              b(offset(guard, tbnz1)));

  // Pool header
  BufferOffset header(pool_start_offset + 4);
  CHECK_EQUAL(masm.getInstructionAt(header)->InstructionBits(), poolheader(1));

  // Veneer branches
  BufferOffset veneer1(pool_start_offset + 8);
  CHECK_EQUAL(masm.getInstructionAt(veneer1)->InstructionBits(), b(unbound));

  // + 10 more veneer branches for |tbz_lbl2| (not checked).

  // Finally bind all labels.
  masm.bind(&tbz_lbl1);
  masm.bind(&tbz_lbl2);

  // Check veneer branch for |tbz_lbl1| is correctly bound.
  CHECK_EQUAL(masm.getInstructionAt(tbz1)->InstructionBits(),
              tbz(tbz1_bitpos, offset(tbz1, veneer1)));
  CHECK_EQUAL(masm.getInstructionAt(veneer1)->InstructionBits(),
              b(label_offset(veneer1, &tbz_lbl1)));

  return true;
}
END_TEST(testAssemblerBuffer_ARM64_BoundLabelBranchDeadline)
#endif /* JS_CODEGEN_ARM64 */
