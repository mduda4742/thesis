cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Build type: domyslnie RelWithDebInfo = -O2 -g -DNDEBUG (zoptymalizowany ORAZ debugowalny).
# Release (-O3, bez -g) lub Debug (-O0) wymusisz przez -DCMAKE_BUILD_TYPE=...
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "Build type" FORCE)
endif()

# Set the project name
set(CMAKE_PROJECT_NAME thesis)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# ARM CMSIS-DSP integration
set(CMSISCORE ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/CMSIS)
set(DISABLEFLOAT16 TRUE)
add_subdirectory(third_party/CMSIS-DSP)

# CMSIS-DSP kompiluje sie bez -fdata-sections, wiec WSZYSTKIE tablice FFT (f32/f64/q31/q15,
# kazda dlugosc) lezą w jednej sekcji .rodata (~578 KB) i linker --gc-sections nie moze ich
# usunac -> przepelnienie Flash. Wymuszamy osobne sekcje + optymalizacje rozmiaru, zeby
# zostaly tylko tablice faktycznie uzywane (f32 dla FFT 1024/512).
target_compile_options(CMSISDSP PRIVATE -fdata-sections -ffunction-sections)

# ARM CMSIS-NN - TYLKO funkcje q7 uzywane przez ds_cnn + customowy arm_avepool (z local_NN).
# Reszta biblioteki (s8/softmax/svd/...) pominieta. arm_fill_q15/arm_copy_q7/arm_fill_q7 sa w CMSIS-DSP.
set(CMSISNN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/CMSIS-NN/Source)
add_library(CMSISNN STATIC
    ${CMSISNN_DIR}/ConvolutionFunctions/arm_convolve_HWC_q7_basic_nonsquare.c
    ${CMSISNN_DIR}/ConvolutionFunctions/arm_depthwise_separable_conv_HWC_q7_nonsquare.c
    ${CMSISNN_DIR}/ConvolutionFunctions/arm_convolve_1x1_HWC_q7_fast_nonsquare.c
    ${CMSISNN_DIR}/ConvolutionFunctions/arm_nn_mat_mult_kernel_q7_q15.c
    ${CMSISNN_DIR}/ConvolutionFunctions/arm_nn_mat_mult_kernel_q7_q15_reordered.c
    ${CMSISNN_DIR}/FullyConnectedFunctions/arm_fully_connected_q7.c
    ${CMSISNN_DIR}/ActivationFunctions/arm_relu_q7.c
    ${CMSISNN_DIR}/NNSupportFunctions/arm_q7_to_q15_no_shift.c
    ${CMSISNN_DIR}/NNSupportFunctions/arm_q7_to_q15_reordered_no_shift.c
    ${CMSISNN_DIR}/PoolingFunctions/arm_avepool_q7_HWC_nonsquare.c
)
target_include_directories(CMSISNN PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/CMSIS-NN/Include)
target_link_libraries(CMSISNN PUBLIC CMSISDSP::CMSISDSP)
# -include cmsis_nn_compat.h: __SXTB16_RORn (sxtb16 z ROR), ktorego starszy rdzen ST nie opakowal.
target_compile_options(CMSISNN PRIVATE -fdata-sections -ffunction-sections
    -include ${CMAKE_CURRENT_SOURCE_DIR}/Core/Inc/cmsis_nn_compat.h)

# PDM2PCM library integration
add_library(PDM2PCM STATIC IMPORTED)
set_target_properties(PDM2PCM PROPERTIES 
    IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/third_party/PDM2PCM/Lib/libPDMFilter_CM4_GCC_wc32.a"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/PDM2PCM/Inc"
)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
    Core/Src/audio_dsp.c
    Core/Src/ds_cnn.c
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
    # Naglowki CMSIS-NN (deklaracje arm_*) dla ds_cnn.c. Biblioteke linkujemy ponizej (CMSISNN).
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/CMSIS-NN/Include
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)
target_link_options(${CMAKE_PROJECT_NAME} PRIVATE
    -T${CMAKE_CURRENT_SOURCE_DIR}/STM32G474xx_FLASH.ld
    -Wl,--no-warn-rwx-segments
)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx
    # Add user defined libraries
    PDM2PCM
    CMSISDSP::CMSISDSP
    CMSISNN
    m
)
