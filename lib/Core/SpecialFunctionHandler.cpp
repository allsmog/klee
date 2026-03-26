//===-- SpecialFunctionHandler.cpp ----------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "SpecialFunctionHandler.h"

#include "ExecutionState.h"
#include "Executor.h"
#include "Memory.h"
#include "MemoryManager.h"
#include "MergeHandler.h"
#include "Searcher.h"
#include "StatsTracker.h"
#include "TimingSolver.h"

#include "klee/Config/config.h"
#include "klee/Module/KInstruction.h"
#include "klee/Module/KModule.h"
#include "klee/Solver/SolverCmdLine.h"
#include "klee/Support/Casting.h"
#include "klee/Support/Debug.h"
#include "klee/Support/ErrorHandling.h"
#include "klee/Support/OptionCategories.h"

#include "klee/Support/CompilerWarning.h"
DISABLE_WARNING_PUSH
DISABLE_WARNING_DEPRECATED_DECLARATIONS
#include "llvm/ADT/Twine.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
DISABLE_WARNING_POP

#include <array>
#include <cerrno>
#include <sstream>

using namespace llvm;
using namespace klee;

namespace {
cl::opt<bool>
    ReadablePosix("readable-posix-inputs", cl::init(false),
                  cl::desc("Prefer creation of POSIX inputs (command-line "
                           "arguments, files, etc.) with human readable bytes. "
                           "Note: option is expensive when creating lots of "
                           "tests (default=false)"),
                  cl::cat(TestGenCat));

cl::opt<bool>
    SilentKleeAssume("silent-klee-assume", cl::init(false),
                     cl::desc("Silently terminate paths with an infeasible "
                              "condition given to klee_assume rather than "
                              "emitting an error (default=false)"),
                     cl::cat(TerminationCat));
} // namespace

/// \todo Almost all of the demands in this file should be replaced
/// with terminateState calls.

///

// FIXME: We are more or less committed to requiring an intrinsic
// library these days. We can move some of this stuff there,
// especially things like realloc which have complicated semantics
// w.r.t. forking. Among other things this makes delayed query
// dispatch easier to implement.
static constexpr std::array handlerInfo = {
#define add(name, handler, ret) SpecialFunctionHandler::HandlerInfo{ name, \
                                  &SpecialFunctionHandler::handler, \
                                  false, ret, false }
#define addDNR(name, handler) SpecialFunctionHandler::HandlerInfo{ name, \
                                &SpecialFunctionHandler::handler, \
                                true, false, false }
  addDNR("__assert_rtn", handleAssertFail),
  addDNR("__assert_fail", handleAssertFail),
  addDNR("__assert", handleAssertFail),
  addDNR("_assert", handleAssert),
  addDNR("abort", handleAbort),
  addDNR("_exit", handleExit),
  addDNR("_Exit", handleExit),
  SpecialFunctionHandler::HandlerInfo{ "exit", &SpecialFunctionHandler::handleExit, true, false, true },
  addDNR("klee_abort", handleAbort),
  addDNR("klee_silent_exit", handleSilentExit),
  addDNR("klee_report_error", handleReportError),
  add("aligned_alloc", handleMemalign, true),
  add("calloc", handleCalloc, true),
  add("free", handleFree, false),
  add("klee_assume", handleAssume, false),
  add("klee_check_memory_access", handleCheckMemoryAccess, false),
  add("klee_get_valuef", handleGetValue, true),
  add("klee_get_valued", handleGetValue, true),
  add("klee_get_valuel", handleGetValue, true),
  add("klee_get_valuell", handleGetValue, true),
  add("klee_get_value_i32", handleGetValue, true),
  add("klee_get_value_i64", handleGetValue, true),
  add("klee_define_fixed_object", handleDefineFixedObject, false),
  add("klee_get_obj_size", handleGetObjSize, true),
  add("klee_get_errno", handleGetErrno, true),
#ifndef __APPLE__
  add("__errno_location", handleErrnoLocation, true),
#else
  add("__error", handleErrnoLocation, true),
#endif
  add("klee_is_symbolic", handleIsSymbolic, true),
  add("klee_make_symbolic", handleMakeSymbolic, false),
  add("klee_make_symbolic_string", handleMakeSymbolicString, true),
  add("klee_string_eq", handleStringEq, true),
  add("klee_string_length", handleStringLength, true),
  add("klee_string_concat", handleStringConcat, true),
  add("klee_string_contains", handleStringContains, true),
  add("klee_string_indexof", handleStringIndexOf, true),
  add("klee_string_char_at", handleStringCharAt, true),
  add("klee_string_substr", handleStringSubstr, true),
  add("klee_string_matches_regex", handleStringMatchesRegex, true),

  // C string function interception — use string theory when operating
  // on string-backed symbolic buffers.
  add("strcmp", handleStrcmpStr, true),
  add("strlen", handleStrlenStr, true),
  add("strstr", handleStrstrStr, true),
  add("strchr", handleStrchrStr, true),
  add("strncmp", handleStrncmpStr, true),
  add("memcmp", handleMemcmpStr, true),
  add("klee_make_symbolic_std_string", handleMakeSymbolicStdString, false),
  add("klee_mark_global", handleMarkGlobal, false),
  add("klee_open_merge", handleOpenMerge, false),
  add("klee_close_merge", handleCloseMerge, false),
  add("klee_prefer_cex", handlePreferCex, false),
  add("klee_posix_prefer_cex", handlePosixPreferCex, false),
  add("klee_print_expr", handlePrintExpr, false),
  add("klee_print_range", handlePrintRange, false),
  add("klee_set_forking", handleSetForking, false),
  add("klee_stack_trace", handleStackTrace, false),
  add("klee_warning", handleWarning, false),
  add("klee_warning_once", handleWarningOnce, false),
  add("malloc", handleMalloc, true),
  add("memalign", handleMemalign, true),
  add("realloc", handleRealloc, true),

#ifdef SUPPORT_KLEE_EH_CXX
  add("_klee_eh_Unwind_RaiseException_impl", handleEhUnwindRaiseExceptionImpl, false),
  add("klee_eh_typeid_for", handleEhTypeid, true),
#endif

  // operator delete[](void*)
  add("_ZdaPv", handleDeleteArray, false),
  // operator delete(void*)
  add("_ZdlPv", handleDelete, false),

  // C++14 sized deallocation
  // operator delete(void*, unsigned long)
  add("_ZdlPvm", handleDelete, false),
  // operator delete[](void*, unsigned long)
  add("_ZdaPvm", handleDeleteArray, false),
  // operator delete(void*, unsigned int) (32-bit)
  add("_ZdlPvj", handleDelete, false),
  // operator delete[](void*, unsigned int) (32-bit)
  add("_ZdaPvj", handleDeleteArray, false),

  // operator new[](unsigned int)
  add("_Znaj", handleNewArray, true),
  // operator new(unsigned int)
  add("_Znwj", handleNew, true),

  // operator new[](unsigned long)
  add("_Znam", handleNewArray, true),
  // operator new(unsigned long)
  add("_Znwm", handleNew, true),

#undef addDNR
#undef add
};

