#include "bridge.hpp"
#include "protocol.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {

std::atomic<bool> g_stop{false};

void on_signal(int) { g_stop.store(true, std::memory_order_release); }

void auto_complete_loop(mcp::Bridge &bridge) {
    while (!g_stop.load(std::memory_order_acquire) && bridge.running()) {
        auto command = bridge.poll_command();
        if (!command) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        if (!command->request_id) {
            continue;
        }

        if (command->tool == mcp::kActionScreenshot) {
            static const std::uint8_t kJpeg[] = {0xff, 0xd8, 0xff, 0xd9};
            bridge.complete_screenshot(*command->request_id, true,
                                       std::vector<std::uint8_t>(kJpeg, kJpeg + sizeof(kJpeg)),
                                       {});
            continue;
        }

        // Waiting Lua / query tools expect {"value": ...}
        bridge.complete_exec(*command->request_id, true, R"({"value":[{"name":"Bot"}]})");
    }
}

} // namespace

int main(int argc, char **argv) {
    std::uint16_t port = 17915;
    bool auto_complete = true;
    std::string token;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            port = static_cast<std::uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--token" && i + 1 < argc) {
            token = argv[++i];
        } else if (arg == "--no-autocomplete") {
            auto_complete = false;
        } else if (arg == "--help") {
            std::printf("Usage: bridge_harness [--port N] [--token SECRET] [--no-autocomplete]\n");
            return 0;
        }
    }

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    auto &bridge = mcp::Bridge::instance();
    if (!token.empty()) {
        bridge.set_auth_token(token);
    }
    bridge.set_status(true, "Garry's Mod is connected.");

    if (!bridge.start("127.0.0.1", port)) {
        std::fprintf(stderr, "bridge_harness: failed to start on 127.0.0.1:%u\n",
                     static_cast<unsigned>(port));
        return 1;
    }

    std::printf("READY 127.0.0.1:%u\n", static_cast<unsigned>(port));
    std::fflush(stdout);

    std::thread completer;
    if (auto_complete) {
        completer = std::thread([&] { auto_complete_loop(bridge); });
    }

    while (!g_stop.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    bridge.stop();
    if (completer.joinable()) {
        completer.join();
    }
    return 0;
}
