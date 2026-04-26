# LuaVM Wrapper — C++17 Lua VM Integration for .NET

A C++17 library that wraps Lua 5.5 / LuaJIT / Luau virtual machines behind a unified C ABI, designed for seamless .NET P/Invoke integration.

## Architecture

```
+-------------------------------------------------------------+
|                     C# Host (.NET 8+)                        |
|  P/Invoke -> AIPixelVM, only holds IntPtr _opaque        |
+-------------------------------------------------------------+
|          AIPixelVM.dll / .so / .dylib  (C ABI)           |
|  +-------------------------------------------------------+  |
|  | Public API: LVM_Create, LVM_Destroy, LVM_Exec...      |  |
|  | (All functions take void* opaque as first argument)    |  |
|  | Never exposes lua_State or any internal type           |  |
|  +---------------------------+---------------------------+  |
|          C++17 abstraction layer (lvm_engine / lvm_backend) |
|  +------------+  +------------+  +------------+            |
|  | Lua 5.5    |  | LuaJIT     |  | Luau       |            |
|  | Backend    |  | Backend    |  | Backend    |            |
|  +------------+  +------------+  +------------+            |
+-------------------------------------------------------------+
```

## Key Design Principles

- **Single Opaque Handle**: C# side only holds `IntPtr _opaque`, never knows internal structure
- **Polymorphic Backends**: `AbstractBackend` virtual interface, switch Lua 5.5 / LuaJIT / Luau at runtime
- **Completely Hidden Internals**: All backend differences (Lua 5.5 seed, Luau bytecode) encapsulated in backend classes
- **Stable C ABI**: All parameters are blittable (int, double, string, IntPtr), no custom marshaling needed
- **Cross-Platform**: Windows / Linux / macOS via CMake + .NET RID

## Requirements

### C++ Build
- CMake >= 3.20
- C++17 compiler (MSVC 2019+, GCC 9+, Clang 10+)
- Lua 5.4+ source (auto-downloaded by CMake)

### C# Integration
- .NET 8.0+
- Built `AIPixelVM.dll` (or `.so` / `.dylib`)

## Quick Start

### 1. Build the Native Library

```bash
# Default: Lua 5.5 backend only
cmake -S src -B build -DLVM_WITH_LUA55=ON
cmake --build build --config Release
```

Output: `build/Release/AIPixelVM.dll`

### 2. Run C++ Tests

```bash
cmake -S src -B build -DLVM_WITH_LUA55=ON -DLVM_BUILD_TESTS=ON
cmake --build build --config Release
./build/Release/test_lvm.exe
```

### 3. Use from C#

```csharp
using LuaVM;

// Create a Lua 5.5 VM
using var vm = new LuaVM(LuaVMType.Lua55);

// Execute Lua code
vm.Execute("print('Hello from Lua!')");

// Set and get global variables
vm.SetGlobalNumber("answer", 42);
double answer = vm.GetGlobalNumber("answer");

// Work with tables
vm.NewTable();
vm.PushNumber(100);
vm.SetField(1, "x");
vm.SetGlobal("myTable");

// Error handling
int result = vm.Execute("invalid syntax -->");
if (result != 0)
    Console.WriteLine($"Error: {vm.GetLastError()}");
```

Place `AIPixelVM.dll` in your C# project's output directory.

### CMake Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `LVM_WITH_LUA55` | ON | Build with Lua 5.5 backend |
| `LVM_WITH_LUAJIT` | OFF | Build with LuaJIT backend |
| `LVM_WITH_LUAU` | OFF | Build with Luau backend |
| `LVM_BUILD_TESTS` | ON | Build test executable |

### Combined Build Example

```bash
# Build with multiple backends (requires manual setup of LuaJIT/Luau)
cmake -S src -B build \
    -DLVM_WITH_LUA55=ON \
    -DLVM_WITH_LUAJIT=ON \
    -DLVM_WITH_LUAU=ON \
    -DLUAJIT_SOURCE_DIR=/path/to/luajit \
    -DLUAU_SOURCE_DIR=/path/to/luau
```

## Project Structure

```
src/
  CMakeLists.txt              # Build system
  include/
    lvm_api.h                 # Public C ABI header
    lvm_backend.h             # Abstract backend interface
    lvm_engine.hpp            # Opaque state + engine utilities
    lvm_backend_lua55.h       # Lua 5.5 backend header
    lvm_backend_luajit.h      # LuaJIT backend header
    lvm_backend_luau.h        # Luau backend header
  lvm/
    lvm_api.cpp               # Public API implementation
    lvm_backend_lua55.cpp     # Lua 5.5 backend implementation
    lvm_backend_luajit.cpp    # LuaJIT backend implementation
    lvm_backend_luau.cpp      # Luau backend implementation
csharp/
  LuaVM/
    LuaVM.cs                  # C# wrapper class
    Program.cs                # Usage examples
    LuaVM.csproj              # .NET project
tests/
  test_main.cpp               # Unit tests
changelog/                    # Modification records
```

