cmake_minimum_required(VERSION 3.12)
project(HDF5_VOL_BYPASS_TEST VERSION 0.0.1 LANGUAGES C)

add_executable(h5_create h5_create.c)
add_executable(h5_read h5_read.c)
add_executable(posix_read_mthread posix_read_mthread.c)
add_executable(posix_read_tpool posix_read_tpool.c)

target_link_libraries(h5_create PRIVATE ${HDF5_LIBRARIES})
target_link_libraries(h5_read PRIVATE ${HDF5_LIBRARIES})

target_include_directories(h5_create
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PUBLIC ${HDF5_INCLUDE_DIRS}
)

target_include_directories(h5_read
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PUBLIC ${HDF5_INCLUDE_DIRS}
)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_compile_options(posix_read_mthread PRIVATE -lpthread)
  target_compile_options(posix_read_tpool PRIVATE -lpthread)
elseif (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  # other macOS-specific flags for Clang
endif ()

# Copy test scripts from source tree to build tree
set (TEST_SCRIPTS
  run_chunk_simple.sh
  run_contiguous_simple.sh
  run_multi_dsets.sh
  run_multi_files.sh
)

foreach (script ${TEST_SCRIPTS})
  add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${script}
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/${script} ${CMAKE_CURRENT_BINARY_DIR}/${script}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${script}
    COMMENT "Copying script to build tree"
  )

  add_custom_target(${script}
    ALL
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${script}
  )
endforeach()