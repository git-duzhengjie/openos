cmake_minimum_required(VERSION 3.16)

project(OpenOS LANGUAGES NONE)

set(OPENOS_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(OPENOS_BUILD_SCRIPT "${OPENOS_SOURCE_DIR}/build.sh" CACHE FILEPATH "OpenOS legacy build script")
set(OPENOS_DEFAULT_ARCH "i386" CACHE STRING "Default OpenOS architecture")
set_property(CACHE OPENOS_DEFAULT_ARCH PROPERTY STRINGS i386 x86_64)

find_program(BASH_EXECUTABLE bash REQUIRED)
find_program(NASM_EXECUTABLE nasm)
find_program(GCC_EXECUTABLE gcc)

add_custom_target(openos-image ALL
    COMMAND ${CMAKE_COMMAND} -E env ARCH=${OPENOS_DEFAULT_ARCH} ${BASH_EXECUTABLE} ${OPENOS_BUILD_SCRIPT}
    WORKING_DIRECTORY ${OPENOS_SOURCE_DIR}
    COMMENT "Building OpenOS image via build.sh (ARCH=${OPENOS_DEFAULT_ARCH})"
    VERBATIM
)

add_custom_target(openos-i386
    COMMAND ${CMAKE_COMMAND} -E env ARCH=i386 ${BASH_EXECUTABLE} ${OPENOS_BUILD_SCRIPT}
    WORKING_DIRECTORY ${OPENOS_SOURCE_DIR}
    COMMENT "Building OpenOS i386 image"
    VERBATIM
)

add_custom_target(openos-x86_64
    COMMAND ${CMAKE_COMMAND} -E env ARCH=x86_64 ${BASH_EXECUTABLE} ${OPENOS_BUILD_SCRIPT}
    WORKING_DIRECTORY ${OPENOS_SOURCE_DIR}
    COMMENT "Building OpenOS x86_64 kernel skeleton"
    VERBATIM
)

add_custom_target(openos-clean
    COMMAND ${BASH_EXECUTABLE} ${OPENOS_BUILD_SCRIPT} clean
    WORKING_DIRECTORY ${OPENOS_SOURCE_DIR}
    COMMENT "Cleaning OpenOS build artifacts"
    VERBATIM
)

if(NASM_EXECUTABLE)
    add_custom_target(openos-bootloader-check
        COMMAND ${NASM_EXECUTABLE} -f bin ${OPENOS_SOURCE_DIR}/src/boot/boot.asm -o ${CMAKE_BINARY_DIR}/boot-check.bin
        COMMAND ${CMAKE_COMMAND} -DINPUT=${CMAKE_BINARY_DIR}/boot-check.bin -P ${OPENOS_SOURCE_DIR}/cmake/VerifyBootSector.cmake
        WORKING_DIRECTORY ${OPENOS_SOURCE_DIR}
        COMMENT "Assembling and validating the 512-byte boot sector"
        VERBATIM
    )
endif()

if(GCC_EXECUTABLE)
    add_custom_target(openos-arch-syntax-check
        COMMAND ${GCC_EXECUTABLE} -ffreestanding -nostdlib -Wall -Wextra -Werror -O2 -Isrc/arch/arm/include -c src/arch/arm/kernel/kernel_arm.c -o ${CMAKE_BINARY_DIR}/kernel_arm.o
        COMMAND ${GCC_EXECUTABLE} -ffreestanding -nostdlib -Wall -Wextra -Werror -O2 -Isrc/arch/arm/include -c src/arch/arm/kernel/platform_arm.c -o ${CMAKE_BINARY_DIR}/platform_arm.o
        COMMAND ${GCC_EXECUTABLE} -ffreestanding -nostdlib -Wall -Wextra -Werror -O2 -Isrc/arch/arm/include -c src/arch/arm/kernel/early_console_arm.c -o ${CMAKE_BINARY_DIR}/early_console_arm.o
        COMMAND ${GCC_EXECUTABLE} -ffreestanding -nostdlib -Wall -Wextra -Werror -O2 -Isrc/arch/riscv/include -c src/arch/riscv/kernel/kernel_riscv.c -o ${CMAKE_BINARY_DIR}/kernel_riscv.o
        COMMAND ${GCC_EXECUTABLE} -ffreestanding -nostdlib -Wall -Wextra -Werror -O2 -Isrc/arch/riscv/include -c src/arch/riscv/kernel/platform_riscv.c -o ${CMAKE_BINARY_DIR}/platform_riscv.o
        COMMAND ${GCC_EXECUTABLE} -ffreestanding -nostdlib -Wall -Wextra -Werror -O2 -Isrc/arch/riscv/include -c src/arch/riscv/kernel/early_console_riscv.c -o ${CMAKE_BINARY_DIR}/early_console_riscv.o
        WORKING_DIRECTORY ${OPENOS_SOURCE_DIR}
        COMMENT "Compiling ARM/RISC-V architecture skeletons for syntax checks"
        VERBATIM
    )
endif()
