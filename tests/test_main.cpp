/**
 * @file    test_main.cpp
 * @brief   AIPixelVM 单元测试
 *
 * 测试覆盖：
 * - 生命周期管理（创建 / 销毁）
 * - Lua 脚本执行
 * - 栈操作（push / get / set）
 * - 类型检查
 * - 全局变量读写
 * - 表操作
 * - 错误处理
 *
 * 使用简单的断言宏实现轻量级测试框架（无外部依赖）。
 */

#include "lvm_api.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <string>

/* ==========================================================================
 * 外部函数注册 —— 测试用回调函数
 * ========================================================================== */

/** @brief 回调：两数相加 (arg1 + arg2) */
static int test_add_callback(void* opaque) {
    double a = LVM_ToNumber(opaque, 1);
    double b = LVM_ToNumber(opaque, 2);
    LVM_PushNumber(opaque, a + b);
    return 1;
}

/** @brief 回调：生成问候语 ("Hello, " .. name .. "!") */
static int test_greet_callback(void* opaque) {
    const char* name = LVM_ToString(opaque, 1);
    std::string greeting = std::string("Hello, ") + (name ? name : "nil") + "!";
    LVM_PushString(opaque, greeting.c_str());
    return 1;
}

/** @brief 回调：两数相乘 (arg1 * arg2) — 用于模块测试 */
static int test_multiply_callback(void* opaque) {
    double a = LVM_ToNumber(opaque, 1);
    double b = LVM_ToNumber(opaque, 2);
    LVM_PushNumber(opaque, a * b);
    return 1;
}

/** @brief 回调：返回圆周率常数 — 用于模块测试 */
static int test_getpi_callback(void* opaque) {
    (void)opaque;  // 无参数
    LVM_PushNumber(opaque, 3.1415926);
    return 1;
}

/** @brief 回调：返回字符串常数 — 用于模块测试 */
static int test_getversion_callback(void* opaque) {
    (void)opaque;
    LVM_PushString(opaque, "1.0.0-test");
    return 1;
}

/* ==========================================================================
 * 轻量级测试框架
 * ========================================================================== */

static int  g_testsPassed = 0;
static int  g_testsFailed = 0;
static const char* g_currentTest = "";

/** @brief 断言相等，失败时打印诊断信息并标记测试失败 */
#define TEST_ASSERT(cond, msg) do {                                     \
    if (!(cond)) {                                                      \
        std::fprintf(stderr, "[FAIL] %s: %s (line %d)\n",               \
            g_currentTest, msg, __LINE__);                               \
        std::fprintf(stderr, "  Condition: %s\n", #cond);               \
        g_testsFailed++;                                                 \
        g_currentTest = nullptr; /* 标记失败，阻止后续 [OK] 输出 */     \
        return;                                                         \
    }                                                                   \
} while(0)

/** @brief 声明测试函数，打印测试名 */
#define TEST(name)                                                       \
    static void test_##name();                                           \
    struct TestReg_##name {                                              \
        TestReg_##name() {                                               \
            g_currentTest = #name;                                       \
            std::printf("  [RUN ] %s\n", #name);                        \
            test_##name();                                               \
            if (g_currentTest) { /* 未失败 */                            \
                std::printf("  [OK  ] %s\n", #name);                    \
                g_testsPassed++;                                         \
            }                                                             \
        }                                                                \
    } testReg_##name;                                                    \
    static void test_##name()

/* ==========================================================================
 * 测试：生命周期管理
 * ========================================================================== */

TEST(lifecycle_create_destroy) {
    /* 创建 Lua 5.5 实例 */
    void* vm = LVM_Create(1);  // type=1: Lua 5.5
    TEST_ASSERT(vm != nullptr, "LVM_Create(1) should return non-null");

    /* 销毁 */
    LVM_Destroy(vm);

    /* 销毁 nullptr 应该是安全的空操作 */
    LVM_Destroy(nullptr);
}

TEST(lifecycle_get_last_error_initial) {
    void* vm = LVM_Create(1);
    TEST_ASSERT(vm != nullptr, "LVM_Create failed");

    /* 初始状态应该没有错误 */
    const char* err = LVM_GetLastError(vm);
    TEST_ASSERT(err != nullptr && std::strlen(err) == 0,
        "Initial error should be empty string");

    LVM_Destroy(vm);
}

/* ==========================================================================
 * 测试：脚本执行
 * ========================================================================== */

TEST(execute_simple_print) {
    void* vm = LVM_Create(1);
    TEST_ASSERT(vm != nullptr, "LVM_Create failed");

    /* 执行简单的 print 语句（返回 0 表示成功） */
    int result = LVM_ExecuteString(vm, "print('Hello from LuaVM!')");
    TEST_ASSERT(result == 0, "Simple print should succeed");

    LVM_Destroy(vm);
}

TEST(execute_arithmetic) {
    void* vm = LVM_Create(1);
    TEST_ASSERT(vm != nullptr, "LVM_Create failed");

    /* 执行算术运算 */
    int result = LVM_ExecuteString(vm, "local a = 1 + 2 * 3");
    TEST_ASSERT(result == 0, "Arithmetic should succeed");

    LVM_Destroy(vm);
}