SpecialFunctionHandler::SpecialFunctionHandler(Executor &_executor) 
  : executor(_executor) {}

void SpecialFunctionHandler::prepare(
    std::vector<const char *> &preservedFunctions) {
  for (auto &hi : handlerInfo) {
    Function *f = executor.kmodule->module->getFunction(hi.name);

    // No need to create if the function doesn't exist, since it cannot
    // be called in that case.
    if (f && (!hi.doNotOverride || f->isDeclaration())) {
      preservedFunctions.push_back(hi.name);
      // Make sure NoReturn attribute is set, for optimization and
      // coverage counting.
      if (hi.doesNotReturn)
        f->addFnAttr(Attribute::NoReturn);

      // Change to a declaration since we handle internally (simplifies
      // module and allows deleting dead code).
      if (!f->isDeclaration())
        f->deleteBody();
    }
  }
}

void SpecialFunctionHandler::bind() {
  for (auto &hi : handlerInfo) {
    Function *f = executor.kmodule->module->getFunction(hi.name);

    if (f && (!hi.doNotOverride || f->isDeclaration()))
      handlers[f] = std::make_pair(hi.handler, hi.hasReturnValue);
  }

}


bool SpecialFunctionHandler::handle(ExecutionState &state, 
                                    Function *f,
                                    KInstruction *target,
                                    std::vector< ref<Expr> > &arguments) {
  handlers_ty::iterator it = handlers.find(f);
  if (it != handlers.end()) {    
    Handler h = it->second.first;
    bool hasReturnValue = it->second.second;
     // FIXME: Check this... add test?
    if (!hasReturnValue && !target->inst->use_empty()) {
      executor.terminateStateOnExecError(state, 
                                         "expected return value from void special function");
    } else {
      (this->*h)(state, target, arguments);
    }
    return true;
  } else {
    return false;
  }
}

/****/

// reads a concrete string from memory
std::string 
SpecialFunctionHandler::readStringAtAddress(ExecutionState &state, 
                                            ref<Expr> addressExpr) {
  ObjectPair op;
  addressExpr = executor.toUnique(state, addressExpr);
  if (!isa<ConstantExpr>(addressExpr)) {
    executor.terminateStateOnUserError(
        state, "Symbolic string pointer passed to one of the klee_ functions");
    return "";
  }
  ref<ConstantExpr> address = cast<ConstantExpr>(addressExpr);
  if (!state.addressSpace.resolveOne(address, op)) {
    executor.terminateStateOnUserError(
        state, "Invalid string pointer passed to one of the klee_ functions");
    return "";
  }
  const MemoryObject *mo = op.first;
  const ObjectState *os = op.second;

  auto relativeOffset = mo->getOffsetExpr(address);
  // the relativeOffset must be concrete as the address is concrete
  size_t offset = cast<ConstantExpr>(relativeOffset)->getZExtValue();

  std::ostringstream buf;
  char c = 0;
  for (size_t i = offset; i < mo->size; ++i) {
    ref<Expr> cur = os->read8(i);
    cur = executor.toUnique(state, cur);
    assert(isa<ConstantExpr>(cur) && 
           "hit symbolic char while reading concrete string");
    c = cast<ConstantExpr>(cur)->getZExtValue(8);
    if (c == '\0') {
      // we read the whole string
      break;
    }

    buf << c;
  }

  if (c != '\0') {
      klee_warning_once(0, "String not terminated by \\0 passed to "
                           "one of the klee_ functions");
  }

  return buf.str();
}

/****/

void SpecialFunctionHandler::handleAbort(ExecutionState &state,
                                         KInstruction *target,
                                         std::vector<ref<Expr>> &arguments) {
  assert(arguments.size() == 0 && "invalid number of arguments to abort");
  executor.terminateStateOnProgramError(state, "abort failure",
                                        StateTerminationType::Abort);
}

void SpecialFunctionHandler::handleExit(ExecutionState &state,
                                        KInstruction *target,
                                        std::vector<ref<Expr>> &arguments) {
  assert(arguments.size() == 1 && "invalid number of arguments to exit");
  executor.terminateStateOnExit(state);
}

void SpecialFunctionHandler::handleSilentExit(
    ExecutionState &state, KInstruction *target,
    std::vector<ref<Expr>> &arguments) {
  assert(arguments.size() == 1 && "invalid number of arguments to exit");
  executor.terminateStateEarlyUser(state, "");
}

void SpecialFunctionHandler::handleAssert(ExecutionState &state,
                                          KInstruction *target,
                                          std::vector<ref<Expr>> &arguments) {
  assert(arguments.size() == 3 && "invalid number of arguments to _assert");
  executor.terminateStateOnProgramError(
      state, "ASSERTION FAIL: " + readStringAtAddress(state, arguments[0]),
      StateTerminationType::Assert);
}

void SpecialFunctionHandler::handleAssertFail(
    ExecutionState &state, KInstruction *target,
    std::vector<ref<Expr>> &arguments) {
  assert(arguments.size() == 4 &&
         "invalid number of arguments to __assert_fail");
  executor.terminateStateOnProgramError(
      state, "ASSERTION FAIL: " + readStringAtAddress(state, arguments[0]),
      StateTerminationType::Assert);
}

void SpecialFunctionHandler::handleReportError(
    ExecutionState &state, KInstruction *target,
    std::vector<ref<Expr>> &arguments) {
  assert(arguments.size() == 4 &&
         "invalid number of arguments to klee_report_error");

  // arguments[0,1,2,3] are file, line, message, suffix
  executor.terminateStateOnProgramError(
      state, readStringAtAddress(state, arguments[2]),
      StateTerminationType::ReportError, "",
      readStringAtAddress(state, arguments[3]).c_str());
}

void SpecialFunctionHandler::handleOpenMerge(ExecutionState &state,
    KInstruction *target,
    std::vector<ref<Expr> > &arguments) {
  if (!UseMerge) {
    klee_warning_once(0, "klee_open_merge ignored, use '-use-merge'");
    return;
  }

  state.openMergeStack.push_back(
      ref<MergeHandler>(new MergeHandler(&executor, &state)));

  if (DebugLogMerge)
    llvm::errs() << "open merge: " << &state << "\n";
}

void SpecialFunctionHandler::handleCloseMerge(ExecutionState &state,
    KInstruction *target,
    std::vector<ref<Expr> > &arguments) {
  if (!UseMerge) {
    klee_warning_once(0, "klee_close_merge ignored, use '-use-merge'");
    return;
  }
  Instruction *i = target->inst;

  if (DebugLogMerge)
    llvm::errs() << "close merge: " << &state << " at [" << *i << "]\n";

  if (state.openMergeStack.empty()) {
    std::ostringstream warning;
    warning << &state << " ran into a close at " << i << " without a preceding open";
    klee_warning("%s", warning.str().c_str());
  } else {
    assert(executor.mergingSearcher->inCloseMerge.find(&state) ==
               executor.mergingSearcher->inCloseMerge.end() &&
           "State cannot run into close_merge while being closed");
    executor.mergingSearcher->inCloseMerge.insert(&state);
    state.openMergeStack.back()->addClosedState(&state, i);
    state.openMergeStack.pop_back();
  }
}

