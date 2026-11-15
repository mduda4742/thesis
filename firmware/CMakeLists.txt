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


# Default build type: Release (override with -DCMAKE_BUILD_TYPE=Debug)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
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

# Release: max speed (-O3) + debug info (-g: names/types for the debugger, no effect on code or Flash).
set(CMAKE_C_FLAGS_RELEASE "-O3 -g -DNDEBUG")

# For ALL targets (own code, HAL, FreeRTOS, CMSIS). Must be before add_executable/add_subdirectory.
#  -ffunction-sections -fdata-sections: one section per function/variable -> --gc-sections drops unused ones
#  -fstack-usage: writes a .su file per source with the stack usage of each function
add_compile_options(
    $<$<COMPILE_LANGUAGE:C>:-ffunction-sections>
    $<$<COMPILE_LANGUAGE:C>:-fdata-sections>
    $<$<COMPILE_LANGUAGE:C>:-fstack-usage>
)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# ARM CMSIS-DSP integration
set(CMSISCORE ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/CMSIS)
set(DISABLEFLOAT16 TRUE)
add_subdirectory(third_party/CMSIS-DSP)

target_compile_options(CMSISDSP PRIVATE -fdata-sections -ffunction-sections)

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
# -include cmsis_nn_compat.h: __SXTB16_RORn (sxtb16 z ROR)
target_compile_options(CMSISNN PRIVATE -fdata-sections -ffunction-sections
    -include ${CMAKE_CURRENT_SOURCE_DIR}/Core/Inc/cmsis_nn_compat.h)

# PDM2PCM library integration
add_library(PDM2PCM STATIC IMPORTED)
set_target_properties(PDM2PCM PROPERTIES 
    IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/third_party/PDM2PCM/Lib/libPDMFilter_CM4_GCC_wc32.a"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/PDM2PCM/Inc"
)

# Micro-ROS static library integration
set(MICROROS_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/micro_ros_stm32cubemx_utils/microros_static_library/libmicroros)
if(NOT EXISTS ${MICROROS_LIB_DIR}/libmicroros.a)
    message(FATAL_ERROR "Brak libmicroros.a - zbuduj ja dockerem (instrukcja w Makefile)")
endif()
add_library(microros STATIC IMPORTED)
set_target_properties(microros PROPERTIES
    IMPORTED_LOCATION "${MICROROS_LIB_DIR}/libmicroros.a"
    INTERFACE_INCLUDE_DIRECTORIES "${MICROROS_LIB_DIR}/microros_include"
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
    microros/custom_memory_manager.c   
    microros/microros_allocators.c     
    microros/microros_time.c           
    microros/dma_transport.c        
    microros/uros_app.c             
    microros/robot_control.c        
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/CMSIS-NN/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/microros
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
    -Wl,-Map=${CMAKE_PROJECT_NAME}.map   # memory map: address and size of every symbol
    -Wl,--print-memory-usage             # Flash/RAM usage in % after each link
)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx
    # Add user defined libraries
    PDM2PCM
    CMSISDSP::CMSISDSP
    CMSISNN
    microros
    m
)
