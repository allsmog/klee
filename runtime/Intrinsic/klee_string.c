/*===-- klee_string.c -----------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===*/

/* Concrete replay stubs for symbolic string intrinsics.
 * During symbolic execution these are intercepted by SpecialFunctionHandler.
 * During concrete replay these provide default behavior. */

typedef unsigned long size_t;

const char *klee_make_symbolic_string(const char *name) {
  (void)name;
  return "";
}

int klee_string_eq(const char *a, const char *b) {
  while (*a && *a == *b) { a++; b++; }
  return *a == *b;
}

size_t klee_string_length(const char *s) {
  size_t len = 0;
  while (*s++) len++;
  return len;
}

void klee_make_symbolic_std_string(void *str, size_t max_len, const char *name) {
  /* In replay mode, do nothing — the string is already concrete. */
  (void)str;
  (void)max_len;
  (void)name;
}
