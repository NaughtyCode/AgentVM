/**
 * @file    lvm_api.cpp
 * @brief   Lua VM Wrapper -- Public C ABI implementation
 *
 * Implements all C ABI functions declared in lvm_api.h.
 * Each function: unwrap opaque -> null check -> call backend -> return.
 */

#include "lvm_api.h"
#include "lvm_engine.hpp"

#ifdef LVM_HAS_LUA55
#include "lvm_backend_lua55.h"
#endif

#ifdef LVM_HAS_LUAJIT
#include "lvm_backend_luajit.h"
#endif

#ifdef LVM_HAS_LUAU
#include "lvm_backend_luau.h"
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <filesystem>
#include <algorithm>
#include <vector>

// LUA_MULTRET: request all return values from lua_pcall
// Defined as -1 in lua.h; defined here to avoid depending on Lua headers
#ifndef LUA_MULTRET
#define LUA_MULTRET (-1)
#endif

using namespace lvm;
using namespace lvm::detail;

// =========================================================================
// Factory: create VM instance
// =========================================================================

extern "C" {

LVM_API void* LVM_Create(int type)
{
    auto* op = new (std::nothrow) OpaqueState();
    if (!op) {
        return nullptr;
    }

    op->type = static_cast<VMType>(type);

    switch (op->type) {
#ifdef LVM_HAS_LUA55
    case VMType::LUA_5_5:
        op->backend = std::make_unique<Lua55Backend>();
        break;
#endif
#ifdef LVM_HAS_LUAJIT
    case VMType::LUAJIT:
        op->backend = std::make_unique<LuaJITBackend>();
        break;
#endif
#ifdef LVM_HAS_LUAU
    case VMType::LUAU:
        op->backend = std::make_unique<LuauBackend>();
        break;
#endif
    default:
        op->error.set("LVM_Create: unsupported VM type or backend not compiled");
        delete op;
        return nullptr;
    }

    try {
        op->native_handle = op->backend->create_state();
    } catch (const std::exception& e) {
        op->error.set(std::string("LVM_Create: backend exception: ") + e.what());
        delete op;
        return nullptr;
    } catch (...) {
        op->error.set("LVM_Create: unknown backend exception");
        delete op;
        return nullptr;
    }

    if (!op->native_handle) {
        op->error.set("LVM_Create: backend failed to create native state");
        delete op;
        return nullptr;
    }

    return wrap(op);
}

// =========================================================================
// Destroy VM instance
// =========================================================================

LVM_API void LVM_Destroy(void* opaque)
{
    if (!opaque) return;

    auto* op = unwrap(opaque);

    if (op->native_handle && op->backend) {
        try {
            op->backend->destroy_state(op->native_handle);
        } catch (...) {
            /* ignore exceptions during destruction */
        }
        op->native_handle = nullptr;
    }

    op->backend.reset();
    delete op;
}

// =========================================================================
// Get last error
// =========================================================================

LVM_API const char* LVM_GetLastError(void* opaque)
{
    if (!opaque) return "null opaque";
    auto* op = unwrap(opaque);
    return op->error.get();
}

// =========================================================================
// Script execution
// =========================================================================

LVM_API int LVM_ExecuteString(void* opaque, const char* code)
{
    if (!opaque) return -1;
    auto* op = unwrap(opaque);

    if (!code) {
        op->error.set("LVM_ExecuteString: code is null");
        return -1;
    }

    auto  L = op->native_handle;
    auto& b = *op->backend;

    int loadResult = b.load_string(L, code);
    if (loadResult != 0) {
        const char* err = b.tostring(L, -1);
        op->error.set(err ? err : "unknown compile error");
        b.settop(L, 0);
        return -1;
    }

    int execResult = b.pcall(L, 0, LUA_MULTRET, 0);
    if (execResult != 0) {
        const char* err = b.tostring(L, -1);
        op->error.set(err ? err : "unknown runtime error");
        b.settop(L, 0);
        return -1;
    }

    b.settop(L, 0);
    op->error.clear();
    return 0;
}

LVM_API int LVM_ExecuteFile(void* opaque, const char* filepath)
{
    if (!opaque) return -1;
    auto* op = unwrap(opaque);

    if (!filepath) {
        op->error.set("LVM_ExecuteFile: filepath is null");
        return -1;
    }

    auto  L = op->native_handle;
    auto& b = *op->backend;

    int loadResult = b.load_file(L, filepath);
    if (loadResult != 0) {
        const char* err = b.tostring(L, -1);
        op->error.set(err ? err : "unknown file load error");
        b.settop(L, 0);
        return -1;
    }

    int execResult = b.pcall(L, 0, LUA_MULTRET, 0);
    if (execResult != 0) {
        const char* err = b.tostring(L, -1);
        op->error.set(err ? err : "unknown runtime error");
        b.settop(L, 0);
        return -1;
    }

    b.settop(L, 0);
    op->error.clear();
    return 0;
}

// =========================================================================
// Stack operations
// =========================================================================

LVM_API int LVM_GetTop(void* opaque)
{
    if (!opaque) return 0;
    auto* op = unwrap(opaque);
    return op->backend->gettop(op->native_handle);
}

LVM_API void LVM_SetTop(void* opaque, int index)
{
    if (!opaque) return;
    auto* op = unwrap(opaque);
    op->backend->settop(op->native_handle, index);
}

// =========================================================================
// Push operations
// =========================================================================

LVM_API void LVM_PushNumber(void* opaque, double value)
{
    if (!opaque) return;
    auto* op = unwrap(opaque);
    op->backend->pushnumber(op->native_handle, value);
}

LVM_API void LVM_PushString(void* opaque, const char* str)
{
    if (!opaque) return;
    auto* op = unwrap(opaque);
    if (!str) {
        op->error.set("LVM_PushString: null string");
        return;
    }
    op->backend->pushstring(op->native_handle, str);
}

LVM_API void LVM_PushBoolean(void* opaque, int value)
{
    if (!opaque) return;
    auto* op = unwrap(opaque);
    op->backend->pushboolean(op->native_handle, value);
}

LVM_API void LVM_PushNil(void* opaque)
{
    if (!opaque) return;
    auto* op = unwrap(opaque);
    op->backend->pushnil(op->native_handle);
}

// =========================================================================
// Type checks
// =========================================================================

LVM_API int LVM_IsNumber(void* opaque, int index)
{
    if (!opaque) return 0;
    auto* op = unwrap(opaque);
    return op->backend->isnumber(op->native_handle, index);
}

LVM_API int LVM_IsString(void* opaque, int index)
{
    if (!opaque) return 0;
    auto* op = unwrap(opaque);
    return op->backend->isstring(op->native_handle, index);
}

LVM_API int LVM_IsBoolean(void* opaque, int index)
{
    if (!opaque) return 0;
    auto* op = unwrap(opaque);
    return op->backend->isboolean(op->native_handle, index);
}

LVM_API int LVM_IsNil(void* opaque, int index)
{
    if (!opaque) return 0;
    auto* op = unwrap(opaque);
    return op->backend->isnil(op->native_handle, index);
}

// =========================================================================
// Value extraction
// =========================================================================

LVM_API double LVM_ToNumber(void* opaque, int index)
{
    if (!opaque) return 0.0;
    auto* op = unwrap(opaque);
    return op->backend->tonumber(op->native_handle, index);
}

LVM_API const char* LVM_ToString(void* opaque, int index)
{
    if (!opaque) return nullptr;
    auto* op = unwrap(opaque);
    const char* result = op->backend->tostring(op->native_handle, index);
    if (!result) {
        op->error.set("LVM_ToString: not a string or index out of range");
    }
    return result;
}

LVM_API int LVM_ToBoolean(void* opaque, int index)
{
    if (!opaque) return 0;
    auto* op = unwrap(opaque);
    return op->backend->toboolean(op->native_handle, index);
}

// =========================================================================
// Global variables
// =========================================================================

LVM_API void LVM_GetGlobal(void* opaque, const char* name)
{
    if (!opaque) return;
    auto* op = unwrap(opaque);
    if (!name) {
        op->error.set("LVM_GetGlobal: null name");
        return;
    }
    op->backend->getglobal(op->native_handle, name);
}

LVM_API void LVM_SetGlobal(void* opaque, const char* name)
{
    if (!opaque) return;
    auto* op = unwrap(opaque);
    if (!name) {
        op->error.set("LVM_SetGlobal: null name");
        return;
    }
    op->backend->setglobal(op->native_handle, name);
}

// =========================================================================
// Table operations
// =========================================================================

LVM_API void LVM_NewTable(void* opaque)
{
    if (!opaque) return;
    auto* op = unwrap(opaque);
    op->backend->newtable(op->native_handle);
}

LVM_API void LVM_GetField(void* opaque, int index, const char* key)
{
    if (!opaque) return;
    auto* op = unwrap(opaque);
    if (!key) {
        op->error.set("LVM_GetField: null key");
        return;
    }
    op->backend->getfield(op->native_handle, index, key);
}

LVM_API void LVM_SetField(void* opaque, int index, const char* key)
{
    if (!opaque) return;
    auto* op = unwrap(opaque);
    if (!key) {
        op->error.set("LVM_SetField: null key");
        return;
    }
    op->backend->setfield(op->native_handle, index, key);
}

// =========================================================================
// Batch script loading from directory
// =========================================================================

/**
 * @brief 检查文件名是否在黑名单中
 * @param filename  文件名（不含路径）
 * @param blacklist 黑名单数组
 * @param len       黑名单长度
 * @return true 表示文件被列入黑名单，应跳过
 */
static bool is_blacklisted(const std::string& filename,
                           const char* const* blacklist, int len)
{
    for (int i = 0; i < len; ++i) {
        if (blacklist[i] && filename == blacklist[i]) {
            return true;
        }
    }
    return false;
}

/**
 * @brief 检查文件名是否匹配指定后缀
 * @param filename  文件名
 * @param suffix    后缀（如 ".lua"）
 * @return true 表示后缀匹配
 */
static bool suffix_matches(const std::string& filename, const std::string& suffix)
{
    if (filename.length() < suffix.length()) return false;
    return filename.compare(filename.length() - suffix.length(),
                            suffix.length(), suffix) == 0;
}

LVM_API int LVM_LoadScriptFiles(void* opaque, const char* dirpath, const char* suffix)
{
    // 委托给 LVM_LoadScriptFilesEx，传入空黑名单
    return LVM_LoadScriptFilesEx(opaque, dirpath, suffix, nullptr, 0);
}

LVM_API int LVM_LoadScriptFilesEx(void* opaque, const char* dirpath, const char* suffix,
                                   const char* const* blacklist, int blacklist_len)
{
    /* 1. 参数校验 */
    if (!opaque) return -1;
    auto* op = unwrap(opaque);

    if (!dirpath) {
        op->error.set("LVM_LoadScriptFilesEx: dirpath is null");
        return -1;
    }

    // 默认后缀为 ".lua"
    const char* actual_suffix = (suffix && suffix[0] != '\0') ? suffix : ".lua";

    /* 2. 检查目录是否存在 */
    std::error_code ec;
    if (!std::filesystem::is_directory(dirpath, ec)) {
        op->error.set(std::string("LVM_LoadScriptFilesEx: not a directory: ")
                      + dirpath);
        return -1;
    }

    /* 3. 收集匹配后缀且不在黑名单中的文件路径 */
    std::vector<std::filesystem::path> scripts;

    for (const auto& entry : std::filesystem::directory_iterator(dirpath, ec)) {
        if (ec) break;  // 目录遍历出错则停止

        if (!entry.is_regular_file(ec)) continue;
        if (ec) continue;

        std::string filename = entry.path().filename().string();

        // 后缀匹配
        if (!suffix_matches(filename, actual_suffix)) continue;

        // 黑名单过滤
        if (blacklist && blacklist_len > 0) {
            if (is_blacklisted(filename, blacklist, blacklist_len)) continue;
        }

        scripts.push_back(entry.path());
    }

    if (ec) {
        op->error.set(std::string("LVM_LoadScriptFilesEx: directory iterator error: ")
                      + ec.message());
        return -1;
    }

    /* 4. 按文件名排序以保证可预测的执行顺序 */
    std::sort(scripts.begin(), scripts.end());

    /* 5. 逐个执行脚本文件 */
    int success_count = 0;
    auto  L = op->native_handle;
    auto& b = *op->backend;

    for (const auto& script_path : scripts) {
        std::string path_str = script_path.string();

        // 编译文件
        int loadResult = b.load_file(L, path_str.c_str());
        if (loadResult != 0) {
            // 记录错误但继续处理后续文件
            const char* err = b.tostring(L, -1);
            op->error.set(std::string("LVM_LoadScriptFilesEx: ")
                          + path_str + ": " + (err ? err : "load error"));
            b.settop(L, 0);
            continue;
        }

        // 执行文件
        int execResult = b.pcall(L, 0, LUA_MULTRET, 0);
        if (execResult != 0) {
            const char* err = b.tostring(L, -1);
            op->error.set(std::string("LVM_LoadScriptFilesEx: ")
                          + path_str + ": " + (err ? err : "runtime error"));
            b.settop(L, 0);
            continue;
        }

        b.settop(L, 0);
        success_count++;
    }

    return success_count;
}

} /* extern "C" */
