#include "protocol.hpp"

#include <cassert>
#include <cstdio>
#include <optional>
#include <string>
#include <cstdlib>

namespace {

void expect_true(bool condition, const char *label) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", label);
        std::exit(1);
    }
    std::printf("ok: %s\n", label);
}

} // namespace

int main() {
    using mcp::parse_request;

    {
        const auto request = parse_request(R"({"action":"status"})");
        expect_true(request.has_value(), "parses status without token");
        expect_true(request->action == "status", "status action");
        expect_true(!request->token.has_value(), "token absent when omitted");
    }

    {
        const auto request =
            parse_request(R"({"action":"status","token":"secret-token"})");
        expect_true(request.has_value(), "parses status with token");
        expect_true(request->token.has_value(), "token present");
        expect_true(*request->token == "secret-token", "token value preserved");
    }

    {
        const auto request = parse_request(
            R"json({"action":"exec_lua_code","state":"server","code":"print(1)","token":"t"})json");
        expect_true(request.has_value(), "parses exec with token");
        expect_true(request->token == std::optional<std::string>("t"), "exec token");
    }

    {
        const std::string body = mcp::error_response("unauthorized", "Invalid or missing token.");
        expect_true(body.find("\"error\":\"unauthorized\"") != std::string::npos,
                    "unauthorized error code");
        expect_true(body.find("\"ok\":false") != std::string::npos, "unauthorized ok=false");
        expect_true(body.find("Invalid or missing token.") != std::string::npos,
                    "unauthorized message");
    }

    {
        expect_true(!parse_request("not-json").has_value(), "rejects invalid json");
        expect_true(!parse_request(R"({"token":"x"})").has_value(), "rejects missing action");
    }

    std::printf("all protocol tests passed\n");
    return 0;
}
