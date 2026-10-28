cmake_minimum_required(VERSION 4.0)

project(fov VERSION 1.0.0 LANGUAGES C CXX)

add_library(FovLib INTERFACE)

target_include_directories(FovLib INTERFACE
"include"
)

if(MSVC)
  add_compile_options(/utf-8)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

add_subdirectory(tests)