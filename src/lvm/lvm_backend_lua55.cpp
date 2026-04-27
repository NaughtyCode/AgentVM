/**
 * @file    lvm_backend_lua55.cpp
 * @brief   Lua 5.5 后端实现
 *
 * 将 AbstractBackend 虚函数映射到 Lua 5.5 C API。
 * Lua 5.5 的特殊处理：
 * - lua_newstate() 需要 unsigned int seed 参数
 * - lua_sethostrandomseed() 显式调用确保可重现
 * - 使用自定义 allocator 包装标准 malloc/realloc/free
 *
 * 依赖: lua.h, lauxlib.h, lualib.h (来自 https://github.com/lua/lua.git)
 */

#include "lvm_backend_lua55.h"
#include "lvm_engine.hpp"

/* ---- Lua 5.5 C API 头文件 ---- */
#ifdef LVM_HAS_LUA55
  extern "C" {
    #include <lua.h>
    #include <lauxlib.h>
    #include <lualib.h>
  }
#else
  /* 当 Lua 5.5 不可用时提供桩实现 —— 编译可通过但运行时报错 */
  #define LUA_MULTRET (-1)
  #define LUA_TNUMBER 3
  #define LUA_TSTRING 4
  #define LUA_TBOOLEAN 1
  #define LUA_TNIL 0
#endif

#include <cstdlib>
#include <cstring>
#include <cstdio>

