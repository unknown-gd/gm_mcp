#pragma once

#include "protocol.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mcp {

struct Result {
    bool ok = false;
    std::string payload;             // JSON wrapper on success, error message on failure
    std::vector<std::uint8_t> bytes; // screenshot binary on success
};

class Bridge {
public:
    static Bridge &instance();

    bool start(std::string host = std::string(kDefaultBindHost),
               std::uint16_t port = kDefaultBindPort);
    void stop();
    bool running() const;

    void set_status(bool connected, std::string message);
    void get_status(bool &connected, std::string &message) const;

    void queue_command(Command command);
    std::optional<Command> poll_command();
    void clear_commands();

    void complete_exec(const std::string &request_id, bool ok, std::string payload);
    void complete_screenshot(const std::string &request_id,
                             bool ok,
                             std::vector<std::uint8_t> data,
                             std::string error);
    void fail_pending(std::string_view message);

    // Fail a waiting exec with a console/runtime error (not a normal respondLua path).
    // With request_id: that pending exec. Without: the active polled exec, if any.
    // Returns true if a waiter was notified.
    bool report_error(std::string message);
    bool report_error(const std::string &request_id, std::string message);

    std::string handle_line(std::string_view line);
    Result handle_screenshot(const BridgeRequest &request);

private:
    Bridge() = default;

    struct PendingSlot {
        std::mutex mutex;
        std::condition_variable cv;
        bool done = false;
        Result result;
    };

    using Clock = std::chrono::steady_clock;

    bool ensure_sockets();
    void close_listen_socket();
    void accept_loop();
    void handle_client(int client);

    std::string handle_request(BridgeRequest request);

    std::pair<std::string, std::shared_ptr<PendingSlot>>
    register_pending(std::mutex &table_mutex,
                     std::unordered_map<std::string, std::shared_ptr<PendingSlot>> &table,
                     const char *prefix,
                     std::atomic<std::uint64_t> &counter);

    Result wait_pending(const std::string &request_id,
                        std::shared_ptr<PendingSlot> slot,
                        std::mutex &table_mutex,
                        std::unordered_map<std::string, std::shared_ptr<PendingSlot>> &table,
                        Clock::duration timeout,
                        std::string_view timeout_message);

    std::string queue_exec_and_wait(Command command, Clock::duration timeout);
    Result queue_screenshot_and_wait(Command command, Clock::duration timeout);

    std::optional<std::string> validate_exec(const BridgeRequest &request, Command &out) const;
    Command make_players_command() const;
    std::optional<std::string> validate_entities(const BridgeRequest &request, Command &out) const;
    std::optional<std::string> validate_screenshot(const BridgeRequest &request,
                                                   Command &out) const;

    mutable std::mutex status_mutex_;
    bool connected_ = false;
    std::string status_message_ = kDefaultStatusMessage;

    mutable std::mutex command_mutex_;
    std::deque<Command> commands_;

    std::mutex exec_mutex_;
    std::unordered_map<std::string, std::shared_ptr<PendingSlot>> exec_pending_;
    std::optional<std::string> active_exec_id_;
    std::atomic<std::uint64_t> next_exec_id_{1};

    std::mutex screenshot_mutex_;
    std::unordered_map<std::string, std::shared_ptr<PendingSlot>> screenshot_pending_;
    std::atomic<std::uint64_t> next_screenshot_id_{1};

    mutable std::mutex lifecycle_mutex_;
    bool running_ = false;
    std::atomic<bool> stop_requested_{false};
    std::string bind_host_ = std::string(kDefaultBindHost);
    std::uint16_t bind_port_ = kDefaultBindPort;
    int listen_fd_ = -1;
    std::thread accept_thread_;
};

} // namespace mcp