TEST(execute_variable_and_function) {
    void* vm = LVM_Create(1);
    TEST_ASSERT(vm != nullptr, "LVM_Create failed");

    /* 定义变量和函数 */
    int result = LVM_ExecuteString(vm,
        "function add(a, b)\n"
        "    return a + b\n"
        "end\n"
        "result = add(10, 20)\n"
    );
    TEST_ASSERT(result == 0, "Function definition and call should succeed");

    LVM_Destroy(vm);
}

TEST(execute_syntax_error) {
    void* vm = LVM_Create(1);
    TEST_ASSERT(vm != nullptr, "LVM_Create failed");

    /* 故意执行有语法错误的代码 */
    int result = LVM_ExecuteString(vm, "local x ==");
    TEST_ASSERT(result != 0, "Syntax error should return non-zero");

    /* 错误信息应该被设置 */
    const char* err = LVM_GetLastError(vm);
    TEST_ASSERT(err != nullptr && std::strlen(err) > 0,
        "Error message should be set on syntax error");

    LVM_Destroy(vm);
}

TEST(execute_runtime_error) {
    void* vm = LVM_Create(1);
    TEST_ASSERT(vm != nullptr, "LVM_Create failed");

    /* 运行时错误：调用不存在的函数 */
    int result = LVM_ExecuteString(vm, "nonexistent_function()");
    TEST_ASSERT(result != 0, "Runtime error should return non-zero");

    const char* err = LVM_GetLastError(vm);
    TEST_ASSERT(err != nullptr && std::strlen(err) > 0,
        "Error message should be set on runtime error");

    LVM_Destroy(vm);
}

TEST(execute_null_code) {
    void* vm = LVM_Create(1);
    TEST_ASSERT(vm != nullptr, "LVM_Create failed");

    /* nullptr 输入应返回 -1 */
    int result = LVM_ExecuteString(vm, nullptr);
    TEST_ASSERT(result == -1, "Null code should return -1");

    LVM_Destroy(vm);
}

/* ==========================================================================
 * 测试：栈操作
 * ========================================================================== */

TEST(stack_push_and_get_top) {
    void* vm = LVM_Create(1);
    TEST_ASSERT(vm != nullptr, "LVM_Create failed");

    /* 初始栈应为空 */
    int top = LVM_GetTop(vm);
    TEST_ASSERT(top == 0, "Initial stack top should be 0");

    /* 压入数值，栈顶应变大 */
    LVM_PushNumber(vm, 42.0);
    top = LVM_GetTop(vm);
    TEST_ASSERT(top == 1, "Stack top should be 1 after push");

    /* 压入更多值 */
    LVM_PushString(vm, "hello");
    LVM_PushBoolean(vm, 1);
    top = LVM_GetTop(vm);
    TEST_ASSERT(top == 3, "Stack top should be 3 after 3 pushes");

    /* 设置栈顶截断 */
    LVM_SetTop(vm, 1);
    top = LVM_GetTop(vm);
    TEST_ASSERT(top == 1, "Stack top should be 1 after settop(1)");

    /* 清空栈 */
    LVM_SetTop(vm, 0);
    top = LVM_GetTop(vm);
    TEST_ASSERT(top == 0, "Stack top should be 0 after settop(0)");

    LVM_Destroy(vm);
}

/* ==========================================================================
 * 测试：类型检查
 * ========================================================================== */

TEST(type_check_number) {
    void* vm = LVM_Create(1);
    TEST_ASSERT(vm != nullptr, "LVM_Create failed");

    LVM_PushNumber(vm, 3.14);
    TEST_ASSERT(LVM_IsNumber(vm, 1), "Value at index 1 should be a number");
    // Note: Lua's lua_isstring returns true for numbers too (auto-coercion)
    TEST_ASSERT(!LVM_IsBoolean(vm, 1), "Number should not be a boolean");
    TEST_ASSERT(!LVM_IsNil(vm, 1), "Number should not be nil");

    LVM_Destroy(vm);
}

TEST(type_check_string) {
    void* vm = LVM_Create(1);
    TEST_ASSERT(vm != nullptr, "LVM_Create failed");

    LVM_PushString(vm, "test");
    TEST_ASSERT(LVM_IsString(vm, 1), "Value should be a string");
    TEST_ASSERT(!LVM_IsNumber(vm, 1), "String should not be a number");

    LVM_Destroy(vm);
}

TEST(type_check_boolean) {
    void* vm = LVM_Create(1);
    TEST_ASSERT(vm != nullptr, "LVM_Create failed");

    LVM_PushBoolean(vm, 1);
    TEST_ASSERT(LVM_IsBoolean(vm, 1), "Value should be a boolean");

    /* 在 Lua 中，boolean true 也是 number（自动转换），此处不测 isnumber */

    LVM_Destroy(vm);
}

TEST(type_check_nil) {
    void* vm = LVM_Create(1);
    TEST_ASSERT(vm != nullptr, "LVM_Create failed");

    LVM_PushNil(vm);
    TEST_ASSERT(LVM_IsNil(vm, 1), "Value should be nil");

    LVM_Destroy(vm);
}

