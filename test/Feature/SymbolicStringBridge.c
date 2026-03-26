// RUN: %clang %s -emit-llvm %O0opt -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --solver-backend=z3 --external-calls=none %t.bc 2>&1 | FileCheck %s
// REQUIRES: z3
// CHECK-DAG: is admin
// CHECK-DAG: long input

#include "klee/klee.h"

int strcmp(const char *a, const char *b);
unsigned long strlen(const char *s);

int main() {
  char buf[32];
  klee_make_symbolic(buf, 32, "input");

  if (strcmp(buf, "admin") == 0) {
    klee_warning("is admin");
  }
  if (strlen(buf) > 10) {
    klee_warning("long input");
  }
  return 0;
}
