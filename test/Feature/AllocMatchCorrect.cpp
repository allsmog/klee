// RUN: %clangxx %s -emit-llvm %O0opt -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --exit-on-error --exit-on-error-type=MismatchedFree --external-calls=none %t.bc

// Verify that correctly paired allocations/deallocations do not produce errors.

#include <cstdlib>

int main() {
  // malloc + free
  int *a = (int *)malloc(sizeof(int));
  free(a);

  // new + delete
  int *b = new int(1);
  delete b;

  // new[] + delete[]
  int *c = new int[10];
  delete[] c;

  return 0;
}
