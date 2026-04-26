# =============================================================================
# Lua VM Wrapper (NativeLibrary) — CMake 构建系统
# =============================================================================
#
# 本 CMake 脚本构建 NativeLibrary 动态库（.dll / .so / .dylib），
# 将 Lua/LuaJIT/Luau 封装为统一的 C ABI 供 .NET P/Invoke 调用。
#
# 构建选项:
#   cmake -B build -DLVM_WITH_LUA55=ON -DLVM_WITH_LUAJIT=OFF -DLVM_WITH_LUAU=OFF
#
# 最低要求:
#   - CMake >= 3.20
#   - C++17 编译器 (MSVC 2019+, GCC 9+, Clang 10+)
#   - 各后端对应的 Lua 源码（通过 FetchContent 或手动指定路径）
# =============================================================================

cmake_minimum_required(VERSION 3.20)
project(LuaVMWrapper VERSION 1.0.0 LANGUAGES CXX C)

# ---- C++17 标准 ----
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- 位置无关代码 (PIC) —— 动态库必需 ----
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# =============================================================================
# 构建选项：按需选择后端
# =============================================================================

option(LVM_WITH_LUA55  "Build with Lua 5.5 backend"  ON)
option(LVM_WITH_LUAJIT "Build with LuaJIT backend"   OFF)
option(LVM_WITH_LUAU   "Build with Luau backend"     OFF)
option(LVM_BUILD_TESTS "Build test executables"      ON)

# 至少需要一个后端
if(NOT LVM_WITH_LUA55 AND NOT LVM_WITH_LUAJIT AND NOT LVM_WITH_LUAU)
    message(FATAL_ERROR "At least one backend must be enabled. Set LVM_WITH_LUA55=ON")
endif()

# =============================================================================
# 平台检测
# =============================================================================

if(WIN32)
    add_definitions(-D_WIN32_WINNT=0x0601)  # Windows 7+
    set(LVM_PLATFORM_WINDOWS TRUE)
elseif(APPLE)
    set(LVM_PLATFORM_MACOS TRUE)
else()
    set(LVM_PLATFORM_LINUX TRUE)
endif()

message(STATUS "LuaVM Wrapper v${PROJECT_VERSION}")
message(STATUS "  Platform: ${CMAKE_SYSTEM_NAME}")
message(STATUS "  Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  Backends: "
    "[Lua55:${LVM_WITH_LUA55}] [LuaJIT:${LVM_WITH_LUAJIT}] [Luau:${LVM_WITH_LUAU}]")

# =============================================================================
# Lua 5.5 后端
# =============================================================================

