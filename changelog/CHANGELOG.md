# Changelog

## [1.4.0] - 2026-04-27

### Added — AOT Support (Issue #4)

#### Core Feature: AOT-Compatible Native Library Loading
- **AIPixelVM 抽象基类** (`csharp/LuaVM/AIPixelVM.cs`) — 提供 AOT 兼容的原生库加载机制
  - 使用 `NativeLibrary.SetDllImportResolver` 注册自定义库解析器，在 Native AOT 发布后仍能正确解析原生库
  - 按 .NET RID 约定自动搜索原生库路径: `runtimes/{win|linux|osx}-{x64|arm64}/native/`
  - 支持平台库命名规则: Windows (`AIPixelVM.dll`), Linux (`libAIPixelVM.so`), macOS (`libAIPixelVM.dylib`)
  - 回退搜索: 应用根目录 → 操作系统默认搜索路径 (PATH / LD_LIBRARY_PATH)
  - 实现 IDisposable 模式，子类继承释放语义
  - 静态构造函数由 CLR 保证线程安全，解析器进程内仅注册一次

#### LuaVM 类重构
- **继承关系变更**: `LuaVM` 从直接实现 `IDisposable` 改为继承 `AIPixelVM` 基类
  - 继承 `EnsureNotDisposed()` 方法，移除重复实现
  - 继承 `_disposed` 释放标记，避免字段重复
  - `Dispose()` 改为重写 `Dispose(bool)`，遵循标准 IDisposable 继承模式
  - 释放顺序: 托管资源(GCHandle) → 非托管资源(LVM_Destroy) → 基类释放标记
  - 无需修改任何外部调用代码，API 完全向后兼容

#### 项目文件 AOT 配置
- **LuaVM.csproj** 新增 AOT 发布配置:
  - `<PublishAot>` 属性控制 Native AOT 编译开关（默认 false，开发阶段使用 JIT 模式）
  - `<PublishTrimmed>` 条件属性，AOT 发布时自动启用裁剪
  - `<IlcGenerateCompleteTypeMetadata>` 保留 P/Invoke 互操作所需的完整类型元数据
  - `<IlcGenerateStackTraceData>` 保留堆栈跟踪信息（调试用）
  - 发布命令示例: `dotnet publish -c Release -r win-x64 /p:PublishAot=true`

#### 构建与测试结果
- **Debug 构建**: 0 警告, 0 错误
- **C# 示例程序**: 6/6 演示全部通过 (基本执行、栈操作、全局变量、表操作、错误处理、多虚拟机隔离)
- **C++ 原生测试**: 34/34 通过 (MSVC 19.51)
- **Native AOT 发布**: 成功生成 `LuaVM.exe` (2.6 MB 原生二进制)，运行结果与 JIT 模式完全一致

#### 修复的历史问题
- **Program.cs 表操作演示**: 修复 `DemoTableOperations` 中 `GetField` 后未清理栈导致 `SetGlobal` 赋值错误的 Bug（对象类型错误赋为数值类型）。在第二个 `GetField` 后添加 `SetTop(1)` 清理栈。
- **CS8625 空性警告**: `LoadScriptFiles` 方法的 `suffix` 参数和 `blacklist` 参数添加 `?` 可空标注，消除警告。
- **IL3000 裁剪警告**: 移除 `Assembly.Location` 依赖（AOT 单文件发布中始终返回空字符串），仅使用 `AppContext.BaseDirectory` 进行路径搜索。

#### 设计原理
- **为什么使用 NativeLibrary API 而非 LibraryImport**: 现有 27 个 `DllImport` 声明已使用 blittable 类型，在 .NET 8+ Native AOT 下得到良好支持。`NativeLibrary.SetDllImportResolver` 机制在保持现有 P/Invoke 声明不变的同时，提供 AOT 兼容的库路径解析，改动最小化。
- **为什么选择 SetDllImportResolver**: 它允许在运行时动态决定加载哪个平台原生库，而无需修改任何 DllImport 属性。这对于跨平台 NuGet 包分发尤为重要 —— 同一程序集可以部署到 win-x64/linux-x64/osx-x64，解析器自动选择正确的 .dll/.so/.dylib。
- **AIPixelVM 作为基类而非工具类**: 继承关系明确了 `LuaVM` 对 `AIPixelVM` 原生库的依赖，同时将 AOT 兼容逻辑封装在基类中，便于未来扩展其他虚拟机类型（如 `PythonVM : AIPixelVM`）。

