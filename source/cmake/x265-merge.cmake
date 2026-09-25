# x265-merge.cmake - Merge several static archives into one.
#
# Usage:
#   cmake -DOUTPUT=<path> [-DINPUT1=<path>] [-DINPUT2=<path>] ...
#         [-DARCHIVER=<path>] -P x265-merge.cmake
#
# The merge is performed with GNU ar (MRI script) on Linux/MinGW, libtool on
# macOS and lib.exe/llvm-lib on MSVC. When ARCHIVER is not given, the tool is
# looked up on the system.

if(NOT DEFINED OUTPUT)
    message(FATAL_ERROR "OUTPUT is required")
endif()

get_filename_component(_out_name "${OUTPUT}" NAME)
get_filename_component(_out_dir "${OUTPUT}" DIRECTORY)
set(_work "${_out_dir}/.x265-merge")
file(REMOVE_RECURSE "${_work}")
file(MAKE_DIRECTORY "${_work}")

set(_inputs)
set(_index 1)
while(DEFINED INPUT${_index})
    set(_input "${INPUT${_index}}")
    if(NOT EXISTS "${_input}")
        message(FATAL_ERROR "Input archive does not exist: ${_input}")
    endif()
    get_filename_component(_input_name "${_input}" NAME)
    get_filename_component(_input_ext "${_input}" EXT)
    file(COPY "${_input}" DESTINATION "${_work}")
    file(RENAME "${_work}/${_input_name}" "${_work}/in${_index}${_input_ext}")
    list(APPEND _inputs "in${_index}${_input_ext}")
    math(EXPR _index "${_index} + 1")
endwhile()
if(NOT _inputs)
    message(FATAL_ERROR "At least one INPUT argument is required")
endif()

if(DEFINED ARCHIVER AND ARCHIVER)
    get_filename_component(_ar_name "${ARCHIVER}" NAME)
    if(_ar_name MATCHES "^(lib|llvm-lib)(\\.exe)?$")
        set(_mode "msvc")
    elseif(_ar_name MATCHES "^(ar|llvm-ar)(\\.exe)?$")
        set(_mode "mri")
    else()
        set(_mode "mri")
    endif()
    set(_archiver "${ARCHIVER}")
else()
    if(WIN32 AND NOT MINGW)
        find_program(_archiver NAMES lib.exe llvm-lib)
        set(_mode "msvc")
    elseif(APPLE)
        find_program(_archiver NAMES libtool)
        set(_mode "libtool")
    else()
        find_program(_archiver NAMES ar)
        set(_mode "mri")
    endif()
    if(NOT _archiver)
        message(FATAL_ERROR "Unable to find an archiver to merge the x265 multilib libraries")
    endif()
endif()

if(_mode STREQUAL "mri")
    set(_script_contents "CREATE ${_out_name}\n")
    foreach(_input IN LISTS _inputs)
        string(APPEND _script_contents "ADDLIB ${_input}\n")
    endforeach()
    string(APPEND _script_contents "SAVE\nEND\n")
    file(WRITE "${_work}/merge.script" "${_script_contents}")
    execute_process(
        COMMAND "${_archiver}" -M
        WORKING_DIRECTORY "${_work}"
        INPUT_FILE "${_work}/merge.script"
        RESULT_VARIABLE _result)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "Failed to merge static archives with ${_archiver}: ${_result}")
    endif()
elseif(_mode STREQUAL "msvc")
    set(_command "/NOLOGO" "/OUT:${_work}/${_out_name}")
    foreach(_input IN LISTS _inputs)
        list(APPEND _command "${_work}/${_input}")
    endforeach()
    execute_process(COMMAND "${_archiver}" ${_command} WORKING_DIRECTORY "${_work}" RESULT_VARIABLE _result)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "Failed to merge static archives with ${_archiver}: ${_result}")
    endif()
else()
    set(_command libtool -static -o "${_work}/${_out_name}")
    foreach(_input IN LISTS _inputs)
        list(APPEND _command "${_work}/${_input}")
    endforeach()
    execute_process(COMMAND ${_command} WORKING_DIRECTORY "${_work}"
                    OUTPUT_VARIABLE _out ERROR_VARIABLE _err RESULT_VARIABLE _result)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "Failed to merge static archives with libtool: ${_result}\n${_out}${_err}")
    endif()
    # libtool warns about the identically-named object members that the three
    # bit-depth builds produce; harmless, filter them from the output
    if(_err)
        string(REGEX REPLACE "[^\n]*member name[^\n]*\n?" "" _err "${_err}")
        string(STRIP "${_err}" _err)
        if(NOT _err STREQUAL "")
            message(STATUS "${_err}")
        endif()
    endif()
endif()

if(EXISTS "${OUTPUT}")
    file(REMOVE "${OUTPUT}")
endif()
file(RENAME "${_work}/${_out_name}" "${OUTPUT}")
file(REMOVE_RECURSE "${_work}")
