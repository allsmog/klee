// RUN: %clang %s -emit-llvm %O0opt -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --solver-backend=z3 --external-calls=none %t.bc 2>&1 | FileCheck %s
// REQUIRES: z3
// CHECK: positive
// CHECK: completed paths = 2

#include "klee/klee.h"

extern int pthread_mutex_init(void*, void*);
extern int pthread_mutex_lock(void*);
extern int pthread_mutex_unlock(void*);

int main() {
  int mutex[8];
  pthread_mutex_init(mutex, 0);
  pthread_mutex_lock(mutex);
  int x;
  klee_make_symbolic(&x, sizeof(x), "x");
  if (x > 0)
    klee_warning("positive");
  pthread_mutex_unlock(mutex);
  return 0;
}
