add_llvm_component_library(LLVMPI32V2Desc
  PI32V2AsmBackend.cpp
  PI32V2BaseInfo.cpp
  PI32V2Compress.cpp
  PI32V2ELFObjectWriter.cpp
  PI32V2FixupKinds.cpp
  PI32V2InstPrinter.cpp
  PI32V2MCAsmInfo.cpp
  PI32V2MCCodeEmitter.cpp
  PI32V2MCTargetDesc.cpp

  LINK_COMPONENTS
  MC
  Support
  TargetParser
  PI32V2Info

  ADD_TO_COMPONENT
  PI32V2
  )