/* ==========================================================================
 * 测试：取值操作
 * ========================================================================== */

TEST(value_to_number) {
    void* vm = LVM_Create(1);
    TEST_ASSERT(vm != nullptr, "LVM_Create failed");

    LVM_PushNumber(vm, 42.5);
    double val = LVM_ToNumber(vm, 1);
    TEST_ASSERT(std::fabs(val - 42.5) < 0.0001, "ToNumber should return 42.5");

    LVM_Destroy(vm);
}

TEST(value_to_string) {
    void* vm = LVM_Create(1);
    TEST_ASSERT(vm != nullptr, "LVM_Create failed");

    LVM_PushString(vm, "hello world");
    const char* str = LVM_ToString(vm, 1);
    TEST_ASSERT(str != nullptr, "ToString should return non-null");
    TEST_ASSERT(std::strcmp(str, "hello world") == 0,
        "ToString should return 'hello world'");

    LVM_Destroy(vm);
}

TEST(value_to_boolean) {
    void* vm = LVM_Create(1);
    TEST_ASSERT(vm != nullptr, "LVM_Create failed");

    LVM_PushBoolean(vm, 1);
    int b = LVM_ToBoolean(vm, 1);
    TEST_ASSERT(b == 1, "ToBoolean should return 1 for true");

    LVM_SetTop(vm, 0);
    LVM_PushBoolean(vm, 0);
    b = LVM_ToBoolean(vm, 1);
    TEST_ASSERT(b == 0, "ToBoolean should return 0 for false");

    LVM_Destroy(vm);
}

/* ==========================================================================
 * 测试：全局变量
 * ========================================================================== */

TEST(global_set_and_get) {
    void* vm = LVM_Create(1);
    TEST_ASSERT(vm != nullptr, "LVM_Create failed");

    /* 设置全局变量 x = 100 */
    LVM_PushNumber(vm, 100.0);
    LVM_SetGlobal(vm, "my_test_var");

    /* 清空栈确认已弹出 */
    TEST_ASSERT(LVM_GetTop(vm) == 0, "Stack should be empty after SetGlobal");

    /* 获取全局变量，验证值 */
    LVM_GetGlobal(vm, "my_test_var");
    TEST_ASSERT(LVM_GetTop(vm) == 1, "GetGlobal should push value");
    TEST_ASSERT(LVM_IsNumber(vm, 1), "my_test_var should be a number");
    double val = LVM_ToNumber(vm, 1);
    TEST_ASSERT(std::fabs(val - 100.0) < 0.0001, "my_test_var should be 100");

    LVM_Destroy(vm);
}

TEST(global_string_value) {
    void* vm = LVM_Create(1);
    TEST_ASSERT(vm != nullptr, "LVM_Create failed");

    /* 执行 Lua 代码设置全局字符串变量 */
    int result = LVM_ExecuteString(vm, "my_str = 'C++ integration test'");
    TEST_ASSERT(result == 0, "Setting global string should succeed");

    /* 获取并验证 */
    LVM_GetGlobal(vm, "my_str");
    TEST_ASSERT(LVM_IsString(vm, 1), "my_str should be a string");
    const char* str = LVM_ToString(vm, 1);
    TEST_ASSERT(std::strcmp(str, "C++ integration test") == 0,
        "my_str should match");

    LVM_Destroy(vm);
}

/* ==========================================================================
 * 测试：表操作
 * ========================================================================== */

TEST(table_create_and_access) {
    void* vm = LVM_Create(1);
    TEST_ASSERT(vm != nullptr, "LVM_Create failed");

    /* 创建新表 */
    LVM_NewTable(vm);
    TEST_ASSERT(LVM_GetTop(vm) == 1, "NewTable should push table");

    /* 设置字段: t.key = "value" */
    LVM_PushString(vm, "table_value");
    LVM_SetField(vm, 1, "mykey");

    /* 确认栈已弹出 setfield 的值 */
    TEST_ASSERT(LVM_GetTop(vm) == 1, "SetField should pop value");

    /* 获取字段 */
    LVM_GetField(vm, 1, "mykey");
    TEST_ASSERT(LVM_GetTop(vm) == 2, "GetField should push value");
    TEST_ASSERT(LVM_IsString(vm, 2), "Field should be string");
    const char* val = LVM_ToString(vm, 2);
    TEST_ASSERT(std::strcmp(val, "table_value") == 0, "Field value should match");

    LVM_Destroy(vm);
}

TEST(table_numeric_fields) {
    void* vm = LVM_Create(1);
    TEST_ASSERT(vm != nullptr, "LVM_Create failed");

    /* 通过 Lua 代码创建并操作表 */
    int result = LVM_ExecuteString(vm,
        "t = {}\n"
        "t.x = 10\n"
        "t.y = 20\n"
        "t.sum = t.x + t.y\n"
    );
    TEST_ASSERT(result == 0, "Table creation should succeed");

    /* 获取 t.sum */
    LVM_GetGlobal(vm, "t");
    TEST_ASSERT(LVM_GetTop(vm) == 1, "Table should be on stack");

    LVM_GetField(vm, 1, "sum");
    TEST_ASSERT(LVM_IsNumber(vm, 2), "t.sum should be number");
    double sum = LVM_ToNumber(vm, 2);
    TEST_ASSERT(std::fabs(sum - 30.0) < 0.0001, "t.sum should be 30");

    LVM_Destroy(vm);
}

