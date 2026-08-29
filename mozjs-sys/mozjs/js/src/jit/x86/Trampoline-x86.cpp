/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "jit/Bailouts.h"
#include "jit/BaselineFrame.h"
#include "jit/BaselineJIT.h"
#include "jit/CalleeToken.h"
#include "jit/JitFrames.h"
#include "jit/JitRuntime.h"
#include "jit/JitSpewer.h"
#include "jit/PerfSpewer.h"
#include "jit/VMFunctions.h"
#include "jit/x86/SharedICHelpers-x86.h"
#include "vm/JitActivation.h"  // js::jit::JitActivation
#include "vm/JSContext.h"
#include "vm/Realm.h"
#ifdef MOZ_VTUNE
#  include "vtune/VTuneWrapper.h"
#endif

#include "jit/MacroAssembler-inl.h"
#include "vm/JSScript-inl.h"

using namespace js;
using namespace js::jit;

// All registers to save and restore. This includes the stack pointer, since we
// use the ability to reference register values on the stack by index.
static const LiveRegisterSet AllRegs =
    LiveRegisterSet(GeneralRegisterSet(Registers::AllMask),
                    FloatRegisterSet(FloatRegisters::AllMask));

enum EnterJitEbpArgumentOffset {
  ARG_JITCODE = 2 * sizeof(void*),
  ARG_ARGC = 3 * sizeof(void*),
  ARG_ARGV = 4 * sizeof(void*),
  ARG_STACKFRAME = 5 * sizeof(void*),
  ARG_CALLEETOKEN = 6 * sizeof(void*),
  ARG_SCOPECHAIN = 7 * sizeof(void*),
  ARG_STACKVALUES = 8 * sizeof(void*),
  ARG_RESULT = 9 * sizeof(void*)
};

