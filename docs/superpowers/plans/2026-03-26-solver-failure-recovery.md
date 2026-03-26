# Solver Failure Recovery Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace 30 `assert(success && "FIXME: Unhandled solver failure")` crash sites with graceful state termination, so KLEE continues exploring other paths when a solver query fails.

**Architecture:** Each crash site becomes a check that calls `terminateStateOnSolverError(state, message)` (already exists in KLEE) and returns/breaks. The terminated state produces a `.solver.err` test file. No new infrastructure needed — just systematic replacement of asserts with error handling.

**Tech Stack:** C++17, KLEE core (lib/Core/)

---

## File Structure

| File | Responsibility | Change |
|------|---------------|--------|
| `lib/Core/Executor.cpp` | Main executor — 18 crash sites | Replace asserts with `terminateStateOnSolverError` + return/break |
| `lib/Core/SpecialFunctionHandler.cpp` | Intrinsic handlers — 4 crash sites | Replace asserts with `terminateStateOnSolverError` + return |
| `lib/Core/SeedInfo.cpp` | Seed patching — 6 crash sites | Replace asserts with `klee_warning` + early return (no direct Executor access) |
| `lib/Core/ImpliedValue.cpp` | Debug-only value checking — 2 crash sites | Replace asserts with `klee_warning` + return (utility function, no state) |
| `test/Feature/SolverFailureRecovery.c` | NEW — end-to-end test | Verify KLEE produces `.solver.err` on timeout, doesn't crash |

---

## Chunk 1: Executor.cpp (18 sites)

### Task 1: Replace solver failure asserts in Executor.cpp — fork/branch functions

**Files:**
- Modify: `lib/Core/Executor.cpp` (lines ~954, 1026, 1114, 1176, 1252)

These are inside `fork()` and `branch()` which handle seeding. The pattern:

```cpp
// BEFORE:
bool success = solver->getValue(...);
assert(success && "FIXME: Unhandled solver failure");
(void) success;

// AFTER:
bool success = solver->getValue(...);
if (!success) {
  terminateStateOnSolverError(state, "Solver failure in fork/branch");
  return; // or break; if inside a loop
}
```

- [ ] **Step 1:** Find all 5 sites in fork/branch (lines ~940-1260). Each `assert(success && "FIXME: Unhandled solver failure")` becomes an `if (!success)` check.

- [ ] **Step 2:** For sites inside `for` loops (lines 954, 1114, 1176, 1252), use `break` instead of `return` — the enclosing code handles the case when no satisfying condition is found.

- [ ] **Step 3:** For the site at line 1026 (not in a loop), use:
```cpp
if (!success) {
  terminateStateOnSolverError(current, "Solver failure in fork");
  return StatePair(nullptr, nullptr);
}
```

- [ ] **Step 4:** Build: `cmake --build build -j$(sysctl -n hw.ncpu)` — verify no errors.

- [ ] **Step 5:** Commit:
```bash
git add lib/Core/Executor.cpp
git commit -m "Replace solver failure asserts in fork/branch with graceful termination"
```

### Task 2: Replace solver failure asserts in Executor.cpp — instruction execution

**Files:**
- Modify: `lib/Core/Executor.cpp` (lines ~1333, 1379, 1391, 2285, 2295, 2375, 2404, 2529)

These are inside `executeInstruction()` for switch, GetElementPtr, and other instructions.

- [ ] **Step 1:** For each site, replace the assert with:
```cpp
if (!success) {
  terminateStateOnSolverError(state, "Solver failure in instruction execution");
  return;
}
```

- [ ] **Step 2:** For sites inside loops (lines 2285, 2375, 2529), use `break` and let the enclosing function handle the empty result.

- [ ] **Step 3:** Build and verify no errors.

- [ ] **Step 4:** Commit:
```bash
git add lib/Core/Executor.cpp
git commit -m "Replace solver failure asserts in executeInstruction with graceful termination"
```

### Task 3: Replace solver failure asserts in Executor.cpp — toConstant and executeAlloc

**Files:**
- Modify: `lib/Core/Executor.cpp` (lines ~3720, 4263, 4274, 4289, 4295)

- [ ] **Step 1:** `toConstant()` (line 3720) — this function returns `ref<ConstantExpr>`. On failure:
```cpp
if (!success) {
  terminateStateOnSolverError(state, "Solver failure in toConstant");
  return ConstantExpr::alloc(0, e->getWidth());
}
```

- [ ] **Step 2:** `executeAlloc()` (lines 4263, 4274, 4289, 4295) — all inside the symbolic-size path. On failure:
```cpp
if (!success) {
  terminateStateOnSolverError(state, "Solver failure in executeAlloc");
  return;
}
```

- [ ] **Step 3:** Build and verify no errors.

- [ ] **Step 4:** Commit:
```bash
git add lib/Core/Executor.cpp
git commit -m "Replace solver failure asserts in toConstant/executeAlloc with graceful termination"
```

## Chunk 2: SpecialFunctionHandler.cpp, SeedInfo.cpp, ImpliedValue.cpp

### Task 4: Replace solver failure asserts in SpecialFunctionHandler.cpp

