/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "jit/EffectiveAddressAnalysis.h"

#include "jit/IonAnalysis.h"
#include "jit/MIR-wasm.h"
#include "jit/MIR.h"
#include "jit/MIRGenerator.h"
#include "jit/MIRGraph.h"

using namespace js;
using namespace jit;

// This is a very simple pass that tries to merge 32-bit shift-and-add into a
// single MIR node.  It results from a lot of experimentation with more
// aggressive load-effective-address formation, as documented in bug 1970035.
//
// This implementation only covers the two-addend form
// `base + (index << {1,2,3})` (and the same the other way around).  Previous
// experimentation showed that, while the 3-addend form
// `base + (index << {1,2,3}) + constant` can be reliably identified and merged
// into a single node, it doesn't reliably produce faster code.  Also, the
// implementation complexity is much higher than what is below.
//
// 3-addend LEAs can be completed in a single cycle on high-end Intels, but
// take 2 cycles on lower end Intels.  By comparison the 2-addend form is
// believed to take a single cycle on all Intels.  On arm64, the 3-addend form
// is not supported in a single machine instruction, and so can require zero,
// one or two extra instructions, depending on the size of the constant,
// possibly an extra register, and consequently some number of extra cycles.
//
// Because of this, restricting the transformation to the 2-addend case
// simplifies both the implementation and more importantly the cost-tradeoff
// landscape.  It gains much of the wins of the 3-addend case while more
// reliably producing nodes that can execute in a single cycle on all primary
// targets.

// =====================================================================

// On non-x86/x64 targets, incorporating any non-zero constant (displacement)
// in an EffectiveAddress2 node is not free, because the constant may have to
// be synthesised into a register in the back end.  Worse, on all such targets,
// arbitrary 32-bit constants will take two instructions to synthesise, which
// can lead to a net performance loss.
//
// `OffsetIsSmallEnough` is used in the logic below to restrict constants to
// single-instruction forms.  It is necessarily target-dependent.  Note this is
// merely a heuristic -- the resulting code should be *correct* on all targets
// regardless of the value returned.

static bool OffsetIsSmallEnough(int32_t imm) {
#if defined(JS_CODEGEN_X86) || defined(JS_CODEGEN_X64)
  // For x86_32 and x86_64 we have the luxury of being able to roll in any
  // 32-bit `imm` value for free.
  return true;
#elif defined(JS_CODEGEN_ARM64) || defined(JS_CODEGEN_ARM)
  // On arm64, this can be synthesised in one insn as `movz #imm` or
  // `movn #imm`.  arm32 is similar.
  return imm >= -0xFFFF && imm <= 0xFFFF;
#elif defined(JS_CODEGEN_RISCV64) || defined(JS_CODEGEN_LOONG64) || \
    defined(JS_CODEGEN_MIPS64)
  return imm >= -0xFFF && imm <= 0xFFF;
#elif defined(JS_CODEGEN_WASM32) || defined(JS_CODEGEN_NONE)
  return true;
#else
#  error "This needs to be filled in for your platform"
#endif
}

// If `def` is of the form `x << {1,2,3}`, return `x` and the shift value.
// Otherwise return the pair `(nullptr, 0)`.
static std::pair<MDefinition*, int32_t> IsShiftBy123(MDefinition* def) {
  MOZ_ASSERT(def->type() == MIRType::Int32);
  if (!def->isLsh()) {
    return std::pair(nullptr, 0);
  }
  MLsh* lsh = def->toLsh();
  if (lsh->isRecoveredOnBailout()) {
    return std::pair(nullptr, 0);
  }
  MDefinition* shamt = lsh->rhs();
  MOZ_ASSERT(shamt->type() == MIRType::Int32);
  MConstant* con = shamt->maybeConstantValue();
  if (!con || con->toInt32() < 1 || con->toInt32() > 3) {
    return std::pair(nullptr, 0);
  }
  return std::pair(lsh->lhs(), con->toInt32());
}

