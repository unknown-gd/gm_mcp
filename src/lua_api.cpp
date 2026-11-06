#include "lua_api.hpp"

#include <cstdio>
#include <cstring>
#include <string>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {

#if defined(_WIN32)
using LibraryHandle = HMODULE;

const char *file_name(const char *path) {
    const char *base = path;
    for (const char *p = path; *p != '\0'; ++p) {
        if (*p == '/' || *p == '\\') {
            base = p + 1;
        }
    }
    return base;
}

LibraryHandle get_loaded_library(const char *path) {
    return GetModuleHandleA(file_name(path));
}

LibraryHandle load_library(const char *path) {
    return LoadLibraryA(path);
}

void close_library(LibraryHandle lib) {
    FreeLibrary(lib);
}

void *resolve_symbol(LibraryHandle lib, const char *name) {
    return reinterpret_cast<void *>(GetProcAddress(lib, name));
}

std::string last_os_error() {
    const DWORD code = GetLastError();
    if (code == 0) {
        return "unknown error";
    }

    char *buffer = nullptr;
    const DWORD size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                          FORMAT_MESSAGE_IGNORE_INSERTS,
                                      nullptr,
                                      code,
                                      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                      reinterpret_cast<LPSTR>(&buffer),
                                      0,
                                      nullptr);

    if (size == 0 || buffer == nullptr) {
        return "Win32 error " + std::to_string(code);
    }

    std::string message(buffer, size);
    LocalFree(buffer);

    while (!message.empty() &&
           (message.back() == '\r' || message.back() == '\n' || message.back() == ' ')) {
        message.pop_back();
    }
    return message;
}
#else
using LibraryHandle = void *;

LibraryHandle get_loaded_library(const char *path) {
    return dlopen(path, RTLD_LAZY | RTLD_LOCAL | RTLD_NOLOAD);
}

LibraryHandle load_library(const char *path) {
    return dlopen(path, RTLD_LAZY | RTLD_LOCAL);
}

void close_library(LibraryHandle lib) {
    dlclose(lib);
}

void *resolve_symbol(LibraryHandle lib, const char *name) {
    return dlsym(lib, name);
}

std::string last_os_error() {
    const char *err = dlerror();
    return err ? err : "unknown error";
}
#endif

struct OpenedLibrary {
    LibraryHandle handle = nullptr;
    // Windows: false for GetModuleHandle (do not FreeLibrary).
    // Linux: always true — every successful dlopen bumps the refcount.
    bool owned = false;
};

const char *const *lua_shared_candidates() {
#if defined(GM_MCP_TARGET_win64)
    static const char *const candidates[] = {
        "lua_shared.dll",
        "bin/win64/lua_shared.dll",
        "garrysmod/bin/win64/lua_shared.dll",
        "bin/lua_shared.dll",
        nullptr,
    };
#elif defined(GM_MCP_TARGET_win32)
    static const char *const candidates[] = {
        "lua_shared.dll",
        "bin/lua_shared.dll",
        "garrysmod/bin/lua_shared.dll",
        nullptr,
    };
#elif defined(GM_MCP_TARGET_linux64)
    static const char *const candidates[] = {
        "lua_shared_srv.so",
        "lua_shared.so",
        "bin/linux64/lua_shared_srv.so",
        "bin/linux64/lua_shared.so",
        "garrysmod/bin/linux64/lua_shared_srv.so",
        "garrysmod/bin/linux64/lua_shared.so",
        "bin/lua_shared_srv.so",
        "bin/lua_shared.so",
        "garrysmod/bin/lua_shared_srv.so",
        "garrysmod/bin/lua_shared.so",
        "../lua_shared_srv.so",
        "../lua_shared.so",
        nullptr,
    };
#else
    static const char *const candidates[] = {
        "lua_shared_srv.so",
        "lua_shared.so",
        "bin/linux32/lua_shared_srv.so",
        "bin/linux32/lua_shared.so",
        "garrysmod/bin/lua_shared_srv.so",
        "garrysmod/bin/lua_shared.so",
        "bin/lua_shared_srv.so",
        "bin/lua_shared.so",
        "../lua_shared_srv.so",
        "../lua_shared.so",
        nullptr,
    };
#endif
    return candidates;
}

