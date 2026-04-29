/**
 * @file    lvm_backend_luau.h
 * @brief   Luau 后端具体实现
 *
 * 基于 Luau 官方源码 (https://github.com/luau-lang/luau.git) 实现 AbstractBackend 接口。
 *
 * 关键适配（Luau 虽然设计上"mostly preserves Lua 5.1 API"，但实际有显著变化）：
 *
 * 1. 创建/销毁：
 *    - 使用 lua_newstate() 和 lua_close()，参数与标准 Lua 一致
 *
 * 2. 脚本加载：
 *    - Luau 支持字节码编译，提供 luau_compile() 函数
 *    - 执行字节码使用 luau_load() 而非标准 luaL_loadstring()
 *    - 也可直接用 luaL_loadstring()（兼容模式），本实现优先使用字节码编译路径
 *
 * 3. 错误处理：
 *    - lua_pcall 在 Luau 中依然可用
 *    - Luau 提供额外的 luau_execute 用于字节码执行
 *
 * 4. 沙箱（Sandboxing）：
 *    - Luau 默认启用沙箱，限制文件系统访问
 *    - 可通过 luaL_sandbox() 控制沙箱行为
 *    - 本实现保留沙箱默认行为，由上层通过配置开关控制
 *
 * 5. 类型注解：
 *    - Luau 支持渐进式类型系统（type / export type）
 *    - 但核心 C API 不受影响，仍使用标准栈操作
 */

#ifndef LVM_BACKEND_LUAU_H
#define LVM_BACKEND_LUAU_H

#include "lvm_backend.h"

namespace lvm {

/**
 * @class LuauBackend
 * @brief Luau 后端实现类
 *
 * 封装 Luau 的 C API 调用。
 * Luau 提供了沙箱安全、字节码编译和渐进式类型等特性，
 * 本后端在 load_string 中使用 luau_compile → luau_load 的字节码路径以利用其优化。
 */
class LuauBackend final : public AbstractBackend {
public:
    LuauBackend() = default;
    ~LuauBackend() override = default;

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
    void pushvalue(void* state, int idx) override;
    void pushnil(void* state) override;

    /* ---- 类型检查与取值 ---- */
    int         isnumber(void* state, int idx) override;
    int         isstring(void* state, int idx) override;
    int         isboolean(void* state, int idx) override;
    int         isnil(void* state, int idx) override;
    int         isfunction(void* state, int idx) override;
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

#endif /* LVM_BACKEND_LUAU_H */
