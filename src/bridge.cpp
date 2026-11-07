#include "bridge.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using socklen_t = int;
#define MCP_CLOSESOCKET closesocket
#define MCP_SOCKET_ERROR SOCKET_ERROR
#define MCP_INVALID_SOCKET INVALID_SOCKET
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#define MCP_CLOSESOCKET close
#define MCP_SOCKET_ERROR (-1)
#define MCP_INVALID_SOCKET (-1)
#endif

namespace mcp {
namespace {

constexpr auto kExecTimeout = std::chrono::seconds(10);
constexpr auto kScreenshotTimeout = std::chrono::seconds(10);

#if defined(_WIN32)
class WinsockLifetime {
public:
    WinsockLifetime() {
        WSADATA data{};
        ready_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }
    ~WinsockLifetime() {
        if (ready_) {
            WSACleanup();
        }
    }
    bool ready() const { return ready_; }

private:
    bool ready_ = false;
};

WinsockLifetime &winsock() {
    static WinsockLifetime state;
    return state;
}
#endif

bool set_nonblocking(int fd) {
#if defined(_WIN32)
    u_long mode = 1;
    return ioctlsocket(fd, FIONBIO, &mode) == 0;
#else
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return false;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

bool set_recv_timeout(int fd, std::chrono::milliseconds timeout) {
#if defined(_WIN32)
    const DWORD ms = static_cast<DWORD>(timeout.count());
    return setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&ms),
                      sizeof(ms)) == 0;
#else
    timeval tv{};
    tv.tv_sec = static_cast<time_t>(timeout.count() / 1000);
    tv.tv_usec = static_cast<suseconds_t>((timeout.count() % 1000) * 1000);
    return setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0;
#endif
}

bool set_send_timeout(int fd, std::chrono::milliseconds timeout) {
#if defined(_WIN32)
    const DWORD ms = static_cast<DWORD>(timeout.count());
    return setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char *>(&ms),
                      sizeof(ms)) == 0;
#else
    timeval tv{};
    tv.tv_sec = static_cast<time_t>(timeout.count() / 1000);
    tv.tv_usec = static_cast<suseconds_t>((timeout.count() % 1000) * 1000);
    return setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) == 0;
#endif
}

bool send_all(int fd, const char *data, std::size_t len) {
    std::size_t sent_total = 0;
    while (sent_total < len) {
#if defined(_WIN32)
        const int sent =
            ::send(fd, data + sent_total, static_cast<int>(len - sent_total), 0);
#else
        const ssize_t sent = ::send(fd, data + sent_total, len - sent_total, 0);
#endif
        if (sent <= 0) {
            return false;
        }
        sent_total += static_cast<std::size_t>(sent);
    }
    return true;
}

bool send_line(int fd, std::string_view line) {
    if (!send_all(fd, line.data(), line.size())) {
        return false;
    }
    return send_all(fd, "\n", 1);
}

std::optional<std::string> read_line(int fd, const std::atomic<bool> &stop) {
    std::string line;
    char buffer[512];

    while (!stop.load(std::memory_order_acquire)) {
#if defined(_WIN32)
        const int received = ::recv(fd, buffer, sizeof(buffer), 0);
#else
        const ssize_t received = ::recv(fd, buffer, sizeof(buffer), 0);
#endif
        if (received > 0) {
            for (int i = 0; i < static_cast<int>(received); ++i) {
                if (buffer[i] == '\n') {
                    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
                        line.pop_back();
                    }
                    return line;
                }
                line.push_back(buffer[i]);
                if (line.size() > 256 * 1024) {
                    return std::nullopt;
                }
            }
            continue;
        }

#if defined(_WIN32)
        const int err = WSAGetLastError();
        if (err == WSAETIMEDOUT || err == WSAEWOULDBLOCK) {
            continue;
        }
#else
        if (received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            continue;
        }
#endif
        return std::nullopt;
    }
    return std::nullopt;
}

} // namespace

Bridge &Bridge::instance() {
    static Bridge bridge;
    return bridge;
}

bool Bridge::ensure_sockets() {
#if defined(_WIN32)
    return winsock().ready();
#else
    return true;
#endif
}