OpenedLibrary find_lua_shared(std::string &error) {
    std::string attempts;

    for (const char *const *path = lua_shared_candidates(); *path != nullptr; ++path) {
        if (LibraryHandle lib = get_loaded_library(*path)) {
#if defined(_WIN32)
            return {lib, false};
#else
            return {lib, true};
#endif
        }

        if (LibraryHandle lib = load_library(*path)) {
            return {lib, true};
        }

        if (!attempts.empty()) {
            attempts += "; ";
        }
        attempts += *path;
        attempts += " (";
        attempts += last_os_error();
        attempts += ")";
    }

    error = "could not load lua_shared; tried: " + attempts;
    return {};
}

template <typename T>
bool bind(LibraryHandle lib, const char *name, T &out, std::string &error) {
#if !defined(_WIN32)
    dlerror();
#endif
    void *symbol = resolve_symbol(lib, name);
    if (!symbol) {
        error = std::string("missing required symbol '") + name + "' in lua_shared (" +
                last_os_error() + ")";
        return false;
    }
    out = reinterpret_cast<T>(symbol);
    return true;
}

} // namespace

bool LuaAPI::load() {
    if (loaded_) {
        return true;
    }

    unload();
    error_.clear();

    const OpenedLibrary opened = find_lua_shared(error_);
    if (!opened.handle) {
        std::fprintf(stderr, "mcp: %s\n", error_.c_str());
        return false;
    }

    library_ = opened.handle;
    library_owned_ = opened.owned;

    auto *const lib = static_cast<LibraryHandle>(library_);

    // Bind raise helpers first so gmod13_open can still throw on later failures.
    if (!bind(lib, "lua_pushlstring", lua_pushlstring, error_) ||
        !bind(lib, "lua_error", lua_error, error_)) {
        std::fprintf(stderr, "mcp: %s\n", error_.c_str());
        unload();
        return false;
    }

    const bool ok =
        bind(lib, "lua_settop", lua_settop, error_) &&
        bind(lib, "lua_gettop", lua_gettop, error_) &&
        bind(lib, "lua_pushvalue", lua_pushvalue, error_) &&
        bind(lib, "lua_pushnil", lua_pushnil, error_) &&
        bind(lib, "lua_pushboolean", lua_pushboolean, error_) &&
        bind(lib, "lua_pushnumber", lua_pushnumber, error_) &&
        bind(lib, "lua_pushcclosure", lua_pushcclosure, error_) &&
        bind(lib, "lua_createtable", lua_createtable, error_) &&
        bind(lib, "lua_settable", lua_settable, error_) &&
        bind(lib, "lua_setfield", lua_setfield, error_) &&
        bind(lib, "lua_type", lua_type, error_) &&
        bind(lib, "lua_toboolean", lua_toboolean, error_) &&
        bind(lib, "lua_tonumber", lua_tonumber, error_) &&
        bind(lib, "lua_tolstring", lua_tolstring, error_);

    if (!ok) {
        std::fprintf(stderr, "mcp: %s\n", error_.c_str());
        // Keep raise helpers + library so gmod13_open can report via lua_error.
        return false;
    }

    loaded_ = true;
    return true;
}

void LuaAPI::unload() {
    if (library_ != nullptr && library_owned_) {
        close_library(static_cast<LibraryHandle>(library_));
    }

    library_ = nullptr;
    library_owned_ = false;
    loaded_ = false;

    lua_settop = nullptr;
    lua_gettop = nullptr;
    lua_pushvalue = nullptr;
    lua_pushnil = nullptr;
    lua_pushboolean = nullptr;
    lua_pushnumber = nullptr;
    lua_pushlstring = nullptr;
    lua_pushcclosure = nullptr;
    lua_createtable = nullptr;
    lua_settable = nullptr;
    lua_setfield = nullptr;
    lua_type = nullptr;
    lua_toboolean = nullptr;
    lua_tonumber = nullptr;
    lua_tolstring = nullptr;
    lua_error = nullptr;
}

LuaAPI &lua_api() {
    static LuaAPI api;
    return api;
}