**Files:**
- Modify: `lib/Core/SpecialFunctionHandler.cpp` (lines ~510, 613, 618, 848)

- [ ] **Step 1:** Each site follows the same pattern. Replace with:
```cpp
if (!success) {
  executor.terminateStateOnSolverError(state, "Solver failure in special function handler");
  return;
}
```

- [ ] **Step 2:** Build and verify.

- [ ] **Step 3:** Commit:
```bash
git add lib/Core/SpecialFunctionHandler.cpp
git commit -m "Replace solver failure asserts in SpecialFunctionHandler with graceful termination"
```

### Task 5: Replace solver failure asserts in SeedInfo.cpp

**Files:**
- Modify: `lib/Core/SeedInfo.cpp` (lines ~97, 103, 118, 137, 143, 160)

SeedInfo doesn't have direct access to `terminateStateOnSolverError`. These are inside `SeedInfo::patchSeed()`. The safest approach: log a warning and return early, letting the caller handle the incomplete seed.

- [ ] **Step 1:** For each site, replace the assert with:
```cpp
if (!success) {
  klee_warning("Solver failure during seed patching, skipping seed");
  return;
}
```

- [ ] **Step 2:** The `#ifndef NDEBUG` block at line 160 can keep its assert — it's debug-only validation.

- [ ] **Step 3:** Build and verify.

- [ ] **Step 4:** Commit:
```bash
git add lib/Core/SeedInfo.cpp
git commit -m "Replace solver failure asserts in SeedInfo with warnings and early return"
```

### Task 6: Replace solver failure asserts in ImpliedValue.cpp

**Files:**
- Modify: `lib/Core/ImpliedValue.cpp` (lines ~229, 234)

This is a debug-checking utility (`checkForImpliedValues`). It's only called from a commented-out/disabled code path (`Executor::doImpliedValueConcretization` which starts with `abort()`). Replace asserts with warnings.

- [ ] **Step 1:** Replace both asserts:
```cpp
if (!success) {
  klee_warning("Solver failure in implied value check");
  return;
}
```

- [ ] **Step 2:** Build and verify.

- [ ] **Step 3:** Commit:
```bash
git add lib/Core/ImpliedValue.cpp
git commit -m "Replace solver failure asserts in ImpliedValue with warnings"
```

## Chunk 3: Testing

### Task 7: Verify no remaining FIXME solver failure asserts

- [ ] **Step 1:** Run:
```bash
grep -rn "FIXME.*[Uu]nhandled solver failure" lib/Core/
```
Expected output: empty (no remaining asserts).

- [ ] **Step 2:** Run:
```bash
grep -rn "FIXME.*solver failure" lib/Core/
```
Expected output: empty.

### Task 8: Create end-to-end solver failure test

**Files:**
- Create: `test/Feature/SolverFailureRecovery.c`

- [ ] **Step 1:** Write test that triggers a solver timeout:
```c
// RUN: %clang %s -emit-llvm %O0opt -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --solver-backend=z3 --max-solver-time=1 %t.bc 2>&1 | FileCheck %s
// REQUIRES: z3
// CHECK: completed paths

#include "klee/klee.h"

int main() {
  int x;
  klee_make_symbolic(&x, sizeof(x), "x");
  // Simple path that should complete even if some solver queries fail
  if (x > 0)
    return 1;
  return 0;
}
```

This test verifies KLEE completes without crashing. The `--max-solver-time=1` (1 second) keeps it fast but shouldn't actually trigger timeout on this simple query — it just confirms the flag works.

- [ ] **Step 2:** Run a more thorough manual test by compiling and running KLEE on the test with a very short timeout:
```bash
clang -I include -emit-llvm -c -g -O0 test/Feature/SolverFailureRecovery.c -o /tmp/solver_test.bc
./build/bin/klee --solver-backend=z3 --max-solver-time=1 --external-calls=none /tmp/solver_test.bc
```
Expected: KLEE completes without crashing, reports completed paths.

- [ ] **Step 3:** Run the existing test suite to verify no regressions:
```bash
# Run the alloc mismatch and string tests from previous work
./build/bin/klee --solver-backend=z3 --external-calls=none /tmp/alloc_mismatch.bc  # should detect mismatch
./build/bin/klee --solver-backend=z3 --external-calls=none /tmp/string_test.bc     # should fork on string eq
./build/bin/klee --solver-backend=z3 --external-calls=none /tmp/klee_test.bc       # basic symbolic test
```

- [ ] **Step 4:** Commit:
```bash
git add test/Feature/SolverFailureRecovery.c
git commit -m "Add solver failure recovery test"
```

### Task 9: Final commit — squash or tag

- [ ] **Step 1:** If desired, squash the solver recovery commits:
```bash
git rebase -i HEAD~5  # squash into one commit
```
Or leave as separate commits for a cleaner history.

- [ ] **Step 2:** Verify final build:
```bash
cmake --build build -j$(sysctl -n hw.ncpu)
```

- [ ] **Step 3:** Run final smoke test:
```bash
./build/bin/klee --version  # should work
```
