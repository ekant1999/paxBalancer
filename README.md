# PaxBalancer (C++)

PaxBalancer is a lightweight **L4 (TCP) load balancer** built with **raw POSIX sockets** and **C++17** — no frameworks. It sits in front of a pool of backend servers, distributes new client connections with **round-robin**, and runs a **watchdog thread** that probes backends with **TCP connect** attempts (configurable interval and timeout). Unhealthy backends are removed from rotation until they recover.

The demo backends speak minimal **HTTP** so you can `curl` or run the included client; the balancer itself only forwards bytes and does not parse HTTP (L4).

## Design (interview notes)

- **Round-robin:** `std::atomic<uint64_t>` counter; each dispatch does `fetch_add(1) % N` under a **shared lock** while reading the healthy `std::vector` (hot path). The counter stays lock-free; the list uses `std::shared_mutex` because it only changes when the health checker updates membership.
- **Threads:** main thread `accept()` loop; **one detached thread per accepted client** runs a **bidirectional relay** (`poll` + `read`/`write`). A separate **health** thread periodically rebuilds the healthy set.
- **Health checks:** **active** probing — TCP `connect` with timeout (not passive inference from application traffic). Interval: `k_health_check_interval_sec` in `include/config.hpp`.
- **Limitations:** thread-per-connection does not scale to huge fan-in; production systems use `epoll`/`kqueue` and often connection pooling, weighted routing, and graceful draining. Mid-transfer backend failure closes the pair; there is no retry/failover for in-flight data.

## Build

```bash
mkdir -p build && cd build
cmake ..
cmake --build .
```

If CMake is unavailable:

```bash
mkdir -p build
clang++ -std=c++17 -Wall -Wextra -I include -pthread -o build/pax_balancer src/load_balancer.cpp
clang++ -std=c++17 -Wall -Wextra -I include -pthread -o build/backend_server src/backend_server.cpp
clang++ -std=c++17 -Wall -Wextra -I include -pthread -o build/pax_client src/client.cpp
```

## Run

1. Start four backends (ports `6025`–`6028` by default):

   ```bash
   ./build/backend_server 6025 &
   ./build/backend_server 6026 &
   ./build/backend_server 6027 &
   ./build/backend_server 6028 &
   ```

2. Start the load balancer (default listen `6020`):

   ```bash
   ./build/pax_balancer
   ```

3. Send traffic (optional):

   ```bash
   ./build/pax_client 127.0.0.1 6020 20
   ```

Or use the helper script (starts backends + balancer + a short client run):

```bash
chmod +x scripts/run_demo.sh
./scripts/run_demo.sh
```
## Layout

| Path | Role |
|------|------|
| `src/load_balancer.cpp` | Listener, atomic RR, health thread, per-connection relay |
| `src/backend_server.cpp` | Demo HTTP backend; `/health` may randomly return 503 (demo only) |
| `src/client.cpp` | Demo client hitting the balancer |
| `include/config.hpp` | Backend list, ports, intervals |

## License
<img width="616" height="341" alt="image" src="https://github.com/user-attachments/assets/78a91d79-cfd5-435b-8d73-0b8340929348" />
![image](https://github.com/user-attachments/assets/33f00576-5231-4866-ad27-a09e3e795a8e)
![image](https://github.com/user-attachments/assets/86e01c23-5b2c-4c26-9b2b-93b1874e7472)

Use and modify as you like for learning and interviews.
