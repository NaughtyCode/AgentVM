/**
 * @file    lvm_backend_luajit.cpp
 * @brief   LuaJIT 后端实现
 *
 * 将 AbstractBackend 虚函数映射到 LuaJIT C API。
 * LuaJIT 与 Lua 5.1 API 高度兼容，大部分调用与标准 Lua 一致。
 *
 * 特点：
 * - 使用 luaL_newstate() 创建（兼容 Lua 5.1 的便捷函数）
 * - LuaJIT 自动包含 jit / ffi 等扩展库
 * - 支持 JIT 编译优化（透明，无需额外配置）
 * - 与 Lua 5.1 二进制兼容的 lua_State*
 *
 * 依赖: lua.h, lauxlib.h, lualib.h (来自 https://github.com/LuaJIT/LuaJIT.git)
 */

#include "lvm_backend_luajit.h"
#include "lvm_engine.hpp"

/* ---- LuaJIT C API 头文件 ---- */
#ifdef LVM_HAS_LUAJIT
  extern "C" {
    #include <lua.h>
    #include <lauxlib.h>
    #include <lualib.h>
  }
#else
  #define LUA_MULTRET (-1)
#endif

#include <cstring>
#include <cstdlib>

namespace lvm {

/* ==========================================================================
 * 生命周期
 * ========================================================================== */

void* LuaJITBackend::create_state() {
#ifdef LVM_HAS_LUAJIT
    /* === LuaJIT 创建流程 ===
     *
     * 1. luaL_newstate() — 创建 lua_State（内部自动调用 lua_newstate 并设置标准 allocator）
     * 2. luaL_openlibs(L) — 加载标准库 + jit 库 + ffi 库
     *
     * 注意：LuaJIT 的 luaL_newstate() 与 Lua 5.1 行为完全一致，
     * 不需要 seed 参数（区别于 Lua 5.5）。
     */
    lua_State* L = luaL_newstate();
    if (!L) {
        return nullptr;  // 内存分配失败
    }

    /* 加载库：LuaJIT 除了标准库还会加载 jit.* 和 ffi.* */
    luaL_openlibs(L);

    return static_cast<void*>(L);
#else
    return nullptr;
#endif
}

void LuaJITBackend::destroy_state(void* state) {
#ifdef LVM_HAS_LUAJIT
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

int LuaJITBackend::load_string(void* state, const char* code) {
#ifdef LVM_HAS_LUAJIT
    auto* L = static_cast<lua_State*>(state);
    /* luaL_loadbuffer: 使用固定 chunk name "=lua" 替代 luaL_loadstring
     * 避免源代码完整出现在错误消息中（安全性与可读性） */
    return luaL_loadbuffer(L, code, std::strlen(code), "=lua");
#else
    (void)state; (void)code;
    return -1;
#endif
}

int LuaJITBackend::load_file(void* state, const char* path) {
#ifdef LVM_HAS_LUAJIT
    auto* L = static_cast<lua_State*>(state);
    return luaL_loadfile(L, path);
#else
    (void)state; (void)path;
    return -1;
#endif
}

int LuaJITBackend::pcall(void* state, int narg, int nret, int errfunc) {
#ifdef LVM_HAS_LUAJIT
    auto* L = static_cast<lua_State*>(state);
    /* LuaJIT 的 pcall 支持 JIT 编译的 trace 内调用，性能优于解释执行 */
    return lua_pcall(L, narg, nret, errfunc);
#else
    (void)state; (void)narg; (void)nret; (void)errfunc;
    return -1;
#endif
}

/* ==========================================================================
 * 栈操作
 * ========================================================================== */

int LuaJITBackend::gettop(void* state) {
#ifdef LVM_HAS_LUAJIT
    return lua_gettop(static_cast<lua_State*>(state));
#else
    (void)state; return 0;
#endif
}

void LuaJITBackend::settop(void* state, int idx) {
#ifdef LVM_HAS_LUAJIT
    lua_settop(static_cast<lua_State*>(state), idx);
#else
    (void)state; (void)idx;
#endif
}

void LuaJITBackend::pushnumber(void* state, double v) {
#ifdef LVM_HAS_LUAJIT
    lua_pushnumber(static_cast<lua_State*>(state), v);
#else
    (void)state; (void)v;
#endif
}

void LuaJITBackend::pushstring(void* state, const char* s) {
#ifdef LVM_HAS_LUAJIT
    lua_pushstring(static_cast<lua_State*>(state), s);
#else
    (void)state; (void)s;
#endif
}

void LuaJITBackend::pushboolean(void* state, int v) {
#ifdef LVM_HAS_LUAJIT
    lua_pushboolean(static_cast<lua_State*>(state), v);
#else
    (void)state; (void)v;
#endif
}

void LuaJITBackend::pushnil(void* state) {
#ifdef LVM_HAS_LUAJIT
    lua_pushnil(static_cast<lua_State*>(state));
#else
    (void)state;
#endif
}

/* ==========================================================================
 * 类型检查
 * ========================================================================== */

int LuaJITBackend::isnumber(void* state, int idx) {
#ifdef LVM_HAS_LUAJIT
    return lua_isnumber(static_cast<lua_State*>(state), idx);
#else
    (void)state; (void)idx; return 0;
#endif
}

int LuaJITBackend::isstring(void* state, int idx) {
#ifdef LVM_HAS_LUAJIT
    return lua_isstring(static_cast<lua_State*>(state), idx);
#else
    (void)state; (void)idx; return 0;
#endif
}

int LuaJITBackend::isboolean(void* state, int idx) {
#ifdef LVM_HAS_LUAJIT
    return lua_isboolean(static_cast<lua_State*>(state), idx);
#else
    (void)state; (void)idx; return 0;
#endif
}

int LuaJITBackend::isnil(void* state, int idx) {
#ifdef LVM_HAS_LUAJIT
    return lua_isnil(static_cast<lua_State*>(state), idx);
#else
    (void)state; (void)idx; return 0;
#endif
}

/* ==========================================================================
 * 取值操作
 * ========================================================================== */

double LuaJITBackend::tonumber(void* state, int idx) {
#ifdef LVM_HAS_LUAJIT
    auto* L = static_cast<lua_State*>(state);
    int isNum = 0;
    double result = lua_tonumberx(L, idx, &isNum);
    return isNum ? result : 0.0;
#else
    (void)state; (void)idx; return 0.0;
#endif
}

const char* LuaJITBackend::tostring(void* state, int idx) {
#ifdef LVM_HAS_LUAJIT
    auto* L = static_cast<lua_State*>(state);
    size_t len = 0;
    return lua_tolstring(L, idx, &len);
#else
    (void)state; (void)idx; return nullptr;
#endif
}

int LuaJITBackend::toboolean(void* state, int idx) {
#ifdef LVM_HAS_LUAJIT
    auto* L = static_cast<lua_State*>(state);
    return lua_toboolean(L, idx);
#else
    (void)state; (void)idx; return 0;
#endif
}

/* ==========================================================================
 * 全局变量
 * ========================================================================== */

void LuaJITBackend::getglobal(void* state, const char* name) {
#ifdef LVM_HAS_LUAJIT
    auto* L = static_cast<lua_State*>(state);
    lua_getglobal(L, name);
#else
    (void)state; (void)name;
#endif
}

void LuaJITBackend::setglobal(void* state, const char* name) {
#ifdef LVM_HAS_LUAJIT
    auto* L = static_cast<lua_State*>(state);
    lua_setglobal(L, name);
#else
    (void)state; (void)name;
#endif
}

/* ==========================================================================
 * 表操作
 * ========================================================================== */

void LuaJITBackend::newtable(void* state) {
#ifdef LVM_HAS_LUAJIT
    auto* L = static_cast<lua_State*>(state);
    lua_newtable(L);
#else
    (void)state;
#endif
}

void LuaJITBackend::getfield(void* state, int idx, const char* k) {
#ifdef LVM_HAS_LUAJIT
    auto* L = static_cast<lua_State*>(state);
    lua_getfield(L, idx, k);
#else
    (void)state; (void)idx; (void)k;
#endif
}

void LuaJITBackend::setfield(void* state, int idx, const char* k) {
#ifdef LVM_HAS_LUAJIT
    auto* L = static_cast<lua_State*>(state);
    lua_setfield(L, idx, k);
#else
    (void)state; (void)idx; (void)k;
#endif
}

/* ==========================================================================
 * 标识
 * ========================================================================== */

int LuaJITBackend::type() const {
    return static_cast<int>(detail::VMType::LUAJIT);
}

const char* LuaJITBackend::name() const {
    return "LuaJIT 2.1";
}

} // namespace lvm
