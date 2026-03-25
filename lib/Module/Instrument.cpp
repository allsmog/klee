//===-- Instrument.cpp ------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// New pass manager implementation of instrumentation passes for LLVM >= 17.
//
//===----------------------------------------------------------------------===//

#include "ModuleHelper.h"

#include "Passes.h"
#include "klee/Support/ErrorHandling.h"

#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Transforms/Scalar/Scalarizer.h"
#include "llvm/Transforms/Scalar/LowerAtomicPass.h"
#include "llvm/Transforms/Scalar/SimplifyCFG.h"
#include "llvm/Transforms/Utils/LowerSwitch.h"

using namespace llvm;
using namespace klee;

/// Set up new PM analysis managers and register all analyses.
static void setupPassInfrastructure(LoopAnalysisManager &LAM,
                                    FunctionAnalysisManager &FAM,
                                    CGSCCAnalysisManager &CGAM,
                                    ModuleAnalysisManager &MAM) {
  PassBuilder PB;
  PB.registerModuleAnalyses(MAM);
  PB.registerCGSCCAnalyses(CGAM);
  PB.registerFunctionAnalyses(FAM);
  PB.registerLoopAnalyses(LAM);
  PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);
}

void klee::instrument(bool CheckDivZero, bool CheckOvershift,
                      llvm::Module *module) {
  // Run KLEE's RaiseAsmPass directly
  RaiseAsmPass raiseAsm;
  raiseAsm.runOnModule(*module);

  // Run LLVM's Scalarizer and LowerAtomic passes via the new PM.
  // Scalarizer must come before division/overshift checks because those
  // passes don't know how to handle vector instructions.
  {
    LoopAnalysisManager LAM;
    FunctionAnalysisManager FAM;
    CGSCCAnalysisManager CGAM;
    ModuleAnalysisManager MAM;
    setupPassInfrastructure(LAM, FAM, CGAM, MAM);

    FunctionPassManager FPM;
    FPM.addPass(ScalarizerPass());
    FPM.addPass(LowerAtomicPass());

    ModulePassManager MPM;
    MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));
    MPM.run(*module, MAM);
  }

  // Run KLEE custom passes directly
  if (CheckDivZero) {
    DivCheckPass divCheck;
    divCheck.runOnModule(*module);
  }
  if (CheckOvershift) {
    OvershiftCheckPass overshiftCheck;
    overshiftCheck.runOnModule(*module);
  }

  IntrinsicCleanerPass intrinsicCleaner(module->getDataLayout());
  intrinsicCleaner.runOnModule(*module);
}

void klee::checkModule(bool DontVerify, llvm::Module *module) {
  // Run LLVM's verifier via the new PM
  if (!DontVerify) {
    LoopAnalysisManager LAM;
    FunctionAnalysisManager FAM;
    CGSCCAnalysisManager CGAM;
    ModuleAnalysisManager MAM;
    setupPassInfrastructure(LAM, FAM, CGAM, MAM);

    ModulePassManager MPM;
    MPM.addPass(VerifierPass());
    MPM.run(*module, MAM);
  }

  // Run KLEE's operand type check pass directly
  InstructionOperandTypeCheckPass operandTypeCheck;
  operandTypeCheck.runOnModule(*module);

  if (!operandTypeCheck.checkPassed()) {
    klee_error("Unexpected instruction operand types detected");
  }
}

void klee::optimiseAndPrepare(bool OptimiseKLEECall, bool Optimize,
                              SwitchImplType SwitchType, std::string EntryPoint,
                              llvm::ArrayRef<const char *> preservedFunctions,
                              llvm::Module *module) {
  // Preserve all functions containing klee-related function calls from being
  // optimised around
  if (!OptimiseKLEECall) {
    OptNonePass optNone;
    optNone.runOnModule(*module);
  }

  if (Optimize)
    optimizeModule(module, preservedFunctions);

  // Needs to happen after linking (since ctors/dtors can be modified)
  // and optimization (since global optimization can rewrite lists).
  injectStaticConstructorsAndDestructors(module, EntryPoint);

  // Run LLVM passes via new PM
  {
    LoopAnalysisManager LAM;
    FunctionAnalysisManager FAM;
    CGSCCAnalysisManager CGAM;
    ModuleAnalysisManager MAM;
    setupPassInfrastructure(LAM, FAM, CGAM, MAM);

    FunctionPassManager FPM;
    FPM.addPass(SimplifyCFGPass());
    FPM.addPass(ScalarizerPass());

    ModulePassManager MPM;
    MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));
    MPM.run(*module, MAM);
  }

  // Handle switch lowering based on configuration
  switch (SwitchType) {
  case SwitchImplType::eSwitchTypeInternal:
    break;
  case SwitchImplType::eSwitchTypeSimple: {
    klee::LowerSwitchPass lowerSwitch;
    for (auto &F : *module)
      if (!F.isDeclaration())
        lowerSwitch.runOnFunction(F);
    break;
  }
  case SwitchImplType::eSwitchTypeLLVM: {
    LoopAnalysisManager LAM;
    FunctionAnalysisManager FAM;
    CGSCCAnalysisManager CGAM;
    ModuleAnalysisManager MAM;
    setupPassInfrastructure(LAM, FAM, CGAM, MAM);

    FunctionPassManager FPM;
    FPM.addPass(llvm::LowerSwitchPass());

    ModulePassManager MPM;
    MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));
    MPM.run(*module, MAM);
    break;
  }
  }

  // Run remaining KLEE custom passes directly
  IntrinsicCleanerPass intrinsicCleaner(module->getDataLayout());
  intrinsicCleaner.runOnModule(*module);

  PhiCleanerPass phiCleaner;
  for (auto &F : *module)
    if (!F.isDeclaration())
      phiCleaner.runOnFunction(F);

  FunctionAliasPass functionAlias;
  functionAlias.runOnModule(*module);
}
