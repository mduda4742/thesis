
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR armv7e-m)

set(ARM_GCC_TOOLCHAIN_PREFIX "$ENV{HOME}/.local/toolchains/arm-gnu-toolchain-13.3.rel1-x86_64-arm-none-eabi")
set(ARM_GCC_TOOLCHAIN_PATH "${ARM_GCC_TOOLCHAIN_PREFIX}/bin")

set(CMAKE_AR                "${ARM_GCC_TOOLCHAIN_PATH}/arm-none-eabi-ar")
set(CMAKE_ASM_COMPILER      "${ARM_GCC_TOOLCHAIN_PATH}/arm-none-eabi-gcc")
set(CMAKE_C_COMPILER        "${ARM_GCC_TOOLCHAIN_PATH}/arm-none-eabi-gcc")
set(CMAKE_CXX_COMPILER      "${ARM_GCC_TOOLCHAIN_PATH}/arm-none-eabi-g++")
set(CMAKE_LINKER            "${ARM_GCC_TOOLCHAIN_PATH}/arm-none-eabi-ld")
set(CMAKE_OBJCOPY           "${ARM_GCC_TOOLCHAIN_PATH}/arm-none-eabi-objcopy")
set(CMAKE_OBJDUMP           "${ARM_GCC_TOOLCHAIN_PATH}/arm-none-eabi-objdump")
set(CMAKE_SIZE              "${ARM_GCC_TOOLCHAIN_PATH}/arm-none-eabi-size")
set(CMAKE_STRIP             "${ARM_GCC_TOOLCHAIN_PATH}/arm-none-eabi-strip")

set(MCU_FLAGS "-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard")

set(CMAKE_C_FLAGS_INIT "${MCU_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${MCU_FLAGS}")
set(CMAKE_ASM_FLAGS_INIT "${MCU_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${MCU_FLAGS} -Wl,--gc-sections --specs=nano.specs --specs=nosys.specs")
# set(CMAKE_EXE_LINKER_FLAGS_INIT "${MCU_FLAGS}")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)



