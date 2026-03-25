// RUN: %clangxx %s -emit-llvm %O0opt -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --external-calls=none %t.bc 2>&1 | FileCheck %s
// RUN: test -f %t.klee-out/test000001.mismatch_free.err

// CHECK: allocation/deallocation mismatch

#include <cstdlib>

int main() {
  int *p = (int *)malloc(sizeof(int));
  delete p; // BUG: allocated with malloc, freed with delete
  return 0;
}
