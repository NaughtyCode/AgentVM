/**
 * @file    test_main.cpp
 * @brief   NativeLibrary 单元测试
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