// Generates a trampoline for calling Jit compiled code from a C++ function.
// The trampoline use the EnterJitCode signature, with the standard cdecl
// calling convention.
void JitRuntime::generateEnterJIT(JSContext* cx, MacroAssembler& masm) {
  AutoCreatedBy acb(masm, "JitRuntime::generateEnterJIT");

  enterJITOffset_ = startTrampolineCode(masm);

  masm.assertStackAlignment(ABIStackAlignment,
                            -int32_t(sizeof(uintptr_t)) /* return address */);

  // Save old stack frame pointer, set new stack frame pointer.
  masm.push(ebp);
  masm.movl(esp, ebp);

  // Save non-volatile registers. These must be saved by the trampoline,
  // rather than the JIT'd code, because they are scanned by the conservative
  // scanner.
  masm.push(ebx);
  masm.push(esi);
  masm.push(edi);

  Register reg_argc = eax;
  masm.loadPtr(Address(ebp, ARG_ARGC), reg_argc);

  Register reg_argv = ebx;
  masm.loadPtr(Address(ebp, ARG_ARGV), reg_argv);

  Register reg_token = edx;
  masm.loadPtr(Address(ebp, ARG_CALLEETOKEN), reg_token);

  generateEnterJitShared(masm, reg_argc, reg_argv, reg_token, ecx, esi, edi);

  // Push the descriptor.
  masm.mov(Operand(ebp, ARG_RESULT), eax);
  masm.unboxInt32(Address(eax, 0x0), eax);
  masm.pushFrameDescriptorForJitCall(FrameType::CppToJSJit, eax, eax);

  // Load the InterpreterFrame address into the OsrFrameReg.
  // This address is also used for setting the constructing bit on all paths.
  masm.loadPtr(Address(ebp, ARG_STACKFRAME), OsrFrameReg);

  CodeLabel returnLabel;
  Label oomReturnLabel;
  {
    // Handle Interpreter -> Baseline OSR.
    AllocatableGeneralRegisterSet regs(GeneralRegisterSet::All());
    MOZ_ASSERT(!regs.has(ebp));
    regs.take(OsrFrameReg);
    regs.take(ReturnReg);

    Register scratch = regs.takeAny();

    Label notOsr;
    masm.branchTestPtr(Assembler::Zero, OsrFrameReg, OsrFrameReg, &notOsr);

    Register numStackValues = regs.takeAny();
    masm.loadPtr(Address(ebp, ARG_STACKVALUES), numStackValues);

    Register jitcode = regs.takeAny();
    masm.loadPtr(Address(ebp, ARG_JITCODE), jitcode);

    // Push return address.
    masm.mov(&returnLabel, scratch);
    masm.push(scratch);

    // Frame prologue.
    masm.push(ebp);
    masm.mov(esp, ebp);

    // Reserve frame.
    masm.subPtr(Imm32(BaselineFrame::Size()), esp);

    Register framePtrScratch = regs.takeAny();
    masm.touchFrameValues(numStackValues, scratch, framePtrScratch);
    masm.mov(esp, framePtrScratch);

    // Reserve space for locals and stack values.
    masm.mov(numStackValues, scratch);
    masm.shll(Imm32(3), scratch);
    masm.subPtr(scratch, esp);

    // Enter exit frame.
    masm.push(FrameDescriptor(FrameType::BaselineJS));
    masm.push(Imm32(0));  // Fake return address.
    masm.push(FramePointer);
    // No GC things to mark on the stack, push a bare token.
    masm.loadJSContext(scratch);
    masm.enterFakeExitFrame(scratch, scratch, ExitFrameType::Bare);

    masm.push(jitcode);

    using Fn = void (*)(BaselineFrame* frame, InterpreterFrame* interpFrame,
                        uint32_t numStackValues);
    masm.setupUnalignedABICall(scratch);
    masm.passABIArg(framePtrScratch);  // BaselineFrame
    masm.passABIArg(OsrFrameReg);      // InterpreterFrame
    masm.passABIArg(numStackValues);
    masm.callWithABI<Fn, jit::InitBaselineFrameForOsr>(
        ABIType::General, CheckUnsafeCallWithABI::DontCheckHasExitFrame);

    masm.pop(jitcode);

    MOZ_ASSERT(jitcode != ReturnReg);

    masm.addPtr(Imm32(ExitFrameLayout::SizeWithFooter()), esp);

    // If OSR-ing, then emit instrumentation for setting lastProfilerFrame
    // if profiler instrumentation is enabled.
    {
      Label skipProfilingInstrumentation;
      AbsoluteAddress addressOfEnabled(
          cx->runtime()->geckoProfiler().addressOfEnabled());
      masm.branch32(Assembler::Equal, addressOfEnabled, Imm32(0),
                    &skipProfilingInstrumentation);
      masm.profilerEnterFrame(ebp, scratch);
      masm.bind(&skipProfilingInstrumentation);
    }

    masm.jump(jitcode);

    masm.bind(&notOsr);
    masm.loadPtr(Address(ebp, ARG_SCOPECHAIN), R1.scratchReg());
  }

  // The call will push the return address and frame pointer on the stack, thus
  // we check that the stack would be aligned once the call is complete.
  masm.assertStackAlignment(JitStackAlignment, 2 * sizeof(uintptr_t));

  /***************************************************************
      Call passed-in code, get return value and fill in the
      passed in return value pointer
  ***************************************************************/
  masm.call(Address(ebp, ARG_JITCODE));

  {
    // Interpreter -> Baseline OSR will return here.
    masm.bind(&returnLabel);
    masm.addCodeLabel(returnLabel);
    masm.bind(&oomReturnLabel);
  }

  // Restore the stack pointer so the stack looks like this:
  //  +20 ... arguments ...
  //  +16 <return>
  //  +12 ebp <- %ebp pointing here.
  //  +8  ebx
  //  +4  esi
  //  +0  edi <- %esp pointing here.
  masm.lea(Operand(ebp, -int32_t(3 * sizeof(void*))), esp);

  // Store the return value.
  masm.loadPtr(Address(ebp, ARG_RESULT), eax);
  masm.storeValue(JSReturnOperand, Operand(eax, 0));

  /**************************************************************
      Return stack and registers to correct state
  **************************************************************/

  // Restore non-volatile registers
  masm.pop(edi);
  masm.pop(esi);
  masm.pop(ebx);

  // Restore old stack frame pointer
  masm.pop(ebp);
  masm.ret();
}

// static
mozilla::Maybe<::JS::ProfilingFrameIterator::RegisterState>
JitRuntime::getCppEntryRegisters(JitFrameLayout* frameStackAddress) {
  // Not supported, or not implemented yet.
  // TODO: Implement along with the corresponding stack-walker changes, in
  // coordination with the Gecko Profiler, see bug 1635987 and follow-ups.
  return mozilla::Nothing{};
}

// Push AllRegs in a way that is compatible with RegisterDump, regardless of
// what PushRegsInMask might do to reduce the set size.
static void DumpAllRegs(MacroAssembler& masm) {
#ifdef ENABLE_WASM_SIMD
  masm.PushRegsInMask(AllRegs);
#else
  // When SIMD isn't supported, PushRegsInMask reduces the set of float
  // registers to be double-sized, while the RegisterDump expects each of
  // the float registers to have the maximal possible size
  // (Simd128DataSize). To work around this, we just spill the double
  // registers by hand here, using the register dump offset directly.
  for (GeneralRegisterBackwardIterator iter(AllRegs.gprs()); iter.more();
       ++iter) {
    masm.Push(*iter);
  }

  masm.reserveStack(sizeof(RegisterDump::FPUArray));
  for (FloatRegisterBackwardIterator iter(AllRegs.fpus()); iter.more();
       ++iter) {
    FloatRegister reg = *iter;
    Address spillAddress(StackPointer, reg.getRegisterDumpOffsetInBytes());
    masm.storeDouble(reg, spillAddress);
  }
#endif
}

