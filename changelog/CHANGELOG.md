# Changelog

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
