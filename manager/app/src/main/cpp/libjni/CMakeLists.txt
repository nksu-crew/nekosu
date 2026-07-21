cmake_minimum_required(VERSION 3.10.0)
project("jni")

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_library(jni SHARED ctl.c jni.c)

target_compile_options(jni PRIVATE -flto=thin)
target_link_options(jni PRIVATE -flto=thin)

target_compile_options(jni PRIVATE -fsanitize=cfi -fsanitize-cfi-cross-dso -fvisibility=hidden)
target_link_options(jni PRIVATE -fsanitize=cfi -fsanitize-cfi-cross-dso)

target_compile_options(jni PRIVATE -mbranch-protection=standard)

target_compile_options(jni PRIVATE -fstack-protector-strong)

target_link_libraries(jni PRIVATE log)