void SpecialFunctionHandler::handleNew(ExecutionState &state,
                         KInstruction *target,
                         std::vector<ref<Expr> > &arguments) {
  // XXX should type check args
  assert(arguments.size()==1 && "invalid number of arguments to new");

  executor.executeAlloc(state, arguments[0], false, target, false, 0, 0,
                        AllocationType::New);
}

void SpecialFunctionHandler::handleDelete(ExecutionState &state,
                            KInstruction *target,
                            std::vector<ref<Expr> > &arguments) {
  // XXX should type check args
  // Sized deallocation (C++14) passes 2 args: (void*, size_t)
  assert((arguments.size()==1 || arguments.size()==2) &&
         "invalid number of arguments to delete");
  executor.executeFree(state, arguments[0], 0, AllocationType::New);
}

void SpecialFunctionHandler::handleNewArray(ExecutionState &state,
                              KInstruction *target,
                              std::vector<ref<Expr> > &arguments) {
  // XXX should type check args
  assert(arguments.size()==1 && "invalid number of arguments to new[]");
  executor.executeAlloc(state, arguments[0], false, target, false, 0, 0,
                        AllocationType::NewArray);
}

void SpecialFunctionHandler::handleDeleteArray(ExecutionState &state,
                                 KInstruction *target,
                                 std::vector<ref<Expr> > &arguments) {
  // XXX should type check args
  // Sized deallocation (C++14) passes 2 args: (void*, size_t)
  assert((arguments.size()==1 || arguments.size()==2) &&
         "invalid number of arguments to delete[]");
  executor.executeFree(state, arguments[0], 0, AllocationType::NewArray);
}

void SpecialFunctionHandler::handleMalloc(ExecutionState &state,
                                  KInstruction *target,
                                  std::vector<ref<Expr> > &arguments) {
  // XXX should type check args
  assert(arguments.size()==1 && "invalid number of arguments to malloc");
  executor.executeAlloc(state, arguments[0], false, target, false, 0, 0,
                        AllocationType::Malloc);
}

void SpecialFunctionHandler::handleMemalign(ExecutionState &state,
                                            KInstruction *target,
                                            std::vector<ref<Expr>> &arguments) {
  if (arguments.size() != 2) {
    executor.terminateStateOnUserError(state,
      "Incorrect number of arguments to memalign(size_t alignment, size_t size)");
    return;
  }

  std::pair<ref<Expr>, ref<Expr>> alignmentRangeExpr =
      executor.solver->getRange(state.constraints, arguments[0],
                                state.queryMetaData);
  ref<Expr> alignmentExpr = alignmentRangeExpr.first;
  auto alignmentConstExpr = dyn_cast<ConstantExpr>(alignmentExpr);

  if (!alignmentConstExpr) {
    executor.terminateStateOnUserError(state, "Could not determine size of symbolic alignment");
    return;
  }

  uint64_t alignment = alignmentConstExpr->getZExtValue();

  // Warn, if the expression has more than one solution
  if (alignmentRangeExpr.first != alignmentRangeExpr.second) {
    klee_warning_once(
        0, "Symbolic alignment for memalign. Choosing smallest alignment");
  }

  executor.executeAlloc(state, arguments[1], false, target, false, 0,
                        alignment, AllocationType::Malloc);
}

#ifdef SUPPORT_KLEE_EH_CXX
void SpecialFunctionHandler::handleEhUnwindRaiseExceptionImpl(
    ExecutionState &state, KInstruction *target,
    std::vector<ref<Expr>> &arguments) {
  assert(arguments.size() == 1 &&
         "invalid number of arguments to _klee_eh_Unwind_RaiseException_impl");

  ref<ConstantExpr> exceptionObject = dyn_cast<ConstantExpr>(arguments[0]);
  if (!exceptionObject.get()) {
    executor.terminateStateOnExecError(state, "Internal error: Symbolic exception pointer");
    return;
  }

  if (isa_and_nonnull<SearchPhaseUnwindingInformation>(
          state.unwindingInformation.get())) {
    executor.terminateStateOnExecError(
        state,
        "Internal error: Unwinding restarted during an ongoing search phase");
    return;
  }

  state.unwindingInformation =
      std::make_unique<SearchPhaseUnwindingInformation>(exceptionObject,
                                                        state.stack.size() - 1);

  executor.unwindToNextLandingpad(state);
}

void SpecialFunctionHandler::handleEhTypeid(ExecutionState &state,
                                            KInstruction *target,
                                            std::vector<ref<Expr>> &arguments) {
  assert(arguments.size() == 1 &&
         "invalid number of arguments to klee_eh_typeid_for");

  executor.bindLocal(target, state, executor.getEhTypeidFor(arguments[0]));
}
#endif // SUPPORT_KLEE_EH_CXX

void SpecialFunctionHandler::handleAssume(ExecutionState &state,
                            KInstruction *target,
                            std::vector<ref<Expr> > &arguments) {
  assert(arguments.size()==1 && "invalid number of arguments to klee_assume");
  
  ref<Expr> e = arguments[0];
  
  if (e->getWidth() != Expr::Bool)
    e = NeExpr::create(e, ConstantExpr::create(0, e->getWidth()));
  
  bool res;
  bool success = executor.solver->mustBeFalse(
      state.constraints, e, res, state.queryMetaData);
  if (!success) {
    executor.terminateStateOnSolverError(state, "Solver failure in klee_assume");
    return;
  }
  if (res) {
    executor.terminateStateOnUserError(
        state, "invalid klee_assume call (provably false)", !SilentKleeAssume);
  } else {
    executor.addConstraint(state, e);
  }
}

void SpecialFunctionHandler::handleIsSymbolic(ExecutionState &state,
                                KInstruction *target,
                                std::vector<ref<Expr> > &arguments) {
  assert(arguments.size()==1 && "invalid number of arguments to klee_is_symbolic");

  executor.bindLocal(target, state, 
                     ConstantExpr::create(!isa<ConstantExpr>(arguments[0]),
                                          Expr::Int32));
}

void SpecialFunctionHandler::handlePreferCex(ExecutionState &state,
                                             KInstruction *target,
                                             std::vector<ref<Expr> > &arguments) {
  assert(arguments.size()==2 &&
         "invalid number of arguments to klee_prefex_cex");

  ref<Expr> cond = arguments[1];
  if (cond->getWidth() != Expr::Bool)
    cond = NeExpr::create(cond, ConstantExpr::alloc(0, cond->getWidth()));

  state.addCexPreference(cond);
}

void SpecialFunctionHandler::handlePosixPreferCex(ExecutionState &state,
                                             KInstruction *target,
                                             std::vector<ref<Expr> > &arguments) {
  if (ReadablePosix)
    return handlePreferCex(state, target, arguments);
}

void SpecialFunctionHandler::handlePrintExpr(ExecutionState &state,
                                  KInstruction *target,
                                  std::vector<ref<Expr> > &arguments) {
  assert(arguments.size()==2 &&
         "invalid number of arguments to klee_print_expr");

  std::string msg_str = readStringAtAddress(state, arguments[0]);
  llvm::errs() << msg_str << ":" << arguments[1] << "\n";
}

