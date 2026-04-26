/**
 * @file    LuaVM.cs
 * @brief   Lua VM Wrapper — C# 集成层
 *
 * 通过 P/Invoke 调用 AIPixelVM (C ABI) 实现 Lua 虚拟机集成。
 * 所有调用通过 IntPtr _opaque 句柄，内部实现完全隐藏。
 *
 * 使用示例:
 *   using var vm = new LuaVM(LuaVMType.Lua55);
 *   vm.Execute("print('Hello!')");
 *   vm.PushNumber(42);
 *   vm.SetGlobal("x");
 */

using System;
using System.Runtime.InteropServices;

namespace LuaVM
{
    /// <summary>
    /// 虚拟机类型枚举，与 C++ 侧 VMType 对齐
    /// </summary>
    public enum LuaVMType
    {
        /// <summary>Lua 5.5 (https://github.com/lua/lua.git)</summary>
        Lua55 = 1,

        /// <summary>LuaJIT 2.1 (https://github.com/LuaJIT/LuaJIT.git)</summary>
        LuaJIT = 2,

        /// <summary>Luau (https://github.com/luau-lang/luau.git)</summary>
        Luau = 3
    }

    /// <summary>
    /// Lua 虚拟机封装类
    ///
    /// 设计要点:
    /// - 实现 IDisposable 以自动管理非托管内存
    /// - 所有方法通过 P/Invoke 调用 AIPixelVM，第一个参数为 IntPtr _opaque
    /// - 不暴露任何内部状态类型，C# 侧完全透明
    /// - 线程安全：同一个实例不应被多线程并发使用（不同实例之间安全）
    /// </summary>
    public sealed class LuaVM : IDisposable
    {
        /* ====================================================================
         * 字段
         * ==================================================================== */

        private IntPtr _opaque;      // 不透明句柄（C++ 侧 OpaqueState*）
        private bool   _disposed;    // 释放标记

        /* ====================================================================
         * P/Invoke 声明 —— 全部使用 Cdecl 调用约定，参数仅为 blittable 类型
         * ==================================================================== */

        private const string NativeLib = "AIPixelVM";

