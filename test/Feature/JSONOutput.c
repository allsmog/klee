// RUN: %clang %s -emit-llvm %O0opt -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --write-json-tests --write-json-summary --external-calls=none %t.bc
// RUN: test -f %t.klee-out/test000001.json
// RUN: test -f %t.klee-out/summary.json

#include "klee/klee.h"

int main() {
  int x;
  klee_make_symbolic(&x, sizeof(x), "x");
  return x > 0 ? 1 : 0;
}
