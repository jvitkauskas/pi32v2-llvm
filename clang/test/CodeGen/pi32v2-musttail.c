// REQUIRES: pi32v2-registered-target
// An aggregate or a _BitInt wider than 64 bits goes as a pointer to a copy in the caller's frame, which a tail call
// gives up, so musttail can't forward one: an error, not a goto with a dangling pointer.
// RUN: %clang_cc1 -triple pi32v2 -target-cpu r3 -O2 -S -o /dev/null -verify %s

struct S { int a, b, c; };
extern int take_s(struct S);
extern int take_w(_BitInt(128));
extern int take_i(int, int);

int fwd_s(struct S s) { // expected-error {{this musttail call cannot be a tail call}}
  [[clang::musttail]] return take_s(s);
}
int fwd_w(_BitInt(128) w) { // expected-error {{this musttail call cannot be a tail call}}
  [[clang::musttail]] return take_w(w);
}
int fwd_i(int a, int b) {
  [[clang::musttail]] return take_i(b, a);
}