void JitRuntime::generateInvalidator(MacroAssembler& masm, Label* bailoutTail) {
  AutoCreatedBy acb(masm, "JitRuntime::generateInvalidator");

  invalidatorOffset_ = startTrampolineCode(masm);

  // We do the minimum amount of work in assembly and shunt the rest
  // off to InvalidationBailout. Assembly does:
  //
  // - Push the machine state onto the stack.
  // - Call the InvalidationBailout routine with the stack pointer.
  // - Now that the frame has been bailed out, convert the invalidated
  //   frame into an exit frame.
  // - Do the normal check-return-code-and-thunk-to-the-interpreter dance.

  // Push registers such that we can access them from [base + code].
  DumpAllRegs(masm);

  masm.movl(esp, eax);  // Argument to jit::InvalidationBailout.

  // Make space for InvalidationBailout's bailoutInfo outparam.
  masm.reserveStack(sizeof(void*));
  masm.movl(esp, ebx);

  using Fn = bool (*)(InvalidationBailoutStack* sp, BaselineBailoutInfo** info);
  masm.setupUnalignedABICall(edx);
  masm.passABIArg(eax);
  masm.passABIArg(ebx);
  masm.callWithABI<Fn, InvalidationBailout>(
      ABIType::General, CheckUnsafeCallWithABI::DontCheckOther);

  masm.pop(ecx);  // Get bailoutInfo outparam.

  // Pop the machine state and the dead frame.
  masm.moveToStackPtr(FramePointer);

  // Jump to shared bailout tail. The BailoutInfo pointer has to be in ecx.
  masm.jmp(bailoutTail);
}

static void PushBailoutFrame(MacroAssembler& masm, Register spArg) {
  // Push registers such that we can access them from [base + code].
  DumpAllRegs(masm);

  // The current stack pointer is the first argument to jit::Bailout.
  masm.movl(esp, spArg);
}

static void GenerateBailoutThunk(MacroAssembler& masm, Label* bailoutTail) {
  PushBailoutFrame(masm, eax);

  // Make space for Bailout's bailoutInfo outparam.
  masm.reserveStack(sizeof(void*));
  masm.movl(esp, ebx);

  // Call the bailout function.
  using Fn = bool (*)(BailoutStack* sp, BaselineBailoutInfo** info);
  masm.setupUnalignedABICall(ecx);
  masm.passABIArg(eax);
  masm.passABIArg(ebx);
  masm.callWithABI<Fn, Bailout>(ABIType::General,
                                CheckUnsafeCallWithABI::DontCheckOther);

  masm.pop(ecx);  // Get the bailoutInfo outparam.

  // Remove both the bailout frame and the topmost Ion frame's stack.
  masm.moveToStackPtr(FramePointer);

  // Jump to shared bailout tail. The BailoutInfo pointer has to be in ecx.
  masm.jmp(bailoutTail);
}

void JitRuntime::generateBailoutHandler(MacroAssembler& masm,
                                        Label* bailoutTail) {
  AutoCreatedBy acb(masm, "JitRuntime::generateBailoutHandler");

  bailoutHandlerOffset_ = startTrampolineCode(masm);

  GenerateBailoutThunk(masm, bailoutTail);
}

