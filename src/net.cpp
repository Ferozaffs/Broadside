#include "net.h"
#include <enet/enet.h>
#include <cstring>

static bool g_netInit = false;

void NetHighResTimer() {
#ifdef _WIN32
    timeBeginPeriod(1);
#endif
}

bool NetInit() {
    if (g_netInit) return true;
    g_netInit = enet_initialize() == 0;
    return g_netInit;
}

void NetShutdown() {
    if (g_netInit) enet_deinitialize();
    g_netInit = false;
}

static void PollHost(ENetHost* host, std::vector<NetEvent>& out, ENetPeer** onlyPeer, bool* connected) {
    ENetEvent ev;
    while (host && enet_host_service(host, &ev, 0) > 0) {
        NetEvent ne;
        ne.peer = ev.peer ? ev.peer->incomingPeerID : 0;
        switch (ev.type) {
            case ENET_EVENT_TYPE_CONNECT:
                ne.type = NetEvent::Connect;
                if (connected) *connected = true;
                out.push_back(std::move(ne));
                break;
            case ENET_EVENT_TYPE_DISCONNECT:
                ne.type = NetEvent::Disconnect;
                if (connected) *connected = false;
                if (onlyPeer) *onlyPeer = nullptr;
                out.push_back(std::move(ne));
                break;
            case ENET_EVENT_TYPE_RECEIVE:
                ne.type = NetEvent::Receive;
                ne.data.assign(ev.packet->data, ev.packet->data + ev.packet->dataLength);
                enet_packet_destroy(ev.packet);
                out.push_back(std::move(ne));
                break;
            default:
                break;
        }
    }
}

static ENetPacket* MakePacket(const void* data, size_t len, bool reliable) {
    return enet_packet_create(data, len, reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
}

// ---- server -------------------------------------------------------------------
bool NetServer::Start(uint16_t port, int maxClients) {
    ENetAddress addr;
    addr.host = ENET_HOST_ANY;
    addr.port = port;
    host = enet_host_create(&addr, (size_t)maxClients, 2, 0, 0);
    rooms::Hook(host);
    return host != nullptr;
}

void NetServer::Stop() {
    if (!host) return;
    ENetHost* h = (ENetHost*)host;
    for (size_t i = 0; i < h->peerCount; ++i)
        if (h->peers[i].state == ENET_PEER_STATE_CONNECTED) enet_peer_disconnect_now(&h->peers[i], 0);
    enet_host_flush(h);
    enet_host_destroy(h);
    host = nullptr;
}

void NetServer::Poll(std::vector<NetEvent>& out) { PollHost((ENetHost*)host, out, nullptr, nullptr); }

void NetServer::Send(uint32_t peer, const void* data, size_t len, bool reliable) {
    ENetHost* h = (ENetHost*)host;
    if (!h || peer >= h->peerCount) return;
    ENetPeer* p = &h->peers[peer];
    if (p->state != ENET_PEER_STATE_CONNECTED) return;
    enet_peer_send(p, reliable ? 0 : 1, MakePacket(data, len, reliable));
}

void NetServer::Broadcast(const void* data, size_t len, bool reliable) {
    ENetHost* h = (ENetHost*)host;
    if (!h) return;
    enet_host_broadcast(h, reliable ? 0 : 1, MakePacket(data, len, reliable));
}

void NetServer::Kick(uint32_t peer) {
    ENetHost* h = (ENetHost*)host;
    if (h && peer < h->peerCount) enet_peer_disconnect(&h->peers[peer], 0);
}

void NetServer::Flush() {
    if (host) enet_host_flush((ENetHost*)host);
}

// ---- client -------------------------------------------------------------------
bool NetClient::Connect(const char* hostname, uint16_t port) {
    Disconnect();
    ENetHost* h = enet_host_create(nullptr, 1, 2, 0, 0);
    if (!h) return false;
    ENetAddress addr;
    if (enet_address_set_host(&addr, hostname) != 0) {
        enet_host_destroy(h);
        return false;
    }
    addr.port = port;
    ENetPeer* p = enet_host_connect(h, &addr, 2, 0);
    if (!p) {
        enet_host_destroy(h);
        return false;
    }
    enet_peer_timeout(p, 0, 4000, 8000);
    host = h;
    peer = p;
    connected = false;
    return true;
}

bool NetClient::ConnectRoom(const std::string& name, int protocol) {
    Disconnect();
    ENetHost* h = enet_host_create(nullptr, 1, 2, 0, 0);
    if (!h) return false;
    rooms::Hook(h);
    host = h;
    peer = nullptr;
    connected = false;
    failure.clear();
    looking = true;
    room.Start(name, h, protocol);
    return true;
}

void NetClient::Disconnect() {
    room.Stop();
    looking = false;
    if (peer && connected) {
        enet_peer_disconnect_now((ENetPeer*)peer, 0);
        enet_host_flush((ENetHost*)host);
    }
    if (host) enet_host_destroy((ENetHost*)host);
    host = peer = nullptr;
    connected = false;
}

void NetClient::Poll(std::vector<NetEvent>& out) {
    if (looking && host) {
        uint32_t ip = 0;
        uint16_t port = 0;
        int r = room.Result(ip, port);
        if (r < 0) {
            failure = room.Describe();
            looking = false;
        } else if (r > 0) {
            ENetAddress a;
            a.host = ip;
            a.port = port;
            ENetPeer* np = enet_host_connect((ENetHost*)host, &a, 2, 0);
            if (np) enet_peer_timeout(np, 0, 4000, 8000);
            peer = np;
            looking = false;
            if (!np) failure = "Couldn't start the connection";
        }
    }
    ENetPeer* p = (ENetPeer*)peer;
    PollHost((ENetHost*)host, out, &p, &connected);
    peer = p;
}

void NetClient::Send(const void* data, size_t len, bool reliable) {
    if (!peer || !connected) return;
    enet_peer_send((ENetPeer*)peer, reliable ? 0 : 1, MakePacket(data, len, reliable));
    enet_host_flush((ENetHost*)host);
}

uint32_t NetClient::RoundTripMs() const { return peer ? ((ENetPeer*)peer)->roundTripTime : 0; }