## Public API Reference

### Lifecycle
| Function | Description |
|----------|-------------|
| `LVM_Create(type)` | Create VM instance (1=Lua55, 2=LuaJIT, 3=Luau) |
| `LVM_Destroy(opaque)` | Destroy VM instance |
| `LVM_GetLastError(opaque)` | Get last error message |

### Script Execution
| Function | Description |
|----------|-------------|
| `LVM_ExecuteString(opaque, code)` | Execute Lua source code string |
| `LVM_ExecuteFile(opaque, path)` | Execute Lua script file |

### Stack Operations
| Function | Description |
|----------|-------------|
| `LVM_GetTop(opaque)` | Get stack top index |
| `LVM_SetTop(opaque, index)` | Set stack top |
| `LVM_PushNumber(opaque, v)` | Push double value |
| `LVM_PushString(opaque, s)` | Push string value |
| `LVM_PushBoolean(opaque, v)` | Push boolean value |
| `LVM_PushNil(opaque)` | Push nil |

### Type Checks
| Function | Description |
|----------|-------------|
| `LVM_IsNumber(opaque, idx)` | Check if value at index is number |
| `LVM_IsString(opaque, idx)` | Check if value at index is string |
| `LVM_IsBoolean(opaque, idx)` | Check if value at index is boolean |
| `LVM_IsNil(opaque, idx)` | Check if value at index is nil |

### Value Extraction
| Function | Description |
|----------|-------------|
| `LVM_ToNumber(opaque, idx)` | Get number at index |
| `LVM_ToString(opaque, idx)` | Get string at index |
| `LVM_ToBoolean(opaque, idx)` | Get boolean at index |

### Global Variables
| Function | Description |
|----------|-------------|
| `LVM_GetGlobal(opaque, name)` | Push global variable onto stack |
| `LVM_SetGlobal(opaque, name)` | Pop stack top to global variable |

### Table Operations
| Function | Description |
|----------|-------------|
| `LVM_NewTable(opaque)` | Create new table, push onto stack |
| `LVM_GetField(opaque, idx, key)` | Get table field |
| `LVM_SetField(opaque, idx, key)` | Set table field |

## Supported Lua Backends

| Backend | Source | Key Features |
|---------|--------|--------------|
| Lua 5.5 | https://github.com/lua/lua.git | Latest official Lua, seed-based random |
| LuaJIT | https://github.com/LuaJIT/LuaJIT.git | JIT compilation, FFI, Lua 5.1 compat |
| Luau | https://github.com/luau-lang/luau.git | Sandboxing, type checking, bytecode compilation |

## Limitations

1. **Stack-based API**: All value passing uses a manual stack, similar to the Lua C API. Some developers may prefer a higher-level API.

2. **No Garbage Collection Hooks**: The wrapper does not expose Lua's GC control functions (`lua_gc`). Memory management is fully automatic.

3. **Single-threaded per Instance**: Each opaque handle is NOT thread-safe for concurrent access. Use separate instances for multi-threaded scenarios.

4. **Cdecl Calling Convention**: P/Invoke requires `CallingConvention.Cdecl`. Ensure this matches the native library build.

5. **String Lifetime**: Strings returned by `LVM_ToString` point to internal buffers and may be overwritten by subsequent operations. Copy immediately if needed.

6. **Lua 5.5 Compatibility**: The current implementation uses Lua 5.4 with a compatibility path. For actual Lua 5.5 features (`lua_sethostrandomseed`), update the backend source when Lua 5.5 is released.

7. **Luau Sandbox**: Luau backend defaults to sandboxed mode. File operations require explicit sandbox configuration.

8. **LuaJIT Build**: LuaJIT uses its own build system (Makefile). CMake integration requires manual setup.

9. **No Coroutine API**: Coroutine operations are not exposed through the current Public API (available through `LVM_ExecuteString` with `coroutine.*` functions).

10. **No Debug API**: Debug hooks and inspection functions are not exposed.

## Extending with a New Backend

1. Create a class inheriting `AbstractBackend`
2. Implement all pure virtual methods
3. Add the backend type to `VMType` enum in `lvm_engine.hpp`
4. Add a case in `LVM_Create()` factory function
5. Set the CMake option `LVM_WITH_*` and add the corresponding compilation flag

## License

This wrapper is provided as-is. See the respective Lua/LuaJIT/Luau repositories for their licenses.