bool JitRuntime::generateVMWrapper(JSContext* cx, MacroAssembler& masm,
                                   VMFunctionId id, const VMFunctionData& f,
                                   DynFn nativeFun, uint32_t* wrapperOffset) {
  AutoCreatedBy acb(masm, "JitRuntime::generateVMWrapper");

  *wrapperOffset = startTrampolineCode(masm);

  // Avoid conflicts with argument registers while discarding the result after
  // the function call.
  AllocatableGeneralRegisterSet regs(Register::Codes::WrapperMask);

  static_assert(
      (Register::Codes::VolatileMask & ~Register::Codes::WrapperMask) == 0,
      "Wrapper register set must be a superset of Volatile register set.");

  // The context is the first argument.
  Register cxreg = regs.takeAny();

  // Stack is:
  //    ... frame ...
  //  +8  [args]
  //  +4  descriptor
  //  +0  returnAddress
  //
  // Push the frame pointer to finish the exit frame, then link it up.
  masm.Push(FramePointer);
  masm.moveStackPtrTo(FramePointer);
  masm.loadJSContext(cxreg);
  masm.enterExitFrame(cxreg, regs.getAny(), id);

  // Reserve space for the outparameter.
  masm.reserveVMFunctionOutParamSpace(f);

  masm.setupUnalignedABICallDontSaveRestoreSP();
  masm.passABIArg(cxreg);

  size_t argDisp = ExitFrameLayout::Size();

  // Copy arguments.
  for (uint32_t explicitArg = 0; explicitArg < f.explicitArgs; explicitArg++) {
    switch (f.argProperties(explicitArg)) {
      case VMFunctionData::WordByValue:
        masm.passABIArg(MoveOperand(FramePointer, argDisp), ABIType::General);
        argDisp += sizeof(void*);
        break;
      case VMFunctionData::DoubleByValue:
        // We don't pass doubles in float registers on x86, so no need
        // to check for argPassedInFloatReg.
        masm.passABIArg(MoveOperand(FramePointer, argDisp), ABIType::General);
        argDisp += sizeof(void*);
        masm.passABIArg(MoveOperand(FramePointer, argDisp), ABIType::General);
        argDisp += sizeof(void*);
        break;
      case VMFunctionData::WordByRef:
        masm.passABIArg(MoveOperand(FramePointer, argDisp,
                                    MoveOperand::Kind::EffectiveAddress),
                        ABIType::General);
        argDisp += sizeof(void*);
        break;
      case VMFunctionData::DoubleByRef:
        masm.passABIArg(MoveOperand(FramePointer, argDisp,
                                    MoveOperand::Kind::EffectiveAddress),
                        ABIType::General);
        argDisp += 2 * sizeof(void*);
        break;
    }
  }

  // Copy the implicit outparam, if any.
  const int32_t outParamOffset =
      -int32_t(ExitFooterFrame::Size()) - f.sizeOfOutParamStackSlot();
  if (f.outParam != Type_Void) {
    masm.passABIArg(MoveOperand(FramePointer, outParamOffset,
                                MoveOperand::Kind::EffectiveAddress),
                    ABIType::General);
  }

  masm.callWithABI(nativeFun, ABIType::General,
                   CheckUnsafeCallWithABI::DontCheckHasExitFrame);

  // Test for failure.
  switch (f.failType()) {
    case Type_Cell:
      masm.branchTestPtr(Assembler::Zero, eax, eax, masm.failureLabel());
      break;
    case Type_Bool:
      masm.testb(eax, eax);
      masm.j(Assembler::Zero, masm.failureLabel());
      break;
    case Type_Void:
      break;
    default:
      MOZ_CRASH("unknown failure kind");
  }

  // Load the outparam.
  masm.loadVMFunctionOutParam(f, Address(FramePointer, outParamOffset));

  // Until C++ code is instrumented against Spectre, prevent speculative
  // execution from returning any private data.
  if (f.returnsData() && JitOptions.spectreJitToCxxCalls) {
    masm.speculationBarrier();
  }

  // Pop frame and restore frame pointer.
  masm.moveToStackPtr(FramePointer);
  masm.pop(FramePointer);

  // Return. Subtract sizeof(void*) for the frame pointer.
  masm.retn(Imm32(sizeof(ExitFrameLayout) - sizeof(void*) +
                  f.explicitStackSlots() * sizeof(void*) +
                  f.extraValuesToPop * sizeof(Value)));

  return true;
}

uint32_t JitRuntime::generatePreBarrier(JSContext* cx, MacroAssembler& masm,
                                        MIRType type) {
  AutoCreatedBy acb(masm, "JitRuntime::generatePreBarrier");

  uint32_t offset = startTrampolineCode(masm);

  static_assert(PreBarrierReg == edx);
  Register temp1 = eax;
  Register temp2 = ebx;
  Register temp3 = ecx;
  masm.push(temp1);
  masm.push(temp2);
  masm.push(temp3);

  Label noBarrier;
  masm.emitPreBarrierFastPath(type, temp1, temp2, temp3, &noBarrier);

  // Call into C++ to mark this GC thing.
  masm.pop(temp3);
  masm.pop(temp2);
  masm.pop(temp1);

  LiveRegisterSet save;
  save.set() = RegisterSet(GeneralRegisterSet(Registers::VolatileMask),
                           FloatRegisterSet(FloatRegisters::VolatileMask));
  masm.PushRegsInMask(save);

  masm.movl(ImmPtr(cx->runtime()), ecx);

  masm.setupUnalignedABICall(eax);
  masm.passABIArg(ecx);
  masm.passABIArg(edx);
  masm.callWithABI(JitPreWriteBarrier(type));

  masm.PopRegsInMask(save);
  masm.ret();

  masm.bind(&noBarrier);
  masm.pop(temp3);
  masm.pop(temp2);
  masm.pop(temp1);
  masm.ret();

  return offset;
}

void JitRuntime::generateBailoutTailStub(MacroAssembler& masm,
                                         Label* bailoutTail) {
  AutoCreatedBy acb(masm, "JitRuntime::generateBailoutTailStub");

  masm.bind(bailoutTail);
  masm.generateBailoutTail(edx, ecx);
}
