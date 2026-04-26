/**
 * @file    lvm_engine.hpp
 * @brief   Lua VM Wrapper — 引擎核心定义
 *
 * 本文件定义了整个封装层的核心内部数据结构：
 * - VMType: 虚拟机类型枚举
 * - OpaqueState: 不透明状态的完整内部结构（对 C# 完全隐藏）
 * - ErrorBuffer: 线程安全的错误信息存储
 * - 引擎工具函数
 *
 * 设计要点（PIMPL 模式）：
 * OpaqueState 仅在 AIPixelVM 内部使用，外部（C# / C ABI）
 * 仅持有 OpaqueHandle（即 void* / IntPtr），完全不感知内部布局。
 */

#ifndef LVM_ENGINE_HPP
#define LVM_ENGINE_HPP

#include "lvm_backend.h"
#include <memory>
#include <string>
#include <mutex>

namespace lvm {
namespace detail {

/* ==========================================================================
 * 虚拟机类型枚举
 * ========================================================================== */

/**
 * @brief 支持的虚拟机类型
 * @note  值从 1 开始，与 Public API 的 type 参数一一对应
 */
enum class VMType : int {
    LUA_5_5 = 1,   ///< Lua 5.5 (https://github.com/lua/lua.git)
    LUAJIT  = 2,   ///< LuaJIT  (https://github.com/LuaJIT/LuaJIT.git)
    LUAU    = 3    ///< Luau    (https://github.com/luau-lang/luau.git)
};

/* ==========================================================================
 * 线程安全的错误缓冲区
 * ========================================================================== */

/**
 * @brief 每个 opaque 实例的错误信息存储
 *
 * 存储最后一次操作的错误信息，支持线程安全读写。
 * 通过 LVM_GetLastError() 对外暴露（返回 const char*）。
 */
class ErrorBuffer {
public:
    /** @brief 设置错误信息（线程安全） */
    void set(const std::string& msg) {
        std::lock_guard<std::mutex> lock(mutex_);
        buffer_ = msg;
    }

    /** @brief 获取错误信息（线程安全） */
    const char* get() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return buffer_.c_str();
    }

    /** @brief 清空错误信息 */
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        buffer_.clear();
    }

private:
    std::string buffer_;
    mutable std::mutex mutex_;
};

/* ==========================================================================
 * OpaqueState — 不透明核心结构
 * ========================================================================== */

/**
 * @brief 虚拟机实例的完整内部表示
 *
 * 包含三个核心成员：
 * - type:          标识当前使用的虚拟机后端类型
 * - backend:       多态后端（通过虚函数转发到具体的 Lua/LuaJIT/Luau 实现）
 * - native_handle: 原生状态指针（内部为 lua_State* 或等价类型）
 *
 * C# 侧仅持有此结构的裸指针（IntPtr _opaque），
 * 所有操作均通过 Public C ABI 转发至 backend 的虚函数。
 */
struct OpaqueState final {
    VMType                           type;           ///< 虚拟机类型标识
    std::unique_ptr<AbstractBackend> backend;        ///< 多态后端指针
    void*                            native_handle;  ///< 原生 lua_State* 或等价句柄
    ErrorBuffer                      error;          ///< 线程安全错误缓冲区

    OpaqueState() : type(VMType::LUA_5_5), backend(nullptr), native_handle(nullptr) {}
};

/**
 * @brief C ABI 层使用的不透明句柄类型
 * C# 侧对应 IntPtr
 */
using OpaqueHandle = OpaqueState*;

/* ==========================================================================
 * 引擎工具函数（内联实现）
 * ========================================================================== */

/**
 * @brief 将 C ABI 的 void* opaque 解包还原为内部 OpaqueState*
 * @param opaque  外部不透明句柄
 * @return 内部状态指针，若 opaque 为 nullptr 则返回 nullptr
 */
inline OpaqueState* unwrap(void* opaque) {
    return static_cast<OpaqueState*>(opaque);
}

/**
 * @brief 将内部 OpaqueState* 包装为 C ABI 的 void*
 */
inline void* wrap(OpaqueState* op) {
    return static_cast<void*>(op);
}

} // namespace detail
} // namespace lvm

#endif /* LVM_ENGINE_HPP */
