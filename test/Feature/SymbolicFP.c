// RUN: %clang %s -emit-llvm %O0opt -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --solver-backend=z3 --external-calls=none %t.bc 2>&1 | FileCheck %s
// REQUIRES: z3
// CHECK-DAG: big
// CHECK-DAG: small

#include "klee/klee.h"

int main() {
  double x;
  klee_make_symbolic(&x, sizeof(x), "x");
  if (x > 1.5) {
    klee_warning("big");
  } else {
    klee_warning("small");
  }
  return 0;
}
