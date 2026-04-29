/**
 * @file    lvm_backend.h
 * @brief   Lua VM Wrapper — 抽象后端接口
 *
 * 本文件定义了所有虚拟机后端的抽象基类 AbstractBackend。
 * 每个具体的虚拟机（Lua 5.5 / LuaJIT / Luau）实现此接口，
 * 通过 C++ 虚函数多态实现对调用方的完全透明。
 *
 * 设计原则：
 * 1. 纯虚接口 —— 强制每个后端实现所有基础操作
 * 2. 所有方法以 void* state 为第一参数（内部为 lua_State*）
 * 3. 不依赖任何特定 Lua 版本的头文件
 * 4. 新增后端只需实现此接口即可无缝集成
 */

#ifndef LVM_BACKEND_H
#define LVM_BACKEND_H

namespace lvm {

/**
 * @class AbstractBackend
 * @brief Lua 虚拟机后端抽象接口
 *
 * 封装了 Lua C API 的核心功能子集。
 * 每个抽象方法对应一条基础的 Lua 栈操作或虚拟机管理操作。
 * 具体后端（Lua55Backend / LuaJITBackend / LuauBackend）
 * 负责将调用转发至对应版本的原生 API。
 */
class AbstractBackend {
public:
    virtual ~AbstractBackend() = default;

    /* ----------------------------------------------------------------------
     * 虚拟机生命周期
     * ---------------------------------------------------------------------- */

    /**
     * @brief 创建一个新的原生 Lua 状态机
     * @return 原生状态指针（内部为 lua_State* 或等价类型）
     * @note  创建后自动加载标准库
     */
    virtual void* create_state() = 0;

    /**
     * @brief 销毁原生 Lua 状态机并释放所有资源
     * @param state  由 create_state() 创建的状态指针
     */
    virtual void  destroy_state(void* state) = 0;

    /* ----------------------------------------------------------------------
     * 脚本加载与执行
     * ---------------------------------------------------------------------- */

    /**
     * @brief 加载 Lua 源码字符串（编译但不执行）
     * @param state  原生状态指针
     * @param code   Lua 源码字符串
     * @return 0 = 成功, 非 0 = 编译错误（错误信息在栈顶）
     */
    virtual int   load_string(void* state, const char* code) = 0;

    /**
     * @brief 加载 Lua 脚本文件（编译但不执行）
     * @param state  原生状态指针
     * @param path   脚本文件路径
     * @return 0 = 成功, 非 0 = 编译/文件错误
     */
    virtual int   load_file(void* state, const char* path) = 0;

    /**
     * @brief 以保护模式调用栈上的已编译函数
     * @param state    原生状态指针
     * @param narg     传递给函数的参数个数
     * @param nret     期望的返回值个数
     * @param errfunc  错误处理函数在栈上的索引（0 = 无）
     * @return 0 = 成功, 非 0 = 运行时错误
     */
    virtual int   pcall(void* state, int narg, int nret, int errfunc) = 0;

    /* ----------------------------------------------------------------------
     * 栈操作
     * ---------------------------------------------------------------------- */

    /** @brief 获取栈顶索引 */
    virtual int    gettop(void* state) = 0;

    /** @brief 设置栈顶索引 */
    virtual void   settop(void* state, int idx) = 0;

    /** @brief 将 double 值压入栈顶 */
    virtual void   pushnumber(void* state, double v) = 0;

    /** @brief 将字符串压入栈顶 */
    virtual void   pushstring(void* state, const char* s) = 0;

    /** @brief 将布尔值压入栈顶 */
    virtual void   pushboolean(void* state, int v) = 0;

    /** @brief 将栈 idx 处的值的副本压入栈顶 */
    virtual void   pushvalue(void* state, int idx) = 0;

    /** @brief 将 nil 压入栈顶 */
    virtual void   pushnil(void* state) = 0;

    /* ----------------------------------------------------------------------
     * 类型检查与值提取
     * ---------------------------------------------------------------------- */

    virtual int    isnumber(void* state, int idx) = 0;
    virtual int    isstring(void* state, int idx) = 0;
    virtual int    isboolean(void* state, int idx) = 0;
    virtual int    isnil(void* state, int idx) = 0;

    /** @brief 检查栈上 idx 位置的值是否为函数 */
    virtual int    isfunction(void* state, int idx) = 0;

    /** @brief 将栈上 idx 位置的值转为 double */
    virtual double tonumber(void* state, int idx) = 0;

    /** @brief 将栈上 idx 位置的值转为字符串 */
    virtual const char* tostring(void* state, int idx) = 0;

    /** @brief 将栈上 idx 位置的值转为布尔值 */
    virtual int    toboolean(void* state, int idx) = 0;

    /* ----------------------------------------------------------------------
     * 全局变量
     * ---------------------------------------------------------------------- */

    /** @brief 将全局变量 name 压入栈顶 */
    virtual void   getglobal(void* state, const char* name) = 0;

    /** @brief 将栈顶值弹出并赋值给全局变量 name */
    virtual void   setglobal(void* state, const char* name) = 0;

    /* ----------------------------------------------------------------------
     * 表操作
     * ---------------------------------------------------------------------- */

    /** @brief 创建新表并压入栈顶 */
    virtual void   newtable(void* state) = 0;

    /** @brief 从栈 idx 处的表中取出 key 字段压入栈顶 */
    virtual void   getfield(void* state, int idx, const char* k) = 0;

    /** @brief 将栈顶值赋给栈 idx 处表的 key 字段 */
    virtual void   setfield(void* state, int idx, const char* k) = 0;

    /* ----------------------------------------------------------------------
     * 后端标识
     * ---------------------------------------------------------------------- */

    /**
     * @brief 获取后端类型标识
     * @return 1 = Lua 5.5, 2 = LuaJIT, 3 = Luau
     */
    virtual int    type() const = 0;

    /**
     * @brief 获取后端名称（用于诊断和日志）
     * @return 后端名称的 C 字符串
     */
    virtual const char* name() const = 0;
};

} // namespace lvm

#endif /* LVM_BACKEND_H */
