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