void SpecialFunctionHandler::handleSetForking(ExecutionState &state,
                                              KInstruction *target,
                                              std::vector<ref<Expr> > &arguments) {
  assert(arguments.size()==1 &&
         "invalid number of arguments to klee_set_forking");
  ref<Expr> value = executor.toUnique(state, arguments[0]);
  
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(value)) {
    state.forkDisabled = CE->isZero();
  } else {
    executor.terminateStateOnUserError(state, "klee_set_forking requires a constant arg");
  }
}

void SpecialFunctionHandler::handleStackTrace(ExecutionState &state,
                                              KInstruction *target,
                                              std::vector<ref<Expr> > &arguments) {
  state.dumpStack(outs());
}

void SpecialFunctionHandler::handleWarning(ExecutionState &state,
                                           KInstruction *target,
                                           std::vector<ref<Expr> > &arguments) {
  assert(arguments.size()==1 && "invalid number of arguments to klee_warning");

  std::string msg_str = readStringAtAddress(state, arguments[0]);
  klee_warning("%s: %s", state.stack.back().kf->function->getName().data(), 
               msg_str.c_str());
}

void SpecialFunctionHandler::handleWarningOnce(ExecutionState &state,
                                               KInstruction *target,
                                               std::vector<ref<Expr> > &arguments) {
  assert(arguments.size()==1 &&
         "invalid number of arguments to klee_warning_once");

  std::string msg_str = readStringAtAddress(state, arguments[0]);
  klee_warning_once(0, "%s: %s", state.stack.back().kf->function->getName().data(),
                    msg_str.c_str());
}

void SpecialFunctionHandler::handlePrintRange(ExecutionState &state,
                                  KInstruction *target,
                                  std::vector<ref<Expr> > &arguments) {
  assert(arguments.size()==2 &&
         "invalid number of arguments to klee_print_range");

  std::string msg_str = readStringAtAddress(state, arguments[0]);
  llvm::errs() << msg_str << ":" << arguments[1];
  if (!isa<ConstantExpr>(arguments[1])) {
    // FIXME: Pull into a unique value method?
    ref<ConstantExpr> value;
    bool success = executor.solver->getValue(
        state.constraints, arguments[1], value, state.queryMetaData);
    if (!success) {
      executor.terminateStateOnSolverError(state, "Solver failure in print_range");
      return;
    }
    bool res;
    success = executor.solver->mustBeTrue(state.constraints,
                                          EqExpr::create(arguments[1], value),
                                          res, state.queryMetaData);
    if (!success) {
      executor.terminateStateOnSolverError(state, "Solver failure in print_range");
      return;
    }
    if (res) {
      llvm::errs() << " == " << value;
    } else { 
      llvm::errs() << " ~= " << value;
      std::pair<ref<Expr>, ref<Expr>> res = executor.solver->getRange(
          state.constraints, arguments[1], state.queryMetaData);
      llvm::errs() << " (in [" << res.first << ", " << res.second <<"])";
    }
  }
  llvm::errs() << "\n";
}

void SpecialFunctionHandler::handleGetObjSize(ExecutionState &state,
                                  KInstruction *target,
                                  std::vector<ref<Expr> > &arguments) {
  // XXX should type check args
  assert(arguments.size()==1 &&
         "invalid number of arguments to klee_get_obj_size");
  Executor::ExactResolutionList rl;
  executor.resolveExact(state, arguments[0], rl, "klee_get_obj_size");
  for (Executor::ExactResolutionList::iterator it = rl.begin(), 
         ie = rl.end(); it != ie; ++it) {
    executor.bindLocal(
        target, *it->second,
        ConstantExpr::create(it->first.first->size,
                             executor.kmodule->targetData->getTypeSizeInBits(
                                 target->inst->getType())));
  }
}

void SpecialFunctionHandler::handleGetErrno(ExecutionState &state,
                                            KInstruction *target,
                                            std::vector<ref<Expr> > &arguments) {
  // XXX should type check args
  assert(arguments.size()==0 &&
         "invalid number of arguments to klee_get_errno");
#ifndef WINDOWS
  int *errno_addr = executor.getErrnoLocation(state);
#else
  int *errno_addr = nullptr;
#endif

  // Retrieve the memory object of the errno variable
  ObjectPair result;
  bool resolved = state.addressSpace.resolveOne(
      ConstantExpr::create((uint64_t)errno_addr, Expr::Int64), result);
  if (!resolved)
    executor.terminateStateOnUserError(state, "Could not resolve address for errno");
  executor.bindLocal(target, state, result.second->read(0, Expr::Int32));
}

void SpecialFunctionHandler::handleErrnoLocation(
    ExecutionState &state, KInstruction *target,
    std::vector<ref<Expr> > &arguments) {
  // Returns the address of the errno variable
  assert(arguments.size() == 0 &&
         "invalid number of arguments to __errno_location/__error");

#ifndef WINDOWS
  int *errno_addr = executor.getErrnoLocation(state);
#else
  int *errno_addr = nullptr;
#endif

  executor.bindLocal(
      target, state,
      ConstantExpr::create((uint64_t)errno_addr,
                           executor.kmodule->targetData->getTypeSizeInBits(
                               target->inst->getType())));
}
void SpecialFunctionHandler::handleCalloc(ExecutionState &state,
                            KInstruction *target,
                            std::vector<ref<Expr> > &arguments) {
  // XXX should type check args
  assert(arguments.size()==2 &&
         "invalid number of arguments to calloc");

  ref<Expr> size = MulExpr::create(arguments[0],
                                   arguments[1]);
  executor.executeAlloc(state, size, false, target, true, 0, 0,
                        AllocationType::Malloc);
}

void SpecialFunctionHandler::handleRealloc(ExecutionState &state,
                            KInstruction *target,
                            std::vector<ref<Expr> > &arguments) {
  // XXX should type check args
  assert(arguments.size()==2 &&
         "invalid number of arguments to realloc");
  ref<Expr> address = arguments[0];
  ref<Expr> size = arguments[1];

  Executor::StatePair zeroSize =
      executor.fork(state, Expr::createIsZero(size), true, BranchType::Realloc);

  if (zeroSize.first) { // size == 0
    executor.executeFree(*zeroSize.first, address, target,
                         AllocationType::Malloc);
  }
  if (zeroSize.second) { // size != 0
    Executor::StatePair zeroPointer =
        executor.fork(*zeroSize.second, Expr::createIsZero(address), true,
                      BranchType::Realloc);

    if (zeroPointer.first) { // address == 0
      executor.executeAlloc(*zeroPointer.first, size, false, target, false, 0,
                            0, AllocationType::Malloc);
    }
    if (zeroPointer.second) { // address != 0
      Executor::ExactResolutionList rl;
      executor.resolveExact(*zeroPointer.second, address, rl, "realloc");

      for (Executor::ExactResolutionList::iterator it = rl.begin(),
             ie = rl.end(); it != ie; ++it) {
        executor.executeAlloc(*it->second, size, false, target, false,
                              it->first.second, 0, AllocationType::Malloc);
      }
    }
  }
}