void Bridge::close_listen_socket() {
    if (listen_fd_ != MCP_INVALID_SOCKET && listen_fd_ >= 0) {
        MCP_CLOSESOCKET(listen_fd_);
        listen_fd_ = -1;
    }
}

bool Bridge::start(std::string host, std::uint16_t port) {
    std::lock_guard<std::mutex> lock(lifecycle_mutex_);
    if (running_) {
        return true;
    }
    if (!ensure_sockets()) {
        std::fprintf(stderr, "mcp: failed to initialize sockets\n");
        return false;
    }

    const int fd = static_cast<int>(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    if (fd == MCP_INVALID_SOCKET || fd < 0) {
        std::fprintf(stderr, "mcp: socket() failed\n");
        return false;
    }

    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&yes), sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        std::fprintf(stderr, "mcp: invalid bind host '%s'\n", host.c_str());
        MCP_CLOSESOCKET(fd);
        return false;
    }

    if (::bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == MCP_SOCKET_ERROR) {
        std::fprintf(stderr, "mcp: bind(%s:%u) failed\n", host.c_str(),
                     static_cast<unsigned>(port));
        MCP_CLOSESOCKET(fd);
        return false;
    }

    if (::listen(fd, 16) == MCP_SOCKET_ERROR) {
        std::fprintf(stderr, "mcp: listen() failed\n");
        MCP_CLOSESOCKET(fd);
        return false;
    }

    if (!set_nonblocking(fd)) {
        std::fprintf(stderr, "mcp: failed to set non-blocking listen socket\n");
        MCP_CLOSESOCKET(fd);
        return false;
    }

    listen_fd_ = fd;
    bind_host_ = std::move(host);
    bind_port_ = port;
    stop_requested_.store(false, std::memory_order_release);
    running_ = true;
    accept_thread_ = std::thread([this] { accept_loop(); });

    std::fprintf(stderr, "mcp: bridge listening on %s:%u\n", bind_host_.c_str(),
                 static_cast<unsigned>(bind_port_));
    return true;
}

void Bridge::stop() {
    {
        std::lock_guard<std::mutex> lock(lifecycle_mutex_);
        if (!running_) {
            return;
        }
        stop_requested_.store(true, std::memory_order_release);
        close_listen_socket();
    }

    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }

    {
        std::lock_guard<std::mutex> lock(lifecycle_mutex_);
        running_ = false;
    }

    clear_commands();
    fail_pending(kDefaultStatusMessage);
}

bool Bridge::running() const {
    std::lock_guard<std::mutex> lock(lifecycle_mutex_);
    return running_;
}

void Bridge::set_status(bool connected, std::string message) {
    std::lock_guard<std::mutex> lock(status_mutex_);
    connected_ = connected;
    status_message_ = std::move(message);
}

void Bridge::get_status(bool &connected, std::string &message) const {
    std::lock_guard<std::mutex> lock(status_mutex_);
    connected = connected_;
    message = status_message_;
}

void Bridge::queue_command(Command command) {
    std::lock_guard<std::mutex> lock(command_mutex_);
    commands_.push_back(std::move(command));
}

std::optional<Command> Bridge::poll_command() {
    Command command;
    {
        std::lock_guard<std::mutex> lock(command_mutex_);
        if (commands_.empty()) {
            return std::nullopt;
        }
        command = std::move(commands_.front());
        commands_.pop_front();
    }

    if (command.request_id) {
        std::lock_guard<std::mutex> lock(exec_mutex_);
        if (exec_pending_.find(*command.request_id) != exec_pending_.end()) {
            active_exec_id_ = *command.request_id;
        }
    }

    return command;
}

void Bridge::clear_commands() {
    std::lock_guard<std::mutex> lock(command_mutex_);
    commands_.clear();
}

void Bridge::complete_exec(const std::string &request_id, bool ok, std::string payload) {
    std::shared_ptr<PendingSlot> slot;
    {
        std::lock_guard<std::mutex> lock(exec_mutex_);
        if (active_exec_id_ && *active_exec_id_ == request_id) {
            active_exec_id_.reset();
        }
        const auto it = exec_pending_.find(request_id);
        if (it == exec_pending_.end()) {
            return;
        }
        slot = it->second;
        exec_pending_.erase(it);
    }

    {
        std::lock_guard<std::mutex> lock(slot->mutex);
        slot->result.ok = ok;
        slot->result.payload = std::move(payload);
        slot->done = true;
    }
    slot->cv.notify_all();
}

