# High-Performance Kafka Ingress Gateway
A high-throughput, low-latency TCP-to-Kafka proxy written in C.

This gateway decouples high-volume producers from Kafka brokers by multiplexing thousands of concurrent TCP connections into a single, optimized producer pipeline. It solves the "Small Message" problem by aggregating incoming data into batches via a lock-free ring buffer before sending to Kafka.

## Key Features

*   **Edge-Triggered Epoll:** Uses non-blocking I/O and event-driven architecture to handle 50k+ concurrent connections on a single thread.
*   **Zero-Copy Architecture:** Implements a memory-mapped (`mmap`) ring buffer. Data is read from the socket directly into the ring and passed to `librdkafka` via pointers, eliminating unnecessary `memcpy` operations.
*   **Manual Memory Management:** Uses a custom arena allocator to prevent memory fragmentation and eliminate `malloc`/`free` overhead in the hot path.
*   **Starvation-Free Scheduling:** Implements a round-robin socket processing strategy to prevent "noisy neighbor" clients from monopolizing the CPU.
*   **Protocol Enforcement:** Validates 8-byte binary headers (Magic + Length) to ensure data integrity before ingestion.

##  Performance Metrics

*   **Throughput:** Sustained ~4M messages/sec (local hose test).
*   **Efficiency:** Achieves **~2 IPC (Instructions Per Cycle)** on modern hardware.
*   **Latency:** Sub-millisecond processing time with 99.9% reliability under connection churn (100k sequential connections).

