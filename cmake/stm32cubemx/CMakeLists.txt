cmake_minimum_required(VERSION 3.22)

# Empty firmware — minimum sources for an STM32F103C8T6 bare loop.
enable_language(C ASM)

# Preprocessor symbols
set(MX_Defines_Syms
    STM32F103xB
    $<$<CONFIG:Debug>:DEBUG>
)

# Include paths (CMSIS device + core headers only)
set(MX_Include_Dirs
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/CMSIS/Device/ST/STM32F1xx/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/CMSIS/Include
)

# Application sources — bare minimum to spin the CPU
set(MX_Application_Src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/system_stm32f1xx.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../startup_stm32f103xb.s
)

# Linker script (passed to the linker by the project root)
set(MX_LINKER_SCRIPT
    ${CMAKE_CURRENT_SOURCE_DIR}/../../STM32F103XX_FLASH.ld
)

# Interface library for includes and symbols
add_library(stm32cubemx INTERFACE)
target_include_directories(stm32cubemx INTERFACE ${MX_Include_Dirs})
target_compile_definitions(stm32cubemx INTERFACE ${MX_Defines_Syms})

# Add sources to the project executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${MX_Application_Src})

# Expose the linker script for the root CMakeLists to consume
set(MX_LINKER_SCRIPT ${MX_LINKER_SCRIPT} PARENT_SCOPE)

# No additional libraries — bare loop firmware
set(MX_LINK_LIBS "")

# Add the map file to the list of files to be removed with 'clean' target
set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES ADDITIONAL_CLEAN_FILES ${CMAKE_PROJECT_NAME}.map)

# Generated code requires C11 or higher
if((CMAKE_C_STANDARD EQUAL 90) OR (CMAKE_C_STANDARD EQUAL 99))
    message(ERROR "Generated code requires C11 or higher")
endif()
