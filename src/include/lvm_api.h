/**
 * @file    lvm_api.h
 * @brief   Lua VM Wrapper — 公共 C ABI 接口定义
 *
 * 本头文件定义了 Lua 虚拟机封装库的完整 Public API。
 * 所有函数均使用 C 调用约定（extern "C"），参数均为 blittable 类型，
 * 适用于 .NET P/Invoke 等跨语言互操作场景。
 *
 * 设计原则：
 * 1. 所有函数第一个参数为 void* opaque（不透明句柄）
 * 2. 不暴露任何内部类型（lua_State、Backend 等）
 * 3. 所有返回字符串均指向内部线程安全缓冲区
 * 4. 线程安全：不同 opaque 实例可并发使用
 *
 * 支持的虚拟机后端（通过 LVM_Create 的 type 参数选择）：
 *   type = 1 → Lua 5.5 (https://github.com/lua/lua.git)
 *   type = 2 → LuaJIT  (https://github.com/LuaJIT/LuaJIT.git)
 *   type = 3 → Luau    (https://github.com/luau-lang/luau.git)
 */

#ifndef LVM_API_H
#define LVM_API_H

/* --------------------------------------------------------------------------
 * 平台导出宏
 * -------------------------------------------------------------------------- */
#ifdef _WIN32
  #define LVM_API __declspec(dllexport)
#else
  #define LVM_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------------------
 * 生命周期管理
 * -------------------------------------------------------------------------- */

/**
 * @brief 创建虚拟机实例
 * @param type  虚拟机类型: 1 = Lua 5.5, 2 = LuaJIT, 3 = Luau
 * @return 成功返回 opaque 句柄，失败返回 nullptr
 * @note   通过 LVM_GetLastError() 获取失败原因
 */
LVM_API void* LVM_Create(int type);

/**
 * @brief 销毁虚拟机实例，释放所有资源
 * @param opaque  由 LVM_Create 返回的句柄
 * @note  传入 nullptr 为安全空操作; 销毁后句柄失效
 */
LVM_API void  LVM_Destroy(void* opaque);

/**
 * @brief 获取最后一次错误信息
 * @param opaque  虚拟机句柄
 * @return 指向 opaque 实例内错误缓冲区的字符串（只读）
 * @note  返回值在下次同 opaque 实例操作时被覆盖；不同 opaque 实例间互不影响
 */
LVM_API const char* LVM_GetLastError(void* opaque);

/* --------------------------------------------------------------------------
 * 脚本执行
 * -------------------------------------------------------------------------- */

/**
 * @brief 执行 Lua 源码字符串
 * @param opaque  虚拟机句柄
 * @param code    Lua 源码（以 '\0' 结尾的 UTF-8 字符串）
 * @return 0 表示成功，非 0 表示失败（错误信息通过 LVM_GetLastError 获取）
 */
LVM_API int   LVM_ExecuteString(void* opaque, const char* code);

/**
 * @brief 执行 Lua 脚本文件
 * @param opaque    虚拟机句柄
 * @param filepath  脚本文件路径（UTF-8 编码）
 * @return 0 表示成功，非 0 表示失败（错误信息通过 LVM_GetLastError 获取）
 */
LVM_API int   LVM_ExecuteFile(void* opaque, const char* filepath);

/* --------------------------------------------------------------------------
 * 批量脚本加载 —— 从指定目录加载脚本文件
 * -------------------------------------------------------------------------- */

/**
 * @brief 从指定目录加载所有匹配后缀的 Lua 脚本文件
 * @param opaque   虚拟机句柄
 * @param dirpath  目标目录路径
 * @param suffix   文件后缀过滤器（如 ".lua"），传 nullptr 默认为 ".lua"
 * @return 成功加载的文件数量（>= 0），失败返回 -1
 * @note  文件按文件名排序后依次执行；单个文件执行失败不中断后续文件加载
 * @note  错误信息通过 LVM_GetLastError 获取（最后失败的文件的错误）
 */
LVM_API int   LVM_LoadScriptFiles(void* opaque, const char* dirpath, const char* suffix);

