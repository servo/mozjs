/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jit_GraphSpewer_h
#define jit_GraphSpewer_h

#ifdef JS_JITSPEW

#  include "js/TypeDecls.h"
#  include "vm/JSONPrinter.h"
#  include "wasm/WasmTypeDef.h"

namespace js {
namespace jit {

class BacktrackingAllocator;
class MDefinition;
class MIRGraph;
class MResumePoint;
class LNode;

class GraphSpewer : JSONPrinter {
 private:
  // Optional; wasm type information to be used when dumping MIR nodes.
  const wasm::CodeMetadata* wasmCodeMeta_;

  void beginPass(const char* pass);
  void spewMDef(MDefinition* def);
  void spewMResumePoint(MResumePoint* rp);
  void spewMIR(MIRGraph* mir);
  void spewLIns(LNode* ins);
  void spewLIR(MIRGraph* mir);
  void spewRanges(BacktrackingAllocator* regalloc);
  void endPass();

 public:
  explicit GraphSpewer(GenericPrinter& out,
                       const wasm::CodeMetadata* wasmCodeMeta = nullptr)
      : JSONPrinter(out, /*indent*/ false), wasmCodeMeta_(wasmCodeMeta) {}

  void begin();
  void beginFunction(JSScript* script);
  void beginWasmFunction(unsigned funcIndex);
  void beginAnonFunction();
  void spewPass(const char* pass, MIRGraph* graph,
                BacktrackingAllocator* ra = nullptr);
  void endFunction();
  void end();
};

using UniqueGraphSpewer = UniquePtr<GraphSpewer>;

}  // namespace jit
}  // namespace js

#endif /* JS_JITSPEW */

#endif /* jit_GraphSpewer_h */
