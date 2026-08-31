/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jit_x64_LIR_x64_h
#define jit_x64_LIR_x64_h

namespace js {
namespace jit {

// Given an untyped input, guards on whether it's a specific type and returns
// the unboxed payload.
class LUnbox : public LInstructionHelper<1, BOX_PIECES, 0> {
 public:
  LIR_HEADER(Unbox)

  explicit LUnbox(const LAllocation& input) : LInstructionHelper(classOpcode) {
    setOperand(0, input);
  }

  static const size_t Input = 0;

  LBoxAllocation input() const { return getBoxOperand(Input); }

  MUnbox* mir() const { return mir_->toUnbox(); }
  const char* extraName() const { return StringFromMIRType(mir()->type()); }
};

}  // namespace jit
}  // namespace js

#endif /* jit_x64_LIR_x64_h */
