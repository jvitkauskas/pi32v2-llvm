// REQUIRES: pi32v2-registered-target
// RUN: %clang_cc1 -triple pi32v2 -target-cpu r3 -fforce-enable-int128 -O1 -emit-llvm -o - %s | FileCheck %s
// RUN: not %clang_cc1 -triple pi32v2 -target-cpu r3 -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=OFF
// __int128 is off unless -fforce-enable-int128 asks for it (JieLi's compiler has none, and the runtime has no
// 128-bit helpers). With it, the type is the backend's i128, which is also Rust's i128 and u128 on this target:
// word-aligned, and an i128 in the IR, as LLVM passes it to its own runtime calls. The backend gives an argument
// four words, and returns the value through a hidden pointer, since r0 and r1 hold only two.
// OFF: __int128 is not supported on this target

typedef unsigned __int128 u128;
struct P { char c; u128 v; };
_Static_assert(_Alignof(u128) == 4, "word-aligned, as the data layout has i128");
_Static_assert(sizeof(struct P) == 20, "no padding to 16");
_Static_assert(__builtin_offsetof(struct P, v) == 4, "");

// CHECK-LABEL: define{{.*}} i128 @pass(i32 noundef %a, i128 noundef %v, i32 noundef %k)
// CHECK: call i128 @take(i32 noundef %a, i128 noundef %v, i32 noundef %k)
u128 take(int, u128, int);
u128 pass(int a, u128 v, int k) { return take(a, v, k); }

// Sixteen bytes inline, word-aligned; the int follows them.
// CHECK-LABEL: @get(
// CHECK: [[PK:%.*]] = getelementptr inbounds nuw i8, ptr %argp.cur, i32 16
// CHECK: load i128, ptr %argp.cur, align 4
// CHECK: load i32, ptr [[PK]], align 4
u128 get(__builtin_va_list *ap) {
  u128 v = __builtin_va_arg(*ap, u128);
  return v + __builtin_va_arg(*ap, int);
}
