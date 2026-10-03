# Print RAM/flash usage (bytes and % of the linked memory regions) after linking.
find_package(Python3 COMPONENTS Interpreter QUIET)
set(PRINT_SIZE_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/print_size.py)

set(Python3_Interpreter_FOUND ${Python3_Interpreter_FOUND} PARENT_SCOPE)
set(Python3_EXECUTABLE ${Python3_EXECUTABLE} PARENT_SCOPE)
set(PRINT_SIZE_SCRIPT ${PRINT_SIZE_SCRIPT} PARENT_SCOPE)

function(print_size_info target)
    if(Python3_Interpreter_FOUND)
        # Derive arm-none-eabi-size dynamically from the compiler path configured for this target
        get_filename_component(TOOLCHAIN_BIN_DIR ${CMAKE_C_COMPILER} DIRECTORY)
        set(TARGET_SIZE_TOOL "${TOOLCHAIN_BIN_DIR}/arm-none-eabi-size")

        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${Python3_EXECUTABLE} ${PRINT_SIZE_SCRIPT}
                    --elf $<TARGET_FILE:${target}>
                    --map $<TARGET_FILE_DIR:${target}>/${target}.elf.map
                    --size-tool ${TARGET_SIZE_TOOL}
            COMMENT "Analyzing binary size with native Pico SDK toolchain..."
        )
    endif()
endfunction()