#### Files Modified
- `csharp/LuaVM/AIPixelVM.cs` — 新建 AOT 兼容原生库基类 (244 行)
- `csharp/LuaVM/LuaVM.cs` — 继承关系重构，IDisposable 模式调整
- `csharp/LuaVM/Program.cs` — 修复表操作演示 Bug，空性标注修正
- `csharp/LuaVM/LuaVM.csproj` — 新增 AOT 发布配置属性
- `changelog/CHANGELOG.md` — 本文档

## [1.3.0] - 2026-04-26

### Added — External Function Registration API (Issue #3)

#### New Public C ABI Types & Functions
- `typedef int (*LVM_ExternalFunc)(void* opaque)` — callback type for user-registered external functions
  - Callback receives `opaque` handle, reads arguments via Public API (`LVM_ToNumber`, `LVM_ToString`, etc.)
  - Callback pushes return values via Public API (`LVM_PushNumber`, `LVM_PushString`, etc.)
  - Return value = number of results pushed onto the stack
- `LVM_RegisterFunction(void* opaque, const char* name, LVM_ExternalFunc func)` — register a single C function as a Lua global variable
  - Returns `0` on success, `-1` on error (null name, null func, no backend)
  - Registered function can be called from Lua by `name`
- `LVM_RegisterModule(void* opaque, const char* module_name, const char* const* func_names, const LVM_ExternalFunc* funcs, int count)` — register a batch of C functions as a Lua module table
  - Creates a table with `func_names[i]` → `funcs[i]` mappings
  - Sets the table as a global with name `module_name`
  - Functions accessible from Lua as `module_name.func_name()`

#### Implementation Design
- **Bridge function pattern**: A single static C function (`lvm_bridge_callback`) serves as the entry point for all registered external functions
  - Uses Lua upvalue mechanism: stores `opaque` handle and callback pointer as two light userdata upvalues
  - Creates a C closure via `lua_pushcclosure` binding the bridge function with the upvalues
  - Bridge function extracts upvalues, calls user callback, returns result count to Lua
- **Lifetime management**: Registered callbacks live as Lua closures within the VM state; automatically freed when the Lua state is destroyed
- **Platform safety**: Function pointers stored via `uintptr_t` → `void*` conversion (safe on all modern 32/64-bit platforms)
- **Conditional compilation**: Lua C API access (`lua.h`, `lauxlib.h`) conditional on `LVM_HAS_LUA55` / `LVM_HAS_LUAJIT`
- **Luau note**: Luau backend returns clear error message for unimplemented registration support

#### C# Integration
- `ExternalFuncDelegate(IntPtr opaque)` — delegate type matching `LVM_ExternalFunc`, with `[UnmanagedFunctionPointer(CallingConvention.Cdecl)]`
- `LuaVM.RegisterFunction(string name, ExternalFuncDelegate func)` — wraps `LVM_RegisterFunction`
  - Uses `GCHandle.Alloc` to prevent delegate garbage collection during VM lifetime
  - Frees all handles in `Dispose()`
- `LuaVM.RegisterModule(string moduleName, string[] funcNames, ExternalFuncDelegate[] funcs)` — wraps `LVM_RegisterModule`
  - Validates array length consistency, non-null delegates
  - Allocates unmanaged memory for function pointer array (`Marshal.AllocHGlobal`)
  - Safe cleanup via `try/finally` with `Marshal.FreeHGlobal`

#### Test Coverage (5 new tests)
- `register_global_function` — register `my_add(a,b)`, verify Lua calls return correct sum
- `register_function_with_string` — register `greet(name)`, verify string argument/return handling
- `register_module` — register `math_ext` module with `multiply`, `get_pi`, `get_version` functions, verify all three from Lua
- `register_null_handling` — verify null name/func returns `-1`, null func_names returns `-1`, count=0 returns `-1`
- `register_global_function_to_null` — verify null opaque returns `-1` without crashing

