// RUN: %clang_cc1 -triple pi32v2 -O1 -emit-llvm -disable-llvm-passes -o - %s | FileCheck %s
// REQUIRES: pi32v2-registered-target
// JieLi's __attribute__((always_inline_when_const_args)), as its clang writes
// it in IR.

// CHECK: define {{.*}}void @f(i32 {{.*}}) [[A:#[0-9]+]]
// CHECK: attributes [[A]] = {{{.*}}"always-inline-when-const-args"
__attribute__((always_inline_when_const_args)) void f(int a) { (void)a; }