bool Bridge::report_error(std::string message) {
    std::string request_id;
    {
        std::lock_guard<std::mutex> lock(exec_mutex_);
        if (!active_exec_id_) {
            return false;
        }
        request_id = *active_exec_id_;
    }
    return report_error(request_id, std::move(message));
}

bool Bridge::report_error(const std::string &request_id, std::string message) {
    std::shared_ptr<PendingSlot> slot;
    {
        std::lock_guard<std::mutex> lock(exec_mutex_);
        if (active_exec_id_ && *active_exec_id_ == request_id) {
            active_exec_id_.reset();
        }
        const auto it = exec_pending_.find(request_id);
        if (it == exec_pending_.end()) {
            return false;
        }
        slot = it->second;
        exec_pending_.erase(it);
    }

    {
        std::lock_guard<std::mutex> lock(slot->mutex);
        slot->result.ok = false;
        slot->result.payload = std::move(message);
        slot->done = true;
    }
    slot->cv.notify_all();
    return true;
}

void Bridge::complete_screenshot(const std::string &request_id,
                                 bool ok,
                                 std::vector<std::uint8_t> data,
                                 std::string error) {
    std::shared_ptr<PendingSlot> slot;
    {
        std::lock_guard<std::mutex> lock(screenshot_mutex_);
        const auto it = screenshot_pending_.find(request_id);
        if (it == screenshot_pending_.end()) {
            return;
        }
        slot = it->second;
        screenshot_pending_.erase(it);
    }

    {
        std::lock_guard<std::mutex> lock(slot->mutex);
        slot->result.ok = ok;
        if (ok) {
            slot->result.bytes = std::move(data);
        } else {
            slot->result.payload = std::move(error);
        }
        slot->done = true;
    }
    slot->cv.notify_all();
}

void Bridge::fail_pending(std::string_view message) {
    std::vector<std::shared_ptr<PendingSlot>> slots;

    {
        std::lock_guard<std::mutex> lock(exec_mutex_);
        active_exec_id_.reset();
        for (auto &entry : exec_pending_) {
            slots.push_back(entry.second);
        }
        exec_pending_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(screenshot_mutex_);
        for (auto &entry : screenshot_pending_) {
            slots.push_back(entry.second);
        }
        screenshot_pending_.clear();
    }

    for (auto &slot : slots) {
        {
            std::lock_guard<std::mutex> lock(slot->mutex);
            slot->result.ok = false;
            slot->result.payload = std::string(message);
            slot->done = true;
        }
        slot->cv.notify_all();
    }
}

auto Bridge::register_pending(
    std::mutex &table_mutex,
    std::unordered_map<std::string, std::shared_ptr<PendingSlot>> &table,
    const char *prefix,
    std::atomic<std::uint64_t> &counter)
    -> std::pair<std::string, std::shared_ptr<PendingSlot>> {
    auto slot = std::make_shared<PendingSlot>();
    const std::uint64_t id = counter.fetch_add(1, std::memory_order_relaxed);
    std::string request_id = prefix + std::to_string(id);
    std::lock_guard<std::mutex> lock(table_mutex);
    table.emplace(request_id, slot);
    return {std::move(request_id), std::move(slot)};
}

Result Bridge::wait_pending(
    const std::string &request_id,
    std::shared_ptr<PendingSlot> slot,
    std::mutex &table_mutex,
    std::unordered_map<std::string, std::shared_ptr<PendingSlot>> &table,
    Clock::duration timeout,
    std::string_view timeout_message) {
    std::unique_lock<std::mutex> lock(slot->mutex);
    const bool ready = slot->cv.wait_for(lock, timeout, [&] { return slot->done; });
    if (!ready) {
        lock.unlock();
        {
            std::lock_guard<std::mutex> table_lock(table_mutex);
            table.erase(request_id);
        }
        Result result;
        result.ok = false;
        result.payload = std::string(timeout_message);
        return result;
    }
    return slot->result;
}

