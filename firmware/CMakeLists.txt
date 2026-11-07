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
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
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
    m
)
