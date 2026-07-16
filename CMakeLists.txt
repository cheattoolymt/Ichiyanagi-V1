# Copyright 2026 nyan<(nyan4)
# Licensed under the Apache License, Version 2.0

cmake_minimum_required(VERSION 3.10)

project(ichiyanagi
    VERSION 1.0
    DESCRIPTION "Ichiyanagi V1 (codename: Tega-Neson) programming language"
    LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wno-unused-parameter")

file(GLOB ICH_SOURCES "${CMAKE_SOURCE_DIR}/src/*.c")

add_executable(ichiyanagi ${ICH_SOURCES})
target_include_directories(ichiyanagi PRIVATE "${CMAKE_SOURCE_DIR}/src")

# math ライブラリ
find_library(MATH_LIBRARY m)
if(MATH_LIBRARY)
    target_link_libraries(ichiyanagi PRIVATE ${MATH_LIBRARY})
endif()

install(TARGETS ichiyanagi DESTINATION bin)

enable_testing()
add_test(NAME hello
         COMMAND ichiyanagi "${CMAKE_SOURCE_DIR}/examples/hello.ich")

# インタプリタのテストスイート
add_test(NAME interp_suite
         COMMAND bash "${CMAKE_SOURCE_DIR}/tests/run_tests.sh")

# codegen (ネイティブコンパイラ) のテストスイート。
# x86-64 Linux 以外では自動的に skip される。
add_test(NAME codegen_suite
         COMMAND bash "${CMAKE_SOURCE_DIR}/tests/run_codegen_tests.sh")
