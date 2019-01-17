set(LLVM_LINK_COMPONENTS
  Support
  )

add_clang_executable(gizmo
  Gizmo.cpp
  )

target_link_libraries(gizmo
  PRIVATE
  Gizmo
  clangBasic
  clangFrontend
  clangRewrite
  clangTooling 
  clangToolingCore
  )

install(TARGETS gizmo
  RUNTIME DESTINATION bin)
