; RUN: llvm-as -disable-verify < %s | llvm-dis 2>&1 | FileCheck %s
; RUN: llvm-as -disable-verify < %s | llvm-dis -disable-output 2>&1 \
; RUN:   | FileCheck --allow-empty --check-prefix=WARN %s

; Before LLVM 5, inlining a function with a loop left the loop's llvm.loop
; locations (start and end) in the inlined function's scope. When a function
; body is read, such a loop gets loop metadata of its own whose locations are
; the branch's; the rest of the debug info is kept.

; WARN-NOT: ignoring invalid debug info

; CHECK: define void @caller(ptr %p) !dbg [[CALLER:![0-9]+]]
; CHECK: br i1 %c, label %loop, label %exit, !dbg [[BR:![0-9]+]], !llvm.loop [[LOOP:![0-9]+]]
; CHECK: define void @other() !dbg
; CHECK: br label %l, !dbg [[OTHERLOC:![0-9]+]], !llvm.loop [[KEEP:![0-9]+]]
; CHECK-DAG: [[LOOP]] = distinct !{[[LOOP]], [[BR]], [[BR]], [[UNROLL:![0-9]+]]}
; CHECK-DAG: [[UNROLL]] = !{!"llvm.loop.unroll.disable"}
; CHECK-DAG: [[BR]] = !DILocation(line: 3, column: 5, scope: {{![0-9]+}}, inlinedAt: {{![0-9]+}})
; CHECK-DAG: [[KEEP]] = distinct !{[[KEEP]], [[OTHERLOC]], [[OTHERLOC]]}

define void @caller(ptr %p) !dbg !4 {
entry:
  br label %loop, !dbg !10
loop:
  %i = phi i32 [ 0, %entry ], [ %n, %loop ]
  store volatile i32 %i, ptr %p, !dbg !11
  %n = add i32 %i, 1, !dbg !11
  %c = icmp ult i32 %n, 8, !dbg !11
  br i1 %c, label %loop, label %exit, !dbg !11, !llvm.loop !12
exit:
  ret void, !dbg !10
}

define void @other() !dbg !20 {
entry:
  br label %l, !dbg !21
l:
  br label %l, !dbg !21, !llvm.loop !22
}

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!2, !3}

!0 = distinct !DICompileUnit(language: DW_LANG_C99, file: !1, emissionKind: FullDebug)
!1 = !DIFile(filename: "t.c", directory: "/")
!2 = !{i32 2, !"Debug Info Version", i32 3}
!3 = !{i32 2, !"Dwarf Version", i32 4}
!4 = distinct !DISubprogram(name: "caller", scope: !1, file: !1, line: 10, type: !5, scopeLine: 10, spFlags: DISPFlagDefinition, unit: !0)
!5 = !DISubroutineType(types: !{null})
!6 = distinct !DISubprogram(name: "inlined", scope: !1, file: !1, line: 1, type: !5, scopeLine: 1, spFlags: DISPFlagDefinition | DISPFlagLocalToUnit, unit: !0)
!10 = !DILocation(line: 11, column: 3, scope: !4)
!11 = !DILocation(line: 3, column: 5, scope: !6, inlinedAt: !10)
!12 = distinct !{!12, !13, !14, !15}
!13 = !DILocation(line: 2, column: 3, scope: !6)
!14 = !DILocation(line: 4, column: 3, scope: !6)
!15 = !{!"llvm.loop.unroll.disable"}
!20 = distinct !DISubprogram(name: "other", scope: !1, file: !1, line: 20, type: !5, scopeLine: 20, spFlags: DISPFlagDefinition, unit: !0)
!21 = !DILocation(line: 21, column: 3, scope: !20)
!22 = distinct !{!22, !21, !21}