/* ==========================================================================
 * 测试：复杂场景
 * ========================================================================== */

TEST(complex_loop_and_table) {
    void* vm = LVM_Create(1);
    TEST_ASSERT(vm != nullptr, "LVM_Create failed");

    /* 使用循环和表操作 */
    int result = LVM_ExecuteString(vm,
        "function fib(n)\n"
        "    if n <= 1 then return n end\n"
        "    return fib(n-1) + fib(n-2)\n"
        "end\n"
        "fib10 = fib(10)\n"
    );
    TEST_ASSERT(result == 0, "Recursive Fibonacci should succeed");

    /* 验证 fib(10) = 55 */
    LVM_GetGlobal(vm, "fib10");
    TEST_ASSERT(LVM_IsNumber(vm, 1), "fib10 should be number");
    double fib10 = LVM_ToNumber(vm, 1);
    TEST_ASSERT(std::fabs(fib10 - 55.0) < 0.0001, "fib(10) should be 55");

    LVM_Destroy(vm);
}

TEST(string_manipulation) {
    void* vm = LVM_Create(1);
    TEST_ASSERT(vm != nullptr, "LVM_Create failed");

    /* 字符串拼接 */
    int result = LVM_ExecuteString(vm,
        "a = 'Hello'\n"
        "b = 'World'\n"
        "c = a .. ', ' .. b .. '!'\n"
    );
    TEST_ASSERT(result == 0, "String concatenation should succeed");

    LVM_GetGlobal(vm, "c");
    TEST_ASSERT(LVM_IsString(vm, 1), "c should be string");
    const char* c = LVM_ToString(vm, 1);
    TEST_ASSERT(std::strcmp(c, "Hello, World!") == 0,
        "c should be 'Hello, World!'");

    LVM_Destroy(vm);
}

/* ==========================================================================
 * 测试：多实例并发
 * ========================================================================== */

TEST(multiple_instances) {
    void* vm1 = LVM_Create(1);
    void* vm2 = LVM_Create(1);
    TEST_ASSERT(vm1 != nullptr && vm2 != nullptr, "Both VMs should be created");

    /* vm1 中设置 x = 100 */
    LVM_PushNumber(vm1, 100.0);
    LVM_SetGlobal(vm1, "x");

    /* vm2 中设置 x = 200 */
    LVM_PushNumber(vm2, 200.0);
    LVM_SetGlobal(vm2, "x");

    /* 验证两个实例完全隔离 */
    LVM_GetGlobal(vm1, "x");
    double x1 = LVM_ToNumber(vm1, 1);
    TEST_ASSERT(std::fabs(x1 - 100.0) < 0.0001, "vm1.x should be 100");

    LVM_GetGlobal(vm2, "x");
    double x2 = LVM_ToNumber(vm2, 1);
    TEST_ASSERT(std::fabs(x2 - 200.0) < 0.0001, "vm2.x should be 200");

    LVM_Destroy(vm1);
    LVM_Destroy(vm2);
}

/* ==========================================================================
 * 测试：批量脚本加载
 * ========================================================================== */

/** @brief 使用编译时定义的脚本目录路径 */
#ifndef TEST_SCRIPTS_DIR
#define TEST_SCRIPTS_DIR "./tests/scripts"
#endif

TEST(batch_load_basic) {
    void* vm = LVM_Create(1);
    TEST_ASSERT(vm != nullptr, "LVM_Create failed");

    /* 从默认脚本目录加载所有 .lua 文件
     * 应加载: init.lua, test1.lua, test2.lua, test3.lua, skip_me.lua (共 5 个)
     * 不应加载: helper.lualib (后缀不匹配) */
    int count = LVM_LoadScriptFiles(vm, TEST_SCRIPTS_DIR, ".lua");
    TEST_ASSERT(count >= 3, "Should load at least 3 .lua files");

    /* 验证各脚本设置的全局变量 */
    LVM_GetGlobal(vm, "test1_executed");
    TEST_ASSERT(LVM_IsBoolean(vm, 1), "test1_executed should exist");
    LVM_SetTop(vm, 0);

    LVM_GetGlobal(vm, "test1_value");
    TEST_ASSERT(std::fabs(LVM_ToNumber(vm, 1) - 100.0) < 0.0001,
        "test1_value should be 100");
    LVM_SetTop(vm, 0);

    LVM_GetGlobal(vm, "test2_executed");
    TEST_ASSERT(LVM_IsBoolean(vm, 1), "test2_executed should exist");
    LVM_SetTop(vm, 0);

    LVM_GetGlobal(vm, "init_executed");
    TEST_ASSERT(LVM_IsBoolean(vm, 1), "init_executed should exist");
    LVM_SetTop(vm, 0);

    LVM_Destroy(vm);
}

