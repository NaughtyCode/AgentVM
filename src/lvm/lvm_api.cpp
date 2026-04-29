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
#include <cstdint>

// 条件包含 Lua 头文件 —— 用于外部函数注册（lua_pushcclosure 等）
// CMake 已将 LUA55_INCLUDE_DIR 加入 AIPixelVM 的 PRIVATE 包含目录
// 注意：lua.h 不含 extern "C" 保护，从 C++ 编译时须手动包裹
#if defined(LVM_HAS_LUA55) || defined(LVM_HAS_LUAJIT)
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
#endif

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

LVM_API void LVM_PushValue(void* opaque, int index)
{
    /* 将栈 index 处元素的副本压入栈顶
     * 典型用途：复制函数引用到栈顶，使得 pcall 后原引用仍保留
     * 使用示例：
     *   LVM_GetGlobal(vm, "my_func");  // [my_func]
     *   LVM_PushValue(vm, -1);         // [my_func, my_func]  -- 复制一份
     *   LVM_PCall(vm, 0, 0);          // [my_func]  -- pcall 弹出副本执行
     *   // 原函数引用仍在栈上，可再次调用
     */
    if (!opaque) return;
    auto* op = unwrap(opaque);
    op->backend->pushvalue(op->native_handle, index);
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

LVM_API int LVM_IsFunction(void* opaque, int index)
{
    /* 参数校验：null opaque 安全返回 0 */
    if (!opaque) return 0;
    auto* op = unwrap(opaque);
    /* 委托给后端的具体实现（Lua 5.5 / LuaJIT / Luau 均支持此操作） */
    return op->backend->isfunction(op->native_handle, index);
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
// Function call — 调用栈上的 Lua 函数（全局函数 / 模块内函数）
// =========================================================================

/**
 * @brief 以保护模式调用栈上的 Lua 函数
 *
 * 本函数是调用 Lua 脚本中定义的函数的核心 API：
 * - 调用前需将函数和参数按正确顺序压入栈中
 * - 函数必须位于参数下方：栈布局为 [..., func, arg1, ..., argN]
 * - 调用成功后将参数和函数弹出，压入指定数量的返回值
 * - 调用失败时错误信息存储在 opaque 的错误缓冲区中
 *
 * 典型使用场景：
 * 1. 调用全局 Lua 函数（由脚本定义）
 * 2. 调用 Lua 模块（表）内的函数
 * 3. 调用外部注册的 C 函数（但通常直接从 Lua 侧调用）
 *
 * @param opaque    虚拟机句柄
 * @param nargs     传递给函数的参数数量
 * @param nresults  期望的返回值数量（-1 = LUA_MULTRET 表示返回所有值）
 * @return 0 = 成功（结果在栈顶），非 0 = 运行时错误
 */
LVM_API int LVM_PCall(void* opaque, int nargs, int nresults)
{
    /* 1. 参数校验 */
    if (!opaque) return -1;
    auto* op = unwrap(opaque);

    auto  L = op->native_handle;
    auto& b = *op->backend;

    /* 2. 调用后端 pcall：
     *    - nargs:  从栈顶往下数的参数个数
     *    - nresults: 期望压入栈中的返回值个数
     *    - errfunc: 0 表示没有错误处理函数
     */
    int execResult = b.pcall(L, nargs, nresults, 0);

    /* 3. 处理执行结果 */
    if (execResult != 0) {
        // 函数执行出错：提取错误信息并存储
        const char* err = b.tostring(L, -1);
        op->error.set(err ? err : "unknown runtime error in pcall");
        // 弹出错误信息（保留栈的干净状态）
        b.settop(L, 0);
        return -1;
    }

    // 执行成功：清除错误缓冲区，结果保留在栈上供调用者读取
    op->error.clear();
    return 0;
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

// =========================================================================
// External function registration — bridge function and API implementation
// =========================================================================

#if defined(LVM_HAS_LUA55) || defined(LVM_HAS_LUAJIT)
/**
 * @brief 桥接函数：将 Lua C 函数调用转发至用户注册的回调
 *
 * 所有通过 LVM_RegisterFunction / LVM_RegisterModule 注册的外部函数
 * 最终都由本函数作为入口，它负责：
 * 1. 从 Lua upvalue 中取出 opaque 句柄与用户回调指针
 * 2. 调用用户回调（回调通过 Public API 读写 VM 栈）
 * 3. 将回调返回值（压入栈中的结果数量）返回给 Lua
 *
 * @param L  Lua 原生状态指针
 * @return 用户回调压入栈中的返回值数量
 */
static int lvm_bridge_callback(lua_State* L)
{
    // 从第一个 upvalue 取出 opaque 句柄（light userdata）
    void* opaque = lua_touserdata(L, lua_upvalueindex(1));

    // 从第二个 upvalue 取出用户回调函数指针
    // 通过 uintptr_t 安全地在函数指针与 void* 之间转换
    LVM_ExternalFunc func = reinterpret_cast<LVM_ExternalFunc>(
        reinterpret_cast<uintptr_t>(lua_touserdata(L, lua_upvalueindex(2))));

    if (!opaque || !func) {
        lua_pushstring(L, "lvm_bridge_callback: invalid opaque or callback");
        lua_error(L);
        return 0;
    }

    // 调用用户回调
    // 回调通过 Public API (LVM_ToNumber, LVM_PushNumber, etc.) 操作栈:
    //   - 读取: LVM_ToNumber(opaque, 1)..LVM_ToNumber(opaque, N) 读取 Lua 参数
    //   - 写入: LVM_Push*(opaque, ...) 压入返回值
    int nresults = func(opaque);

    return nresults;
}
#endif // LVM_HAS_LUA55 || LVM_HAS_LUAJIT

LVM_API int LVM_RegisterFunction(void* opaque, const char* name, LVM_ExternalFunc func)
{
    if (!opaque) return -1;
    auto* op = unwrap(opaque);

    if (!name || !name[0]) {
        op->error.set("LVM_RegisterFunction: name is null or empty");
        return -1;
    }
    if (!func) {
        op->error.set("LVM_RegisterFunction: func is null");
        return -1;
    }

#if defined(LVM_HAS_LUA55) || defined(LVM_HAS_LUAJIT)
    auto* L = static_cast<lua_State*>(op->native_handle);

    // 压入两个 upvalue：
    //   1. opaque 句柄（light userdata，供桥接函数使用）
    //   2. 用户回调指针（通过 uintptr_t 转为 light userdata 安全存储）
    lua_pushlightuserdata(L, opaque);
    lua_pushlightuserdata(L, reinterpret_cast<void*>(
        reinterpret_cast<uintptr_t>(func)));

    // 创建 C 闭包: 桥接函数 + 2 个 upvalue
    lua_pushcclosure(L, lvm_bridge_callback, 2);

    // 设置为全局变量
    lua_setglobal(L, name);

    op->error.clear();
    return 0;
#elif defined(LVM_HAS_LUAU)
    op->error.set("LVM_RegisterFunction: not yet supported for Luau backend");
    return -1;
#else
    op->error.set("LVM_RegisterFunction: no backend compiled (enable LVM_WITH_LUA55)");
    return -1;
#endif
}

LVM_API int LVM_RegisterModule(void* opaque, const char* module_name,
                                const char* const* func_names,
                                const LVM_ExternalFunc* funcs, int count)
{
    if (!opaque) return -1;
    auto* op = unwrap(opaque);

    if (!module_name || !module_name[0]) {
        op->error.set("LVM_RegisterModule: module_name is null or empty");
        return -1;
    }
    if (!func_names || !funcs) {
        op->error.set("LVM_RegisterModule: func_names or funcs is null");
        return -1;
    }
    if (count <= 0) {
        op->error.set("LVM_RegisterModule: count must be positive");
        return -1;
    }

#if defined(LVM_HAS_LUA55) || defined(LVM_HAS_LUAJIT)
    auto* L = static_cast<lua_State*>(op->native_handle);

    // 创建模块表（栈顶）
    lua_newtable(L);

    for (int i = 0; i < count; ++i) {
        if (!func_names[i] || !funcs[i]) {
            op->error.set(std::string("LVM_RegisterModule: null func_name or func at index ")
                          + std::to_string(i));
            // 弹出已部分构造的模块表
            lua_pop(L, 1);
            return -1;
        }

        // 压入两个 upvalue：opaque 句柄 + 回调函数指针
        lua_pushlightuserdata(L, opaque);
        lua_pushlightuserdata(L, reinterpret_cast<void*>(
            reinterpret_cast<uintptr_t>(funcs[i])));

        // 创建 C 闭包
        lua_pushcclosure(L, lvm_bridge_callback, 2);

        // 模块表[func_name] = 闭包
        lua_setfield(L, -2, func_names[i]);
    }

    // 将模块表设置为全局变量
    lua_setglobal(L, module_name);

    op->error.clear();
    return 0;
#elif defined(LVM_HAS_LUAU)
    op->error.set("LVM_RegisterModule: not yet supported for Luau backend");
    return -1;
#else
    op->error.set("LVM_RegisterModule: no backend compiled (enable LVM_WITH_LUA55)");
    return -1;
#endif
}

} /* extern "C" */
