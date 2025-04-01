#include "config.hpp"

#include <chrono>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

int connect_blocking(const std::string& host, std::uint16_t port) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        ::close(fd);
        return -1;
    }
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::perror("connect");
        ::close(fd);
        return -1;
    }
    return fd;
}

void run_once(const std::string& host, std::uint16_t port) {
    int fd = connect_blocking(host, port);
    if (fd < 0) return;

    const std::string req = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
    if (::write(fd, req.data(), req.size()) < 0) {
        std::perror("write");
        ::close(fd);
        return;
    }

    std::vector<char> buf(pax::k_buf_size);
    ssize_t n = ::read(fd, buf.data(), buf.size());
    ::close(fd);

    if (n < 0) {
        std::perror("read");
        return;
    }
    std::cout << "[CLIENT] Response:\n" << std::string(buf.data(), static_cast<std::size_t>(n)) << std::flush;
}

}  // namespace

int main(int argc, char** argv) {
    std::string host = "127.0.0.1";
    std::uint16_t port = pax::k_listen_port;
    int client_count = 20;

    if (argc >= 2) host = argv[1];
    if (argc >= 3) port = static_cast<std::uint16_t>(std::stoi(argv[2]));
    if (argc >= 4) client_count = std::stoi(argv[3]);

    for (int i = 0; i < client_count; ++i) {
        std::cout << "[CLIENT] Request " << (i + 1) << "/" << client_count << "\n";
        run_once(host, port);
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    return 0;
}