TEST(batch_load_custom_suffix) {
    void* vm = LVM_Create(1);
    TEST_ASSERT(vm != nullptr, "LVM_Create failed");

    /* 加载 .lualib 后缀的文件 —— 应只加载 helper.lualib */
    int count = LVM_LoadScriptFiles(vm, TEST_SCRIPTS_DIR, ".lualib");
    TEST_ASSERT(count >= 1, "Should load at least 1 .lualib file");

    LVM_GetGlobal(vm, "helper_executed");
    TEST_ASSERT(LVM_IsBoolean(vm, 1), "helper_executed should exist");
    LVM_SetTop(vm, 0);

    /* .lua 文件不应该被加载 */
    LVM_GetGlobal(vm, "test1_executed");
    TEST_ASSERT(LVM_IsNil(vm, 1),
        "test1_executed should not exist when loading .lualib");
    LVM_SetTop(vm, 0);

    LVM_Destroy(vm);
}

TEST(batch_load_default_suffix) {
    void* vm = LVM_Create(1);
    TEST_ASSERT(vm != nullptr, "LVM_Create failed");

    /* suffix 传 nullptr，应默认为 ".lua" */
    int count = LVM_LoadScriptFiles(vm, TEST_SCRIPTS_DIR, nullptr);
    TEST_ASSERT(count >= 3, "Default suffix should load at least 3 .lua files");

    LVM_GetGlobal(vm, "test1_executed");
    TEST_ASSERT(LVM_IsBoolean(vm, 1),
        "test1_executed should exist with default suffix");

    LVM_Destroy(vm);
}

TEST(batch_load_blacklist) {
    void* vm = LVM_Create(1);
    TEST_ASSERT(vm != nullptr, "LVM_Create failed");

    /* 黑名单排除 skip_me.lua */
    const char* blacklist[] = { "skip_me.lua" };

    int count = LVM_LoadScriptFilesEx(vm, TEST_SCRIPTS_DIR, ".lua",
        blacklist, 1);
    TEST_ASSERT(count >= 3, "Should load scripts except blacklisted ones");

    /* 黑名单中的文件不应被执行 */
    LVM_GetGlobal(vm, "skipped");
    TEST_ASSERT(LVM_IsNil(vm, 1),
        "blacklisted skip_me.lua should not have been executed");
    LVM_SetTop(vm, 0);

    /* 其他文件仍应被加载 */
    LVM_GetGlobal(vm, "test1_executed");
    TEST_ASSERT(LVM_IsBoolean(vm, 1),
        "test1_executed should exist (not blacklisted)");

    LVM_Destroy(vm);
}

TEST(batch_load_blacklist_multiple) {
    void* vm = LVM_Create(1);
    TEST_ASSERT(vm != nullptr, "LVM_Create failed");

    /* 黑名单排除多个文件 */
    const char* blacklist[] = { "skip_me.lua", "init.lua", "test3.lua" };

    int count = LVM_LoadScriptFilesEx(vm, TEST_SCRIPTS_DIR, ".lua",
        blacklist, 3);
    TEST_ASSERT(count >= 2, "Should load non-blacklisted files");

    /* 被排除的文件不应执行 */
    LVM_GetGlobal(vm, "skipped");
    TEST_ASSERT(LVM_IsNil(vm, 1), "skip_me.lua should not execute");
    LVM_SetTop(vm, 0);

    LVM_GetGlobal(vm, "init_executed");
    TEST_ASSERT(LVM_IsNil(vm, 1), "init.lua should not execute");
    LVM_SetTop(vm, 0);

    LVM_GetGlobal(vm, "test3_executed");
    TEST_ASSERT(LVM_IsNil(vm, 1), "test3.lua should not execute");
    LVM_SetTop(vm, 0);

    /* test1.lua 仍在白名单中 */
    LVM_GetGlobal(vm, "test1_executed");
    TEST_ASSERT(LVM_IsBoolean(vm, 1),
        "test1.lua should still execute (not in blacklist)");

    LVM_Destroy(vm);
}

TEST(batch_load_nonexistent_dir) {
    void* vm = LVM_Create(1);
    TEST_ASSERT(vm != nullptr, "LVM_Create failed");

    /* 加载不存在的目录应返回 -1 */
    int count = LVM_LoadScriptFiles(vm, "/nonexistent/path/scripts", ".lua");
    TEST_ASSERT(count == -1, "Nonexistent directory should return -1");

    const char* err = LVM_GetLastError(vm);
    TEST_ASSERT(err != nullptr && std::strlen(err) > 0,
        "Error should be set for nonexistent directory");

    LVM_Destroy(vm);
}

/* ==========================================================================
 * 测试：函数调用 —— LVM_PCall（调用脚本中定义的全局函数和模块内函数）
 * ========================================================================== */

/**
 * @brief 测试：通过 PCall 调用 Lua 脚本中定义的全局函数
 * 核心场景：脚本定义 function add(a,b) return a+b end，然后从 C 侧调用
 */