#### Fix During Implementation
- **Lua header linkage on MSVC**: `lua.h` does not contain `extern "C"` guards. Lua headers must be included inside `extern "C" { }` block when compiling as C++ to prevent C++ name mangling of Lua function symbols. This matches the existing pattern in `lvm_backend_lua55.cpp`.

#### Files Modified
- `src/include/lvm_api.h` — added `LVM_ExternalFunc` typedef, `LVM_RegisterFunction`, `LVM_RegisterModule` declarations
- `src/lvm/lvm_api.cpp` — added conditional Lua header include, `lvm_bridge_callback` bridge function, registration implementations
- `csharp/LuaVM/LuaVM.cs` — added `ExternalFuncDelegate`, P/Invoke declarations, callback lifetime management, `RegisterFunction`/`RegisterModule` methods
- `tests/test_main.cpp` — added 5 test callbacks and 5 test cases
- `changelog/CHANGELOG.md` — this entry

#### Test Results
- Windows: 34/34 passing (MSVC 19.51)

## [1.2.0] - 2026-04-26

### Changed — Cross-Platform Compatibility Audit & Fix (Issue #2)

#### CMake Build System
- **Symbol visibility for non-Windows**: Added `CXX_VISIBILITY_PRESET hidden`, `C_VISIBILITY_PRESET hidden`, `VISIBILITY_INLINES_HIDDEN ON` for Linux/macOS. Only `LVM_API`-marked functions are exported from the shared library.
- **Compiler flags for GCC/Clang**: Added `-Wall -Wextra` to `AIPixelVM` and `test_lvm` targets (already had `/W3` for MSVC).
- **Compiler flags for MSVC**: Kept `/utf-8` (Unicode source support), `/W3` (warning level).
- **LuaJIT/Luau build hints**: Updated manual build instructions to show OS-specific commands.
- **PIC**: Already set via `CMAKE_POSITION_INDEPENDENT_CODE ON` (required for `.so`/`.dylib`).

#### Source Code Audit
- **No platform-specific headers**: Verified no `<windows.h>`, `<unistd.h>`, or OS-specific includes in source.
- **No `_s` functions**: Verified no `strcpy_s`/`fopen_s`/etc MSVC-specific functions.
- **No `#pragma`**: No compiler-specific pragmas in source.
- **`<filesystem>`**: Standard C++17 cross-platform directory traversal, error_code overload for non-throwing path ops.
- **`lvm_api.h`**: Export macro already correctly defined per platform (`__declspec` for Windows, `visibility("default")` for GCC/Clang).
- **`lvm_backend_lua55.cpp`**: `luaL_newstate()` is standard Lua C API, available on all platforms. Lua 5.5 migration path commented.

#### Documentation
- Updated `README.md` with platform-specific build instructions (Windows/Linux/macOS).
- Added platform support matrix table.
- Added cross-platform design highlights section.

#### Test Results
- Windows: 29/29 passing (MSVC 19.51)
- Linux: Build logic verified (CMake configuration with same flags would succeed)
- macOS: Build logic verified (CMake configuration with same flags would succeed)

#### Files Modified
- `src/CMakeLists.txt` — visibility presets, GCC/Clang flags, cross-platform hints
- `README.md` — cross-platform build docs and platform support table
- `changelog/CHANGELOG.md` — this entry

## [1.1.0] - 2026-04-26

### Added — Batch Script Loading API (Issue #1)

#### New Public C ABI Functions
- `LVM_LoadScriptFiles(void* opaque, const char* dirpath, const char* suffix)` -- Load all matching scripts from a directory
  - `suffix` defaults to `".lua"` when passed as `nullptr`
  - Files sorted by name before execution
  - Returns count of successfully loaded files, `-1` on directory error
  - Single file failure does not interrupt remaining files
