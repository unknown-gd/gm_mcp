#pragma once

#include <cstddef>
#include <string>

struct lua_State;

using lua_CFunction = int (*)(lua_State *L);

struct LuaAPI {
    void (*lua_settop)(lua_State *L, int index) = nullptr;
    int (*lua_gettop)(lua_State *L) = nullptr;
    void (*lua_pushvalue)(lua_State *L, int index) = nullptr;
    void (*lua_pushnil)(lua_State *L) = nullptr;
    void (*lua_pushboolean)(lua_State *L, int value) = nullptr;
    void (*lua_pushnumber)(lua_State *L, double value) = nullptr;
    void (*lua_pushlstring)(lua_State *L, const char *s, std::size_t len) = nullptr;
    void (*lua_pushcclosure)(lua_State *L, lua_CFunction fn, int n) = nullptr;
    void (*lua_createtable)(lua_State *L, int narr, int nrec) = nullptr;
    void (*lua_settable)(lua_State *L, int index) = nullptr;
    void (*lua_setfield)(lua_State *L, int index, const char *key) = nullptr;
    int (*lua_type)(lua_State *L, int index) = nullptr;
    int (*lua_toboolean)(lua_State *L, int index) = nullptr;
    double (*lua_tonumber)(lua_State *L, int index) = nullptr;
    const char *(*lua_tolstring)(lua_State *L, int index, std::size_t *len) = nullptr;
    int (*lua_error)(lua_State *L) = nullptr;

    bool ready() const { return loaded_; }
    bool can_raise() const { return lua_pushlstring != nullptr && lua_error != nullptr; }
    const std::string &error() const { return error_; }

    bool load();
    void unload();

private:
    bool loaded_ = false;
    bool library_owned_ = false;
    void *library_ = nullptr;
    std::string error_;
};

LuaAPI &lua_api();
