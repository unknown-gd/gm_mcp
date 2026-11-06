cmake_minimum_required(VERSION 3.16)
project(gm_mcp LANGUAGES CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# GMod binary-module targets: linux32, linux64, win32, win64
set(GM_MCP_TARGET "auto" CACHE STRING "GMod module target: auto, linux32, linux64, win32, win64")
set_property(CACHE GM_MCP_TARGET PROPERTY STRINGS auto linux32 linux64 win32 win64)

if(GM_MCP_TARGET STREQUAL "auto")
    if(WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Windows")
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(GM_MCP_TARGET_RESOLVED "win64")
        else()
            set(GM_MCP_TARGET_RESOLVED "win32")
        endif()
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_SYSTEM_NAME MATCHES ".*Linux.*")
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(GM_MCP_TARGET_RESOLVED "linux64")
        else()
            set(GM_MCP_TARGET_RESOLVED "linux32")
        endif()
    else()
        message(FATAL_ERROR
            "Unsupported host '${CMAKE_SYSTEM_NAME}'. "
            "Set -DGM_MCP_TARGET=linux32|linux64|win32|win64")
    endif()
else()
    set(GM_MCP_TARGET_RESOLVED "${GM_MCP_TARGET}")
endif()

if(GM_MCP_TARGET_RESOLVED STREQUAL "linux32")
    set(GM_MCP_PLATFORM_SUFFIX "linux")
    set(GM_MCP_IS_WINDOWS FALSE)
elseif(GM_MCP_TARGET_RESOLVED STREQUAL "linux64")
    set(GM_MCP_PLATFORM_SUFFIX "linux64")
    set(GM_MCP_IS_WINDOWS FALSE)
elseif(GM_MCP_TARGET_RESOLVED STREQUAL "win32")
    set(GM_MCP_PLATFORM_SUFFIX "win32")
    set(GM_MCP_IS_WINDOWS TRUE)
elseif(GM_MCP_TARGET_RESOLVED STREQUAL "win64")
    set(GM_MCP_PLATFORM_SUFFIX "win64")
    set(GM_MCP_IS_WINDOWS TRUE)
else()
    message(FATAL_ERROR
        "Invalid GM_MCP_TARGET='${GM_MCP_TARGET_RESOLVED}'. "
        "Expected auto, linux32, linux64, win32, or win64")
endif()

include(CheckIPOSupported)
check_ipo_supported(RESULT gm_mcp_ipo_supported OUTPUT gm_mcp_ipo_error)

add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party"
)
add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)

set(GM_MCP_SOURCES
    src/mcp.cpp
    src/bridge.cpp
    src/protocol.cpp
    src/lua_api.cpp
)

set(module_name "gmsv_mcp_${GM_MCP_PLATFORM_SUFFIX}")
message(STATUS "mcp module: ${module_name}.dll (server-only)")

add_library(mcp_module SHARED ${GM_MCP_SOURCES})

target_include_directories(mcp_module PRIVATE src)
target_link_libraries(mcp_module PRIVATE nlohmann_json::nlohmann_json)
target_compile_definitions(mcp_module PRIVATE
    "GM_MCP_TARGET_${GM_MCP_TARGET_RESOLVED}=1"
    GM_MCP_SERVER=1
)

set_target_properties(mcp_module PROPERTIES
    PREFIX ""
    OUTPUT_NAME "${module_name}"
    SUFFIX ".dll"
)

if(GM_MCP_IS_WINDOWS)
    target_link_libraries(mcp_module PRIVATE ws2_32)
    if(MSVC)
        set_property(TARGET mcp_module PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    elseif(MINGW OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_link_options(mcp_module PRIVATE
            -static-libgcc
            -static-libstdc++
            -static
        )
    endif()
else()
    set_target_properties(mcp_module PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
    )
    target_link_libraries(mcp_module PRIVATE ${CMAKE_DL_LIBS} pthread)
endif()

if(NOT MSVC)
    target_compile_options(mcp_module PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(gm_mcp_ipo_supported)
    set_property(TARGET mcp_module PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()

install(TARGETS mcp_module
    RUNTIME DESTINATION lib
    LIBRARY DESTINATION lib
)
