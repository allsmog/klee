// RUN: %clang %s -emit-llvm %O0opt -c -I %klee-include -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --solver-backend=z3 --external-calls=none %t.bc 2>&1 | FileCheck %s
// REQUIRES: z3

#include "klee/klee.h"

int main() {
  const char *s = klee_make_symbolic_string("test_str");

  // Test: string equality forking
  if (klee_string_eq(s, "hello")) {
    // CHECK-DAG: string is hello
    klee_warning("string is hello");
  } else {
    // CHECK-DAG: string is not hello
    klee_warning("string is not hello");
  }

  return 0;
}
