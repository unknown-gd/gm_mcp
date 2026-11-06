#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace mcp {

inline constexpr std::string_view kDefaultBindHost = "127.0.0.1";
inline constexpr std::uint16_t kDefaultBindPort = 17905;

inline constexpr std::size_t kMaxLuaCodeBytes = 60 * 1024;
inline constexpr std::size_t kMaxTargetBytes = 256;
inline constexpr std::uint32_t kMaxEntityLimit = 200;
inline constexpr std::size_t kMaxScreenshotBytes = 1024 * 1024;

inline constexpr const char *kActionStatus = "status";
inline constexpr const char *kActionExecLua = "exec_lua_code";
inline constexpr const char *kActionPlayers = "gmod_players";
inline constexpr const char *kActionEntities = "gmod_entities";
inline constexpr const char *kActionScreenshot = "screenshot";
inline constexpr const char *kToolStatus = "gmod_status";

inline constexpr const char *kDefaultStatusMessage = "Garry's Mod is unavailable.";

struct Command {
    std::string tool;
    std::string state;
    std::string code;
    std::optional<std::string> target;
    std::optional<std::string> class_filter;
    std::optional<std::string> custom_check;
    std::optional<std::uint32_t> limit;
    std::optional<std::string> request_id;
    bool return_result = false;
};

struct BridgeRequest {
    std::string action;
    std::optional<std::string> state;
    std::optional<std::string> code;
    std::optional<std::string> target;
    std::optional<std::string> class_filter;
    std::optional<std::string> custom_check;
    std::optional<std::uint32_t> limit;
    std::optional<bool> return_result;
};

std::optional<BridgeRequest> parse_request(std::string_view raw);

std::string command_response(bool ok, std::string_view message);
std::string status_response(bool connected, std::string_view message);
std::string exec_result_response(bool ok, std::string_view payload_or_error);
std::string screenshot_header(bool ok, std::size_t size, std::string_view message = {});

std::string trim_copy(std::string_view value);
bool is_broadcast_target(std::string_view target);

} // namespace mcp