void SpecialFunctionHandler::handleFree(ExecutionState &state,
                          KInstruction *target,
                          std::vector<ref<Expr> > &arguments) {
  // XXX should type check args
  assert(arguments.size()==1 &&
         "invalid number of arguments to free");
  executor.executeFree(state, arguments[0], 0, AllocationType::Malloc);
}

void SpecialFunctionHandler::handleCheckMemoryAccess(ExecutionState &state,
                                                     KInstruction *target,
                                                     std::vector<ref<Expr> > 
                                                       &arguments) {
  assert(arguments.size()==2 &&
         "invalid number of arguments to klee_check_memory_access");

  ref<Expr> address = executor.toUnique(state, arguments[0]);
  ref<Expr> size = executor.toUnique(state, arguments[1]);
  if (!isa<ConstantExpr>(address) || !isa<ConstantExpr>(size)) {
    executor.terminateStateOnUserError(state, "check_memory_access requires constant args");
  } else {
    ObjectPair op;

    if (!state.addressSpace.resolveOne(cast<ConstantExpr>(address), op)) {
      executor.terminateStateOnProgramError(
          state, "check_memory_access: memory error", StateTerminationType::Ptr,
          executor.getAddressInfo(state, address));
    } else {
      ref<Expr> chk = 
        op.first->getBoundsCheckPointer(address, 
                                        cast<ConstantExpr>(size)->getZExtValue());
      if (!chk->isTrue()) {
        executor.terminateStateOnProgramError(
            state, "check_memory_access: memory error",
            StateTerminationType::Ptr, executor.getAddressInfo(state, address));
      }
    }
  }
}

void SpecialFunctionHandler::handleGetValue(ExecutionState &state,
                                            KInstruction *target,
                                            std::vector<ref<Expr> > &arguments) {
  assert(arguments.size()==1 &&
         "invalid number of arguments to klee_get_value");

  executor.executeGetValue(state, arguments[0], target);
}

void SpecialFunctionHandler::handleDefineFixedObject(ExecutionState &state,
                                                     KInstruction *target,
                                                     std::vector<ref<Expr> > &arguments) {
  assert(arguments.size()==2 &&
         "invalid number of arguments to klee_define_fixed_object");
  assert(isa<ConstantExpr>(arguments[0]) &&
         "expect constant address argument to klee_define_fixed_object");
  assert(isa<ConstantExpr>(arguments[1]) &&
         "expect constant size argument to klee_define_fixed_object");
  
  uint64_t address = cast<ConstantExpr>(arguments[0])->getZExtValue();
  uint64_t size = cast<ConstantExpr>(arguments[1])->getZExtValue();
  MemoryObject *mo = executor.memory->allocateFixed(address, size, state.prevPC->inst);
  executor.bindObjectInState(state, mo, false);
  mo->isUserSpecified = true; // XXX hack;
}

void SpecialFunctionHandler::handleMakeSymbolic(ExecutionState &state,
                                                KInstruction *target,
                                                std::vector<ref<Expr> > &arguments) {
  std::string name;

  if (arguments.size() != 3) {
    executor.terminateStateOnUserError(state,
        "Incorrect number of arguments to klee_make_symbolic(void*, size_t, char*)");
    return;
  }

  name = arguments[2]->isZero() ? "" : readStringAtAddress(state, arguments[2]);

  if (name.length() == 0) {
    name = "unnamed";
    klee_warning("klee_make_symbolic: renamed empty name to \"unnamed\"");
  }

  Executor::ExactResolutionList rl;
  executor.resolveExact(state, arguments[0], rl, "make_symbolic");
  
  for (Executor::ExactResolutionList::iterator it = rl.begin(), 
         ie = rl.end(); it != ie; ++it) {
    const MemoryObject *mo = it->first.first;
    mo->setName(name);
    
    const ObjectState *old = it->first.second;
    ExecutionState *s = it->second;
    
    if (old->readOnly) {
      executor.terminateStateOnUserError(*s, "cannot make readonly object symbolic");
      return;
    } 

    // FIXME: Type coercion should be done consistently somewhere.
    bool res;
    bool success = executor.solver->mustBeTrue(
        s->constraints,
        EqExpr::create(
            ZExtExpr::create(arguments[1], Context::get().getPointerWidth()),
            mo->getSizeExpr()),
        res, s->queryMetaData);
    if (!success) {
      executor.terminateStateOnSolverError(*s, "Solver failure in make_symbolic");
      return;
    }
    
    if (res) {
      executor.executeMakeSymbolic(*s, mo, name);

      // Create a dual string theory representation for this buffer.
      // This enables strcmp/strlen/strstr to use Z3 string theory
      // instead of byte-by-byte comparison.
      std::string strName = name + "_str";
      ref<Expr> strVar = StrVarExpr::create(strName);
      s->stringBackedBuffers[mo->id] = strVar;
      s->symbolicStrings.push_back({name, strVar});

      // Constrain: string length must fit in buffer (leave room for null)
      ref<Expr> lenExpr = StrLenExpr::create(strVar);
      ref<Expr> maxLen = ConstantExpr::alloc(mo->size, Expr::Int64);
      executor.addConstraint(*s, UltExpr::create(lenExpr, maxLen));
    } else {
      executor.terminateStateOnUserError(*s, "Wrong size given to klee_make_symbolic");
    }
  }
}

void SpecialFunctionHandler::handleMarkGlobal(ExecutionState &state,
                                              KInstruction *target,
                                              std::vector<ref<Expr> > &arguments) {
  assert(arguments.size()==1 &&
         "invalid number of arguments to klee_mark_global");  

  Executor::ExactResolutionList rl;
  executor.resolveExact(state, arguments[0], rl, "mark_global");
  
  for (Executor::ExactResolutionList::iterator it = rl.begin(), 
         ie = rl.end(); it != ie; ++it) {
    const MemoryObject *mo = it->first.first;
    assert(!mo->isLocal);
    mo->isGlobal = true;
  }
}

void SpecialFunctionHandler::handleMakeSymbolicString(
    ExecutionState &state, KInstruction *target,
    std::vector<ref<Expr>> &arguments) {
  assert(arguments.size() == 1 &&
         "invalid number of arguments to klee_make_symbolic_string");

  std::string name = readStringAtAddress(state, arguments[0]);
  if (name.empty())
    name = "unnamed_str";

  // Ensure unique name
  unsigned id = 0;
  std::string uniqueName = name;
  while (!state.arrayNames.insert(uniqueName).second)
    uniqueName = name + "_" + std::to_string(++id);

  // Create the symbolic string expression
  ref<Expr> strExpr = StrVarExpr::create(uniqueName);

  // Track in state for test case generation
  state.symbolicStrings.push_back({uniqueName, strExpr});

  // Return the 1-based index as a pointer-width constant handle.
  // The string_eq/string_length handlers interpret this as an index.
  unsigned index = state.symbolicStrings.size();
  executor.bindLocal(target, state,
                     ConstantExpr::alloc(index,
                                         Context::get().getPointerWidth()));
}

