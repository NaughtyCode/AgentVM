/**
 * @file    lvm_backend_luajit.h
 * @brief   LuaJIT 后端具体实现
 *
 * 基于 LuaJIT 官方源码 (https://github.com/LuaJIT/LuaJIT.git) 实现 AbstractBackend 接口。
 *
 * 特点：
 * 1. 与 Lua 5.1 API 高度兼容，可直接使用 lua_State* 和标准 lua_* 系列函数
 * 2. LuaJIT 的 FFI 虽能提供更高性能，但本方案仅使用传统 C API 以保证统一性
 * 3. 使用 luaL_newstate() 创建状态机（与 Lua 5.1 相同）
 * 4. 自动加载扩展库（jit、ffi 等）
 */

#ifndef LVM_BACKEND_LUAJIT_H
#define LVM_BACKEND_LUAJIT_H

#include "lvm_backend.h"

namespace lvm {

/**
 * @class LuaJITBackend
 * @brief LuaJIT 后端实现类
 *
 * 封装 LuaJIT 的 C API 调用。
 * 虽然 LuaJIT 支持独特的 FFI 扩展，但本后端仅使用标准 lua_* C API，
 * 确保与其他后端行为一致。
 */
class LuaJITBackend final : public AbstractBackend {
public:
    LuaJITBackend() = default;
    ~LuaJITBackend() override = default;

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

#endif /* LVM_BACKEND_LUAJIT_H */
