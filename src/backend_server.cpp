// Minimal HTTP backend over raw POSIX sockets (for demo with the L4 load balancer).
#include "config.hpp"

#include <cerrno>
#include <cstring>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

int make_listener(std::uint16_t port) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        std::perror("socket");
        return -1;
    }
    int on = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::perror("bind");
        ::close(fd);
        return -1;
    }
    if (listen(fd, 128) < 0) {
        std::perror("listen");
        ::close(fd);
        return -1;
    }
    return fd;
}

ssize_t writen(int fd, const void* buf, std::size_t len) {
    const char* p = static_cast<const char*>(buf);
    std::size_t left = len;
    while (left > 0) {
        ssize_t n = ::write(fd, p, left);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        p += static_cast<std::size_t>(n);
        left -= static_cast<std::size_t>(n);
    }
    return static_cast<ssize_t>(len);
}

bool is_health_request(const std::string& req) { return req.find("GET /health") != std::string::npos; }

void send_all(int fd, const std::string& s) { writen(fd, s.data(), s.size()); }

void handle_one(int client_fd, std::uint16_t port) {
    std::vector<char> buf(pax::k_buf_size);
    ssize_t n = ::read(client_fd, buf.data(), buf.size());
    if (n <= 0) return;
    std::string req(buf.data(), static_cast<std::size_t>(n));
    std::cout << "[BACKEND " << port << "] Request:\n" << req << std::flush;

    if (is_health_request(req)) {
        thread_local std::mt19937 rng{std::random_device{}()};
        std::uniform_int_distribution<int> dist(0, 2);
        if (dist(rng) == 1) {
            send_all(client_fd, "HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\n\r\n");
            std::this_thread::sleep_for(std::chrono::seconds(3));
        } else {
            send_all(client_fd, "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
        }
        return;
    }

    const std::string body = "Hello from backend on port " + std::to_string(port);
    const std::string response =
        "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: " + std::to_string(body.size()) +
        "\r\n\r\n" + body;
    send_all(client_fd, response);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: backend_server <port>\n";
        return 1;
    }
    const std::uint16_t port = static_cast<std::uint16_t>(std::stoi(argv[1]));

    int listen_fd = make_listener(port);
    if (listen_fd < 0) return 1;

    std::cout << "[BACKEND] Listening on 0.0.0.0:" << port << "\n";

    for (;;) {
        int cfd = ::accept(listen_fd, nullptr, nullptr);
        if (cfd < 0) {
            if (errno == EINTR) continue;
            std::perror("accept");
            continue;
        }
        handle_one(cfd, port);
        ::close(cfd);
    }
}
