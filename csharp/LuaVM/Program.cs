/**
 * @file    Program.cs
 * @brief   LuaVM C# 使用示例
 *
 * 演示 LuaVM 类的完整使用流程，包括:
 * - 基本脚本执行
 * - 栈操作
 * - 全局变量读写
 * - 表操作
 * - 不同虚拟机后端切换
 * - 错误处理
 *
 * 运行前需确保 AIPixelVM.dll 在可搜索路径中
 * (如程序目录、runtimes/win-x64/native/ 或 PATH 中)。
 */

using System;
using LuaVM;

class Program
{
    static void Main(string[] args)
    {
        Console.WriteLine("=== LuaVM Wrapper — C# Integration Demo ===\n");

        try
        {
            DemoBasicExecution();
            DemoStackOperations();
            DemoGlobalVariables();
            DemoTableOperations();
            DemoErrorHandling();
            DemoMultipleVM();
        }
        catch (Exception ex)
        {
            Console.WriteLine($"\n[FATAL] {ex.GetType().Name}: {ex.Message}");
            Console.WriteLine(ex.StackTrace);
            Environment.Exit(1);
        }

        Console.WriteLine("\n=== All demos completed successfully ===");
    }

    /// <summary>
    /// 演示基本脚本执行
    /// </summary>
    static void DemoBasicExecution()
    {
        Console.WriteLine("--- Demo: Basic Execution (Lua 5.5) ---");

        using var vm = new LuaVM.LuaVM(LuaVMType.Lua55);

        // 执行简单的 Lua 代码
        int result = vm.Execute("print('Hello from Lua 5.5 via C#!')");
        Console.WriteLine($"  Execute result: {result}");

        // 执行算术运算
        result = vm.Execute("local sum = 1 + 2 + 3 + 4 + 5");
        Console.WriteLine($"  Arithmetic result: {result}");

        Console.WriteLine("  OK\n");
    }

    /// <summary>
    /// 演示栈操作
    /// </summary>
    static void DemoStackOperations()
    {
        Console.WriteLine("--- Demo: Stack Operations ---");

        using var vm = new LuaVM.LuaVM(LuaVMType.Lua55);

        // 压入各种类型的值
        vm.PushNumber(3.14159);
        vm.PushString("Hello C#");
        vm.PushBoolean(true);
        vm.PushNil();

        Console.WriteLine($"  Stack top after 4 pushes: {vm.GetTop()}");

        // 类型检查
        Console.WriteLine($"  Index 1 is number:  {vm.IsNumber(1)}");
        Console.WriteLine($"  Index 2 is string:  {vm.IsString(2)}");
        Console.WriteLine($"  Index 3 is boolean: {vm.IsBoolean(3)}");
        Console.WriteLine($"  Index 4 is nil:     {vm.IsNil(4)}");

        // 取值
        Console.WriteLine($"  Value at 1: {vm.GetNumber(1)}");
        Console.WriteLine($"  Value at 2: {vm.GetString(2)}");
        Console.WriteLine($"  Value at 3: {vm.GetBoolean(3)}");

        // 截断栈
        vm.SetTop(0);
        Console.WriteLine($"  Stack top after clear: {vm.GetTop()}");

        Console.WriteLine("  OK\n");
    }

    /// <summary>
    /// 演示全局变量读写
    /// </summary>
    static void DemoGlobalVariables()
    {
        Console.WriteLine("--- Demo: Global Variables ---");

        using var vm = new LuaVM.LuaVM(LuaVMType.Lua55);

        // 使用便捷方法设置数值
        vm.SetGlobalNumber("answer", 42.0);
        Console.WriteLine($"  Set answer = 42");

        // 读取数值
        double answer = vm.GetGlobalNumber("answer");
        Console.WriteLine($"  Get answer = {answer}");

        // 使用便捷方法设置字符串
        vm.SetGlobalString("greeting", "Hello from C#");
        string greeting = vm.GetGlobalString("greeting");
        Console.WriteLine($"  Get greeting = '{greeting}'");

        // 通过 Lua 代码设置全局变量
        vm.Execute("product = answer * 2");
        double product = vm.GetGlobalNumber("product");
        Console.WriteLine($"  product = answer * 2 = {product}");

        Console.WriteLine("  OK\n");
    }

    /// <summary>
    /// 演示表操作
    /// </summary>
    static void DemoTableOperations()
    {
        Console.WriteLine("--- Demo: Table Operations ---");

        using var vm = new LuaVM.LuaVM(LuaVMType.Lua55);

        // 创建表并设置字段
        vm.NewTable();                    // 压入新表
        vm.PushNumber(100.0);             // 压入值
        vm.SetField(1, "x");              // 设置 t.x = 100
        vm.PushNumber(200.0);
        vm.SetField(1, "y");              // 设置 t.y = 200

        // 读取字段
        vm.GetField(1, "x");
        Console.WriteLine($"  t.x = {vm.GetNumber(2)}");
        vm.SetTop(1);  // 弹出 x 的值

        vm.GetField(1, "y");
        Console.WriteLine($"  t.y = {vm.GetNumber(2)}");

        // 将表赋值给全局变量
        vm.SetGlobal("point");
        Console.WriteLine("  Set global 'point' = table");

        // 从 Lua 访问此表
        int result = vm.Execute(
            "if point.x + point.y ~= 300 then " +
            "    error('Table fields mismatch!') " +
            "end"
        );
        Console.WriteLine($"  Verify point.x + point.y == 300: {(result == 0 ? "OK" : "FAIL")}");

        Console.WriteLine("  OK\n");
    }

    /// <summary>
    /// 演示错误处理
    /// </summary>
    static void DemoErrorHandling()
    {
        Console.WriteLine("--- Demo: Error Handling ---");

        using var vm = new LuaVM.LuaVM(LuaVMType.Lua55);

        // 语法错误
        int result = vm.Execute("syntax error --> @@@");
        if (result != 0)
        {
            Console.WriteLine($"  Syntax error caught: {vm.GetLastError()}");
        }

        // 运行时错误
        result = vm.Execute("error('intentional error for testing')");
        if (result != 0)
        {
            Console.WriteLine($"  Runtime error caught: {vm.GetLastError()}");
        }

        // 成功执行的错误信息应被清除
        result = vm.Execute("local x = 1");
        Console.WriteLine($"  After success, error is empty: '{vm.GetLastError()}'");

        Console.WriteLine("  OK\n");
    }

    /// <summary>
    /// 演示多虚拟机实例隔离
    /// </summary>
    static void DemoMultipleVM()
    {
        Console.WriteLine("--- Demo: Multiple VM Instances ---");

        using var vm1 = new LuaVM.LuaVM(LuaVMType.Lua55);
        using var vm2 = new LuaVM.LuaVM(LuaVMType.Lua55);

        // 在两个独立虚拟机中设置不同的值
        vm1.SetGlobalNumber("value", 100.0);
        vm2.SetGlobalNumber("value", 200.0);

        // 验证隔离
        double val1 = vm1.GetGlobalNumber("value");
        double val2 = vm2.GetGlobalNumber("value");

        Console.WriteLine($"  VM1.value = {val1}");
        Console.WriteLine($"  VM2.value = {val2}");
        Console.WriteLine($"  Instances isolated: {(val1 != val2 ? "YES" : "NO")}");

        Console.WriteLine("  OK\n");
    }
}
