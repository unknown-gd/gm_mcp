#include "bridge.hpp"
#include "lua.hpp"
#include "protocol.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define MCP_EXPORT extern "C" __declspec(dllexport)
#else
#define MCP_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace {

[[noreturn]] void raise_message(LuaState &state, std::string message) {
    state.push_string(message);
    std::string{}.swap(message);
    state.raise();
}

bool check_boolean_arg(LuaState &state, int index, const char *name) {
    if (state.type(index) != LUA_TBOOLEAN) {
        std::string message = "mcp.";
        message += name;
        message += ": argument #";
        message += std::to_string(index);
        message += " must be a boolean";
        raise_message(state, std::move(message));
    }
    return state.to_boolean(index);
}

std::pair<const char *, std::size_t> check_lstring_arg(LuaState &state, int index, const char *name) {
    auto [data, len] = state.get_string_data(index);
    if (!data) {
        std::string message = "mcp.";
        message += name;
        message += ": argument #";
        message += std::to_string(index);
        message += " must be a string";
        raise_message(state, std::move(message));
    }
    return {data, len};
}

std::string check_string_arg(LuaState &state, int index, const char *name) {
    auto [data, len] = check_lstring_arg(state, index, name);
    return std::string(data, len);
}

void push_optional_string(LuaState &state, const std::optional<std::string> &value) {
    if (value) {
        state.push_string(*value);
    } else {
        state.push_nil();
    }
}

void push_command_table(LuaState &state, const mcp::Command &command) {
    state.new_table();

    state.push_string(command.tool);
    state.set_field(-2, "tool");

    state.push_string(command.state);
    state.set_field(-2, "state");

    state.push_string(command.code);
    state.set_field(-2, "code");

    push_optional_string(state, command.target);
    state.set_field(-2, "target");

    push_optional_string(state, command.class_filter);
    state.set_field(-2, "classFilter");

    push_optional_string(state, command.custom_check);
    state.set_field(-2, "customCheck");

    if (command.limit) {
        state.push_number(static_cast<double>(*command.limit));
    } else {
        state.push_nil();
    }
    state.set_field(-2, "limit");

    push_optional_string(state, command.request_id);
    state.set_field(-2, "requestId");

    state.push_boolean(command.return_result);
    state.set_field(-2, "returnResult");
}

int lua_set_status(lua_State *L) {
    LuaState state(L);
    const bool connected = check_boolean_arg(state, 1, "setStatus");
    std::string message = check_string_arg(state, 2, "setStatus");
    mcp::Bridge::instance().set_status(connected, std::move(message));
    return 0;
}

int lua_get_status(lua_State *L) {
    LuaState state(L);
    bool connected = false;
    std::string message;
    mcp::Bridge::instance().get_status(connected, message);

    state.new_table();
    state.push_boolean(connected);
    state.set_field(-2, "connected");
    state.push_string(message);
    state.set_field(-2, "message");
    return 1;
}

int lua_shutdown(lua_State *L) {
    (void)L;
    mcp::Bridge::instance().stop();
    mcp::Bridge::instance().clear_commands();
    mcp::Bridge::instance().fail_pending(mcp::kDefaultStatusMessage);
    return 0;
}

int lua_running(lua_State *L) {
    LuaState state(L);
    state.push_boolean(mcp::Bridge::instance().running());
    return 1;
}

int lua_poll_command(lua_State *L) {
    LuaState state(L);
    auto command = mcp::Bridge::instance().poll_command();
    if (!command) {
        state.push_nil();
        return 1;
    }
    push_command_table(state, *command);
    return 1;
}

int lua_respond_lua(lua_State *L) {
    LuaState state(L);
    std::string request_id = check_string_arg(state, 1, "respondLua");
    const bool ok = check_boolean_arg(state, 2, "respondLua");
    std::string payload = check_string_arg(state, 3, "respondLua");
    mcp::Bridge::instance().complete_exec(request_id, ok, std::move(payload));
    return 0;
}

int lua_respond_error(lua_State *L) {
    LuaState state(L);
    const int top = state.get_top();

    bool delivered = false;
    if (top >= 2) {
        std::string request_id = check_string_arg(state, 1, "respondError");
        std::string message = check_string_arg(state, 2, "respondError");
        delivered = mcp::Bridge::instance().report_error(request_id, std::move(message));
    } else {
        std::string message = check_string_arg(state, 1, "respondError");
        delivered = mcp::Bridge::instance().report_error(std::move(message));
    }

    state.push_boolean(delivered);
    return 1;
}