        // ---- 生命周期 ----
        [DllImport(NativeLib, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr LVM_Create(int type);

        [DllImport(NativeLib, CallingConvention = CallingConvention.Cdecl)]
        private static extern void LVM_Destroy(IntPtr opaque);

        [DllImport(NativeLib, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr LVM_GetLastError(IntPtr opaque);

        // ---- 脚本执行 ----
        [DllImport(NativeLib, CallingConvention = CallingConvention.Cdecl)]
        private static extern int LVM_ExecuteString(IntPtr opaque, string code);

        [DllImport(NativeLib, CallingConvention = CallingConvention.Cdecl)]
        private static extern int LVM_ExecuteFile(IntPtr opaque, string filepath);

        // ---- 栈操作 ----
        [DllImport(NativeLib, CallingConvention = CallingConvention.Cdecl)]
        private static extern int LVM_GetTop(IntPtr opaque);

        [DllImport(NativeLib, CallingConvention = CallingConvention.Cdecl)]
        private static extern void LVM_SetTop(IntPtr opaque, int index);

        // ---- 压栈 ----
        [DllImport(NativeLib, CallingConvention = CallingConvention.Cdecl)]
        private static extern void LVM_PushNumber(IntPtr opaque, double value);

        [DllImport(NativeLib, CallingConvention = CallingConvention.Cdecl)]
        private static extern void LVM_PushString(IntPtr opaque, string str);

        [DllImport(NativeLib, CallingConvention = CallingConvention.Cdecl)]
        private static extern void LVM_PushBoolean(IntPtr opaque, int value);

        [DllImport(NativeLib, CallingConvention = CallingConvention.Cdecl)]
        private static extern void LVM_PushNil(IntPtr opaque);

        // ---- 类型检查 ----
        [DllImport(NativeLib, CallingConvention = CallingConvention.Cdecl)]
        private static extern int LVM_IsNumber(IntPtr opaque, int index);

        [DllImport(NativeLib, CallingConvention = CallingConvention.Cdecl)]
        private static extern int LVM_IsString(IntPtr opaque, int index);

        [DllImport(NativeLib, CallingConvention = CallingConvention.Cdecl)]
        private static extern int LVM_IsBoolean(IntPtr opaque, int index);

        [DllImport(NativeLib, CallingConvention = CallingConvention.Cdecl)]
        private static extern int LVM_IsNil(IntPtr opaque, int index);

        // ---- 取值 ----
        [DllImport(NativeLib, CallingConvention = CallingConvention.Cdecl)]
        private static extern double LVM_ToNumber(IntPtr opaque, int index);

        [DllImport(NativeLib, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr LVM_ToString(IntPtr opaque, int index);

        [DllImport(NativeLib, CallingConvention = CallingConvention.Cdecl)]
        private static extern int LVM_ToBoolean(IntPtr opaque, int index);

        // ---- 全局变量 ----
        [DllImport(NativeLib, CallingConvention = CallingConvention.Cdecl)]
        private static extern void LVM_GetGlobal(IntPtr opaque, string name);

        [DllImport(NativeLib, CallingConvention = CallingConvention.Cdecl)]
        private static extern void LVM_SetGlobal(IntPtr opaque, string name);

        // ---- 表操作 ----
        [DllImport(NativeLib, CallingConvention = CallingConvention.Cdecl)]
        private static extern void LVM_NewTable(IntPtr opaque);

        [DllImport(NativeLib, CallingConvention = CallingConvention.Cdecl)]
        private static extern void LVM_GetField(IntPtr opaque, int index, string key);

        [DllImport(NativeLib, CallingConvention = CallingConvention.Cdecl)]
        private static extern void LVM_SetField(IntPtr opaque, int index, string key);

        /* ====================================================================
         * 构造函数与析构
         * ==================================================================== */

        /// <summary>
        /// 创建指定类型的 Lua 虚拟机实例
        /// </summary>
        /// <param name="type">虚拟机类型 (Lua55 / LuaJIT / Luau)</param>
        /// <exception cref="InvalidOperationException">创建失败时抛出</exception>
        public LuaVM(LuaVMType type)
        {
            _opaque = LVM_Create((int)type);
            if (_opaque == IntPtr.Zero)
            {
                throw new InvalidOperationException(
                    $"Failed to create Lua VM. Type: {type}. " +
                    "Ensure AIPixelVM is available and the backend is compiled.");
            }
        }

        /// <summary>
        /// 释放虚拟机资源
        /// </summary>
        public void Dispose()
        {
            if (!_disposed && _opaque != IntPtr.Zero)
            {
                LVM_Destroy(_opaque);
                _opaque = IntPtr.Zero;
                _disposed = true;
            }
        }

        /// <summary>
        /// 确认未释放，否则抛出
        /// </summary>
        private void EnsureNotDisposed()
        {
            if (_disposed)
                throw new ObjectDisposedException(nameof(LuaVM));
        }

        /* ====================================================================
         * 公共方法 —— 脚本执行
         * ==================================================================== */

        /// <summary>
        /// 执行 Lua 源码字符串
        /// </summary>
        /// <param name="code">Lua 源码</param>
        /// <returns>0 = 成功, 非 0 = 失败</returns>
        public int Execute(string code)
        {
            EnsureNotDisposed();
            return LVM_ExecuteString(_opaque, code ?? string.Empty);
        }

        /// <summary>
        /// 执行 Lua 脚本文件
        /// </summary>
        /// <param name="filepath">脚本文件路径</param>
        /// <returns>0 = 成功, 非 0 = 失败</returns>
        public int ExecuteFile(string filepath)
        {
            EnsureNotDisposed();
            return LVM_ExecuteFile(_opaque, filepath);
        }

        /// <summary>
        /// 获取最后一次错误信息
        /// </summary>
        public string GetLastError()
        {
            EnsureNotDisposed();
            IntPtr ptr = LVM_GetLastError(_opaque);
            return Marshal.PtrToStringAnsi(ptr) ?? string.Empty;
        }

        /* ====================================================================
         * 公共方法 —— 栈操作
         * ==================================================================== */

        /// <summary>获取当前栈顶索引（栈中元素个数）</summary>
        public int GetTop()
        {
            EnsureNotDisposed();
            return LVM_GetTop(_opaque);
        }

        /// <summary>设置栈顶索引（截断或扩展栈）</summary>
        public void SetTop(int index)
        {
            EnsureNotDisposed();
            LVM_SetTop(_opaque, index);
        }

        /// <summary>清空整个栈</summary>
        public void ClearStack() => SetTop(0);

        /* ====================================================================
         * 公共方法 —— 压栈
         * ==================================================================== */

        /// <summary>将数值压入栈顶</summary>
        public void PushNumber(double value)
        {
            EnsureNotDisposed();
            LVM_PushNumber(_opaque, value);
        }

        /// <summary>将字符串压入栈顶</summary>
        public void PushString(string str)
        {
            EnsureNotDisposed();
            LVM_PushString(_opaque, str ?? string.Empty);
        }

        /// <summary>将布尔值压入栈顶</summary>
        public void PushBoolean(bool value)
        {
            EnsureNotDisposed();
            LVM_PushBoolean(_opaque, value ? 1 : 0);
        }

        /// <summary>将 nil 压入栈顶</summary>
        public void PushNil()
        {
            EnsureNotDisposed();
            LVM_PushNil(_opaque);
        }

        /* ====================================================================
         * 公共方法 —— 取值
         * ==================================================================== */

        /// <summary>取栈上 index 处的数值</summary>
        public double GetNumber(int index)
        {
            EnsureNotDisposed();
            return LVM_ToNumber(_opaque, index);
        }

        /// <summary>取栈上 index 处的字符串</summary>
        public string GetString(int index)
        {
            EnsureNotDisposed();
            IntPtr ptr = LVM_ToString(_opaque, index);
            return Marshal.PtrToStringAnsi(ptr) ?? string.Empty;
        }

        /// <summary>取栈上 index 处的布尔值</summary>
        public bool GetBoolean(int index)
        {
            EnsureNotDisposed();
            return LVM_ToBoolean(_opaque, index) != 0;
        }

        /// <summary>检查栈上 index 处是否为数值</summary>
        public bool IsNumber(int index) => LVM_IsNumber(_opaque, index) != 0;

        /// <summary>检查栈上 index 处是否为字符串</summary>
        public bool IsString(int index) => LVM_IsString(_opaque, index) != 0;

        /// <summary>检查栈上 index 处是否为布尔值</summary>
        public bool IsBoolean(int index) => LVM_IsBoolean(_opaque, index) != 0;

        /// <summary>检查栈上 index 处是否为 nil</summary>
        public bool IsNil(int index) => LVM_IsNil(_opaque, index) != 0;

        /* ====================================================================
         * 公共方法 —— 全局变量
         * ==================================================================== */

        /// <summary>获取全局变量的值并压入栈顶</summary>
        public void GetGlobal(string name)
        {
            EnsureNotDisposed();
            LVM_GetGlobal(_opaque, name);
        }

        /// <summary>从栈顶弹出值并赋给全局变量</summary>
        public void SetGlobal(string name)
        {
            EnsureNotDisposed();
            LVM_SetGlobal(_opaque, name);
        }

        /* ====================================================================
         * 公共方法 —— 表操作
         * ==================================================================== */

        /// <summary>创建新的空表并压入栈顶</summary>
        public void NewTable()
        {
            EnsureNotDisposed();
            LVM_NewTable(_opaque);
        }

        /// <summary>从栈 index 处表中获取 key 字段压入栈顶</summary>
        public void GetField(int index, string key)
        {
            EnsureNotDisposed();
            LVM_GetField(_opaque, index, key);
        }

        /// <summary>将栈顶值弹出并赋给栈 index 处表的 key 字段</summary>
        public void SetField(int index, string key)
        {
            EnsureNotDisposed();
            LVM_SetField(_opaque, index, key);
        }

        /* ====================================================================
         * 便捷方法 —— 高层 API
         * ==================================================================== */

        /// <summary>
        /// 执行 Lua 代码并返回一个 double 值
        /// (要求 Lua 代码执行后在栈顶留下一个数值)
        /// </summary>
        public double ExecuteAndGetNumber(string code)
        {
            int result = Execute(code);
            if (result != 0)
                throw new InvalidOperationException(
                    $"Lua execution failed: {GetLastError()}");

            double val = GetNumber(-1);  // -1 = 栈顶
            ClearStack();
            return val;
        }

        /// <summary>
        /// 从 Lua 获取一个全局数值变量
        /// </summary>
        public double GetGlobalNumber(string name)
        {
            GetGlobal(name);
            if (!IsNumber(-1))
            {
                ClearStack();
                throw new InvalidOperationException(
                    $"Global '{name}' is not a number");
            }
            double val = GetNumber(-1);
            ClearStack();
            return val;
        }

        /// <summary>
        /// 设置一个全局数值变量
        /// </summary>
        public void SetGlobalNumber(string name, double value)
        {
            PushNumber(value);
            SetGlobal(name);
        }

        /// <summary>
        /// 从 Lua 获取一个全局字符串变量
        /// </summary>
        public string GetGlobalString(string name)
        {
            GetGlobal(name);
            if (!IsString(-1))
            {
                ClearStack();
                throw new InvalidOperationException(
                    $"Global '{name}' is not a string");
            }
            string val = GetString(-1);
            ClearStack();
            return val;
        }

        /// <summary>
        /// 设置一个全局字符串变量
        /// </summary>
        public void SetGlobalString(string name, string value)
        {
            PushString(value);
            SetGlobal(name);
        }
    }
}
