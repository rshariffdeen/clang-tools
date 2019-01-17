set(LLVM_LINK_COMPONENTS
  Support
  )


include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}
  include
  )


add_subdirectory(tool)

