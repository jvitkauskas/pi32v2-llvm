// RUN: %clang_cc1 -triple pi32v2 -O1 -emit-llvm -o - %s | FileCheck %s
// REQUIRES: pi32v2-registered-target

// CHECK-LABEL: @low_word(
// CHECK: call i32 asm {{.*}}"=w,0"
int low_word(int x) {
  asm("%0 += 1" : "+w"(x));
  return x;
}

// CHECK-LABEL: @high_word(
// CHECK: call i32 asm {{.*}}"=W,W"
int high_word(int x) {
  int y;
  asm("%0 = %1" : "=W"(y) : "W"(x));
  return y;
}

// CHECK-LABEL: @low_pair(
// CHECK: call i64 asm {{.*}}"=d,0"
long long low_pair(long long x) {
  asm("%0 <<= 1" : "+d"(x));
  return x;
}

// CHECK-LABEL: @high_pair(
// CHECK: call i64 asm {{.*}}"=D,D"
long long high_pair(long long x) {
  long long y;
  asm("%0 = %1" : "=D"(y) : "D"(x));
  return y;
}

// The assembly has no dialects: { | } reach the assembler as they are written (blocks nest), and only $ is doubled.
// CHECK-LABEL: @blocks(
// CHECK: call {{.*}} asm sideeffect "rep $0 {\0A\09iff($0 > $1) { $0 = $1 }\0A\09}\0A\09 a|b $$"
void blocks(int n, int m) {
  asm volatile("rep %0 {\n\tiff(%0 > %1) { %0 = %1 }\n\t}\n\t a|b $" : "+r"(n) : "r"(m));
}
