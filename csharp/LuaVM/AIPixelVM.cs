/**
 * @file    AIPixelVM.cs
 * @brief   AIPixelVM 原生库抽象基类 —— AOT 兼容的动态库加载与生命周期管理
 *
 * 设计目标:
 *   通过 NativeLibrary API 实现与 DllImport 兼容的原生库解析机制，
 *   使子类 LuaVM 在 .NET Native AOT 场景下仍能正确加载 AIPixelVM.dll/.so/.dylib。
 *
 * 核心机制:
 *   1. 静态构造函数中注册 DllImportResolver —— 进程内仅执行一次
 *   2. 解析器按 RID (Runtime Identifier) 约定搜索原生库
 *   3. 基类实现 IDisposable 模式，子类继承释放语义
 *
 * 使用示例:
 *   // LuaVM 继承自 AIPixelVM，自动获得 AOT 兼容的原生库加载能力
 *   using var vm = new LuaVM(LuaVMType.Lua55);
 *   vm.Execute("print('Hello AOT!')");
 */

using System;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;

namespace LuaVM
{
    /// <summary>
    /// AIPixelVM 原生库抽象基类
    ///
    /// 职责:
    /// - 提供 AOT 兼容的原生库加载机制（通过 NativeLibrary API）
    /// - 管理原生库句柄生命周期
    /// - 为子类提供统一的 IDisposable 实现
    ///
    /// 线程安全:
    /// - 静态构造函数由 CLR 保证线程安全（仅执行一次）
    /// - 实例成员非线程安全，同一实例不应多线程并发使用
    /// </summary>
    public abstract class AIPixelVM : IDisposable
    {
        /* ====================================================================
         * 静态成员 —— 原生库解析（进程生命周期内一次性初始化）
         * ==================================================================== */

        /// <summary>原生库基础名称（不含前缀/后缀，交由平台逻辑拼接）</summary>
        protected const string NativeLibName = "AIPixelVM";

        /// <summary>
        /// 静态构造函数 —— 注册 DllImportResolver
        ///
        /// CLR 保证此构造函数在类型首次被访问前执行且仅执行一次。
        /// 注册的解析器拦截本程序集中所有 DllImport 调用，
        /// 当 libraryName 匹配 "AIPixelVM" 时按 AOT 兼容逻辑解析路径。
        /// </summary>
        static AIPixelVM()
        {
            // 为当前程序集注册自定义原生库解析器
            // 此解析器仅处理 AIPixelVM，其他库名返回 IntPtr.Zero 交由默认逻辑处理
            NativeLibrary.SetDllImportResolver(
                typeof(AIPixelVM).Assembly,
                ResolveNativeLibrary
            );
        }

        /// <summary>
        /// AOT 兼容的原生库解析回调
        ///
        /// 搜索优先级:
        ///   1. {BaseDir}/runtimes/{RID}/native/{lib}  — NuGet RID 约定路径
        ///   2. {BaseDir}/{lib}                          — 应用程序同目录
        ///   3. {AssemblyDir}/{lib}                      — 程序集所在目录
        ///   4. 系统默认搜索（通过 NativeLibrary.TryLoad 简单名称回退）
        ///
        /// AOT 说明:
        ///   NativeLibrary.TryLoad 在 Native AOT 下受支持，
        ///   不依赖 JIT 或动态代码生成。
        /// </summary>
        /// <param name="libraryName">DllImport 中声明的库名</param>
        /// <param name="assembly">发起调用的程序集</param>
        /// <param name="searchPath">DllImportSearchPath 标志（可能为 null）</param>
        /// <returns>成功加载的库句柄，或 IntPtr.Zero 表示交由默认解析</returns>
        private static IntPtr ResolveNativeLibrary(
            string libraryName,
            Assembly assembly,
            DllImportSearchPath? searchPath)
        {
            // 仅处理 AIPixelVM，其他库名交由 .NET 默认解析
            if (!string.Equals(libraryName, NativeLibName, StringComparison.OrdinalIgnoreCase))
                return IntPtr.Zero;

            // 获取当前运行时标识符 (RID)，如 win-x64 / linux-x64 / osx-x64
            string rid = GetRuntimeIdentifier();

            // 应用程序基目录（发布后为可执行文件所在目录）
            string baseDir = AppContext.BaseDirectory;

            // 按优先级依次尝试加载
            // 注意: 不依赖 Assembly.Location — 在 AOT 单文件发布中始终返回空字符串
            string[] searchDirs = new[]
            {
                Path.Combine(baseDir, "runtimes", rid, "native"),          // NuGet RID 路径
                baseDir,                                                    // 应用根目录
            };

            foreach (string dir in searchDirs)
            {
                if (string.IsNullOrEmpty(dir))
                    continue;

                string libPath = GetPlatformLibraryPath(dir);
                if (NativeLibrary.TryLoad(libPath, out IntPtr handle))
                    return handle;
            }

            // 回退: 不指定路径，依赖操作系统默认搜索（PATH / LD_LIBRARY_PATH 等）
            string platformName = GetPlatformLibraryName();
            if (NativeLibrary.TryLoad(platformName, out IntPtr fallbackHandle))
                return fallbackHandle;

            // 所有尝试失败，返回零指针让 DllImport 抛出 DllNotFoundException
            return IntPtr.Zero;
        }

