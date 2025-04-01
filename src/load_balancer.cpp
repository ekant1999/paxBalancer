// PaxBalancer — L4 TCP load balancer: raw POSIX sockets, round-robin, health watchdog.
#include "config.hpp"

#include <atomic>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

void set_reuseaddr(int fd) {
    int on = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0) {
        std::perror("setsockopt SO_REUSEADDR");
    }
}

bool set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

bool set_blocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) == 0;
}

bool connect_with_timeout(int sock, const sockaddr* addr, socklen_t len, int timeout_ms) {
    if (!set_nonblocking(sock)) return false;
    int rc = ::connect(sock, addr, len);
    if (rc == 0) {
        return set_blocking(sock);
    }
    if (errno != EINPROGRESS) return false;

    pollfd pfd{};
    pfd.fd = sock;
    pfd.events = POLLOUT;
    rc = poll(&pfd, 1, timeout_ms);
    if (rc <= 0) return false;

    int so_error = 0;
    socklen_t slen = sizeof(so_error);
    if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_error, &slen) < 0) return false;
    if (so_error != 0) {
        errno = so_error;
        return false;
    }
    return set_blocking(sock);
}

int make_listener(std::uint16_t port) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        std::perror("socket");
        return -1;
    }
    set_reuseaddr(fd);

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

int connect_backend(const pax::Backend& b) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(b.port);
    if (inet_pton(AF_INET, b.host.c_str(), &addr.sin_addr) != 1) {
        ::close(fd);
        return -1;
    }

    if (!connect_with_timeout(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr), pax::k_connect_timeout_ms)) {
        ::close(fd);
        return -1;
    }
    return fd;
}

bool tcp_probe(const pax::Backend& b) {
    int fd = connect_backend(b);
    if (fd < 0) return false;
    ::close(fd);
    return true;
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
        if (n == 0) return static_cast<ssize_t>(len - left);
        p += static_cast<std::size_t>(n);
        left -= static_cast<std::size_t>(n);
    }
    return static_cast<ssize_t>(len);
}

struct ScopedFd {
    int fd{-1};
    explicit ScopedFd(int f) : fd(f) {}
    ~ScopedFd() {
        if (fd >= 0) ::close(fd);
    }
    ScopedFd(const ScopedFd&) = delete;
    ScopedFd& operator=(const ScopedFd&) = delete;
};

void relay_bidirectional(int client_fd, int backend_fd) {
    std::vector<char> buf(pax::k_buf_size);
    pollfd pfds[2]{};
    pfds[0].fd = client_fd;
    pfds[0].events = POLLIN;
    pfds[1].fd = backend_fd;
    pfds[1].events = POLLIN;

    for (;;) {
        int pr = poll(pfds, 2, -1);
        if (pr < 0) {
            if (errno == EINTR) continue;
            break;
        }

        auto pump = [&](int from, int to) {
            ssize_t n = ::read(from, buf.data(), buf.size());
            if (n <= 0) return false;
            if (writen(to, buf.data(), static_cast<std::size_t>(n)) < 0) return false;
            return true;
        };

        if (pfds[0].revents & (POLLIN | POLLERR | POLLHUP)) {
            if (!pump(client_fd, backend_fd)) return;
        }
        if (pfds[1].revents & (POLLIN | POLLERR | POLLHUP)) {
            if (!pump(backend_fd, client_fd)) return;
        }
    }
}

class LoadBalancer {
   public:
    explicit LoadBalancer(std::vector<pax::Backend> all_backends)
        : all_backends_(std::move(all_backends)) {
        healthy_ = all_backends_;
    }

    void health_loop() {
        for (;;) {
            std::vector<pax::Backend> next;
            next.reserve(all_backends_.size());
            for (const auto& b : all_backends_) {
                if (tcp_probe(b)) {
                    next.push_back(b);
                } else {
                    std::cerr << "[WARN] Health check failed (TCP connect) " << b.host << ":" << b.port << "\n";
                }
            }
            {
                std::unique_lock<std::shared_mutex> lk(healthy_mutex_);
                healthy_ = std::move(next);
            }
            if (healthy_.empty()) {
                std::cerr << "[WARN] No healthy backends in rotation\n";
            }
            std::this_thread::sleep_for(std::chrono::seconds(pax::k_health_check_interval_sec));
        }
    }

    pax::Backend pick_backend() {
        std::shared_lock<std::shared_mutex> lk(healthy_mutex_);
        if (healthy_.empty()) {
            throw std::runtime_error("No healthy backend servers available");
        }
        const std::size_t n = healthy_.size();
        const std::uint64_t idx = rr_.fetch_add(1, std::memory_order_relaxed) % n;
        return healthy_[idx];
    }

    void handle_client(int client_fd) {
        ScopedFd client_guard(client_fd);

        pax::Backend b;
        try {
            b = pick_backend();
        } catch (const std::exception& e) {
            std::cerr << "[ERROR] " << e.what() << "\n";
            return;
        }

        std::cerr << "[INFO] Forwarding to " << b.host << ":" << b.port << "\n";
        int backend_fd = connect_backend(b);
        if (backend_fd < 0) {
            std::cerr << "[ERROR] Backend connect failed: " << std::strerror(errno) << "\n";
            return;
        }
        ScopedFd backend_guard(backend_fd);

        relay_bidirectional(client_guard.fd, backend_guard.fd);
    }

    void run(std::uint16_t port) {
        int listen_fd = make_listener(port);
        if (listen_fd < 0) std::exit(1);
        ScopedFd listen_guard(listen_fd);

        std::cerr << "[INFO] Load balancer listening on 0.0.0.0:" << port << "\n";

        for (;;) {
            int client_fd = ::accept(listen_guard.fd, nullptr, nullptr);
            if (client_fd < 0) {
                if (errno == EINTR) continue;
                std::perror("accept");
                continue;
            }
            std::thread(&LoadBalancer::handle_client, this, client_fd).detach();
        }
    }

   private:
    std::vector<pax::Backend> all_backends_;
    std::vector<pax::Backend> healthy_;
    std::shared_mutex healthy_mutex_;
    std::atomic<std::uint64_t> rr_{0};
};

}  // namespace

int main(int argc, char** argv) {
    std::uint16_t port = pax::k_listen_port;
    if (argc >= 2) {
        port = static_cast<std::uint16_t>(std::stoi(argv[1]));
    }

    LoadBalancer lb(pax::static_backends());
    std::thread watchdog(&LoadBalancer::health_loop, &lb);
    watchdog.detach();

    lb.run(port);
    return 0;
}
