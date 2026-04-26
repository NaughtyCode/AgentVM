1. 设计一个方案，使用C++ 17将各种类型的lua虚拟机封装成一个库，方便集成到C#中，虚拟机包括：Lua、LuaJIT、Luau，这个库需要有足够的自由度，同时又能将各种类型虚拟机的细节隐藏起来不暴露到外面。方案要具体可直接落地.
2. Public API提供创建和销毁一个Opaque对象，所有其他开放的Public API第一个参数传入这个Opaque对象，内存通过Opaque对象调用实际的虚拟机API

下面地址需要放在方案里面，并说明用处：
lua需要用5.5，下载地址：https://github.com/lua/lua.git
LuaJIT地址：https://github.com/LuaJIT/LuaJIT.git
luau地址：https://github.com/luau-lang/luau.git