std::string Bridge::queue_exec_and_wait(Command command, Clock::duration timeout) {
    auto [request_id, slot] =
        register_pending(exec_mutex_, exec_pending_, "e", next_exec_id_);
    command.request_id = request_id;
    queue_command(std::move(command));

    const Result result =
        wait_pending(request_id, slot, exec_mutex_, exec_pending_, timeout, "GMod query timed out.");
    return exec_result_response(result.ok, result.payload);
}

Result Bridge::queue_screenshot_and_wait(Command command, Clock::duration timeout) {
    auto [request_id, slot] =
        register_pending(screenshot_mutex_, screenshot_pending_, "s", next_screenshot_id_);
    command.request_id = request_id;
    queue_command(std::move(command));

    return wait_pending(request_id,
                        slot,
                        screenshot_mutex_,
                        screenshot_pending_,
                        timeout,
                        "Screenshot timed out.");
}

std::optional<std::string> Bridge::validate_exec(const BridgeRequest &request,
                                                 Command &out) const {
    if (!request.state) {
        return std::string("state must be 'server' or 'client'.");
    }
    if (!request.code) {
        return std::string("code must not be missing.");
    }

    const std::string &state = *request.state;
    const std::string &code = *request.code;
    if (trim_copy(code).empty()) {
        return std::string("Lua code must not be empty.");
    }
    if (code.size() > kMaxLuaCodeBytes) {
        return "Lua code exceeds the " + std::to_string(kMaxLuaCodeBytes) + " byte limit.";
    }

    const bool return_result = request.return_result.value_or(false);

    if (state == "server") {
        if (request.target) {
            return std::string("target is only valid for client execution.");
        }
        out = Command{
            kActionExecLua,
            state,
            code,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            return_result,
        };
        return std::nullopt;
    }

    if (state == "client") {
        if (!request.target) {
            return std::string("target is required for client execution.");
        }
        std::string target = trim_copy(*request.target);
        if (target.empty()) {
            return std::string("target is required for client execution.");
        }
        if (target.size() > kMaxTargetBytes) {
            return "target exceeds the " + std::to_string(kMaxTargetBytes) + " byte limit.";
        }
        if (return_result && is_broadcast_target(target)) {
            return std::string("return_result requires one client target.");
        }
        out = Command{
            kActionExecLua,
            state,
            code,
            std::move(target),
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            return_result,
        };
        return std::nullopt;
    }

    return std::string("state must be 'server' or 'client'.");
}

Command Bridge::make_players_command() const {
    return Command{
        kActionPlayers,
        "server",
        "players",
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        true,
    };
}

std::optional<std::string> Bridge::validate_entities(const BridgeRequest &request,
                                                     Command &out) const {
    std::optional<std::string> class_filter;
    if (request.class_filter) {
        std::string value = trim_copy(*request.class_filter);
        if (!value.empty()) {
            if (value.size() > kMaxTargetBytes) {
                return "class_filter exceeds the " + std::to_string(kMaxTargetBytes) +
                       " byte limit.";
            }
            class_filter = std::move(value);
        }
    }

    const std::uint32_t limit = request.limit.value_or(100);
    if (limit == 0 || limit > kMaxEntityLimit) {
        return "limit must be between 1 and " + std::to_string(kMaxEntityLimit) + ".";
    }

    std::optional<std::string> custom_check;
    if (request.custom_check) {
        std::string value = trim_copy(*request.custom_check);
        if (!value.empty()) {
            if (value.size() > kMaxLuaCodeBytes) {
                return "customCheck exceeds the " + std::to_string(kMaxLuaCodeBytes) +
                       " byte limit.";
            }
            custom_check = std::move(value);
        }
    }

    out = Command{
        kActionEntities,
        "server",
        "entities",
        std::nullopt,
        std::move(class_filter),
        std::move(custom_check),
        limit,
        std::nullopt,
        true,
    };
    return std::nullopt;
}

