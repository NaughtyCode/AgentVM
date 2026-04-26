/**
 * @file    lvm_backend_lua55.h
 * @brief   Lua 5.5 后端具体实现
 *
 * 基于 Lua 5.5 官方源码 (https://github.com/lua/lua.git) 实现 AbstractBackend 接口。
 *
 * Lua 5.5 关键 API 差异：
 * 1. lua_newstate 需要 unsigned int seed 参数（用于随机数生成器种子）
 * 2. float 精度打印行为变更
 * 3. luaL_newstate() 已废弃，需使用 lua_newstate() 并显式调用 lua_sethostrandomseed()
 *
 * 其余核心栈 API（lua_gettop / lua_settop / lua_pushnumber / lua_pushstring /
 * lua_pcall 等）保持不变。
 */

#ifndef LVM_BACKEND_LUA55_H
#define LVM_BACKEND_LUA55_H

#include "lvm_backend.h"

namespace lvm {

/**
 * @class Lua55Backend
 * @brief Lua 5.5 后端实现类
 *
 * 封装完整的 Lua 5.5 C API 调用。
 * 使用标准内存分配器和固定随机种子以保证行为可重现。
 */
class Lua55Backend final : public AbstractBackend {
public:
    /** @brief Lua 5.5 要求的固定随机种子（确保跨平台一致行为） */
    static constexpr unsigned int LUA55_RANDOM_SEED = 42;

    Lua55Backend() = default;
    ~Lua55Backend() override = default;

    /* ---- 生命周期 ---- */
    void* create_state() override;
    void  destroy_state(void* state) override;

    /* ---- 脚本加载与执行 ---- */
    int load_string(void* state, const char* code) override;
    int load_file(void* state, const char* path) override;
    int pcall(void* state, int narg, int nret, int errfunc) override;

    /* ---- 栈操作 ---- */
    int  gettop(void* state) override;
    void settop(void* state, int idx) override;
    void pushnumber(void* state, double v) override;
    void pushstring(void* state, const char* s) override;
    void pushboolean(void* state, int v) override;
    void pushnil(void* state) override;

    /* ---- 类型检查与取值 ---- */
    int         isnumber(void* state, int idx) override;
    int         isstring(void* state, int idx) override;
    int         isboolean(void* state, int idx) override;
    int         isnil(void* state, int idx) override;
    double      tonumber(void* state, int idx) override;
    const char* tostring(void* state, int idx) override;
    int         toboolean(void* state, int idx) override;

    /* ---- 全局变量 ---- */
    void getglobal(void* state, const char* name) override;
    void setglobal(void* state, const char* name) override;

    /* ---- 表操作 ---- */
    void newtable(void* state) override;
    void getfield(void* state, int idx, const char* k) override;
    void setfield(void* state, int idx, const char* k) override;

    /* ---- 标识 ---- */
    int         type() const override;
    const char* name() const override;
};

} // namespace lvm

#endif /* LVM_BACKEND_LUA55_H */
