// Exercises the MCP transport end to end (sockets + worker thread + main-thread
// poll + JSON-RPC framing) without needing the GUI editor: a background client
// sends requests, the main thread services them via McpServer::poll.

#include "mcp/McpServer.hpp"

#include "nlohmann/json.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <cassert>
#include <chrono>
#include <string>
#include <thread>

using json = nlohmann::json;

namespace {
// Minimal blocking client: connect, send one line, read one line.
std::string roundtrip(uint16_t port, const std::string& request) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    assert(sock != INVALID_SOCKET);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    // Retry connect briefly while the server thread spins up.
    for (int i = 0; i < 50; ++i) {
        if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    std::string line = request + "\n";
    send(sock, line.c_str(), static_cast<int>(line.size()), 0);

    std::string buffer;
    char chunk[1024];
    while (buffer.find('\n') == std::string::npos) {
        int n = recv(sock, chunk, sizeof(chunk), 0);
        if (n <= 0) break;
        buffer.append(chunk, static_cast<size_t>(n));
    }
    closesocket(sock);
    auto nl = buffer.find('\n');
    return nl == std::string::npos ? buffer : buffer.substr(0, nl);
}
} // namespace

int main() {
    const uint16_t port = 8791;
    ne::McpServer server;
    assert(server.start(port));

    // Handler: echo for "ping", throw for "boom".
    auto handler = [](const std::string& method, const json&) -> json {
        if (method == "boom") throw std::runtime_error("kaboom");
        return {{"method", method}, {"ok", true}};
    };

    std::atomic<bool> done{false};
    std::string okResponse, errResponse;
    std::thread client([&] {
        okResponse = roundtrip(port, R"({"jsonrpc":"2.0","id":1,"method":"ping"})");
        errResponse = roundtrip(port, R"({"jsonrpc":"2.0","id":2,"method":"boom"})");
        done.store(true);
    });

    // Pump the main-thread queue until the client has both replies (or timeout).
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!done.load() && std::chrono::steady_clock::now() < deadline) {
        server.poll(handler);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    client.join();
    server.stop();

    assert(!okResponse.empty());
    json ok = json::parse(okResponse);
    assert(ok["id"] == 1);
    assert(ok["result"]["ok"] == true);
    assert(ok["result"]["method"] == "ping");

    json err = json::parse(errResponse);
    assert(err["id"] == 2);
    assert(err.contains("error"));
    assert(err["error"]["message"] == "kaboom");

    return 0;
}
