#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pax {

struct Backend {
    std::string host;
    std::uint16_t port;
};

inline const std::vector<Backend>& static_backends() {
    static const std::vector<Backend> k = {
        {"127.0.0.1", 6025},
        {"127.0.0.1", 6026},
        {"127.0.0.1", 6027},
        {"127.0.0.1", 6028},
    };
    return k;
}

constexpr std::uint16_t k_listen_port = 6020;
constexpr int k_health_check_interval_sec = 5;
constexpr int k_connect_timeout_ms = 2000;
constexpr std::size_t k_buf_size = 4096;

}  // namespace pax