if(LVM_WITH_LUA55)
    message(STATUS "Configuring Lua 5.5 backend...")

    # Lua 源码获取方式:
    # 1. 系统已安装: 设置 LUA55_INCLUDE_DIR 和 LUA55_LIBRARY
    # 2. 手动指定: 设置 LUA55_SOURCE_DIR 指向源码根目录
    # 3. 自动下载: 使用 FetchContent (需要网络)

    if(NOT DEFINED LUA55_SOURCE_DIR AND NOT DEFINED LUA55_INCLUDE_DIR)
        include(FetchContent)
        FetchContent_Declare(
            lua
            URL https://www.lua.org/ftp/lua-5.4.7.tar.gz
            SOURCE_DIR ${CMAKE_BINARY_DIR}/_deps/lua-src
        )
        # Lua 源码使用简单 Makefile，不包含 CMakeLists.txt
        # 因此使用 FetchContent_Populate 仅下载，手动编译
        FetchContent_GetProperties(lua)
        if(NOT lua_POPULATED)
            FetchContent_Populate(lua)
        endif()
        set(LUA55_SOURCE_DIR ${lua_SOURCE_DIR})
    endif()

    if(DEFINED LUA55_SOURCE_DIR)
        message(STATUS "  Building Lua 5.5 from source: ${LUA55_SOURCE_DIR}")

        # 收集 Lua 核心源文件
        # Lua 5.4 源码在 src/ 子目录下
        set(LUA_SRC_DIR ${LUA55_SOURCE_DIR}/src)

        # 排除 lua.c 和 luac.c（它们是独立可执行程序入口）
        set(LUA_CORE_SOURCES
            ${LUA_SRC_DIR}/lapi.c
            ${LUA_SRC_DIR}/lauxlib.c
            ${LUA_SRC_DIR}/lbaselib.c
            ${LUA_SRC_DIR}/lcode.c
            ${LUA_SRC_DIR}/lcorolib.c
            ${LUA_SRC_DIR}/lctype.c
            ${LUA_SRC_DIR}/ldblib.c
            ${LUA_SRC_DIR}/ldebug.c
            ${LUA_SRC_DIR}/ldo.c
            ${LUA_SRC_DIR}/ldump.c
            ${LUA_SRC_DIR}/lfunc.c
            ${LUA_SRC_DIR}/lgc.c
            ${LUA_SRC_DIR}/linit.c
            ${LUA_SRC_DIR}/liolib.c
            ${LUA_SRC_DIR}/llex.c
            ${LUA_SRC_DIR}/lmathlib.c
            ${LUA_SRC_DIR}/lmem.c
            ${LUA_SRC_DIR}/loadlib.c
            ${LUA_SRC_DIR}/lobject.c
            ${LUA_SRC_DIR}/lopcodes.c
            ${LUA_SRC_DIR}/loslib.c
            ${LUA_SRC_DIR}/lparser.c
            ${LUA_SRC_DIR}/lstate.c
            ${LUA_SRC_DIR}/lstring.c
            ${LUA_SRC_DIR}/lstrlib.c
            ${LUA_SRC_DIR}/ltable.c
            ${LUA_SRC_DIR}/ltablib.c
            ${LUA_SRC_DIR}/ltm.c
            ${LUA_SRC_DIR}/lundump.c
            ${LUA_SRC_DIR}/lutf8lib.c
            ${LUA_SRC_DIR}/lvm.c
            ${LUA_SRC_DIR}/lzio.c
        )

        add_library(lua55 STATIC ${LUA_CORE_SOURCES})
        target_include_directories(lua55 PUBLIC ${LUA_SRC_DIR})

        # Lua 在 Windows 上需要特殊编译定义
        if(LVM_PLATFORM_WINDOWS)
            target_compile_definitions(lua55 PRIVATE
                LUA_BUILD_AS_DLL
                _CRT_SECURE_NO_WARNINGS
            )
        endif()

        # 禁用 Lua 源码的某些警告
        if(MSVC)
            target_compile_options(lua55 PRIVATE /W0)
        else()
            target_compile_options(lua55 PRIVATE -w)
        endif()

        set(LUA55_INCLUDE_DIR ${LUA_SRC_DIR})
        set(LUA55_LIBRARY lua55)
    else()
        # 使用系统安装的 Lua
        find_path(LUA55_INCLUDE_DIR lua.h)
        find_library(LUA55_LIBRARY NAMES lua lua55 lua5.4 lua54)
        if(NOT LUA55_INCLUDE_DIR OR NOT LUA55_LIBRARY)
            message(FATAL_ERROR "Lua 5.5 not found. Set LUA55_SOURCE_DIR or LUA55_INCLUDE_DIR/LUA55_LIBRARY")
        endif()
    endif()
endif()

# =============================================================================
# LuaJIT 后端（可选）
# =============================================================================

if(LVM_WITH_LUAJIT)
    message(STATUS "Configuring LuaJIT backend...")

    if(NOT DEFINED LUAJIT_SOURCE_DIR AND NOT DEFINED LUAJIT_INCLUDE_DIR)
        include(FetchContent)
        FetchContent_Declare(
            luajit
            GIT_REPOSITORY https://github.com/LuaJIT/LuaJIT.git
            GIT_TAG v2.1
            SOURCE_DIR ${CMAKE_BINARY_DIR}/_deps/luajit-src
        )
        FetchContent_GetProperties(luajit)
        if(NOT luajit_POPULATED)
            FetchContent_Populate(luajit)
        endif()
        set(LUAJIT_SOURCE_DIR ${luajit_SOURCE_DIR})
    endif()

    if(DEFINED LUAJIT_SOURCE_DIR)
        # LuaJIT 使用自己的 Makefile 系统，CMake 集成较复杂
        # 此处使用预编译的 lua51.lib 或手动编译
        message(STATUS "  LuaJIT source: ${LUAJIT_SOURCE_DIR}")
        message(STATUS "  LuaJIT requires manual build. Run: cd ${LUAJIT_SOURCE_DIR} && make")
    endif()