// Try to convert `base + (index << {1,2,3})` into either an MEffectiveAddress2
// node (if base is a constant) or an MEffectiveAddress3 node with zero
// displacement (if base is non-constant).
static void TryMatchShiftAdd(TempAllocator& alloc, MAdd* root) {
  MOZ_ASSERT(root->isAdd());
  MOZ_ASSERT(root->type() == MIRType::Int32);
  MOZ_ASSERT(root->hasUses());

  // Try to match
  //
  //   base + (index << {1,2,3})
  //
  // in which the addends can appear in either order.  Obviously the shift
  // amount must be a constant, but `base` and `index` can be anything.

  MDefinition* base = nullptr;
  MDefinition* index = nullptr;
  int32_t shift = 0;

  auto pair = IsShiftBy123(root->rhs());
  MOZ_ASSERT((pair.first == nullptr) == (pair.second == 0));
  if (pair.first) {
    base = root->lhs();
    index = pair.first;
    shift = pair.second;
  } else {
    pair = IsShiftBy123(root->lhs());
    MOZ_ASSERT((pair.first == nullptr) == (pair.second == 0));
    if (pair.first) {
      base = root->rhs();
      index = pair.first;
      shift = pair.second;
    }
  }

  if (!base) {
    return;
  }
  MOZ_ASSERT(shift >= 1 && shift <= 3);

  // IsShiftBy123 ensures that the MLsh node is not `recoveredOnBailout`, and
  // this test takes care of the MAdd node.
  if (root->isRecoveredOnBailout()) {
    return;
  }

  // Pattern matching succeeded.
  Scale scale = ShiftToScale(shift);
  MOZ_ASSERT(scale != TimesOne);

  MInstruction* replacement = nullptr;
  if (base->maybeConstantValue()) {
    int32_t baseValue = base->maybeConstantValue()->toInt32();
    if (baseValue == 0) {
      // We'd only be rolling one operation -- the shift -- into the result, so
      // don't bother.
      return;
    }
    if (!OffsetIsSmallEnough(baseValue)) {
      // `baseValue` would take more than one insn to get into a register,
      // which makes the change less likely to be a win.  See bug 1979829.
      return;
    }
    replacement = MEffectiveAddress2::New(alloc, index, scale, baseValue);
  } else {
    replacement = MEffectiveAddress3::New(alloc, base, index, scale, 0);
  }

  root->replaceAllUsesWith(replacement);
  root->block()->insertAfter(root, replacement);

  if (JitSpewEnabled(JitSpew_EAA)) {
    AutoJitSpewMessage msg(JitSpew_EAA, "  create: '");
    DumpMIRDefinition(msg.printer(), replacement, /*showDetails=*/false);
    msg.append("'");
  }
}

// =====================================================================
//
// Top level driver.

bool EffectiveAddressAnalysis::analyze() {
  JitSpew(JitSpew_EAA, "Begin");

  for (ReversePostorderIterator block(graph_.rpoBegin());
       block != graph_.rpoEnd(); block++) {
    // Traverse backwards through `block`, trying to rewrite each MIR node if
    // we can.  Rewriting may cause nodes to become dead.  We do not try to
    // remove those here, but leave them for a later DCE pass to clear up.

    MInstructionReverseIterator ri(block->rbegin());
    while (ri != block->rend()) {
      // Nodes are added immediately after `curr`, so the iterator won't
      // traverse them, since we're iterating backwards.
      MInstruction* curr = *ri;
      ri++;

      if (MOZ_LIKELY(!curr->isAdd())) {
        continue;
      }
      if (curr->type() != MIRType::Int32 || !curr->hasUses()) {
        continue;
      }

      // This check needs to precede any allocation done in this loop.
      if (MOZ_UNLIKELY(!graph_.alloc().ensureBallast())) {
        return false;
      }

      TryMatchShiftAdd(graph_.alloc(), curr->toAdd());
    }
  }

  JitSpew(JitSpew_EAA, "End");
  return true;
}