        /// <summary>
        /// 获取当前运行时的 RID 字符串
        ///
        /// 通过 RuntimeInformation 判断操作系统和架构，返回 .NET RID 目录名。
        /// 用于构造 runtimes/{RID}/native/ 搜索路径。
        /// </summary>
        private static string GetRuntimeIdentifier()
        {
            string os = "unknown";
            if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
                os = "win";
            else if (RuntimeInformation.IsOSPlatform(OSPlatform.Linux))
                os = "linux";
            else if (RuntimeInformation.IsOSPlatform(OSPlatform.OSX))
                os = "osx";

            string arch = RuntimeInformation.ProcessArchitecture switch
            {
                Architecture.X64 => "x64",
                Architecture.Arm64 => "arm64",
                Architecture.X86 => "x86",
                Architecture.Arm => "arm",
                _ => "unknown"
            };

            return $"{os}-{arch}";
        }

        /// <summary>
        /// 获取平台相关的完整库文件路径
        ///
        /// 拼接规则:
        /// - Windows: {dir}\AIPixelVM.dll
        /// - Linux:   {dir}/libAIPixelVM.so
        /// - macOS:   {dir}/libAIPixelVM.dylib
        /// </summary>
        /// <param name="directory">搜索目录</param>
        /// <returns>平台相关的完整库路径</returns>
        private static string GetPlatformLibraryPath(string directory)
        {
            return Path.Combine(directory, GetPlatformLibraryName());
        }

        /// <summary>
        /// 获取平台相关的库文件名（不含路径）
        /// </summary>
        private static string GetPlatformLibraryName()
        {
            if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
                return $"{NativeLibName}.dll";
            if (RuntimeInformation.IsOSPlatform(OSPlatform.Linux))
                return $"lib{NativeLibName}.so";
            if (RuntimeInformation.IsOSPlatform(OSPlatform.OSX))
                return $"lib{NativeLibName}.dylib";
            // 未知平台回退
            return $"{NativeLibName}";
        }

        /* ====================================================================
         * 实例成员 —— IDisposable 模式
         * ==================================================================== */

        /// <summary>释放标记，防止重复释放（子类可访问以判断自身释放状态）</summary>
        protected bool _disposed;

        /// <summary>
        /// 检查实例是否已释放，若已释放则抛出 ObjectDisposedException
        ///
        /// 子类应在每个公共方法入口调用此方法，
        /// 确保在对象释放后不再访问原生资源。
        /// </summary>
        /// <exception cref="ObjectDisposedException">实例已释放时抛出</exception>
        protected void EnsureNotDisposed()
        {
            if (_disposed)
                throw new ObjectDisposedException(GetType().Name);
        }

        /// <summary>
        /// 释放托管与非托管资源
        ///
        /// 子类可重写 Dispose(bool) 添加自有清理逻辑，
        /// 但必须调用 base.Dispose(disposing)。
        /// </summary>
        public void Dispose()
        {
            Dispose(true);
            GC.SuppressFinalize(this);
        }

        /// <summary>
        /// 可重写的释放逻辑
        /// </summary>
        /// <param name="disposing">
        /// true  = 由 Dispose() 或 using 触发（释放托管+非托管资源）
        /// false = 由终结器触发（仅释放非托管资源）
        /// </param>
        protected virtual void Dispose(bool disposing)
        {
            if (!_disposed)
            {
                if (disposing)
                {
                    // 托管资源清理占位 —— 子类可在此释放托管资源
                }

                // 非托管资源清理占位 —— 子类可在此释放非托管资源

                _disposed = true;
            }
        }

        /// <summary>
        /// 终结器 —— 最后的释放保障
        /// 注意: .NET Native AOT 下终结器行为与 CoreCLR 一致
        /// </summary>
        ~AIPixelVM()
        {
            Dispose(false);
        }
    }
}