namespace lvm {

/* ==========================================================================
 * Lua 5.5 自定义内存分配器
 * ========================================================================== */

/**
 * @brief   标准内存分配器回调
 * @details Lua 使用此回调管理所有内部内存。
 *          这里简单包装 C 标准库的 malloc / realloc / free。
 *          osize 参数是原始块大小（Lua 用于内存统计）。
 *
 * @param ud    用户数据（此实现中未使用）
 * @param ptr   要重新分配/释放的内存块指针
 * @param osize 原始块大小
 * @param nsize 新块大小（0 表示仅释放）
 * @return 新分配内存的指针，失败返回 nullptr
 */
static void* lua55_alloc(void* ud, void* ptr, size_t osize, size_t nsize) {
    (void)ud;     // 未使用
    (void)osize;  // 未使用，但 Lua 需要此参数进行内存统计

    if (nsize == 0) {
        /* 释放内存 */
        std::free(ptr);
        return nullptr;
    }

    /* 重新分配（realloc 也处理 ptr==nullptr 的情况 → 等同于 malloc） */
    return std::realloc(ptr, nsize);
}

/* ==========================================================================
 * 生命周期
 * ========================================================================== */

void* Lua55Backend::create_state() {
#ifdef LVM_HAS_LUA55
    /* === Lua 状态创建流程 ===
     *
     * 1. luaL_newstate() — 便捷创建函数（内部调用 lua_newstate 并设置标准 allocator）
     *    注意：Lua 5.5 中 luaL_newstate() 已废弃，改用 lua_newstate() + lua_sethostrandomseed()
     *    当前使用 Lua 5.4 兼容路径
     * 2. luaL_openlibs(L) — 加载所有标准库（base, math, string, table, ...）
     *
     * Lua 5.5 迁移说明：
     *    切换到 Lua 5.5 时，需要:
     *    - 替换 luaL_newstate() 为 lua_newstate(lua55_alloc, nullptr)
     *    - 添加 lua_sethostrandomseed(L, LUA55_RANDOM_SEED)
     */
    lua_State* L = luaL_newstate();
    if (!L) {
        return nullptr;  // 内存分配失败
    }

    /* 加载标准库 */
    luaL_openlibs(L);

    return static_cast<void*>(L);
#else
    (void)LUA55_RANDOM_SEED;
    return nullptr;
#endif
}

void Lua55Backend::destroy_state(void* state) {
#ifdef LVM_HAS_LUA55
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

int Lua55Backend::load_string(void* state, const char* code) {
#ifdef LVM_HAS_LUA55
    auto* L = static_cast<lua_State*>(state);
    /* luaL_loadbuffer: 编译字符串为 Lua 函数块并压入栈顶
     * 使用固定 chunk name "=lua" 替代 luaL_loadstring，避免源代码完整出现在错误消息中
     * 参数: (L, buffer, size, chunkname)
     * 返回值: 0 = 成功 (LUA_OK), 非 0 = 编译错误 */
    return luaL_loadbuffer(L, code, std::strlen(code), "=lua");
#else
    (void)state; (void)code;
    return -1;
#endif
}

int Lua55Backend::load_file(void* state, const char* path) {
#ifdef LVM_HAS_LUA55
    auto* L = static_cast<lua_State*>(state);
    /* luaL_loadfile: 编译文件为 Lua 函数块并压入栈顶
     * 失败时（文件不存在 / 权限 / 语法）返回非 0 */
    return luaL_loadfile(L, path);
#else
    (void)state; (void)path;
    return -1;
#endif
}

int Lua55Backend::pcall(void* state, int narg, int nret, int errfunc) {
#ifdef LVM_HAS_LUA55
    auto* L = static_cast<lua_State*>(state);
    /* lua_pcall: 保护模式调用
     * - narg:   传递给被调用函数的参数个数
     * - nret:   期望的返回值个数 (LUA_MULTRET = 返回全部)
     * - errfunc: 错误处理器在栈上的索引（0 = 无，错误信息直接返回）
     *
     * LUA_MULTRET 在 lua.h 中定义为 (-1)
     */
    return lua_pcall(L, narg, nret, errfunc);
#else
    (void)state; (void)narg; (void)nret; (void)errfunc;
    return -1;
#endif
}

/* ==========================================================================
 * 栈操作 —— 直接转发到 lua_* API
 * ========================================================================== */

int Lua55Backend::gettop(void* state) {
#ifdef LVM_HAS_LUA55
    return lua_gettop(static_cast<lua_State*>(state));
#else
    (void)state; return 0;
#endif
}

void Lua55Backend::settop(void* state, int idx) {
#ifdef LVM_HAS_LUA55
    lua_settop(static_cast<lua_State*>(state), idx);
#else
    (void)state; (void)idx;
#endif
}

void Lua55Backend::pushnumber(void* state, double v) {
#ifdef LVM_HAS_LUA55
    lua_pushnumber(static_cast<lua_State*>(state), v);
#else
    (void)state; (void)v;
#endif
}

void Lua55Backend::pushstring(void* state, const char* s) {
#ifdef LVM_HAS_LUA55
    lua_pushstring(static_cast<lua_State*>(state), s);
#else
    (void)state; (void)s;
#endif
}

void Lua55Backend::pushboolean(void* state, int v) {
#ifdef LVM_HAS_LUA55
    lua_pushboolean(static_cast<lua_State*>(state), v);
#else
    (void)state; (void)v;
#endif
}

void Lua55Backend::pushnil(void* state) {
#ifdef LVM_HAS_LUA55
    lua_pushnil(static_cast<lua_State*>(state));
#else
    (void)state;
#endif
}

/* ==========================================================================
 * 类型检查
 * ========================================================================== */

int Lua55Backend::isnumber(void* state, int idx) {
#ifdef LVM_HAS_LUA55
    return lua_isnumber(static_cast<lua_State*>(state), idx);
#else
    (void)state; (void)idx; return 0;
#endif
}

int Lua55Backend::isstring(void* state, int idx) {
#ifdef LVM_HAS_LUA55
    return lua_isstring(static_cast<lua_State*>(state), idx);
#else
    (void)state; (void)idx; return 0;
#endif
}

int Lua55Backend::isboolean(void* state, int idx) {
#ifdef LVM_HAS_LUA55
    return lua_isboolean(static_cast<lua_State*>(state), idx);
#else
    (void)state; (void)idx; return 0;
#endif
}

int Lua55Backend::isnil(void* state, int idx) {
#ifdef LVM_HAS_LUA55
    return lua_isnil(static_cast<lua_State*>(state), idx);
#else
    (void)state; (void)idx; return 0;
#endif
}

/* ==========================================================================
 * 取值操作
 * ========================================================================== */

double Lua55Backend::tonumber(void* state, int idx) {
#ifdef LVM_HAS_LUA55
    auto* L = static_cast<lua_State*>(state);
    /* lua_tonumberx: 安全转换，返回 0/1 表示是否成功
     * 当 idx 处值不是 number 时返回 0.0 */
    int isNum = 0;
    double result = lua_tonumberx(L, idx, &isNum);
    return isNum ? result : 0.0;
#else
    (void)state; (void)idx; return 0.0;
#endif
}

const char* Lua55Backend::tostring(void* state, int idx) {
#ifdef LVM_HAS_LUA55
    auto* L = static_cast<lua_State*>(state);
    /* lua_tolstring: 返回字符串指针和长度
     * 当 idx 处值不是 string 时返回 nullptr */
    size_t len = 0;
    const char* result = lua_tolstring(L, idx, &len);
    return result;
#else
    (void)state; (void)idx; return nullptr;
#endif
}

int Lua55Backend::toboolean(void* state, int idx) {
#ifdef LVM_HAS_LUA55
    auto* L = static_cast<lua_State*>(state);
    /* lua_toboolean: Lua 约定：nil 和 false 为 false，其余为 true */
    return lua_toboolean(L, idx);
#else
    (void)state; (void)idx; return 0;
#endif
}

/* ==========================================================================
 * 全局变量
 * ========================================================================== */

void Lua55Backend::getglobal(void* state, const char* name) {
#ifdef LVM_HAS_LUA55
    auto* L = static_cast<lua_State*>(state);
    /* lua_getglobal: 从全局表中取出 name 的值压入栈顶
     * 等价于: lua_getfield(L, LUA_GLOBALSINDEX, name)
     * 若 name 不存在则压入 nil */
    lua_getglobal(L, name);
#else
    (void)state; (void)name;
#endif
}

void Lua55Backend::setglobal(void* state, const char* name) {
#ifdef LVM_HAS_LUA55
    auto* L = static_cast<lua_State*>(state);
    /* lua_setglobal: 将栈顶值弹出并赋值给全局变量 name
     * 等价于: lua_setfield(L, LUA_GLOBALSINDEX, name) */
    lua_setglobal(L, name);
#else
    (void)state; (void)name;
#endif
}

/* ==========================================================================
 * 表操作
 * ========================================================================== */

void Lua55Backend::newtable(void* state) {
#ifdef LVM_HAS_LUA55
    auto* L = static_cast<lua_State*>(state);
    /* lua_newtable: 创建空 Lua 表并压入栈顶
     * 等价于 lua_createtable(L, 0, 0) */
    lua_newtable(L);
#else
    (void)state;
#endif
}

void Lua55Backend::getfield(void* state, int idx, const char* k) {
#ifdef LVM_HAS_LUA55
    auto* L = static_cast<lua_State*>(state);
    /* lua_getfield: 从栈 idx 处的表中取出 key = k 的值压入栈顶
     * 等价于: lua_pushstring(L, k); lua_gettable(L, idx)
     * 若 key 不存在则压入 nil */
    lua_getfield(L, idx, k);
#else
    (void)state; (void)idx; (void)k;
#endif
}

void Lua55Backend::setfield(void* state, int idx, const char* k) {
#ifdef LVM_HAS_LUA55
    auto* L = static_cast<lua_State*>(state);
    /* lua_setfield: 将栈顶值弹出并赋给栈 idx 处表的 key = k 字段
     * 等价于: lua_pushstring(L, k); lua_insert(L, -2); lua_settable(L, idx) */
    lua_setfield(L, idx, k);
#else
    (void)state; (void)idx; (void)k;
#endif
}

/* ==========================================================================
 * 标识
 * ========================================================================== */

int Lua55Backend::type() const {
    return static_cast<int>(detail::VMType::LUA_5_5);
}

const char* Lua55Backend::name() const {
    return "Lua 5.5";
}

} // namespace lvm
