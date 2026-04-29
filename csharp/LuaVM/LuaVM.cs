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
using System.Collections.Generic;
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
    /// 外部函数回调委托类型 —— 与 C ABI 的 LVM_ExternalFunc 对齐
    /// 回调中通过 LuaVM 实例的 Public API 读取参数与压入返回值
    /// </summary>
    /// <param name="opaque">虚拟机不透明句柄</param>
    /// <returns>压入栈中的返回值数量</returns>
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate int ExternalFuncDelegate(IntPtr opaque);

    /// <summary>
    /// Lua 虚拟机封装类
    ///
    /// 设计要点:
    /// - 继承 AIPixelVM 基类，继承 AOT 兼容的原生库加载与 IDisposable 模式
    /// - 所有方法通过 P/Invoke 调用 AIPixelVM，第一个参数为 IntPtr _opaque
    /// - 不暴露任何内部状态类型，C# 侧完全透明
    /// - 线程安全：同一个实例不应被多线程并发使用（不同实例之间安全）
    ///
    /// AOT 说明:
    ///   基类 AIPixelVM 在静态构造函数中通过 NativeLibrary.SetDllImportResolver
    ///   注册了 AOT 兼容的库解析器，因此本类中的 DllImport 声明在 Native AOT
    ///   发布后仍能正确解析 AIPixelVM 原生库。
    /// </summary>
    public sealed class LuaVM : AIPixelVM
    {
        /* ====================================================================
         * 字段
         * ==================================================================== */

        private IntPtr _opaque;      // 不透明句柄（C++ 侧 OpaqueState*）
        /// <summary>保持已注册回调的 GCHandle，防止委托被 GC 回收</summary>
        private List<GCHandle> _registeredCallbacks;

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

        // ---- 批量脚本加载 ----
        [DllImport(NativeLib, CallingConvention = CallingConvention.Cdecl)]
        private static extern int LVM_LoadScriptFiles(IntPtr opaque, string dirpath, string suffix);

        [DllImport(NativeLib, CallingConvention = CallingConvention.Cdecl)]
        private static extern int LVM_LoadScriptFilesEx(IntPtr opaque, string dirpath,
            string suffix, IntPtr[] blacklist, int blacklist_len);

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
        private static extern void LVM_PushValue(IntPtr opaque, int index);

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

        [DllImport(NativeLib, CallingConvention = CallingConvention.Cdecl)]
        private static extern int LVM_IsFunction(IntPtr opaque, int index);

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

        // ---- 函数调用 ----
        [DllImport(NativeLib, CallingConvention = CallingConvention.Cdecl)]
        private static extern int LVM_PCall(IntPtr opaque, int nargs, int nresults);

        // ---- 外部函数注册 ----
        [DllImport(NativeLib, CallingConvention = CallingConvention.Cdecl)]
        private static extern int LVM_RegisterFunction(IntPtr opaque, string name, IntPtr func);

        [DllImport(NativeLib, CallingConvention = CallingConvention.Cdecl)]
        private static extern int LVM_RegisterModule(IntPtr opaque, string moduleName,
            [MarshalAs(UnmanagedType.LPArray, ArraySubType = UnmanagedType.LPStr)] string[] funcNames,
            IntPtr funcPtrs, int count);

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
            _registeredCallbacks = new List<GCHandle>();
            if (_opaque == IntPtr.Zero)
            {
                throw new InvalidOperationException(
                    $"Failed to create Lua VM. Type: {type}. " +
                    "Ensure AIPixelVM is available and the backend is compiled.");
            }
        }

        /// <summary>
        /// 释放虚拟机资源（重写基类 IDisposable 模式）
        ///
        /// 清理顺序:
        ///   1. 释放托管资源: 已注册回调的 GCHandle
        ///   2. 释放非托管资源: 销毁 C++ 侧虚拟机实例
        ///   3. 调用基类 Dispose(bool) 完成释放标记
        /// </summary>
        /// <param name="disposing">
        /// true  = 由 Dispose() 或 using 触发
        /// false = 由终结器触发（不应从此路径调用，因为基类已有终结器）
        /// </param>
        protected override void Dispose(bool disposing)
        {
            if (!_disposed)
            {
                if (disposing)
                {
                    // 释放托管资源: 已注册回调的 GCHandle，允许委托被 GC 回收
                    if (_registeredCallbacks != null)
                    {
                        foreach (var handle in _registeredCallbacks)
                        {
                            if (handle.IsAllocated)
                                handle.Free();
                        }
                        _registeredCallbacks.Clear();
                    }
                }

                // 释放非托管资源: 销毁 C++ 侧 Lua 虚拟机实例
                if (_opaque != IntPtr.Zero)
                {
                    LVM_Destroy(_opaque);
                    _opaque = IntPtr.Zero;
                }

                // 调用基类完成 _disposed 标记
                base.Dispose(disposing);
            }
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
        /// 从指定目录加载所有匹配后缀的 Lua 脚本文件
        /// </summary>
        /// <param name="directory">目标目录路径</param>
        /// <param name="suffix">文件后缀（如 ".lua"），传 null 默认为 ".lua"</param>
        /// <returns>成功加载并执行的文件数量</returns>
        public int LoadScriptFiles(string directory, string? suffix = null)
        {
            EnsureNotDisposed();
            return LVM_LoadScriptFiles(_opaque, directory, suffix ?? ".lua");
        }

        /// <summary>
        /// 从指定目录加载匹配后缀的脚本文件（支持黑名单过滤）
        /// </summary>
        /// <param name="directory">目标目录路径</param>
        /// <param name="suffix">文件后缀（如 ".lua"），传 null 默认为 ".lua"</param>
        /// <param name="blacklist">需要排除的文件名列表（不含路径，含后缀）</param>
        /// <returns>成功加载并执行的文件数量</returns>
        public int LoadScriptFiles(string directory, string? suffix, string[]? blacklist)
        {
            EnsureNotDisposed();

            if (blacklist == null || blacklist.Length == 0)
            {
                // 无黑名单，走快速路径
                return LVM_LoadScriptFiles(_opaque, directory, suffix ?? ".lua");
            }

            // 将 C# string[] 转换为 IntPtr[]（非托管 C 字符串数组）
            IntPtr[] blacklistPtrs = new IntPtr[blacklist.Length];
            try
            {
                for (int i = 0; i < blacklist.Length; i++)
                {
                    blacklistPtrs[i] = Marshal.StringToHGlobalAnsi(blacklist[i]);
                }

                return LVM_LoadScriptFilesEx(_opaque, directory,
                    suffix ?? ".lua", blacklistPtrs, blacklist.Length);
            }
            finally
            {
                // 释放非托管内存
                for (int i = 0; i < blacklistPtrs.Length; i++)
                {
                    if (blacklistPtrs[i] != IntPtr.Zero)
                        Marshal.FreeHGlobal(blacklistPtrs[i]);
                }
            }
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

        /// <summary>
        /// 将栈 index 处元素的副本压入栈顶（值复制，非引用）
        /// 常用于在 PCall 前保留函数引用的副本，调用后原引用仍可继续使用
        /// </summary>
        /// <param name="index">源元素的栈索引（支持正数和负数索引）</param>
        public void PushValue(int index)
        {
            EnsureNotDisposed();
            LVM_PushValue(_opaque, index);
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

        /// <summary>
        /// 检查栈上 index 处是否为函数
        /// 适用于验证通过 GetGlobal 或 GetField 获取的值是否为可调用函数，
        /// 在 PCall 之前进行类型安全检查
        /// </summary>
        public bool IsFunction(int index)
        {
            EnsureNotDisposed();
            return LVM_IsFunction(_opaque, index) != 0;
        }

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
         * 公共方法 —— 函数调用
         * ==================================================================== */

        /// <summary>
        /// 以保护模式调用栈上的 Lua 函数
        /// 调用前栈布局: [..., func, arg1, ..., argN]
        /// 调用后栈布局: [..., result1, ..., resultM]
        /// </summary>
        /// <param name="nargs">传递给函数的参数个数</param>
        /// <param name="nresults">期望的返回值个数（-1 表示返回所有值）</param>
        /// <returns>0 = 成功，非 0 = 运行时错误（通过 GetLastError 获取详情）</returns>
        /// <exception cref="ObjectDisposedException">实例已释放时抛出</exception>
        public int PCall(int nargs, int nresults)
        {
            EnsureNotDisposed();
            return LVM_PCall(_opaque, nargs, nresults);
        }

        /// <summary>
        /// 调用 Lua 全局函数（无参数，无返回值）
        /// 这是最简单的全局函数调用形式
        /// </summary>
        /// <param name="name">全局函数名</param>
        /// <example>
        /// vm.Execute("function greet() print('hello') end");
        /// vm.CallGlobal("greet");  // 调用 greet()
        /// </example>
        public void CallGlobal(string name)
        {
            EnsureNotDisposed();
            int oldTop = GetTop();   // 保存当前栈顶，避免破坏调用者的栈数据
            GetGlobal(name);         // 压入函数
            int ret = LVM_PCall(_opaque, 0, 0);  // 0 参数，0 返回值
            if (ret != 0)
            {
                SetTop(oldTop);      // 错误时恢复到调用前的栈状态
                string err = GetLastError();
                throw new InvalidOperationException(
                    $"CallGlobal('{name}') failed: {err}");
            }
            // 成功：无返回值，栈已恢复至调用前状态（函数已由 pcall 弹出）
        }

        /// <summary>
        /// 调用 Lua 全局函数并获取一个数值返回值（无参数）
        /// </summary>
        /// <param name="name">全局函数名</param>
        /// <returns>函数的数值返回值</returns>
        /// <exception cref="InvalidOperationException">调用失败或返回值不是数值时抛出</exception>
        /// <example>
        /// vm.Execute("function get_answer() return 42 end");
        /// double answer = vm.CallGlobalForNumber("get_answer");
        /// </example>
        public double CallGlobalForNumber(string name)
        {
            EnsureNotDisposed();
            int oldTop = GetTop();   // 保存调用前栈顶
            GetGlobal(name);         // 压入函数
            int ret = LVM_PCall(_opaque, 0, 1);  // 0 参数，1 返回值
            if (ret != 0)
            {
                SetTop(oldTop);      // 错误时恢复栈
                string err = GetLastError();
                throw new InvalidOperationException(
                    $"CallGlobalForNumber('{name}') failed: {err}");
            }
            if (!IsNumber(-1))
            {
                SetTop(oldTop);      // 类型不匹配时恢复栈
                throw new InvalidOperationException(
                    $"CallGlobalForNumber('{name}'): return value is not a number");
            }
            double val = GetNumber(-1);
            SetTop(oldTop);          // 弹出返回值，恢复调用前栈状态
            return val;
        }

        /// <summary>
        /// 调用 Lua 全局函数并获取一个字符串返回值（无参数）
        /// </summary>
        /// <param name="name">全局函数名</param>
        /// <returns>函数的字符串返回值</returns>
        /// <exception cref="InvalidOperationException">调用失败或返回值不是字符串时抛出</exception>
        public string CallGlobalForString(string name)
        {
            EnsureNotDisposed();
            int oldTop = GetTop();   // 保存调用前栈顶
            GetGlobal(name);         // 压入函数
            int ret = LVM_PCall(_opaque, 0, 1);  // 0 参数，1 返回值
            if (ret != 0)
            {
                SetTop(oldTop);      // 错误时恢复栈
                string err = GetLastError();
                throw new InvalidOperationException(
                    $"CallGlobalForString('{name}') failed: {err}");
            }
            if (!IsString(-1))
            {
                SetTop(oldTop);      // 类型不匹配时恢复栈
                throw new InvalidOperationException(
                    $"CallGlobalForString('{name}'): return value is not a string");
            }
            string val = GetString(-1);
            SetTop(oldTop);          // 弹出返回值，恢复调用前栈状态
            return val;
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

            if (!IsNumber(-1))
            {
                ClearStack();
                throw new InvalidOperationException(
                    $"ExecuteAndGetNumber: result is not a number (type mismatch)");
            }
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

        /* ====================================================================
         * 公共方法 —— 外部函数注册
         * ==================================================================== */

        /// <summary>
        /// 注册单个外部函数为 Lua 全局变量
        /// </summary>
        /// <param name="name">Lua 全局变量名</param>
        /// <param name="func">回调委托</param>
        /// <returns>0 = 成功，-1 = 失败（通过 GetLastError 获取详情）</returns>
        /// <exception cref="ArgumentNullException">func 为 null 时抛出</exception>
        public int RegisterFunction(string name, ExternalFuncDelegate func)
        {
            EnsureNotDisposed();
            if (func == null)
                throw new ArgumentNullException(nameof(func));

            // 固定委托防止 GC 回收（委托在 Lua 虚拟机生命周期内需保持有效）
            var handle = GCHandle.Alloc(func);
            _registeredCallbacks.Add(handle);

            IntPtr funcPtr = Marshal.GetFunctionPointerForDelegate(func);
            int result = LVM_RegisterFunction(_opaque, name, funcPtr);

            // 注册失败时立即释放 GCHandle，避免内存泄漏
            if (result != 0)
            {
                handle.Free();
                _registeredCallbacks.Remove(handle);
            }

            return result;
        }

        /// <summary>
        /// 注册一批外部函数为一个 Lua 模块（表）
        /// </summary>
        /// <param name="moduleName">模块名称（Lua 全局变量名）</param>
        /// <param name="funcNames">函数名称数组（模块表中的字段名）</param>
        /// <param name="funcs">回调委托数组（与 funcNames 一一对应）</param>
        /// <returns>0 = 成功，-1 = 失败</returns>
        /// <exception cref="ArgumentException">参数数组长度不一致时抛出</exception>
        public int RegisterModule(string moduleName, string[] funcNames, ExternalFuncDelegate[] funcs)
        {
            EnsureNotDisposed();
            if (funcNames == null || funcs == null)
                throw new ArgumentNullException(funcNames == null ? nameof(funcNames) : nameof(funcs));
            if (funcNames.Length != funcs.Length)
                throw new ArgumentException("funcNames and funcs must have the same length");
            if (funcNames.Length == 0)
                throw new ArgumentException("funcNames and funcs must not be empty");

            int count = funcs.Length;
            int ptrSize = IntPtr.Size;

            // 固定所有委托并获取函数指针
            IntPtr[] funcPtrs = new IntPtr[count];
            var handles = new GCHandle[count];  // 跟踪本次分配的 GCHandle，失败时回滚
            for (int i = 0; i < count; i++)
            {
                if (funcs[i] == null)
                    throw new ArgumentException($"funcs[{i}] is null");
                handles[i] = GCHandle.Alloc(funcs[i]);
                _registeredCallbacks.Add(handles[i]);
                funcPtrs[i] = Marshal.GetFunctionPointerForDelegate(funcs[i]);
            }

            // 分配非托管内存存放函数指针数组
            IntPtr unmanagedArray = Marshal.AllocHGlobal(count * ptrSize);
            try
            {
                for (int i = 0; i < count; i++)
                {
                    Marshal.WriteIntPtr(unmanagedArray, i * ptrSize, funcPtrs[i]);
                }

                int result = LVM_RegisterModule(_opaque, moduleName, funcNames, unmanagedArray, count);

                // 注册失败时释放本次分配的 GCHandle，避免内存泄漏
                if (result != 0)
                {
                    for (int i = 0; i < count; i++)
                    {
                        handles[i].Free();
                        _registeredCallbacks.Remove(handles[i]);
                    }
                }

                return result;
            }
            finally
            {
                Marshal.FreeHGlobal(unmanagedArray);
            }
        }
    }
}
