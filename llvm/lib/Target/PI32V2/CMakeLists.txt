add_llvm_component_group(PI32V2)

set(LLVM_TARGET_DEFINITIONS PI32V2.td)

tablegen(LLVM PI32V2GenAsmWriter.inc -gen-asm-writer)
tablegen(LLVM PI32V2GenCallingConv.inc -gen-callingconv)
tablegen(LLVM PI32V2GenDAGISel.inc -gen-dag-isel)
tablegen(LLVM PI32V2GenSDNodeInfo.inc -gen-sd-node-info)
tablegen(LLVM PI32V2GenDisassemblerTables.inc -gen-disassembler)
tablegen(LLVM PI32V2GenInstrInfo.inc -gen-instr-info)
tablegen(LLVM PI32V2GenMCCodeEmitter.inc -gen-emitter)
tablegen(LLVM PI32V2GenRegisterInfo.inc -gen-register-info)
tablegen(LLVM PI32V2GenSubtargetInfo.inc -gen-subtarget)

add_public_tablegen_target(PI32V2CommonTableGen)

add_llvm_target(PI32V2CodeGen
  PI32V2AsmPrinter.cpp
  PI32V2FrameLowering.cpp
  PI32V2IfBlocks.cpp
  PI32V2InstrInfo.cpp
  PI32V2ISelDAGToDAG.cpp
  PI32V2ISelLowering.cpp
  PI32V2RegisterInfo.cpp
  PI32V2SelectionDAGInfo.cpp
  PI32V2Subtarget.cpp
  PI32V2TargetMachine.cpp

  LINK_COMPONENTS
  Analysis
  AsmPrinter
  CodeGen
  CodeGenTypes
  Core
  MC
  SelectionDAG
  Support
  Target
  TargetParser
  PI32V2Desc
  PI32V2Info

  ADD_TO_COMPONENT
  PI32V2
  )

add_subdirectory(AsmParser)
add_subdirectory(Disassembler)
add_subdirectory(MCTargetDesc)
add_subdirectory(TargetInfo)
