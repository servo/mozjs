/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifdef JS_JITSPEW

#  include "jit/GraphSpewer.h"

#  include "jit/BacktrackingAllocator.h"
#  include "jit/LIR.h"
#  include "jit/MIR.h"
#  include "jit/MIRGraph.h"
#  include "jit/RangeAnalysis.h"
#  include "wasm/WasmMetadata.h"

using namespace js;
using namespace js::jit;

static constexpr uint32_t IonGraphVersion = 1;

// Hash pointers to make them smaller, while still (probably) unique.
static uint32_t HashedPointer(const void* pointer) {
  return mozilla::HashGeneric((uintptr_t)pointer);
}

void GraphSpewer::begin() {
  beginObject();
  property("version", IonGraphVersion);
  beginListProperty("functions");
}

void GraphSpewer::beginFunction(JSScript* script) {
  beginObject();
  formatProperty("name", "%s:%u", script->filename(), script->lineno());
  beginListProperty("passes");
}

void GraphSpewer::beginWasmFunction(unsigned funcIndex) {
  beginObject();
  formatProperty("name", "wasm-func%u", funcIndex);
  beginListProperty("passes");
}

void GraphSpewer::beginAnonFunction() {
  beginObject();
  property("name", "unknown");
  beginListProperty("passes");
}

void GraphSpewer::spewPass(const char* pass, MIRGraph* graph,
                           BacktrackingAllocator* ra) {
  beginPass(pass);
  spewMIR(graph);
  spewLIR(graph);
  if (ra) {
    spewRanges(ra);
  }
  endPass();
}

void GraphSpewer::beginPass(const char* pass) {
  beginObject();
  property("name", pass);
}

void GraphSpewer::spewMResumePoint(MResumePoint* rp) {
  if (!rp) {
    return;
  }

  beginObjectProperty("resumePoint");

  if (rp->caller()) {
    property("caller", rp->caller()->block()->id());
  }

  property("mode", ResumeModeToString(rp->mode()));

  beginListProperty("operands");
  for (MResumePoint* iter = rp; iter; iter = iter->caller()) {
    for (int i = iter->numOperands() - 1; i >= 0; i--) {
      value(iter->getOperand(i)->id());
    }
    if (iter->caller()) {
      value("|");
    }
  }
  endList();

  endObject();
}