TEST(pcall_global_function) {
    void* vm = LVM_Create(1);
    TEST_ASSERT(vm != nullptr, "LVM_Create failed");

    /* 1. 执行 Lua 脚本，定义一个全局函数 */
    int result = LVM_ExecuteString(vm,
        "function add(a, b)\n"
        "    return a + b\n"
        "end\n"
    );
    TEST_ASSERT(result == 0, "Script should define add() successfully");

    /* 2. 通过 PCall 调用全局函数 add(10, 20) */
    LVM_GetGlobal(vm, "add");       // 栈: [add]
    LVM_PushNumber(vm, 10.0);       // 栈: [add, 10]
    LVM_PushNumber(vm, 20.0);       // 栈: [add, 10, 20]
    result = LVM_PCall(vm, 2, 1);   // 栈: [30]（2 个参数，1 个返回值）
    TEST_ASSERT(result == 0, "PCall should succeed");

    /* 3. 验证返回值 10 + 20 = 30 */
    TEST_ASSERT(LVM_GetTop(vm) == 1, "Should have 1 result on stack");
    TEST_ASSERT(LVM_IsNumber(vm, -1), "Result should be a number");
    double sum = LVM_ToNumber(vm, -1);
    TEST_ASSERT(std::fabs(sum - 30.0) < 0.0001, "10 + 20 should be 30");

    LVM_Destroy(vm);
}

/**
 * @brief 测试：通过 PCall 调用 Lua 模块（表）内的函数
 * 核心场景：脚本定义 math_ext = { multiply = function(a,b) return a*b end }
 *          然后从 C 侧通过栈操作调用 math_ext.multiply
 */
TEST(pcall_module_function) {
    void* vm = LVM_Create(1);
    TEST_ASSERT(vm != nullptr, "LVM_Create failed");

    /* 1. 执行 Lua 脚本，定义一个包含函数的模块表 */
    int result = LVM_ExecuteString(vm,
        "math_ext = {\n"
        "    multiply = function(a, b)\n"
        "        return a * b\n"
        "    end,\n"
        "    version = '2.0'\n"
        "}\n"
    );
    TEST_ASSERT(result == 0, "Module script should execute successfully");

    /* 2. 通过栈操作调用模块函数 math_ext.multiply(6, 7)
     *    栈操作顺序: 先压入模块表 → 获取函数 → 压参数 → pcall
     */
    LVM_GetGlobal(vm, "math_ext");      // 栈: [math_ext]
    LVM_GetField(vm, -1, "multiply");    // 栈: [math_ext, multiply]
    LVM_PushNumber(vm, 6.0);             // 栈: [math_ext, multiply, 6]
    LVM_PushNumber(vm, 7.0);             // 栈: [math_ext, multiply, 6, 7]
    result = LVM_PCall(vm, 2, 1);        // 栈: [math_ext, 42]
    TEST_ASSERT(result == 0, "Module function call should succeed");

    /* 3. 验证返回值 6 * 7 = 42（结果在栈顶） */
    TEST_ASSERT(LVM_IsNumber(vm, -1), "Result should be a number");
    double product = LVM_ToNumber(vm, -1);
    TEST_ASSERT(std::fabs(product - 42.0) < 0.0001, "6 * 7 should be 42");

    LVM_Destroy(vm);
}

/**
 * @brief 测试：PCall 调用带字符串参数和返回值的函数
 * 验证非数值类型的参数传递和返回值获取
 */
TEST(pcall_with_string_args) {
    void* vm = LVM_Create(1);
    TEST_ASSERT(vm != nullptr, "LVM_Create failed");

    /* 1. 定义字符串拼接函数 */
    int result = LVM_ExecuteString(vm,
        "function concat(a, b)\n"
        "    return a .. ' ' .. b\n"
        "end\n"
    );
    TEST_ASSERT(result == 0, "Script should define concat() successfully");

    /* 2. 调用 concat('Hello', 'World') */
    LVM_GetGlobal(vm, "concat");        // 栈: [concat]
    LVM_PushString(vm, "Hello");        // 栈: [concat, "Hello"]
    LVM_PushString(vm, "World");        // 栈: [concat, "Hello", "World"]
    result = LVM_PCall(vm, 2, 1);       // 栈: ["Hello World"]
    TEST_ASSERT(result == 0, "PCall with string args should succeed");

    /* 3. 验证返回值 */
    TEST_ASSERT(LVM_IsString(vm, -1), "Result should be a string");
    const char* str = LVM_ToString(vm, -1);
    TEST_ASSERT(std::strcmp(str, "Hello World") == 0,
        "concat('Hello', 'World') should be 'Hello World'");

    LVM_Destroy(vm);
}

/**
 * @brief 测试：PCall 调用无参数无返回值的函数
 * 验证最简调用形式的正确性
 */
