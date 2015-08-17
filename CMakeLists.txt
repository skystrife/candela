cmake_minimum_required(VERSION 3.0.0)
project(candela)

include(CheckCXXCompilerFlag)
include(CheckCXXSourceCompiles)
include(CheckCXXSourceRuns)

option(USE_LIBCXX "Use libc++ for the C++ standard library" ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS 1)

if(UNIX OR MINGW)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic")

    # if we don't already set the standard for the compiler, detect the
    # best one available and use it
    if(NOT "${CMAKE_CXX_FLAGS}" MATCHES "std=c\\+\\+(0x|11|1y|14)")
        check_cxx_compiler_flag(-std=c++14 HAS_CXX14)
        if(HAS_CXX14)
            message("-- Compiler supports C++14 (using it)")
            set(STDOPT "-std=c++14")
        endif()

        if(NOT STDOPT)
            check_cxx_compiler_flag(-std=c++1y HAS_CXX1Y)
            if(HAS_CXX1Y)
                message("-- Compiler supports C++1y (using it)")
                set(STDOPT "-std=c++1y")
            endif()
        endif()

        if(NOT STDOPT)
            check_cxx_compiler_flag(-std=c++11 HAS_CXX11)
            if(HAS_CXX11)
                message("-- Compiler supports C++11 (using it)")
                set(STDOPT "-std=c++11")
            endif()
        endif()

        if(NOT STDOPT)
            check_cxx_compiler_flag(-std=c++0x HAS_CXX0X)
            if(HAS_CXXOX)
                message("-- Compiler supports C++0x (using it)")
                set(STDOPT "-std=c++0x")
            endif()
        endif()

        if(NOT STDOPT)
            message(FATAL_ERROR
                    "candela requires a compiler with at least C++0x support")
        endif()

        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${STDOPT}")
    endif()

    if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
       if(CMAKE_GENERATOR STREQUAL "Ninja")
          set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fcolor-diagnostics")
        endif()
        if(USE_LIBCXX)
           message("-- Locating libc++...")
            find_library(LIBCXX_LIBRARY NAMES c++ cxx)
            if(LIBCXX_LIBRARY)
                message("-- Located libc++: ${LIBCXX_LIBRARY}")
                set(LIBCXX_OPTIONS "-stdlib=libc++")
                get_filename_component(LIBCXX_LIB_PATH ${LIBCXX_LIBRARY}
                                       DIRECTORY)
                find_path(LIBCXX_PREFIX c++/v1/algorithm
                          PATHS ${LIBCXX_LIB_PATH}/../include
                          ${CMAKE_SYSTEM_PREFIX_PATH})
                set(LIBCXX_INCLUDE_DIR ${LIBCXX_PREFIX}/c++/v1/)
                message("-- Located libc++ include path: ${LIBCXX_INCLUDE_DIR}")

                message("--     Locating libc++'s abi...")
                find_library(LIBCXXABI_LIBRARY NAMES c++abi)
                find_library(LIBCXXRT_LIBRARY NAMES cxxrt)
                if(LIBCXXABI_LIBRARY)
                    message("--     Found libc++abi: ${LIBCXXABI_LIBRARY}")
                    set(CXXABI_LIBRARY ${LIBCXXABI_LIBRARY})
                elseif(LIBCXXRT_LIBRARY)
                    message("--     Found libcxxrt: ${LIBCXXRT_LIBRARY}")
                    set(CXXABI_LIBRARY ${LIBCXXRT_LIBRARY})
                else()
                    message("--     No abi library found. "
                            "Attempting to continue without one...")
                endif()
            else()
                message("-- Could not find libc++, will not use it.")
            endif()
        endif()
    endif()

    find_library(LIBDL_LIBRARY NAMES dl ldl)
    if(LIBDL_LIBRARY)
      set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} ${LIBDL_LIBRARY}")
    endif()

    if(LIBCXX_OPTIONS)
      set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${LIBCXX_OPTIONS}")
    endif()

    if(CXXABI_LIBRARY)
      set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} ${LIBCXX_OPTIONS} ${CXXABI_LIBRARY} -L${LIBCXX_LIB_PATH}")
    endif()
    if(LIBCXX_INCLUDE_DIR)
      set(CMAKE_REQUIRED_INCLUDES "${CMAKE_REQUIRED_INCLUDES} ${LIBCXX_INCLUDE_DIR}")
    endif()
endif()

add_library(candela-options INTERFACE)
if (STDOPT)
    target_compile_options(candela-options INTERFACE ${STDOPT})
endif()

if (CXXABI_LIBRARY)
    target_link_libraries(candela-options INTERFACE ${CXXABI_LIBRARY})
endif()

if (LIBCXX_LIBRARY)
    target_include_directories(candela-options SYSTEM INTERFACE ${LIBCXX_INCLUDE_DIR})
    target_compile_options(candela-options INTERFACE ${LIBCXX_OPTIONS})
    target_link_libraries(candela-options INTERFACE -L${LIBCXX_LIB_PATH})
endif()

add_executable(candela src/candela.cpp)
target_link_libraries(candela candela-options)

find_library(XCB_LIB NAMES xcb REQUIRED)
target_link_libraries(candela ${XCB_LIB})

find_library(XCB_RANDR_LIB NAMES xcb-randr REQUIRED)
target_link_libraries(candela ${XCB_RANDR_LIB})