void GraphSpewer::spewMDef(MDefinition* def) {
  beginObject();

  property("ptr", HashedPointer(def));
  property("id", def->id());

  propertyName("opcode");
  out_.printf("\"");
  def->printOpcode(out_);
  out_.printf("\"");

  beginListProperty("attributes");
#  define OUTPUT_ATTRIBUTE(X)      \
    do {                           \
      if (def->is##X()) value(#X); \
    } while (0);
  MIR_FLAG_LIST(OUTPUT_ATTRIBUTE);
#  undef OUTPUT_ATTRIBUTE
  endList();

  beginListProperty("inputs");
  for (size_t i = 0, e = def->numOperands(); i < e; i++) {
    value(def->getOperand(i)->id());
  }
  endList();

  beginListProperty("uses");
  for (MUseDefIterator use(def); use; use++) {
    value(use.def()->id());
  }
  endList();

  if (!def->isLowered()) {
    beginListProperty("memInputs");
    if (def->dependency()) {
      value(def->dependency()->id());
    }
    endList();
  }

  bool isTruncated = false;
  if (def->isAdd() || def->isSub() || def->isMod() || def->isMul() ||
      def->isDiv()) {
    isTruncated = static_cast<MBinaryArithInstruction*>(def)->isTruncated();
  }

  beginStringProperty("type");
  if (def->type() != MIRType::None && def->range()) {
    def->range()->dump(out_);
    out_.printf(": ");
  }
  if (def->wasmRefType().isSome()) {
    out_.printf("%s: ",
                wasm::ToString(def->wasmRefType(), wasmCodeMeta_->types).get());
  }
  out_.printf("%s", StringFromMIRType(def->type()));
  if (isTruncated) {
    out_.printf(" (t)");
  }
  endStringProperty();

  if (def->isInstruction()) {
    if (MResumePoint* rp = def->toInstruction()->resumePoint()) {
      spewMResumePoint(rp);
    }
  }

  endObject();
}

void GraphSpewer::spewMIR(MIRGraph* mir) {
  beginObjectProperty("mir");
  beginListProperty("blocks");

  for (MBasicBlockIterator block(mir->begin()); block != mir->end(); block++) {
    beginObject();

    property("ptr", HashedPointer(*block));
    property("id", block->id());
    property("loopDepth", block->loopDepth());

    beginListProperty("attributes");
    if (block->hasLastIns()) {
      if (block->isLoopBackedge()) {
        value("backedge");
      }
      if (block->isLoopHeader()) {
        value("loopheader");
      }
      if (block->isSplitEdge()) {
        value("splitedge");
      }
      if (*block == mir->osrBlock()) {
        value("osr");
      }
    }
    endList();

    beginListProperty("predecessors");
    for (size_t i = 0; i < block->numPredecessors(); i++) {
      value(block->getPredecessor(i)->id());
    }
    endList();

    beginListProperty("successors");
    if (block->hasLastIns()) {
      for (size_t i = 0; i < block->numSuccessors(); i++) {
        value(block->getSuccessor(i)->id());
      }
    }
    endList();

    beginListProperty("instructions");
    for (MPhiIterator phi(block->phisBegin()); phi != block->phisEnd(); phi++) {
      spewMDef(*phi);
    }
    for (MInstructionIterator i(block->begin()); i != block->end(); i++) {
      spewMDef(*i);
    }
    endList();

    spewMResumePoint(block->entryResumePoint());

    endObject();
  }

  endList();
  endObject();
}

void GraphSpewer::spewLIns(LNode* ins) {
  beginObject();

  property("ptr", HashedPointer(ins));
  property("id", ins->id());
  if (ins->mirRaw()) {
    property("mirPtr", HashedPointer(ins->mirRaw()));
  } else {
    nullProperty("mirPtr");
  }

  propertyName("opcode");
  out_.printf("\"");
  ins->dump(out_);
  out_.printf("\"");

  beginListProperty("defs");
  for (size_t i = 0; i < ins->numDefs(); i++) {
    if (ins->isPhi()) {
      value(ins->toPhi()->getDef(i)->virtualRegister());
    } else {
      value(ins->toInstruction()->getDef(i)->virtualRegister());
    }
  }
  endList();

  endObject();
}

void GraphSpewer::spewLIR(MIRGraph* mir) {
  beginObjectProperty("lir");
  beginListProperty("blocks");

  for (MBasicBlockIterator i(mir->begin()); i != mir->end(); i++) {
    LBlock* block = i->lir();
    if (!block) {
      continue;
    }

    beginObject();
    property("id", i->id());
    property("ptr", HashedPointer(*i));

    beginListProperty("instructions");
    for (size_t p = 0; p < block->numPhis(); p++) {
      spewLIns(block->getPhi(p));
    }
    for (LInstructionIterator ins(block->begin()); ins != block->end(); ins++) {
      spewLIns(*ins);
    }
    endList();

    endObject();
  }

  endList();
  endObject();
}

void GraphSpewer::spewRanges(BacktrackingAllocator* regalloc) {
  beginObjectProperty("ranges");
  beginListProperty("blocks");

  for (size_t bno = 0; bno < regalloc->graph.numBlocks(); bno++) {
    beginObject();
    property("number", bno);
    beginListProperty("vregs");

    LBlock* lir = regalloc->graph.getBlock(bno);
    for (LInstructionIterator ins = lir->begin(); ins != lir->end(); ins++) {
      for (size_t k = 0; k < ins->numDefs(); k++) {
        uint32_t id = ins->getDef(k)->virtualRegister();
        VirtualRegister* vreg = &regalloc->vregs[id];

        beginObject();
        property("vreg", id);
        beginListProperty("ranges");

        for (VirtualRegister::RangeIterator iter(*vreg); iter; iter++) {
          LiveRange* range = *iter;

          beginObject();
          property("allocation",
                   range->bundle()->allocation().toString().get());
          property("start", range->from().bits());
          property("end", range->to().bits());
          endObject();
        }

        endList();
        endObject();
      }
    }

    endList();
    endObject();
  }

  endList();
  endObject();
}

void GraphSpewer::endPass() { endObject(); }

void GraphSpewer::endFunction() {
  endList();
  endObject();
}

void GraphSpewer::end() {
  endList();
  endObject();
}

#endif /* JS_JITSPEW */
