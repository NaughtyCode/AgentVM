/**
 * @file    lvm_backend_luau.cpp
 * @brief   Luau 后端实现
 *
 * 将 AbstractBackend 虚函数映射到 Luau C API。
 * Luau 的设计目标是"mostly preserves Lua 5.1 API"，但有以下关键差异：
 *
 * 差异点对比：
 * ┌─────────────────┬────────────────────┬────────────────────┐
 * │ 功能            │ 标准 Lua           │ Luau               │
 * ├─────────────────┼────────────────────┼────────────────────┤
 * │ 创建状态        │ luaL_newstate()    │ lua_newstate()     │
 * │ 加载源码        │ luaL_loadstring()  │ luau_compile()     │
 * │                 │                    │ → luau_load()      │
 * │ 沙箱            │ 无                 │ luaL_sandbox()     │
 * │ 类型注解        │ 无                 │ 渐进式类型系统     │
 * │ 错误信息        │ lua_tostring()     │ 额外调试信息       │
 * └─────────────────┴────────────────────┴────────────────────┘
 *
 * 依赖: lua.h, lualib.h (来自 https://github.com/luau-lang/luau.git)
 */

#include "lvm_backend_luau.h"
#include "lvm_engine.hpp"

/* ---- Luau C API 头文件 ---- */
#ifdef LVM_HAS_LUAU
  extern "C" {
    #include <lua.h>
    #include <lualib.h>
  }
#else
  #define LUA_MULTRET (-1)
#endif

#include <cstdlib>
#include <cstring>

