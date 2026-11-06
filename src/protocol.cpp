#include "protocol.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>

namespace mcp {
namespace {

using nlohmann::json;

std::optional<std::string> optional_string(const json &root, const char *key) {
    const auto it = root.find(key);
    if (it == root.end() || it->is_null()) {
        return std::nullopt;
    }
    if (!it->is_string()) {
        return std::nullopt;
    }
    return it->get<std::string>();
}

std::optional<bool> optional_bool(const json &root, const char *key) {
    const auto it = root.find(key);
    if (it == root.end() || it->is_null()) {
        return std::nullopt;
    }
    if (!it->is_boolean()) {
        return std::nullopt;
    }
    return it->get<bool>();
}

std::optional<std::uint32_t> optional_u32(const json &root, const char *key) {
    const auto it = root.find(key);
    if (it == root.end() || it->is_null()) {
        return std::nullopt;
    }
    if (!it->is_number()) {
        return std::nullopt;
    }
    const double number = it->get<double>();
    if (number < 0.0 || number > static_cast<double>(UINT32_MAX)) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(number);
}

} // namespace

std::string trim_copy(std::string_view value) {
    std::size_t begin = 0;
    while (begin < value.size() &&
           std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }
    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }
    return std::string(value.substr(begin, end - begin));
}

bool is_broadcast_target(std::string_view target) {
    std::string lowered = trim_copy(target);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return lowered == "all" || lowered == "tout le monde";
}

std::optional<BridgeRequest> parse_request(std::string_view raw) {
    json root;
    try {
        root = json::parse(raw);
    } catch (const json::exception &) {
        return std::nullopt;
    }

    if (!root.is_object()) {
        return std::nullopt;
    }

    const auto action = optional_string(root, "action");
    if (!action) {
        return std::nullopt;
    }

    BridgeRequest request;
    request.action = std::move(*action);
    request.state = optional_string(root, "state");
    request.code = optional_string(root, "code");
    request.target = optional_string(root, "target");
    request.class_filter = optional_string(root, "class_filter");
    request.custom_check = optional_string(root, "custom_check");
    if (!request.custom_check) {
        // Accept camelCase from some clients.
        request.custom_check = optional_string(root, "customCheck");
    }
    request.limit = optional_u32(root, "limit");
    request.return_result = optional_bool(root, "return_result");
    return request;
}

std::string command_response(bool ok, std::string_view message) {
    return json{{"ok", ok}, {"message", message}}.dump();
}

std::string status_response(bool connected, std::string_view message) {
    return json{{"connected", connected}, {"message", message}}.dump();
}

std::string exec_result_response(bool ok, std::string_view payload_or_error) {
    if (!ok) {
        return command_response(false, payload_or_error);
    }

    json wrapper;
    try {
        wrapper = json::parse(payload_or_error);
    } catch (const json::exception &) {
        return command_response(false, "Lua result payload is invalid JSON.");
    }

    if (!wrapper.is_object() || !wrapper.contains("value")) {
        return command_response(false, "Lua result payload is missing its value.");
    }

    return json{{"ok", true}, {"result", wrapper.at("value")}}.dump();
}

std::string screenshot_header(bool ok, std::size_t size, std::string_view message) {
    if (!ok) {
        return command_response(false, message.empty() ? "Screenshot failed." : message);
    }
    return json{{"ok", true}, {"format", "jpeg"}, {"size", size}}.dump();
}

} // namespace mcp