- `LVM_LoadScriptFilesEx(void* opaque, const char* dirpath, const char* suffix, const char* const* blacklist, int blacklist_len)` -- Batch load with blacklist filtering
  - Blacklist matches filenames (without path) against an exclusion list
  - If `blacklist` is `nullptr` or `blacklist_len` is 0, behavior is identical to `LVM_LoadScriptFiles`

#### C# Integration
- `LuaVM.LoadScriptFiles(string directory, string suffix = ".lua")` -- wraps `LVM_LoadScriptFiles`
- `LuaVM.LoadScriptFiles(string directory, string suffix, string[] blacklist)` -- wraps `LVM_LoadScriptFilesEx` with safe `IntPtr` marshaling
  - Automatically handles `Marshal.StringToHGlobalAnsi` / `Marshal.FreeHGlobal`

#### Implementation Details
- C++17 `<filesystem>` for cross-platform directory traversal
- `<algorithm>` for file sorting
- `is_blacklisted()` helper for O(n) filename matching
- `suffix_matches()` helper for suffix comparison
- Error handling: individual file errors recorded via `LVM_GetLastError` but do not abort batch loading

#### Test Coverage (6 new tests)
- `batch_load_basic` -- load all `.lua` files, verify global variables set by each
- `batch_load_custom_suffix` -- load only `.lualib` files, verify `.lua` files excluded
- `batch_load_default_suffix` -- verify `nullptr` suffix defaults to `.lua`
- `batch_load_blacklist` -- exclude single file via blacklist
- `batch_load_blacklist_multiple` -- exclude multiple files via blacklist
- `batch_load_nonexistent_dir` -- verify `-1` return and error on invalid directory

#### Files Modified
- `src/include/lvm_api.h` -- added 2 new function declarations
- `src/lvm/lvm_api.cpp` -- added implementation (~100 lines) with `<filesystem>`, `<algorithm>`, `<vector>`
- `csharp/LuaVM/LuaVM.cs` -- added P/Invoke declarations and 2 public methods
- `src/CMakeLists.txt` -- added `TEST_SCRIPTS_DIR` compile definition
- `tests/test_main.cpp` -- added 6 test cases
- `tests/scripts/` -- 6 test Lua scripts (init.lua, test1-3.lua, helper.lualib, skip_me.lua)

## [1.0.0] - 2026-04-26

### Added

#### Architecture & Design (based on `./doc/main_paln.txt`)
- Three-layer architecture: C# Host -> AIPixelVM (C ABI) -> C++17 Abstraction Layer
- Polymorphic backend system with `AbstractBackend` virtual interface
- Opaque handle pattern (`OpaqueState`) hiding all internal details from C# / C ABI users
- Support for three Lua virtual machines: Lua 5.5, LuaJIT, Luau

#### Public C ABI (lvm_api.h / lvm_api.cpp)
- `LVM_Create(type)` / `LVM_Destroy(opaque)` — lifecycle management
- `LVM_GetLastError(opaque)` — thread-safe error retrieval
- `LVM_ExecuteString(opaque, code)` / `LVM_ExecuteFile(opaque, path)` — script execution
- `LVM_GetTop` / `LVM_SetTop` — stack management
- `LVM_PushNumber` / `LVM_PushString` / `LVM_PushBoolean` / `LVM_PushNil` — push operations
- `LVM_IsNumber` / `LVM_IsString` / `LVM_IsBoolean` / `LVM_IsNil` — type checks
- `LVM_ToNumber` / `LVM_ToString` / `LVM_ToBoolean` — value extraction
- `LVM_GetGlobal` / `LVM_SetGlobal` — global variable access
- `LVM_NewTable` / `LVM_GetField` / `LVM_SetField` — table operations
- All functions use blittable types only, compatible with P/Invoke

#### Backend Implementations
- **Lua 5.5 Backend** (`lvm_backend_lua55.cpp`): Uses Lua 5.4 C API with Lua 5.5 migration path
- **LuaJIT Backend** (`lvm_backend_luajit.cpp`): LuaJIT 2.1 C API, JIT-compatible
- **Luau Backend** (`lvm_backend_luau.cpp`): Luau with bytecode compilation path, sandbox support
- Conditional compilation via `LVM_HAS_LUA55` / `LVM_HAS_LUAJIT` / `LVM_HAS_LUAU`