void SpecialFunctionHandler::handleStringEq(
    ExecutionState &state, KInstruction *target,
    std::vector<ref<Expr>> &arguments) {
  assert(arguments.size() == 2 &&
         "invalid number of arguments to klee_string_eq");

  // arg[0] is the handle (1-based index as a pointer)
  ref<Expr> handleExpr = executor.toUnique(state, arguments[0]);
  if (!isa<ConstantExpr>(handleExpr)) {
    executor.terminateStateOnUserError(
        state, "klee_string_eq: could not resolve string handle");
    return;
  }
  uint64_t handle = cast<ConstantExpr>(handleExpr)->getZExtValue();
  if (handle == 0 || handle > state.symbolicStrings.size()) {
    executor.terminateStateOnUserError(
        state, "klee_string_eq: invalid string handle");
    return;
  }
  uint64_t index = handle - 1;

  ref<Expr> symbolicStr = state.symbolicStrings[index].second;

  // Read the literal string from arg[1]
  std::string literal = readStringAtAddress(state, arguments[1]);
  ref<Expr> literalExpr = StrLiteralExpr::create(literal);

  // Build string equality expression and fork
  ref<Expr> eqExpr = StrEqExpr::create(symbolicStr, literalExpr);

  Executor::StatePair sp =
      executor.fork(state, eqExpr, false, BranchType::StrEq);

  if (sp.first)
    executor.bindLocal(target, *sp.first, ConstantExpr::alloc(1, Expr::Int32));
  if (sp.second)
    executor.bindLocal(target, *sp.second,
                       ConstantExpr::alloc(0, Expr::Int32));
}

void SpecialFunctionHandler::handleStringLength(
    ExecutionState &state, KInstruction *target,
    std::vector<ref<Expr>> &arguments) {
  assert(arguments.size() == 1 &&
         "invalid number of arguments to klee_string_length");

  ref<Expr> handleExpr = executor.toUnique(state, arguments[0]);
  if (!isa<ConstantExpr>(handleExpr)) {
    executor.terminateStateOnUserError(
        state, "klee_string_length: could not resolve string handle");
    return;
  }
  uint64_t handle = cast<ConstantExpr>(handleExpr)->getZExtValue();
  if (handle == 0 || handle > state.symbolicStrings.size()) {
    executor.terminateStateOnUserError(
        state, "klee_string_length: invalid string handle");
    return;
  }
  uint64_t index = handle - 1;

  ref<Expr> symbolicStr = state.symbolicStrings[index].second;

  // Build string length expression (returns Int64)
  ref<Expr> lenExpr = StrLenExpr::create(symbolicStr);
  executor.bindLocal(target, state, lenExpr);
}

// Helper to extract a symbolic string from a handle argument.
static ref<Expr> resolveStringHandle(const ExecutionState &state,
                                     ref<Expr> handleExpr) {
  assert(isa<klee::ConstantExpr>(handleExpr) &&
         "String handle must be concrete");
  uint64_t handle = cast<klee::ConstantExpr>(handleExpr)->getZExtValue();
  assert(handle > 0 && handle <= state.symbolicStrings.size() &&
         "Invalid string handle");
  return state.symbolicStrings[handle - 1].second;
}

void SpecialFunctionHandler::handleStringConcat(
    ExecutionState &state, KInstruction *target,
    std::vector<ref<Expr>> &arguments) {
  assert(arguments.size() == 2 &&
         "invalid number of arguments to klee_string_concat");

  ref<Expr> s1 = resolveStringHandle(state, arguments[0]);
  ref<Expr> s2 = resolveStringHandle(state, arguments[1]);
  ref<Expr> result = StrConcatExpr::create(s1, s2);

  // Store the result as a new symbolic string and return its handle
  state.symbolicStrings.push_back({"concat_result", result});
  unsigned newIndex = state.symbolicStrings.size();
  executor.bindLocal(target, state,
                     ConstantExpr::alloc(newIndex,
                                         Context::get().getPointerWidth()));
}

void SpecialFunctionHandler::handleStringContains(
    ExecutionState &state, KInstruction *target,
    std::vector<ref<Expr>> &arguments) {
  assert(arguments.size() == 2 &&
         "invalid number of arguments to klee_string_contains");

  ref<Expr> str = resolveStringHandle(state, arguments[0]);
  std::string substr = readStringAtAddress(state, arguments[1]);
  ref<Expr> subExpr = StrLiteralExpr::create(substr);
  ref<Expr> containsExpr = StrContainsExpr::create(str, subExpr);

  Executor::StatePair sp =
      executor.fork(state, containsExpr, false, BranchType::StrEq);
  if (sp.first)
    executor.bindLocal(target, *sp.first, ConstantExpr::alloc(1, Expr::Int32));
  if (sp.second)
    executor.bindLocal(target, *sp.second,
                       ConstantExpr::alloc(0, Expr::Int32));
}

void SpecialFunctionHandler::handleStringIndexOf(
    ExecutionState &state, KInstruction *target,
    std::vector<ref<Expr>> &arguments) {
  assert(arguments.size() == 2 &&
         "invalid number of arguments to klee_string_indexof");

  ref<Expr> str = resolveStringHandle(state, arguments[0]);
  std::string substr = readStringAtAddress(state, arguments[1]);
  ref<Expr> subExpr = StrLiteralExpr::create(substr);
  ref<Expr> idxExpr = StrIndexOfExpr::create(str, subExpr);

  executor.bindLocal(target, state, idxExpr);
}

void SpecialFunctionHandler::handleStringCharAt(
    ExecutionState &state, KInstruction *target,
    std::vector<ref<Expr>> &arguments) {
  assert(arguments.size() == 2 &&
         "invalid number of arguments to klee_string_char_at");

  ref<Expr> str = resolveStringHandle(state, arguments[0]);
  ref<Expr> idx = arguments[1]; // Already a bitvector expression
  ref<Expr> charExpr = StrCharAtExpr::create(str, idx);

  executor.bindLocal(target, state, charExpr);
}

void SpecialFunctionHandler::handleStringSubstr(
    ExecutionState &state, KInstruction *target,
    std::vector<ref<Expr>> &arguments) {
  assert(arguments.size() == 3 &&
         "invalid number of arguments to klee_string_substr");

  ref<Expr> str = resolveStringHandle(state, arguments[0]);
  ref<Expr> offset = arguments[1];
  ref<Expr> length = arguments[2];
  ref<Expr> result = StrSubstrExpr::create(str, offset, length);

  // Store result as a new symbolic string
  state.symbolicStrings.push_back({"substr_result", result});
  unsigned newIndex = state.symbolicStrings.size();
  executor.bindLocal(target, state,
                     ConstantExpr::alloc(newIndex,
                                         Context::get().getPointerWidth()));
}