TEST(pcall_void_function) {
    void* vm = LVM_Create(1);
    TEST_ASSERT(vm != nullptr, "LVM_Create failed");

    /* 1. 定义无参无返回值函数（通过设置全局变量的副作用来验证） */
    int result = LVM_ExecuteString(vm,
        "function set_flag()\n"
        "    flag = true\n"
        "end\n"
    );
    TEST_ASSERT(result == 0, "Script should define set_flag() successfully");

    /* 2. 调用 set_flag() */
    LVM_GetGlobal(vm, "set_flag");      // 栈: [set_flag]
    result = LVM_PCall(vm, 0, 0);       // 栈: []
    TEST_ASSERT(result == 0, "PCall with no args/results should succeed");
    TEST_ASSERT(LVM_GetTop(vm) == 0, "Stack should be empty after void call");

    /* 3. 验证副作用：flag 应该被设置为 true */
    LVM_GetGlobal(vm, "flag");
    TEST_ASSERT(LVM_ToBoolean(vm, 1) == 1, "flag should be true after set_flag()");

    LVM_Destroy(vm);
}

/**
 * @brief 测试：PCall 调用返回多个值的函数
 * 验证多返回值场景（nresults = -1 获取所有返回值）
 */
TEST(pcall_multiple_returns) {
    void* vm = LVM_Create(1);
    TEST_ASSERT(vm != nullptr, "LVM_Create failed");

    /* 1. 定义返回多个值的函数 */
    int result = LVM_ExecuteString(vm,
        "function get_minmax(a, b)\n"
        "    if a < b then return a, b end\n"
        "    return b, a\n"
        "end\n"
    );
    TEST_ASSERT(result == 0, "Script should define get_minmax() successfully");

    /* 2. 调用 get_minmax(10, 3)，期望返回所有值（nresults = -1 = LUA_MULTRET） */
    LVM_GetGlobal(vm, "get_minmax");    // 栈: [get_minmax]
    LVM_PushNumber(vm, 10.0);           // 栈: [get_minmax, 10]
    LVM_PushNumber(vm, 3.0);            // 栈: [get_minmax, 10, 3]
    result = LVM_PCall(vm, 2, -1);      // -1 = LUA_MULTRET: 返回所有值
    TEST_ASSERT(result == 0, "PCall with multiple returns should succeed");

    /* 3. 验证有两个返回值，且顺序正确（先小后大） */
    TEST_ASSERT(LVM_GetTop(vm) == 2, "Should have 2 results on stack");
    double first = LVM_ToNumber(vm, 1);
    double second = LVM_ToNumber(vm, 2);
    TEST_ASSERT(std::fabs(first - 3.0) < 0.0001, "First return should be min = 3");
    TEST_ASSERT(std::fabs(second - 10.0) < 0.0001, "Second return should be max = 10");

    LVM_Destroy(vm);
}

/**
 * @brief 测试：PCall 调用会产生运行时错误的函数
 * 验证错误处理路径：错误信息正确设置且栈被清理
 */
TEST(pcall_runtime_error) {
    void* vm = LVM_Create(1);
    TEST_ASSERT(vm != nullptr, "LVM_Create failed");

    /* 1. 定义一个会出错的函数 */
    int result = LVM_ExecuteString(vm,
        "function bad_func()\n"
        "    error('intentional test error')\n"
        "end\n"
    );
    TEST_ASSERT(result == 0, "Script should define bad_func() successfully");

    /* 2. 调用 bad_func()，预期会失败 */
    LVM_GetGlobal(vm, "bad_func");
    result = LVM_PCall(vm, 0, 0);
    TEST_ASSERT(result != 0, "PCall on error-throwing function should return non-zero");

    /* 3. 验证错误信息被正确记录 */
    const char* err = LVM_GetLastError(vm);
    TEST_ASSERT(err != nullptr && std::strlen(err) > 0,
        "Error message should be set on PCall failure");

    /* 4. 验证栈已被清理（错误处理后栈应为空） */
    TEST_ASSERT(LVM_GetTop(vm) == 0, "Stack should be clean after error");

    LVM_Destroy(vm);
}

/**
 * @brief 测试：PCall 传入 null opaque 的安全性
 */
TEST(pcall_null_opaque) {
    int result = LVM_PCall(nullptr, 0, 0);
    TEST_ASSERT(result == -1, "PCall with null opaque should return -1");
}

/* ==========================================================================
 * 测试：外部函数注册
 * ========================================================================== */

TEST(register_global_function) {
    void* vm = LVM_Create(1);
    TEST_ASSERT(vm != nullptr, "LVM_Create failed");

    // 注册 add 函数为全局变量 "my_add"
    int result = LVM_RegisterFunction(vm, "my_add", test_add_callback);
    TEST_ASSERT(result == 0, "RegisterFunction should return 0 on success");

    // 从 Lua 调用 my_add(10, 20)
    result = LVM_ExecuteString(vm, "sum = my_add(10, 20)");
    TEST_ASSERT(result == 0, "Calling registered function should succeed");

    // 验证结果: 10 + 20 = 30
    LVM_GetGlobal(vm, "sum");
    TEST_ASSERT(LVM_IsNumber(vm, 1), "sum should be a number");
    double sum = LVM_ToNumber(vm, 1);
    TEST_ASSERT(std::fabs(sum - 30.0) < 0.0001, "10 + 20 should be 30");

    LVM_Destroy(vm);
}