std::optional<std::string> Bridge::validate_screenshot(const BridgeRequest &request,
                                                      Command &out) const {
    if (!request.target) {
        return std::string("target is required for a screenshot.");
    }
    std::string target = trim_copy(*request.target);
    if (target.empty()) {
        return std::string("target is required for a screenshot.");
    }
    if (target.size() > kMaxTargetBytes) {
        return "target exceeds the " + std::to_string(kMaxTargetBytes) + " byte limit.";
    }
    if (is_broadcast_target(target)) {
        return std::string("a screenshot target must identify one client.");
    }

    out = Command{
        kActionScreenshot,
        "client",
        "screenshot",
        std::move(target),
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        false,
    };
    return std::nullopt;
}

std::string Bridge::handle_request(BridgeRequest request) {
    if (request.action == kActionStatus) {
        queue_command(Command{
            kToolStatus,
            "server",
            "status",
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            false,
        });
        bool connected = false;
        std::string message;
        get_status(connected, message);
        return status_response(connected, message);
    }

    if (request.action == kActionExecLua) {
        Command command;
        if (auto error = validate_exec(request, command)) {
            return command_response(false, *error);
        }
        if (command.return_result) {
            return queue_exec_and_wait(std::move(command), kExecTimeout);
        }
        const char *message = command.state == "server"
                                  ? "Lua code queued for server execution."
                                  : "Lua code queued for client execution.";
        queue_command(std::move(command));
        return command_response(true, message);
    }

    if (request.action == kActionPlayers) {
        return queue_exec_and_wait(make_players_command(), kExecTimeout);
    }

    if (request.action == kActionEntities) {
        Command command;
        if (auto error = validate_entities(request, command)) {
            return command_response(false, *error);
        }
        return queue_exec_and_wait(std::move(command), kExecTimeout);
    }

    return "{\"error\":\"unknown request\"}";
}

std::string Bridge::handle_line(std::string_view line) {
    const std::string trimmed = trim_copy(line);
    if (trimmed == "{\"action\":\"status\"}") {
        BridgeRequest request;
        request.action = kActionStatus;
        return handle_request(std::move(request));
    }

    auto request = parse_request(trimmed);
    if (!request) {
        return "{\"error\":\"invalid request\"}";
    }
    if (request->action == kActionScreenshot) {
        // Screenshots are handled separately for binary framing.
        return command_response(false, "screenshot requests must use the binary response path.");
    }
    return handle_request(std::move(*request));
}

Result Bridge::handle_screenshot(const BridgeRequest &request) {
    Command command;
    if (auto error = validate_screenshot(request, command)) {
        Result result;
        result.ok = false;
        result.payload = *error;
        return result;
    }
    return queue_screenshot_and_wait(std::move(command), kScreenshotTimeout);
}

void Bridge::accept_loop() {
    while (!stop_requested_.load(std::memory_order_acquire)) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        const int client =
            static_cast<int>(::accept(listen_fd_, reinterpret_cast<sockaddr *>(&client_addr),
                                      &client_len));
        if (client >= 0 && client != MCP_INVALID_SOCKET) {
            handle_client(client);
            continue;
        }

#if defined(_WIN32)
        const int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK || err == WSAEINTR) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
#else
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
#endif
        if (!stop_requested_.load(std::memory_order_acquire)) {
            std::fprintf(stderr, "mcp: accept() failed\n");
        }
        break;
    }
}

void Bridge::handle_client(int client) {
    set_recv_timeout(client, std::chrono::milliseconds(50));
    set_send_timeout(client, std::chrono::seconds(2));

    const auto line = read_line(client, stop_requested_);
    if (!line) {
        MCP_CLOSESOCKET(client);
        return;
    }

    auto request = parse_request(*line);
    if (request && request->action == kActionScreenshot) {
        const Result result = handle_screenshot(*request);
        if (result.ok) {
            const std::string header = screenshot_header(true, result.bytes.size());
            if (send_line(client, header)) {
                send_all(client,
                         reinterpret_cast<const char *>(result.bytes.data()),
                         result.bytes.size());
            }
        } else {
            send_line(client, screenshot_header(false, 0, result.payload));
        }
        MCP_CLOSESOCKET(client);
        return;
    }

    const std::string body = request ? handle_request(std::move(*request))
                                     : std::string("{\"error\":\"invalid request\"}");
    send_line(client, body);
    MCP_CLOSESOCKET(client);
}

} // namespace mcp