namespace lvm {

/* ==========================================================================
 * 生命周期
 * ========================================================================== */

void* LuauBackend::create_state() {
#ifdef LVM_HAS_LUAU
    /* === Luau 创建流程 ===
     *
     * 1. lua_newstate(alloc, ud) — 使用自定义分配器创建 lua_State
     * 2. luaL_openlibs(L)         — 加载 Luau 标准库
     * 3. 沙箱默认启用             — 若需文件访问需调用 luaL_sandbox(L, 0)
     *
     * 注意：Luau 的 luaL_newstate() 别名可能可用，但推荐使用 lua_newstate()
     */
    lua_State* L = lua_newstate(
        /* allocator: 标准 malloc/realloc/free 包装 */
        [](void*, void* ptr, size_t osize, size_t nsize) -> void* {
            (void)osize;
            if (nsize == 0) {
                std::free(ptr);
                return nullptr;
            }
            return std::realloc(ptr, nsize);
        },
        nullptr  // 用户数据
    );

    if (!L) {
        return nullptr;
    }

    /* 加载标准库（Luau 版本的 base, math, string, table, coroutine 等） */
    luaL_openlibs(L);

    /* 注释掉的沙箱配置（按需启用）：
     * luaL_sandbox(L, 0);  // 0 = 关闭沙箱，允许文件系统访问
     * 当前保留默认沙箱行为 —— 安全优先
     */

    return static_cast<void*>(L);
#else
    return nullptr;
#endif
}

void LuauBackend::destroy_state(void* state) {
#ifdef LVM_HAS_LUAU
    if (state) {
        lua_close(static_cast<lua_State*>(state));
    }
#else
    (void)state;
#endif
}

/* ==========================================================================
 * 脚本加载与执行
 * ========================================================================== */

int LuauBackend::load_string(void* state, const char* code) {
#ifdef LVM_HAS_LUAU
    auto* L = static_cast<lua_State*>(state);

    /* === Luau 特有的字节码编译路径 ===
     *
     * 1. luau_compile() → 将源码编译为 Luau 字节码
     *    参数: (source, source_len, options, out_size)
     *    options 为 nullptr 表示使用默认编译选项
     *
     * 2. luau_load() → 加载编译后的字节码到 lua_State
     *    参数: (L, chunkname, bytecode, bytecode_size, env=0)
     *
     * 相比直接用 luaL_loadstring，此路径可以：
     *   - 利用 Luau 编译器优化
     *   - 支持渐进式类型检查
     *   - 生成更好的错误诊断信息
     */

    /* Step 1: 编译 Lua 源码为字节码 */
    size_t bytecodeSize = 0;
    char* bytecode = luau_compile(code, std::strlen(code), nullptr, &bytecodeSize);

    if (!bytecode) {
        /* 编译失败：luau_compile 返回 nullptr 表示语法/类型错误 */
        lua_pushstring(L, "Luau compilation failed");
        return -1;
    }

    /* Step 2: 加载字节码到状态机 */
    int result = luau_load(L, "=script", bytecode, bytecodeSize, 0);

    /* 释放字节码临时缓冲区（luau_load 已复制所需数据） */
    std::free(bytecode);

    return result;
#else
    (void)state; (void)code;
    return -1;
#endif
}

int LuauBackend::load_file(void* state, const char* path) {
#ifdef LVM_HAS_LUAU
    auto* L = static_cast<lua_State*>(state);

    /* Luau 文件加载：读取文件 → 编译为字节码 → 加载
     * 注意：Luau 默认启用沙箱，若未关闭则文件系统访问受限制
     * 此处使用标准 luaL_loadfile 作为兼容路径 */

    /* 先尝试直接使用 luaL_loadfile（兼容模式） */
    return luaL_loadfile(L, path);
#else
    (void)state; (void)path;
    return -1;
#endif
}

int LuauBackend::pcall(void* state, int narg, int nret, int errfunc) {
#ifdef LVM_HAS_LUAU
    auto* L = static_cast<lua_State*>(state);
    /* lua_pcall 在 Luau 中依然可用，行为与标准 Lua 一致 */
    return lua_pcall(L, narg, nret, errfunc);
#else
    (void)state; (void)narg; (void)nret; (void)errfunc;
    return -1;
#endif
}

/* ==========================================================================
 * 栈操作
 * ========================================================================== */

int LuauBackend::gettop(void* state) {
#ifdef LVM_HAS_LUAU
    return lua_gettop(static_cast<lua_State*>(state));
#else
    (void)state; return 0;
#endif
}

void LuauBackend::settop(void* state, int idx) {
#ifdef LVM_HAS_LUAU
    lua_settop(static_cast<lua_State*>(state), idx);
#else
    (void)state; (void)idx;
#endif
}

void LuauBackend::pushnumber(void* state, double v) {
#ifdef LVM_HAS_LUAU
    lua_pushnumber(static_cast<lua_State*>(state), v);
#else
    (void)state; (void)v;
#endif
}

void LuauBackend::pushstring(void* state, const char* s) {
#ifdef LVM_HAS_LUAU
    lua_pushstring(static_cast<lua_State*>(state), s);
#else
    (void)state; (void)s;
#endif
}

void LuauBackend::pushboolean(void* state, int v) {
#ifdef LVM_HAS_LUAU
    lua_pushboolean(static_cast<lua_State*>(state), v);
#else
    (void)state; (void)v;
#endif
}

void LuauBackend::pushnil(void* state) {
#ifdef LVM_HAS_LUAU
    lua_pushnil(static_cast<lua_State*>(state));
#else
    (void)state;
#endif
}

/* ==========================================================================
 * 类型检查
 * ========================================================================== */

int LuauBackend::isnumber(void* state, int idx) {
#ifdef LVM_HAS_LUAU
    return lua_isnumber(static_cast<lua_State*>(state), idx);
#else
    (void)state; (void)idx; return 0;
#endif
}

int LuauBackend::isstring(void* state, int idx) {
#ifdef LVM_HAS_LUAU
    return lua_isstring(static_cast<lua_State*>(state), idx);
#else
    (void)state; (void)idx; return 0;
#endif
}

int LuauBackend::isboolean(void* state, int idx) {
#ifdef LVM_HAS_LUAU
    return lua_isboolean(static_cast<lua_State*>(state), idx);
#else
    (void)state; (void)idx; return 0;
#endif
}

int LuauBackend::isnil(void* state, int idx) {
#ifdef LVM_HAS_LUAU
    return lua_isnil(static_cast<lua_State*>(state), idx);
#else
    (void)state; (void)idx; return 0;
#endif
}

/* ==========================================================================
 * 取值操作
 * ========================================================================== */

double LuauBackend::tonumber(void* state, int idx) {
#ifdef LVM_HAS_LUAU
    auto* L = static_cast<lua_State*>(state);
    int isNum = 0;
    double result = lua_tonumberx(L, idx, &isNum);
    return isNum ? result : 0.0;
#else
    (void)state; (void)idx; return 0.0;
#endif
}

const char* LuauBackend::tostring(void* state, int idx) {
#ifdef LVM_HAS_LUAU
    auto* L = static_cast<lua_State*>(state);
    size_t len = 0;
    return lua_tolstring(L, idx, &len);
#else
    (void)state; (void)idx; return nullptr;
#endif
}

int LuauBackend::toboolean(void* state, int idx) {
#ifdef LVM_HAS_LUAU
    auto* L = static_cast<lua_State*>(state);
    return lua_toboolean(L, idx);
#else
    (void)state; (void)idx; return 0;
#endif
}

/* ==========================================================================
 * 全局变量
 * ========================================================================== */

void LuauBackend::getglobal(void* state, const char* name) {
#ifdef LVM_HAS_LUAU
    auto* L = static_cast<lua_State*>(state);
    lua_getglobal(L, name);
#else
    (void)state; (void)name;
#endif
}

void LuauBackend::setglobal(void* state, const char* name) {
#ifdef LVM_HAS_LUAU
    auto* L = static_cast<lua_State*>(state);
    lua_setglobal(L, name);
#else
    (void)state; (void)name;
#endif
}

/* ==========================================================================
 * 表操作
 * ========================================================================== */

void LuauBackend::newtable(void* state) {
#ifdef LVM_HAS_LUAU
    auto* L = static_cast<lua_State*>(state);
    lua_newtable(L);
#else
    (void)state;
#endif
}

void LuauBackend::getfield(void* state, int idx, const char* k) {
#ifdef LVM_HAS_LUAU
    auto* L = static_cast<lua_State*>(state);
    lua_getfield(L, idx, k);
#else
    (void)state; (void)idx; (void)k;
#endif
}

void LuauBackend::setfield(void* state, int idx, const char* k) {
#ifdef LVM_HAS_LUAU
    auto* L = static_cast<lua_State*>(state);
    lua_setfield(L, idx, k);
#else
    (void)state; (void)idx; (void)k;
#endif
}

/* ==========================================================================
 * 标识
 * ========================================================================== */

int LuauBackend::type() const {
    return static_cast<int>(detail::VMType::LUAU);
}

const char* LuauBackend::name() const {
    return "Luau";
}

} // namespace lvm