void SpecialFunctionHandler::handleStringMatchesRegex(
    ExecutionState &state, KInstruction *target,
    std::vector<ref<Expr>> &arguments) {
  assert(arguments.size() == 2 &&
         "invalid number of arguments to klee_string_matches_regex");

  ref<Expr> str = resolveStringHandle(state, arguments[0]);
  std::string pattern = readStringAtAddress(state, arguments[1]);
  ref<Expr> matchExpr = StrMatchesRegexExpr::create(str, pattern);

  Executor::StatePair sp =
      executor.fork(state, matchExpr, false, BranchType::StrEq);
  if (sp.first)
    executor.bindLocal(target, *sp.first, ConstantExpr::alloc(1, Expr::Int32));
  if (sp.second)
    executor.bindLocal(target, *sp.second,
                       ConstantExpr::alloc(0, Expr::Int32));
}

void SpecialFunctionHandler::handleStrcmpStr(
    ExecutionState &state, KInstruction *target,
    std::vector<ref<Expr>> &arguments) {
  assert(arguments.size() == 2 && "invalid number of arguments to strcmp");

  // Helper lambda: resolve pointer to string variable
  auto getStrVar = [&](ref<Expr> ptr) -> ref<Expr> {
    ObjectPair op;
    ref<Expr> addr = executor.toUnique(state, ptr);
    if (!isa<klee::ConstantExpr>(addr))
      return ref<Expr>(0);
    if (!state.addressSpace.resolveOne(cast<klee::ConstantExpr>(addr), op))
      return ref<Expr>(0);
    auto it = state.stringBackedBuffers.find(op.first->id);
    if (it == state.stringBackedBuffers.end())
      return ref<Expr>(0);
    return it->second;
  };

  ref<Expr> strVar1 = getStrVar(arguments[0]);
  ref<Expr> strVar2 = getStrVar(arguments[1]);

  // Build the two sides. Non-string-backed args are read as concrete literals.
  ref<Expr> left, right;
  if (!strVar1.isNull()) {
    left = strVar1;
  } else {
    std::string lit = readStringAtAddress(state, arguments[0]);
    left = StrLiteralExpr::create(lit);
  }
  if (!strVar2.isNull()) {
    right = strVar2;
  } else {
    std::string lit = readStringAtAddress(state, arguments[1]);
    right = StrLiteralExpr::create(lit);
  }

  // If both are literals (neither is symbolic), just compare directly
  if (strVar1.isNull() && strVar2.isNull()) {
    StrLiteralExpr *l = cast<StrLiteralExpr>(left);
    StrLiteralExpr *r = cast<StrLiteralExpr>(right);
    int cmp = l->getValue().compare(r->getValue());
    executor.bindLocal(target, state, ConstantExpr::alloc(cmp, Expr::Int32));
    return;
  }

  ref<Expr> eqExpr = StrEqExpr::create(left, right);
  Executor::StatePair sp =
      executor.fork(state, eqExpr, false, BranchType::StrEq);
  if (sp.first)
    executor.bindLocal(target, *sp.first,
                       ConstantExpr::alloc(0, Expr::Int32));
  if (sp.second)
    executor.bindLocal(target, *sp.second,
                       ConstantExpr::alloc(1, Expr::Int32));
}

void SpecialFunctionHandler::handleStrlenStr(
    ExecutionState &state, KInstruction *target,
    std::vector<ref<Expr>> &arguments) {
  assert(arguments.size() == 1 && "invalid number of arguments to strlen");

  ref<Expr> strVar;
  {
    ObjectPair op;
    ref<Expr> addr = executor.toUnique(state, arguments[0]);
    if (isa<klee::ConstantExpr>(addr) &&
        state.addressSpace.resolveOne(cast<klee::ConstantExpr>(addr), op)) {
      auto it = state.stringBackedBuffers.find(op.first->id);
      if (it != state.stringBackedBuffers.end())
        strVar = it->second;
    }
  }
  if (strVar.isNull()) {
    // Not string-backed — fall back to concrete strlen
    std::string str = readStringAtAddress(state, arguments[0]);
    executor.bindLocal(
        target, state,
        ConstantExpr::alloc(str.length(), Expr::Int64));
    return;
  }

  executor.bindLocal(target, state, StrLenExpr::create(strVar));
}

void SpecialFunctionHandler::handleStrstrStr(
    ExecutionState &state, KInstruction *target,
    std::vector<ref<Expr>> &arguments) {
  assert(arguments.size() == 2 && "invalid number of arguments to strstr");

  ref<Expr> strVar;
  {
    ObjectPair op;
    ref<Expr> addr = executor.toUnique(state, arguments[0]);
    if (isa<klee::ConstantExpr>(addr) &&
        state.addressSpace.resolveOne(cast<klee::ConstantExpr>(addr), op)) {
      auto it = state.stringBackedBuffers.find(op.first->id);
      if (it != state.stringBackedBuffers.end())
        strVar = it->second;
    }
  }
  if (strVar.isNull()) {
    executor.terminateStateOnUserError(
        state, "strstr: first argument is not a string-backed symbolic buffer");
    return;
  }

  std::string needle = readStringAtAddress(state, arguments[1]);
  ref<Expr> needleExpr = StrLiteralExpr::create(needle);
  ref<Expr> containsExpr = StrContainsExpr::create(strVar, needleExpr);

  Executor::StatePair sp =
      executor.fork(state, containsExpr, false, BranchType::StrEq);
  if (sp.first) // contains → return non-NULL (original pointer)
    executor.bindLocal(target, *sp.first, arguments[0]);
  if (sp.second) // doesn't contain → return NULL
    executor.bindLocal(target, *sp.second,
                       ConstantExpr::alloc(0, Context::get().getPointerWidth()));
}

void SpecialFunctionHandler::handleStrchrStr(
    ExecutionState &state, KInstruction *target,
    std::vector<ref<Expr>> &arguments) {
  assert(arguments.size() == 2 && "invalid number of arguments to strchr");

  ref<Expr> strVar;
  {
    ObjectPair op;
    ref<Expr> addr = executor.toUnique(state, arguments[0]);
    if (isa<klee::ConstantExpr>(addr) &&
        state.addressSpace.resolveOne(cast<klee::ConstantExpr>(addr), op)) {
      auto it = state.stringBackedBuffers.find(op.first->id);
      if (it != state.stringBackedBuffers.end())
        strVar = it->second;
    }
  }
  if (strVar.isNull()) {
    // Not string-backed — concrete fallback
    std::string str = readStringAtAddress(state, arguments[0]);
    ref<Expr> ch = executor.toUnique(state, arguments[1]);
    if (isa<klee::ConstantExpr>(ch)) {
      char c = (char)cast<klee::ConstantExpr>(ch)->getZExtValue();
      const char *found = nullptr;
      for (size_t i = 0; i <= str.size(); i++) {
        if ((i < str.size() ? str[i] : '\0') == c) { found = str.c_str() + i; break; }
      }
      if (found)
        executor.bindLocal(target, state, arguments[0]);
      else
        executor.bindLocal(target, state,
            ConstantExpr::alloc(0, Context::get().getPointerWidth()));
    } else {
      executor.terminateStateOnUserError(state, "strchr: symbolic char on non-string-backed buffer");
    }
    return;
  }

  // Build single-char string literal from the char argument
  ref<Expr> ch = executor.toUnique(state, arguments[1]);
  if (!isa<klee::ConstantExpr>(ch)) {
    executor.terminateStateOnUserError(state, "strchr: symbolic char argument not yet supported");
    return;
  }
  char c = (char)cast<klee::ConstantExpr>(ch)->getZExtValue();
  char s[2] = {c, '\0'};
  ref<Expr> charStr = StrLiteralExpr::create(std::string(s));
  ref<Expr> containsExpr = StrContainsExpr::create(strVar, charStr);

  Executor::StatePair sp =
      executor.fork(state, containsExpr, false, BranchType::StrEq);
  if (sp.first)
    executor.bindLocal(target, *sp.first, arguments[0]);
  if (sp.second)
    executor.bindLocal(target, *sp.second,
                       ConstantExpr::alloc(0, Context::get().getPointerWidth()));
}

