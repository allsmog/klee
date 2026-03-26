# KLEE Enhancements

This fork adds symbolic string support (via Z3 string theory), symbolic floating-point (via Z3 FP theory), allocation mismatch detection, JSON/HTML reporting, pthread stubs, an external function plugin system, LLVM 20 support, and solver failure recovery to KLEE. All features are tested end-to-end (28/28 pass).

## Quick Start

```bash
# Dependencies: LLVM 20, Z3, CMake, SQLite
brew install llvm z3 cmake sqlite

# Build
mkdir build && cd build
cmake -DLLVM_DIR=$(brew --prefix llvm)/lib/cmake/llvm \
      -DENABLE_SOLVER_Z3=ON -DENABLE_SOLVER_STP=OFF \
      -DCMAKE_BUILD_TYPE=Debug ..
cmake --build . -j$(sysctl -n hw.ncpu)

# Test
./bin/klee --version  # KLEE 3.3-pre, LLVM 20.1.7
```

---

## Allocation/Deallocation Mismatch Detection

Detects undefined behavior from mismatched allocation/deallocation pairs.

**What it catches:**
- `new` + `free` (should be `delete`)
- `malloc` + `delete` (should be `free`)
- `new[]` + `delete` (should be `delete[]`)

**Example:**
```c
int *p = new int(42);
free(p);  // BUG: allocated with new, freed with free
```

```bash
klee --external-calls=none program.bc
# Output: KLEE: ERROR: allocation/deallocation mismatch: allocated with new, freed with malloc
# File: test000001.mismatch_free.err
```

**CLI:** `--exit-on-error-type=MismatchedFree` to stop on first mismatch.

---

## Symbolic Strings

Three layers of string support, all powered by Z3's native string solver (Z3str3).

### Intrinsic API

```c
#include "klee/klee.h"

const char *s = klee_make_symbolic_string("input");

if (klee_string_eq(s, "hello")) { /* KLEE forks here */ }

size_t len = klee_string_length(s);
if (len > 10) { /* forks on length constraint */ }

if (klee_string_contains(s, "admin")) { /* forks */ }
if (klee_string_matches_regex(s, "[a-z]+@[a-z]+")) { /* regex match */ }

long idx = klee_string_indexof(s, "key");
char c = klee_string_char_at(s, 0);
const char *sub = klee_string_substr(s, 0, 5);
const char *cat = klee_string_concat(s1, s2);
```

### C String Bridge (Transparent)

Normal C code automatically uses Z3 string theory when operating on symbolic buffers:

```c
char buf[32];
klee_make_symbolic(buf, 32, "input");

if (strcmp(buf, "admin") == 0) { ... }  // 1 fork, not 6
if (strlen(buf) > 10) { ... }          // 1 fork, not 32
if (strstr(buf, "secret")) { ... }     // 1 fork via Z3
```

**Intercepted functions:** `strcmp`, `strlen`, `strstr`, `strchr`, `strncmp`, `memcmp`

No special intrinsics needed. KLEE automatically creates a dual representation (byte array + Z3 string variable) for every symbolic buffer.

### std::string Support

```c
#include "klee/klee.h"
#include <string>

std::string s;
klee_make_symbolic_std_string(&s, 256, "input");
// s is now symbolic — std::string methods that call memcmp/strcmp
// will use Z3 string theory automatically
```

### Regex

```c
if (klee_string_matches_regex(s, "[a-z]+@[a-z]+\\.com")) {
    // Z3 checks regex membership
}
```

Supports: `.`, `*`, `+`, `?`, `[a-z]` ranges, `|` alternation, `()` groups, `\\` escapes.

---

## Symbolic Floating Point

Floating-point operations are now symbolic instead of concretized.

**Before:** `if (x > 1.5)` with symbolic `double x` picked ONE value, explored ONE path.
**After:** KLEE forks and explores both paths using Z3's FP theory.

```c
double x;
klee_make_symbolic(&x, sizeof(x), "x");

if (x > 1.5) { ... }       // forks: x > 1.5 and x <= 1.5
if (x * 2.0 > 10.0) { ... } // symbolic multiplication + comparison

int i = (int)x;              // symbolic FP-to-int conversion
double y = (double)i;         // symbolic int-to-FP conversion
```

**Symbolic operations:** `fadd`, `fsub`, `fmul`, `fdiv`, `frem`, `fneg`, `fcmp`, `fptoui`, `fptosi`, `uitofp`, `sitofp`, `fptrunc`, `fpext`

---

## JSON/HTML Reports

### JSON Test Cases

```bash
klee --write-json-tests program.bc
```