/**
 * @brief 从指定目录加载匹配后缀的 Lua 脚本文件（带黑名单过滤）
 * @param opaque         虚拟机句柄
 * @param dirpath        目标目录路径
 * @param suffix         文件后缀过滤器，传 nullptr 默认为 ".lua"
 * @param blacklist      需要排除的文件名数组（仅匹配文件名，不含路径）
 * @param blacklist_len  黑名单数组长度
 * @return 成功加载的文件数量（>= 0），失败返回 -1
 * @note  黑名单中的文件名将被跳过，支持精确匹配（含后缀）
 * @note  若 blacklist 为 nullptr 或 blacklist_len 为 0，行为等同于 LVM_LoadScriptFiles
 */
LVM_API int   LVM_LoadScriptFilesEx(void* opaque, const char* dirpath, const char* suffix,
                                     const char* const* blacklist, int blacklist_len);

/* --------------------------------------------------------------------------
 * 栈操作 —— 数值直接返回，无需解析 IntPtr
 * -------------------------------------------------------------------------- */

/**
 * @brief 获取当前栈顶索引
 * @return 栈中元素个数（栈顶索引）
 */
LVM_API int    LVM_GetTop(void* opaque);

/**
 * @brief 设置栈顶索引（可截断或扩展栈）
 * @param index  新的栈顶索引（>= 0）
 */
LVM_API void   LVM_SetTop(void* opaque, int index);

/* --------------------------------------------------------------------------
 * 压栈操作 —— 支持基本类型
 * -------------------------------------------------------------------------- */

/** @brief 将 double 压入栈顶 */
LVM_API void   LVM_PushNumber(void* opaque, double value);

/** @brief 将字符串压入栈顶 */
LVM_API void   LVM_PushString(void* opaque, const char* str);

/** @brief 将布尔值压入栈顶（0 = false, 非 0 = true） */
LVM_API void   LVM_PushBoolean(void* opaque, int value);

/** @brief 将 nil 压入栈顶 */
LVM_API void   LVM_PushNil(void* opaque);

/* --------------------------------------------------------------------------
 * 取值操作 —— 按 index 获取栈上元素
 * -------------------------------------------------------------------------- */

/** @brief 检查栈 index 处是否为数字 */
LVM_API int    LVM_IsNumber(void* opaque, int index);

/** @brief 检查栈 index 处是否为字符串 */
LVM_API int    LVM_IsString(void* opaque, int index);

/** @brief 检查栈 index 处是否为布尔值 */
LVM_API int    LVM_IsBoolean(void* opaque, int index);

/** @brief 检查栈 index 处是否为 nil */
LVM_API int    LVM_IsNil(void* opaque, int index);

/** @brief 将栈 index 处数值转为 double 返回 */
LVM_API double LVM_ToNumber(void* opaque, int index);

/** @brief 将栈 index 处值转为字符串（返回内部缓冲区） */
LVM_API const char* LVM_ToString(void* opaque, int index);

/** @brief 将栈 index 处值转为布尔值返回（0/1） */
LVM_API int    LVM_ToBoolean(void* opaque, int index);

/* --------------------------------------------------------------------------
 * 全局变量访问
 * -------------------------------------------------------------------------- */

/**
 * @brief 将全局变量 name 的值压入栈顶
 * @param opaque  虚拟机句柄
 * @param name    全局变量名
 */
LVM_API void   LVM_GetGlobal(void* opaque, const char* name);

/**
 * @brief 从栈顶取值并设置为全局变量 name
 * @param opaque  虚拟机句柄
 * @param name    全局变量名
 * @note  调用后栈顶元素被弹出
 */
LVM_API void   LVM_SetGlobal(void* opaque, const char* name);

/* --------------------------------------------------------------------------
 * 表操作
 * -------------------------------------------------------------------------- */

/** @brief 创建一个新表并压入栈顶 */
LVM_API void   LVM_NewTable(void* opaque);

/**
 * @brief 从栈 index 处的表中获取 key 字段并压入栈顶
 * @param opaque  虚拟机句柄
 * @param index   表在栈中的索引
 * @param key     字段名
 */
LVM_API void   LVM_GetField(void* opaque, int index, const char* key);

