#pragma once

#include "lua_api.hpp"

#include <cstddef>
#include <string_view>
#include <utility>

inline constexpr int LUA_GLOBALSINDEX = -10002;
inline constexpr int LUA_TNIL = 0;
inline constexpr int LUA_TBOOLEAN = 1;
inline constexpr int LUA_TNUMBER = 3;
inline constexpr int LUA_TSTRING = 4;
inline constexpr int LUA_TTABLE = 5;
inline constexpr int LUA_TFUNCTION = 6;

struct LuaState {
    lua_State *L = nullptr;

    explicit LuaState(lua_State *state) : L(state) {}

    int get_top() const { return lua_api().lua_gettop(L); }

    void pop(int count = 1) const { set_top(-count - 1); }

    void set_top(int index) const { lua_api().lua_settop(L, index); }

    void push_value(int index) const { lua_api().lua_pushvalue(L, index); }

    void push_globals() const { push_value(LUA_GLOBALSINDEX); }

    void push_nil() const { lua_api().lua_pushnil(L); }

    void push_boolean(bool value) const { lua_api().lua_pushboolean(L, value ? 1 : 0); }

    void push_number(double value) const { lua_api().lua_pushnumber(L, value); }

    void push_lstring(const char *data, std::size_t len) const {
        lua_api().lua_pushlstring(L, data, len);
    }

    void push_string(std::string_view data) const { push_lstring(data.data(), data.size()); }

    void push_function(lua_CFunction func) const { lua_api().lua_pushcclosure(L, func, 0); }

    void new_table() const { lua_api().lua_createtable(L, 0, 0); }

    void set_table(int index) const { lua_api().lua_settable(L, index); }

    void set_field(int index, const char *key) const { lua_api().lua_setfield(L, index, key); }

    int type(int index) const { return lua_api().lua_type(L, index); }

    bool is_nil(int index) const { return type(index) == LUA_TNIL; }

    bool is_boolean(int index) const { return type(index) == LUA_TBOOLEAN; }

    bool is_string(int index) const {
        const int t = type(index);
        return t == LUA_TSTRING || t == LUA_TNUMBER;
    }

    bool to_boolean(int index) const { return lua_api().lua_toboolean(L, index) != 0; }

    // Returns {pointer, length}. pointer is null if the value is not a string.
    std::pair<const char *, std::size_t> get_string_data(int index) const {
        std::size_t len = 0;
        const char *ptr = lua_api().lua_tolstring(L, index, &len);
        return {ptr, len};
    }

    // Raises a Lua error from the value currently on top of the stack.
    // Uses longjmp — destroy C++ locals with non-trivial destructors first.
    [[noreturn]] void raise() const {
        lua_api().lua_error(L);
#if defined(_MSC_VER)
        __assume(0);
#else
        __builtin_unreachable();
#endif
    }

    // Pushes message, then raises. Safe with temporary std::string_view only.
    [[noreturn]] void error(std::string_view message) const {
        push_string(message);
        raise();
    }
};