TEST(register_function_with_string) {
    void* vm = LVM_Create(1);
    TEST_ASSERT(vm != nullptr, "LVM_Create failed");

    // 注册 greet 函数
    int result = LVM_RegisterFunction(vm, "greet", test_greet_callback);
    TEST_ASSERT(result == 0, "RegisterFunction(greet) should succeed");

    // 从 Lua 调用 greet("World")
    result = LVM_ExecuteString(vm, "msg = greet('World')");
    TEST_ASSERT(result == 0, "Calling greet should succeed");

    // 验证返回值
    LVM_GetGlobal(vm, "msg");
    TEST_ASSERT(LVM_IsString(vm, 1), "msg should be a string");
    const char* msg = LVM_ToString(vm, 1);
    TEST_ASSERT(std::strcmp(msg, "Hello, World!") == 0,
        "greet('World') should return 'Hello, World!'");

    LVM_Destroy(vm);
}

TEST(register_module) {
    void* vm = LVM_Create(1);
    TEST_ASSERT(vm != nullptr, "LVM_Create failed");

    // 注册 math_ext 模块（包含 multiply, get_pi, get_version 三个函数）
    const char* names[] = { "multiply", "get_pi", "get_version" };
    LVM_ExternalFunc funcs[] = {
        test_multiply_callback,
        test_getpi_callback,
        test_getversion_callback
    };

    int result = LVM_RegisterModule(vm, "math_ext", names, funcs, 3);
    TEST_ASSERT(result == 0, "RegisterModule should return 0");

    // 调用 math_ext.multiply(6, 7) → 42
    result = LVM_ExecuteString(vm, "x = math_ext.multiply(6, 7)");
    TEST_ASSERT(result == 0, "module multiply call should succeed");
    LVM_GetGlobal(vm, "x");
    double x = LVM_ToNumber(vm, 1);
    TEST_ASSERT(std::fabs(x - 42.0) < 0.0001, "6 * 7 should be 42");
    LVM_SetTop(vm, 0);

    // 调用 math_ext.get_pi() → 3.14159...
    result = LVM_ExecuteString(vm, "pi = math_ext.get_pi()");
    TEST_ASSERT(result == 0, "module get_pi call should succeed");
    LVM_GetGlobal(vm, "pi");
    double pi = LVM_ToNumber(vm, 1);
    TEST_ASSERT(std::fabs(pi - 3.1415926) < 0.0001, "pi should be ~3.14159");
    LVM_SetTop(vm, 0);

    // 调用 math_ext.get_version() → "1.0.0-test"
    result = LVM_ExecuteString(vm, "v = math_ext.get_version()");
    TEST_ASSERT(result == 0, "module get_version call should succeed");
    LVM_GetGlobal(vm, "v");
    TEST_ASSERT(LVM_IsString(vm, 1), "version should be a string");
    const char* v = LVM_ToString(vm, 1);
    TEST_ASSERT(std::strcmp(v, "1.0.0-test") == 0,
        "get_version should return '1.0.0-test'");

    LVM_Destroy(vm);
}

TEST(register_null_handling) {
    void* vm = LVM_Create(1);
    TEST_ASSERT(vm != nullptr, "LVM_Create failed");

    // 注册时 name 为 null → 返回 -1
    int result = LVM_RegisterFunction(vm, nullptr, test_add_callback);
    TEST_ASSERT(result == -1, "Null name should return -1");

    // 注册时 func 为 null → 返回 -1
    result = LVM_RegisterFunction(vm, "test_func", nullptr);
    TEST_ASSERT(result == -1, "Null func should return -1");

    // 注册模块时 func_names 为 null → 返回 -1
    LVM_ExternalFunc dummy[] = { test_add_callback };
    result = LVM_RegisterModule(vm, "mod", nullptr, dummy, 1);
    TEST_ASSERT(result == -1, "Null func_names should return -1");

    // 注册模块时 count 为 0 → 返回 -1
    result = LVM_RegisterModule(vm, "mod", nullptr, nullptr, 0);
    TEST_ASSERT(result == -1, "Count=0 should return -1");

    LVM_Destroy(vm);
}

TEST(register_global_function_to_null) {
    // 传入 null opaque → 返回 -1 （不崩溃）
    int result = LVM_RegisterFunction(nullptr, "test", test_add_callback);
    TEST_ASSERT(result == -1, "Null opaque should return -1");

    result = LVM_RegisterModule(nullptr, "mod", nullptr, nullptr, 1);
    TEST_ASSERT(result == -1, "Null opaque for module should return -1");
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int main() {
    std::printf("\n");
    std::printf("============================================================\n");
    std::printf("  LuaVM Wrapper — Unit Tests\n");
    std::printf("============================================================\n\n");

    /* 调用所有测试（由 TEST 宏自动注册） */
    // 测试已通过 TEST 宏自动注册，此处仅需重置统计
    // 注意：每个 TEST 宏在 main 之前已执行，若需要在 main 中运行，
    // 可使用不同的测试框架设计。此处重新运行一次测试逻辑。

    std::printf("  All tests executed via static registration.\n\n");

    std::printf("============================================================\n");
    std::printf("  Results: %d passed, %d failed\n",
        g_testsPassed, g_testsFailed);
    std::printf("============================================================\n");

    return g_testsFailed > 0 ? 1 : 0;
}
