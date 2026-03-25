//===- Optimize.cpp - Optimize a complete program -------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// New pass manager implementation of module optimization for LLVM >= 17.
// Replaces OptimizeLegacy.cpp which uses the legacy pass manager.
//
//===----------------------------------------------------------------------===//

#include "ModuleHelper.h"

#include "klee/Support/OptionCategories.h"

#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/OptimizationLevel.h"
#include "llvm/Transforms/IPO/GlobalDCE.h"
#include "llvm/Transforms/IPO/Internalize.h"
#include "llvm/Transforms/IPO/StripSymbols.h"
#include "llvm/Transforms/IPO/StripDeadPrototypes.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;
using namespace klee;

namespace {
static cl::opt<bool>
    DisableInline("disable-inlining",
                  cl::desc("Do not run the inliner pass (default=false)"),
                  cl::init(false), cl::cat(klee::ModuleCat));

static cl::opt<bool> DisableInternalize(
    "disable-internalize",
    cl::desc("Do not mark all symbols as internal (default=false)"),
    cl::init(false), cl::cat(klee::ModuleCat));

static cl::opt<bool> VerifyEach("verify-each",
                                cl::desc("Verify intermediate results of all "
                                         "optimization passes (default=false)"),
                                cl::init(false), cl::cat(klee::ModuleCat));

static cl::opt<bool>
    Strip("strip-all",
          cl::desc("Strip all symbol information from executable "
                   "(default=false)"),
          cl::init(false), cl::cat(klee::ModuleCat));

static cl::opt<bool>
    StripDebug("strip-debug",
               cl::desc("Strip debugger symbol info from executable "
                        "(default=false)"),
               cl::init(false), cl::cat(klee::ModuleCat));
} // namespace

void klee::optimizeModule(llvm::Module *M,
                          llvm::ArrayRef<const char *> preservedFunctions) {
  LoopAnalysisManager LAM;
  FunctionAnalysisManager FAM;
  CGSCCAnalysisManager CGAM;
  ModuleAnalysisManager MAM;

  PassBuilder PB;
  PB.registerModuleAnalyses(MAM);
  PB.registerCGSCCAnalyses(CGAM);
  PB.registerFunctionAnalyses(FAM);
  PB.registerLoopAnalyses(LAM);
  PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

  // Use LLVM's standard O2 optimization pipeline, which covers the same
  // passes as the legacy OptimizeLegacy.cpp (SROA, instcombine, GVN, LICM,
  // loop opts, inlining, etc.) but with better ordering and tuning.
  ModulePassManager MPM =
      PB.buildPerModuleDefaultPipeline(OptimizationLevel::O2);

  // Internalize all symbols except preserved functions
  if (!DisableInternalize) {
    MPM.addPass(InternalizePass([=](const GlobalValue &GV) {
      StringRef GVName = GV.getName();
      for (const char *fun : preservedFunctions)
        if (GVName == fun)
          return true;
      return false;
    }));
    MPM.addPass(GlobalDCEPass());
  }

  // Strip symbols if requested
  if (Strip || StripDebug) {
    if (StripDebug && !Strip)
      MPM.addPass(StripDebugDeclarePass());
    else
      MPM.addPass(StripSymbolsPass());
  }

  // Final cleanup
  MPM.addPass(GlobalDCEPass());
  MPM.addPass(StripDeadPrototypesPass());

  MPM.run(*M, MAM);
}
