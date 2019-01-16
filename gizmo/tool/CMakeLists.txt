set(LLVM_LINK_COMPONENTS
  Support
  )

add_clang_executable(gizmo
  CrochetPatch.cpp
  )

target_link_libraries(gizmo
  PRIVATE
  crochetPatch
  clangBasic
  clangFrontend
  clangRewrite
  clangTooling 
  clangToolingCore
  )

install(TARGETS crochet-patch
  RUNTIME DESTINATION bin)