endif()

# =============================================================================
# Luau 后端（可选）
# =============================================================================

if(LVM_WITH_LUAU)
    message(STATUS "Configuring Luau backend...")

    if(NOT DEFINED LUAU_SOURCE_DIR AND NOT DEFINED LUAU_INCLUDE_DIR)
        include(FetchContent)
        FetchContent_Declare(
            luau
            GIT_REPOSITORY https://github.com/luau-lang/luau.git
            GIT_TAG master
            SOURCE_DIR ${CMAKE_BINARY_DIR}/_deps/luau-src
        )
        FetchContent_GetProperties(luau)
        if(NOT luau_POPULATED)
            FetchContent_Populate(luau)
        endif()
        set(LUAU_SOURCE_DIR ${luau_SOURCE_DIR})
    endif()

    if(DEFINED LUAU_SOURCE_DIR)
        message(STATUS "  Luau source: ${LUAU_SOURCE_DIR}")
        message(STATUS "  Luau requires CMake build. Run: cd ${LUAU_SOURCE_DIR} && cmake -B build")
    endif()
endif()

# =============================================================================
# NativeLibrary 动态库
# =============================================================================

set(LVM_SOURCES
    lvm/lvm_api.cpp
    lvm/lvm_backend_lua55.cpp
    lvm/lvm_backend_luajit.cpp
    lvm/lvm_backend_luau.cpp
)

add_library(NativeLibrary SHARED ${LVM_SOURCES})

# ---- 包含目录 ----
target_include_directories(NativeLibrary
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${LUA55_INCLUDE_DIR}
)

# ---- 编译定义：控制后端条件编译 ----
if(LVM_WITH_LUA55)
    target_compile_definitions(NativeLibrary PRIVATE LVM_HAS_LUA55)
    target_link_libraries(NativeLibrary PRIVATE ${LUA55_LIBRARY})
endif()

if(LVM_WITH_LUAJIT)
    target_compile_definitions(NativeLibrary PRIVATE LVM_HAS_LUAJIT)
endif()

if(LVM_WITH_LUAU)
    target_compile_definitions(NativeLibrary PRIVATE LVM_HAS_LUAU)
endif()

# ---- 平台特定设置 ----
if(LVM_PLATFORM_WINDOWS)
    target_compile_definitions(NativeLibrary PRIVATE LVM_EXPORTS _CRT_SECURE_NO_WARNINGS)

    # 导出所有 extern "C" 符号
    set_target_properties(NativeLibrary PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS ON
    )
endif()

# ---- MSVC 特定设置 ----
if(MSVC)
    target_compile_options(NativeLibrary PRIVATE /W3 /utf-8)
    target_compile_options(lua55 PRIVATE /utf-8)
endif()

# ---- 安装规则 ----
install(TARGETS NativeLibrary
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(DIRECTORY include/
    DESTINATION include/lvm
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
)

# =============================================================================
# 测试
# =============================================================================

if(LVM_BUILD_TESTS)
    enable_testing()

    add_executable(test_lvm
        ${CMAKE_SOURCE_DIR}/../tests/test_main.cpp
    )

    target_include_directories(test_lvm PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_link_libraries(test_lvm PRIVATE NativeLibrary)

    if(MSVC)
        target_compile_options(test_lvm PRIVATE /W3 /utf-8)
    endif()

    # 添加 CTest 测试
    add_test(NAME LvmUnitTests COMMAND test_lvm)

    message(STATUS "Tests enabled: test_lvm")
endif()

# =============================================================================
# 摘要
# =============================================================================

message(STATUS "")
message(STATUS "=== LuaVM Wrapper Configuration Summary ===")
message(STATUS "  Lua 5.5:    ${LVM_WITH_LUA55}")
message(STATUS "  LuaJIT:     ${LVM_WITH_LUAJIT}")
message(STATUS "  Luau:       ${LVM_WITH_LUAU}")
message(STATUS "  Tests:      ${LVM_BUILD_TESTS}")
message(STATUS "  Output:     NativeLibrary${CMAKE_SHARED_LIBRARY_SUFFIX}")
message(STATUS "")
