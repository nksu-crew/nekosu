cmake_minimum_required(VERSION 3.10)

project(init C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_program(LLVM_OBJCOPY llvm-objcopy REQUIRED)

# 各内核版本（KMI）对应的 nksu.ko，文件命名: ko/<kmi>_nksu.ko
set(KO_LIST
    android12-5.10
    android13-5.10
    android13-5.15
    android14-5.15
    android14-6.1
    android15-6.6
    android16-6.12
)

# 嵌入对象的目标架构，x86_64 构建时传入 -DKO_ELF_FORMAT=elf64-x86-64 -DKO_ELF_ARCH=i386:x86-64
set(KO_ELF_FORMAT "elf64-littleaarch64" CACHE STRING "ELF format for embedded ko")
set(KO_ELF_ARCH "aarch64" CACHE STRING "ELF arch for embedded ko")

set(KO_OBJECTS "")
foreach(KMI ${KO_LIST})
    string(REPLACE "-" "_" KMI_SYM ${KMI})
    string(REPLACE "." "_" KMI_SYM ${KMI_SYM})
    set(KO_FILE ${CMAKE_CURRENT_SOURCE_DIR}/ko/${KMI}_nksu.ko)
    if(NOT EXISTS ${KO_FILE})
        message(FATAL_ERROR "Missing ${KO_FILE}. Run LKM build first (see .github/workflows/build-lkm.yml)")
    endif()
    set(KO_OBJ ${CMAKE_BINARY_DIR}/${KMI_SYM}_nksu_ko.o)
    get_filename_component(KO_BASENAME ${KO_FILE} NAME)
    add_custom_command(
        OUTPUT ${KO_OBJ}

        # 在 ko 目录内执行，使 objcopy 按 basename 生成符号（_binary_<kmi>_nksu_ko_*）
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/ko

        COMMAND ${LLVM_OBJCOPY}
            -I binary
            -O ${KO_ELF_FORMAT}
            -B ${KO_ELF_ARCH}
            ${KO_BASENAME}
            ${KO_OBJ}

        DEPENDS ${KO_FILE}
    )
    list(APPEND KO_OBJECTS ${KO_OBJ})
endforeach()

add_custom_target(embed_ko ALL DEPENDS ${KO_OBJECTS})

add_executable(init
    init.c
    kmod.c
    hashtable.c
    ${KO_OBJECTS}
)

add_dependencies(init embed_ko)

target_compile_options(init PRIVATE
    -Os
    -ffunction-sections
    -fdata-sections
)

# Android 内核强制 PIE，静态链接需使用 -static-pie
target_link_options(init PRIVATE
    -static-pie
    -Wl,--gc-sections
    -s
)