Each test case is written as a `.json` file:
```json
{
  "testId": 1,
  "error": {
    "type": "mismatch_free.err",
    "message": "allocation/deallocation mismatch: allocated with new, freed with malloc"
  },
  "inputs": [
    {"name": "x", "size": 4, "data": [42, 0, 0, 0]}
  ]
}
```

### JSON Summary

```bash
klee --write-json-summary program.bc
```

Writes `summary.json`:
```json
{
  "totalTests": 8,
  "generatedTests": 8,
  "completedPaths": 8,
  "exploredPaths": 8
}
```

### HTML Coverage Report

```bash
klee --write-html-cov program.bc
```

Generates `coverage/index.html` with per-file source listing, covered lines highlighted green, and a summary table. Self-contained HTML with inline CSS.

---

## Solver Failure Recovery

Replaced 30 `assert(success && "FIXME: Unhandled solver failure")` crash sites with graceful error handling. When Z3 times out or encounters an unsupported expression, KLEE now terminates only the affected state (producing a `.solver.err` file) and continues exploring other paths.

**Before:** Any solver timeout crashed KLEE.
**After:** KLEE continues and reports which states had solver issues.

---

## Pthread Stubs

Single-threaded models for common pthread operations. Programs that use mutexes and condition variables no longer crash on "disallowed external call."

**Modeled (return 0):** `pthread_mutex_init`, `lock`, `unlock`, `destroy`, `trylock`, `pthread_cond_init`, `wait`, `signal`, `broadcast`, `destroy`

**Unsupported (terminates state):** `pthread_create`, `pthread_join`

```c
pthread_mutex_t mtx;
pthread_mutex_init(&mtx, NULL);
pthread_mutex_lock(&mtx);
// ... symbolic execution continues normally ...
pthread_mutex_unlock(&mtx);
```

---

## External Function Plugin System

Write custom function handlers as shared libraries and load them at runtime.

### Writing a Plugin

```cpp
// my_plugin.cpp
#include "klee/Core/PluginHandler.h"
#include <cstdio>

static void handleMyFunc(void *sfh, void *state, void *target, void *args) {
    fprintf(stderr, "my_function was intercepted!\n");
}

static const KleePluginHandlerInfo handlers[] = {
    {"my_function", handleMyFunc, false, false},
    {nullptr, nullptr, false, false}  // sentinel
};

extern "C" const KleePluginHandlerInfo *klee_plugin_get_handlers() {
    return handlers;
}
```

### Building and Loading

```bash
clang++ -shared -fPIC -I/path/to/klee/include my_plugin.cpp -o my_plugin.so
klee --load-plugin=./my_plugin.so --external-calls=none program.bc
```

Plugins are checked before builtin handlers, so they can override default behavior. Multiple plugins can be loaded with multiple `--load-plugin` flags.

---

## LLVM 20 Support

Ported KLEE to build with LLVM 20 by implementing the new pass manager pipelines:

- **`Instrument.cpp`**: Scalarizer, LowerAtomic, custom KLEE passes via direct `runOnModule()`/`runOnFunction()` calls
- **`Optimize.cpp`**: `PassBuilder::buildPerModuleDefaultPipeline(O2)` + InternalizePass + StripSymbolsPass
- **`KModule.cpp`**: Fixed `DataLayout` constructor (removed `Module*` overload in LLVM 20)
- **`ExprPPrinter.cpp`**: Fixed `APInt` assertion for LLVM 20's stricter value range checking
- **`test/lit.cfg`**: Added `20.1` to known LLVM versions

---

## Test Suite

28 tests covering all features:

```bash
# Run from the build directory after building KLEE
# Tests require Z3 solver backend

# Individual feature tests:
klee --solver-backend=z3 --external-calls=none test.bc

# Full suite (see test/Feature/ for lit tests):
# AllocMismatch*.cpp, SymbolicFP.c, SymbolicStringBridge.c,
# PthreadStubs.c, JSONOutput.c, SymbolicString.c, AllocMatchCorrect.cpp
```

| Category | Tests | Status |
|----------|-------|--------|
| Content correctness (replay) | 2 | Pass |
| Negative cases | 2 | Pass |
| Feature interaction (string+FP+pthread) | 1 | Pass |
| Edge cases (empty string, NaN, nested mutex) | 3 | Pass |
| PPrinter fix | 1 | Pass |
| Memory pressure | 1 | Pass |
| Upstream regression (IsSymbolic, DivCheck, etc.) | 5 | Pass |
| Our features (13 individual tests) | 13 | Pass |
| **Total** | **28** | **All pass** |