/**
 * @brief 将栈顶值设入栈 index 处表的 key 字段（弹出栈顶值）
 * @param opaque  虚拟机句柄
 * @param index   表在栈中的索引
 * @param key     字段名
 */
LVM_API void   LVM_SetField(void* opaque, int index, const char* key);

/* --------------------------------------------------------------------------
 * 函数调用 —— 调用 Lua 脚本中定义的函数（全局函数 / 模块内函数）
 * -------------------------------------------------------------------------- */

/**
 * @brief 以保护模式调用栈上的 Lua 函数
 * @param opaque    虚拟机句柄
 * @param nargs     传递给函数的参数个数
 * @param nresults  期望的返回值个数（LUA_MULTRET = -1 表示返回所有结果）
 * @return 0 = 成功（结果在栈顶），非 0 = 运行时错误
 * @note  调用前栈布局: [..., func, arg1, ..., argN]
 *         调用后栈布局: [..., result1, ..., resultM]
 * @note  若函数执行出错，错误信息通过 LVM_GetLastError() 获取
 *
 * 使用示例（调用全局函数 "add(10, 20)"）：
 * @code
 *   LVM_GetGlobal(vm, "add");      // 压入函数
 *   LVM_PushNumber(vm, 10);        // 压入参数 1
 *   LVM_PushNumber(vm, 20);        // 压入参数 2
 *   int ret = LVM_PCall(vm, 2, 1); // 调用，2 个参数，1 个返回值
 *   double result = LVM_ToNumber(vm, -1);  // 获取结果
 * @endcode
 *
 * 使用示例（调用模块函数 "math_ext.multiply(6, 7)"）：
 * @code
 *   LVM_GetGlobal(vm, "math_ext");     // 压入模块表
 *   LVM_GetField(vm, -1, "multiply");   // 压入模块内函数
 *   LVM_PushNumber(vm, 6);              // 压入参数 1
 *   LVM_PushNumber(vm, 7);              // 压入参数 2
 *   int ret = LVM_PCall(vm, 2, 1);      // 调用，2 个参数，1 个返回值
 *   double result = LVM_ToNumber(vm, -1); // 获取结果
 * @endcode
 */
LVM_API int LVM_PCall(void* opaque, int nargs, int nresults);

/* --------------------------------------------------------------------------
 * 外部函数注册 —— 将 C/C++ 函数注册为 Lua 全局函数或模块
 * -------------------------------------------------------------------------- */

/**
 * @brief 外部函数回调类型
 * @param opaque  虚拟机句柄（回调中通过 Public API 读取参数、压入返回值）
 * @return 压入栈中的返回值数量（0 = 无返回值，非零 = 返回值数量）
 * @note  回调通过 LVM_ToNumber / LVM_ToString 等读取 Lua 传入的参数（栈索引 1..N）
 *        回调通过 LVM_PushNumber / LVM_PushString 等压入返回值至栈顶
 *        返回值应当与压入的返回值数量一致
 */
typedef int (*LVM_ExternalFunc)(void* opaque);

/**
 * @brief 注册单个外部函数为 Lua 全局变量
 * @param opaque  虚拟机句柄
 * @param name    Lua 全局变量名（即 Lua 中调用此函数的名字）
 * @param func    回调函数指针
 * @return 0 = 成功，-1 = 失败（通过 LVM_GetLastError 获取详情）
 */
LVM_API int LVM_RegisterFunction(void* opaque, const char* name, LVM_ExternalFunc func);

/**
 * @brief 注册一批外部函数为一个 Lua 模块表
 * @param opaque       虚拟机句柄
 * @param module_name  模块名称（Lua 全局变量名，其值为一个表）
 * @param func_names   函数名称数组（模块表中各函数的字段名）
 * @param funcs        回调函数指针数组（与 func_names 一一对应）
 * @param count        函数数量
 * @return 0 = 成功，-1 = 失败
 * @note  调用后在 Lua 中可通过 module_name.func_names[i]() 调用对应函数
 */
LVM_API int LVM_RegisterModule(void* opaque, const char* module_name,
                                const char* const* func_names,
                                const LVM_ExternalFunc* funcs, int count);

#ifdef __cplusplus
}
#endif

#endif /* LVM_API_H */
