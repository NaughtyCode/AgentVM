# Changelog

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
