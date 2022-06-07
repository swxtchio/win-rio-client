
set(RIO_HOME "${CMAKE_SOURCE_DIR}")

# Set directory where all binary files will be put after build
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${RIO_HOME}/bin")

# Check the Build Type.
IF(NOT "${CMAKE_BUILD_TYPE}" MATCHES "^(Debug|Release)\$")
  MESSAGE(FATAL_ERROR
          "Unknown keyword for CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}\n"
          "Acceptable keywords: Debug,Release")
ENDIF()

# Including custom cmake rules
include(${RIO_HOME}/.cmake/version-vars-windows.cmake)
#include(${SWXTCH_HOME}/.cmake/clang-cxx-dev-tools.cmake)

# Including version
set(RIO_BUILD_VER "1.0.0")
message("Build Version: ${RIO_BUILD_VER}")
message("Build Commit: ${RIO_BUILD_COMMIT}")
message("Build Date: ${RIO_BUILD_DATE}")
configure_file("auto_gen_ver_info.h.in" "${CMAKE_CURRENT_SOURCE_DIR}/auto_gen_ver_info.h" NEWLINE_STYLE UNIX)


set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
SET(CMAKE_EXE_LINKER_FLAGS  "${CMAKE_EXE_LINKER_FLAGS}")


include_directories(${RIO_HOME}/common)

if (NOT TARGET fmt)
    add_subdirectory(${RIO_HOME}/common/fmt  ${CMAKE_BINARY_DIR}/common/fmt)
endif()


if (NOT TARGET argparse)
    add_subdirectory(${RIO_HOME}/common/argparse  ${CMAKE_BINARY_DIR}/common/argparse)
endif()


add_executable(swxtch-perf-rio
  swxtch-perf-rio.cpp
  RioSession.cpp
  RioConsumer.cpp
  RioProducer.cpp
  stdafx.cpp
  args.cpp
  StringUtils.cpp
)

set_property(TARGET swxtch-perf-rio PROPERTY
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set_property(TARGET fmt PROPERTY
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

target_link_libraries(swxtch-perf-rio
  Threads::Threads
  fmt::fmt
  argparse
)