int lua_respond_screenshot(lua_State *L) {
    LuaState state(L);
    std::string request_id = check_string_arg(state, 1, "respondScreenshot");
    const bool ok = check_boolean_arg(state, 2, "respondScreenshot");

    if (ok) {
        auto [data, len] = check_lstring_arg(state, 3, "respondScreenshot");
        if (len == 0) {
            mcp::Bridge::instance().complete_screenshot(request_id, false, {}, "Screenshot is empty.");
            return 0;
        }
        if (len > mcp::kMaxScreenshotBytes) {
            mcp::Bridge::instance().complete_screenshot(
                request_id,
                false,
                {},
                "Screenshot exceeds the " + std::to_string(mcp::kMaxScreenshotBytes) +
                    " byte limit.");
            return 0;
        }
        std::vector<std::uint8_t> bytes(data, data + len);
        mcp::Bridge::instance().complete_screenshot(request_id, true, std::move(bytes), {});
        return 0;
    }

    std::string error = check_string_arg(state, 3, "respondScreenshot");
    mcp::Bridge::instance().complete_screenshot(request_id, false, {}, std::move(error));
    return 0;
}

std::string env_or(const char *name, std::string fallback) {
    if (const char *value = std::getenv(name)) {
        if (value[0] != '\0') {
            return value;
        }
    }
    return fallback;
}

std::uint16_t env_port_or(const char *name, std::uint16_t fallback) {
    if (const char *value = std::getenv(name)) {
        if (value[0] == '\0') {
            return fallback;
        }
        try {
            const int parsed = std::stoi(value);
            if (parsed > 0 && parsed <= 65535) {
                return static_cast<std::uint16_t>(parsed);
            }
        } catch (...) {
        }
    }
    return fallback;
}

void register_mcp_library(LuaState &state) {
    state.new_table();

    state.push_function(lua_set_status);
    state.set_field(-2, "setStatus");

    state.push_function(lua_get_status);
    state.set_field(-2, "getStatus");

    state.push_function(lua_shutdown);
    state.set_field(-2, "shutdown");

    state.push_function(lua_running);
    state.set_field(-2, "isRunning");

    state.push_function(lua_poll_command);
    state.set_field(-2, "pollCommand");

    state.push_function(lua_respond_lua);
    state.set_field(-2, "respondLua");

    state.push_function(lua_respond_error);
    state.set_field(-2, "respondError");

    state.push_function(lua_respond_screenshot);
    state.set_field(-2, "respondScreenshot");

    // Expose as global `mcp` and leave the table on the stack for require().
    state.push_globals();     // table, globals
    state.push_string("mcp"); // table, globals, key
    state.push_value(-3);     // table, globals, key, table
    state.set_table(-3);      // table, globals
    state.pop();              // table
}

} // namespace

MCP_EXPORT int gmod13_open(lua_State *L) {
    LuaAPI &api = lua_api();

    if (!api.load()) {
        const char *detail =
            api.error().empty() ? "Lua API is not initialized" : api.error().c_str();
        std::fprintf(stderr, "mcp: refusing to register module (%s)\n", detail);

        if (api.can_raise()) {
            LuaState state(L);
            std::string message = "mcp: failed to initialize (";
            message += detail;
            message += ")";
            raise_message(state, std::move(message));
        }
        return 0;
    }

    mcp::Bridge::instance().clear_commands();
    mcp::Bridge::instance().fail_pending(mcp::kDefaultStatusMessage);

    const std::string host = env_or("GM_MCP_HOST", std::string(mcp::kDefaultBindHost));
    const std::uint16_t port = env_port_or("GM_MCP_PORT", mcp::kDefaultBindPort);
    if (!mcp::Bridge::instance().start(host, port)) {
        std::fprintf(stderr, "mcp: failed to start bridge on %s:%u\n", host.c_str(),
                     static_cast<unsigned>(port));
    }

    LuaState state(L);
    register_mcp_library(state);
    return 1;
}

MCP_EXPORT int gmod13_close(lua_State * /*L*/) {
    mcp::Bridge::instance().stop();
    mcp::Bridge::instance().clear_commands();
    mcp::Bridge::instance().fail_pending(mcp::kDefaultStatusMessage);
    lua_api().unload();
    return 0;
}
