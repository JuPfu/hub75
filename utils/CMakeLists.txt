# Print RAM/flash usage (bytes and % of the linked memory regions) after linking.
find_program(ARM_SIZE_TOOL NAMES arm-none-eabi-size ${CMAKE_SIZE})
find_package(Python3 COMPONENTS Interpreter QUIET)
set(PRINT_SIZE_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/print_size.py)
# function() bodies resolve variables from the caller's scope, not the scope they were defined
# in, and add_subdirectory() doesn't propagate this directory's variables to the parent scope on
# its own - so anything print_size_info() needs (paths, find_package() results) must be pushed
# up explicitly, or it would silently read as unset when called from the top-level CMakeLists.txt.
set(Python3_Interpreter_FOUND ${Python3_Interpreter_FOUND} PARENT_SCOPE)
set(Python3_EXECUTABLE ${Python3_EXECUTABLE} PARENT_SCOPE)
set(PRINT_SIZE_SCRIPT ${PRINT_SIZE_SCRIPT} PARENT_SCOPE)
function(print_size_info target)
    if(ARM_SIZE_TOOL AND Python3_Interpreter_FOUND)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${Python3_EXECUTABLE} ${PRINT_SIZE_SCRIPT}
                    --elf $<TARGET_FILE:${target}>
                    --map $<TARGET_FILE_DIR:${target}>/${target}.elf.map
                    --size-tool ${ARM_SIZE_TOOL}
        )
    endif()
endfunction()