void SpecialFunctionHandler::handleStrncmpStr(
    ExecutionState &state, KInstruction *target,
    std::vector<ref<Expr>> &arguments) {
  assert(arguments.size() == 3 && "invalid number of arguments to strncmp");

  auto getStrVar = [&](ref<Expr> ptr) -> ref<Expr> {
    ObjectPair op;
    ref<Expr> addr = executor.toUnique(state, ptr);
    if (!isa<klee::ConstantExpr>(addr)) return ref<Expr>(0);
    if (!state.addressSpace.resolveOne(cast<klee::ConstantExpr>(addr), op))
      return ref<Expr>(0);
    auto it = state.stringBackedBuffers.find(op.first->id);
    if (it == state.stringBackedBuffers.end()) return ref<Expr>(0);
    return it->second;
  };

  ref<Expr> strVar1 = getStrVar(arguments[0]);
  ref<Expr> strVar2 = getStrVar(arguments[1]);

  if (strVar1.isNull() && strVar2.isNull()) {
    // Both concrete — compare directly
    std::string s1 = readStringAtAddress(state, arguments[0]);
    std::string s2 = readStringAtAddress(state, arguments[1]);
    ref<Expr> nExpr = executor.toUnique(state, arguments[2]);
    size_t n = isa<klee::ConstantExpr>(nExpr) ?
        cast<klee::ConstantExpr>(nExpr)->getZExtValue() : s1.size();
    int cmp = s1.compare(0, n, s2, 0, n);
    executor.bindLocal(target, state, ConstantExpr::alloc(cmp, Expr::Int32));
    return;
  }

  // At least one is symbolic — use StrSubstr + StrEq
  ref<Expr> left = strVar1.isNull() ?
      StrLiteralExpr::create(readStringAtAddress(state, arguments[0])) : strVar1;
  ref<Expr> right = strVar2.isNull() ?
      StrLiteralExpr::create(readStringAtAddress(state, arguments[1])) : strVar2;

  ref<Expr> n = arguments[2];
  ref<Expr> zero = ConstantExpr::alloc(0, Expr::Int64);
  ref<Expr> sub1 = StrSubstrExpr::create(left, zero, n);
  ref<Expr> sub2 = StrSubstrExpr::create(right, zero, n);
  ref<Expr> eqExpr = StrEqExpr::create(sub1, sub2);

  Executor::StatePair sp =
      executor.fork(state, eqExpr, false, BranchType::StrEq);
  if (sp.first)
    executor.bindLocal(target, *sp.first, ConstantExpr::alloc(0, Expr::Int32));
  if (sp.second)
    executor.bindLocal(target, *sp.second, ConstantExpr::alloc(1, Expr::Int32));
}

void SpecialFunctionHandler::handleMemcmpStr(
    ExecutionState &state, KInstruction *target,
    std::vector<ref<Expr>> &arguments) {
  assert(arguments.size() == 3 && "invalid number of arguments to memcmp");

  // memcmp is identical to strncmp for string-backed buffers
  handleStrncmpStr(state, target, arguments);
}

void SpecialFunctionHandler::handleMakeSymbolicStdString(
    ExecutionState &state, KInstruction *target,
    std::vector<ref<Expr>> &arguments) {
  assert(arguments.size() == 3 &&
         "invalid number of arguments to klee_make_symbolic_std_string");

  // args: (void *std_string_ptr, size_t max_len, const char *name)
  ref<Expr> strPtr = arguments[0];
  ref<Expr> maxLenExpr = executor.toUnique(state, arguments[1]);
  std::string name = readStringAtAddress(state, arguments[2]);

  if (!isa<klee::ConstantExpr>(maxLenExpr)) {
    executor.terminateStateOnUserError(
        state, "klee_make_symbolic_std_string: max_len must be concrete");
    return;
  }
  uint64_t maxLen = cast<klee::ConstantExpr>(maxLenExpr)->getZExtValue();
  if (maxLen == 0 || maxLen > 4096) maxLen = 256;

  // Resolve the std::string pointer
  Executor::ExactResolutionList rl;
  executor.resolveExact(state, strPtr, rl, "make_symbolic_std_string");

  for (auto &it : rl) {
    const MemoryObject *strMo = it.first.first;
    const ObjectState *strOs = it.first.second;
    ExecutionState *s = it.second;

    // Allocate a backing buffer for the string's character data
    MemoryObject *bufMo = executor.memory->allocate(
        maxLen + 1, /*isLocal=*/false, /*isGlobal=*/false,
        s, s->prevPC->inst, 8);
    if (!bufMo) {
      executor.terminateStateOnUserError(
          *s, "klee_make_symbolic_std_string: failed to allocate buffer");
      return;
    }

    // Make the buffer content symbolic
    executor.executeMakeSymbolic(*s, bufMo, name);

    // Create Z3 string variable linked to this buffer
    std::string strName = name + "_str";
    ref<Expr> strVar = StrVarExpr::create(strName);
    s->stringBackedBuffers[bufMo->id] = strVar;
    s->symbolicStrings.push_back({name, strVar});

    // Constrain string length < maxLen
    ref<Expr> lenExpr = StrLenExpr::create(strVar);
    executor.addConstraint(*s, UltExpr::create(lenExpr,
        ConstantExpr::alloc(maxLen, Expr::Int64)));

    // Write the std::string struct in "long mode" layout (libc++ arm64/x86_64):
    //   offset 0: pointer to data (8 bytes)
    //   offset 8: size (8 bytes) — we use a symbolic length
    //   offset 16: capacity with MSB set (8 bytes) — marks long mode
    ObjectState *wos = s->addressSpace.getWriteable(strMo, strOs);

    // Write data pointer (concrete, points to our buffer)
    ref<Expr> bufAddr = bufMo->getBaseExpr();
    wos->write(0, bufAddr);  // 64-bit pointer at offset 0

    // Write size = 0 for now. The actual length is tracked symbolically
    // via the string theory. std::string methods that check size() will
    // read from the buffer using string-backed intercepts.
    wos->write(8, ConstantExpr::alloc(0, Expr::Int64));

    // Write capacity with MSB set to indicate long mode
    uint64_t cap = maxLen | (1ULL << 63);
    wos->write(16, ConstantExpr::alloc(cap, Expr::Int64));
  }
}
