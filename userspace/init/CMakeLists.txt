cmake_minimum_required(VERSION 3.10)

project(init C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_program(LLVM_OBJCOPY llvm-objcopy REQUIRED)

add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/nksu_ko.o

    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}

    COMMAND ${LLVM_OBJCOPY}
        -I binary
        -O elf64-littleaarch64
        -B aarch64
        nksu.ko
        ${CMAKE_BINARY_DIR}/nksu_ko.o

    DEPENDS ${CMAKE_SOURCE_DIR}/nksu.ko
)

add_custom_target(embed_ko ALL
    DEPENDS ${CMAKE_BINARY_DIR}/nksu_ko.o
)

add_executable(init
    init.c
    kmod/kmod.c
    kmod/hashtable.c
    ${CMAKE_BINARY_DIR}/nksu_ko.o
)

add_dependencies(init embed_ko)

target_include_directories(init PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/kmod
)

target_compile_options(init PRIVATE
    -Os
    -ffunction-sections
    -fdata-sections
)

target_link_options(init PRIVATE
    -static
    -Wl,--gc-sections
    -s
)