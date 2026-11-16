// Thin ENet transport. Kept free of raylib includes (ENet pulls in windows.h).
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

struct NetEvent {
    enum Type { Connect, Disconnect, Receive } type;
    uint32_t peer = 0;
    std::vector<uint8_t> data;
};

bool NetInit();
void NetShutdown();
void NetHighResTimer();  // 1 ms sleep granularity for the dedicated server loop

class NetServer {
public:
    bool Start(uint16_t port, int maxClients);
    void Stop();
    void Poll(std::vector<NetEvent>& out);
    void Send(uint32_t peer, const void* data, size_t len, bool reliable);
    void Broadcast(const void* data, size_t len, bool reliable);
    void Kick(uint32_t peer);
    void Flush();

private:
    void* host = nullptr;
};

class NetClient {
public:
    bool Connect(const char* hostname, uint16_t port);  // starts connecting; poll for Connect event
    void Disconnect();
    void Poll(std::vector<NetEvent>& out);
    void Send(const void* data, size_t len, bool reliable);
    bool Connected() const { return connected; }
    uint32_t RoundTripMs() const;

private:
    void* host = nullptr;
    void* peer = nullptr;
    bool connected = false;
};
