// RUN: %clang_cc1 -triple pi32v2 -emit-llvm -o - %s | FileCheck %s
// REQUIRES: pi32v2-registered-target

// CHECK: define{{.*}} void @irq() [[ATTRS:#[0-9]+]]
// CHECK: attributes [[ATTRS]] = {{.*}}"interrupt"
void __attribute__((interrupt(""))) irq(void) {}