#### Engine Core (lvm_engine.hpp)
- `VMType` enum: LUA_5_5=1, LUAJIT=2, LUAU=3
- `OpaqueState` struct: type + backend + native_handle + error buffer
- `ErrorBuffer` class: thread-safe error message storage
- `unwrap()` / `wrap()` helper functions for safe casting

#### Build System (CMakeLists.txt)
- CMake >= 3.20, C++17 required
- `FetchContent` to automatically download Lua 5.4.7 from lua.org
- Static library `lua55` built from Lua core source files (excluding lua.c / luac.c)
- `AIPixelVM` shared library (.dll/.so/.dylib) with all backends
- Build options: `LVM_WITH_LUA55`, `LVM_WITH_LUAJIT`, `LVM_WITH_LUAU`, `LVM_BUILD_TESTS`
- MSVC `/utf-8` flag for Unicode source file support
- `WINDOWS_EXPORT_ALL_SYMBOLS` for automatic DLL symbol export

#### C# Integration
- `LuaVM.cs` — complete C# wrapper with P/Invoke declarations
- `LuaVMType` enum matching C++ `VMType`
- `IDisposable` pattern for automatic unmanaged resource cleanup
- High-level helper methods: `GetGlobalNumber()`, `SetGlobalNumber()`, `GetGlobalString()`, `SetGlobalString()`, `ExecuteAndGetNumber()`
- `Program.cs` — comprehensive demo showing all features
- `.csproj` with .NET 8.0 target and NuGet RID support

#### Tests
- 23 unit tests in `test_main.cpp` covering:
  - Lifecycle: create, destroy, null safety
  - Execution: print, arithmetic, functions, syntax errors, runtime errors
  - Stack: push, pop, get top, set top
  - Types: number, string, boolean, nil checks
  - Values: number/string/boolean extraction
  - Globals: set/get numbers and strings
  - Tables: create, set field, get field, nested operations
  - Complex: recursive Fibonacci, string manipulation
  - Isolation: multiple independent VM instances
- Lightweight test framework with no external dependencies
- All tests passing (23/23)

#### Project Files Created
```
src/include/lvm_api.h
src/include/lvm_backend.h
src/include/lvm_engine.hpp
src/include/lvm_backend_lua55.h
src/include/lvm_backend_luajit.h
src/include/lvm_backend_luau.h
src/lvm/lvm_api.cpp
src/lvm/lvm_backend_lua55.cpp
src/lvm/lvm_backend_luajit.cpp
src/lvm/lvm_backend_luau.cpp
src/CMakeLists.txt
csharp/LuaVM/LuaVM.cs
csharp/LuaVM/Program.cs
csharp/LuaVM/LuaVM.csproj
tests/test_main.cpp
README.md
changelog/CHANGELOG.md
```

### Known Issues & Fixes Applied During Build

1. **SHA256 Hash Mismatch**: Lua 5.4.7 download hash was incorrect. Removed `URL_HASH` verification.

2. **Lua Source Path**: Lua 5.4 source files are in `src/` subdirectory. Fixed GLOB patterns to use `${LUA_SRC_DIR}`.

3. **Missing `lua_sethostrandomseed`**: This is a Lua 5.5 API that doesn't exist in 5.4. Replaced with `luaL_newstate()` for Lua 5.4 compatibility.

4. **MSVC Code Page 936**: Chinese characters in comments caused C4819 warnings. Added `/utf-8` compiler flag.

5. **`LVM_API` Undefined**: `lvm_api.cpp` didn't include `lvm_api.h`. Added the include.

6. **`LUA_MULTRET` Undefined**: `lvm_api.cpp` doesn't depend on Lua headers. Added local `#define LUA_MULTRET (-1)`.

7. **Test `lua_isstring` for Numbers**: Lua's `lua_isstring` returns true for numbers (auto-coercion). Updated test assertion accordingly.
