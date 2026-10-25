add_llvm_component_library(LLVMPI32V2Info
  PI32V2TargetInfo.cpp

  LINK_COMPONENTS
  MC
  Support

  ADD_TO_COMPONENT
  PI32V2
  )
