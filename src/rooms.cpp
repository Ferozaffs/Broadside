#include "rooms.h"
#include <enet/enet.h>
#ifdef _WIN32
#include <winhttp.h>
#endif
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

namespace rooms {
namespace {

void Log(const char* fmt, const std::string& a = "", const std::string& b = "") {
    static const bool on = getenv("BS_ROOMLOG") != nullptr;
    if (!on) return;
    printf("[rooms] ");
    printf(fmt, a.c_str(), b.c_str());
    printf("\n");
    fflush(stdout);
}

double Now() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

std::string RandHex(int n) {
    std::random_device rd;
    static const char* hx = "0123456789abcdef";
    std::string s;
    for (int i = 0; i < n; ++i) s += hx[rd() & 15];
    return s;
}

// ---- raw UDP on the game's sockets: STUN answers and punches ---------------------------------------------------------
struct Seen {
    std::mutex m;
    std::vector<std::pair<std::string, ENetAddress>> stun;      // transaction id -> our address as the STUN server saw it
    std::vector<std::pair<ENetHost*, ENetAddress>> punched;      // a punch reached this host from there
};
Seen& G() {
    static Seen s;
    return s;
}

int ENET_CALLBACK Intercept(ENetHost* h, ENetEvent*) {
    const enet_uint8* d = h->receivedData;
    size_t n = h->receivedDataLength;
    if (n >= 20 && d[0] == 0x01 && d[1] == 0x01 && d[4] == 0x21 && d[5] == 0x12 && d[6] == 0xA4 && d[7] == 0x42) {
        // a STUN binding success: find (XOR-)MAPPED-ADDRESS
        std::string tx((const char*)d + 8, 12);
        size_t len = ((size_t)d[2] << 8) | d[3], end = std::min(n, 20 + len), p = 20;
        bool found = false;
        ENetAddress a{};
        while (p + 4 <= end) {
            int type = (d[p] << 8) | d[p + 1];
            size_t al = ((size_t)d[p + 2] << 8) | d[p + 3];
            if (p + 4 + al > end) break;
            if ((type == 0x0020 || (type == 0x0001 && !found)) && al >= 8 && d[p + 5] == 0x01) {
                uint16_t port = (uint16_t)((d[p + 6] << 8) | d[p + 7]);
                uint32_t ip = ((uint32_t)d[p + 8] << 24) | ((uint32_t)d[p + 9] << 16) | ((uint32_t)d[p + 10] << 8) | d[p + 11];
                if (type == 0x0020) {
                    port ^= 0x2112;
                    ip ^= 0x2112A442u;
                }
                a.host = htonl(ip);
                a.port = port;
                found = true;
            }
            p += 4 + ((al + 3) & ~(size_t)3);
        }
        if (found) {
            std::lock_guard<std::mutex> l(G().m);
            G().stun.push_back({tx, a});
        }
        return 1;
    }
    if (n >= 8 && memcmp(d, "BSPUNCH", 7) == 0) {
        std::lock_guard<std::mutex> l(G().m);
        if (G().punched.size() < 64) G().punched.push_back({h, h->receivedAddress});
        return 1;
    }
    return 0;
}

void SendRaw(ENetHost* h, const ENetAddress& to, const void* data, size_t n) {
    ENetBuffer b;
    b.data = (void*)data;
    b.dataLength = n;
    enet_socket_send(h->socket, &to, &b, 1);
}

void Punch(ENetHost* h, const ENetAddress& to) { SendRaw(h, to, "BSPUNCH!", 8); }

// what's reached this host by a punch (taken off the list)
std::vector<ENetAddress> TakePunched(ENetHost* h) {
    std::vector<ENetAddress> out;
    std::lock_guard<std::mutex> l(G().m);
    auto& v = G().punched;
    for (size_t i = 0; i < v.size();)
        if (v[i].first == h) {
            out.push_back(v[i].second);
            v.erase(v.begin() + i);
        } else ++i;
    return out;
}

bool Same(const ENetAddress& a, const ENetAddress& b) { return a.host == b.host && a.port == b.port; }

// Our public address, as STUN servers see this socket
bool StunQuery(ENetHost* h, ENetAddress& out, const std::atomic<bool>& stop) {
    struct S { const char* host; uint16_t port; } servers[] = {{"stun.l.google.com", 19302}, {"stun1.l.google.com", 19302}, {"stun.cloudflare.com", 3478}};
    std::random_device rd;
    for (int round = 0; round < 2; ++round)
        for (const S& s : servers) {
            if (stop) return false;
            ENetAddress a;
            if (enet_address_set_host(&a, s.host) != 0) continue;
            a.port = s.port;
            uint8_t req[20] = {0x00, 0x01, 0x00, 0x00, 0x21, 0x12, 0xA4, 0x42};
            for (int i = 8; i < 20; ++i) req[i] = (uint8_t)rd();
            std::string tx((const char*)req + 8, 12);
            for (int k = 0; k < 10 && !stop; ++k) {
                if (k % 4 == 0) SendRaw(h, a, req, 20);
                std::this_thread::sleep_for(std::chrono::milliseconds(80));
                std::lock_guard<std::mutex> l(G().m);
                auto& v = G().stun;
                for (size_t i = 0; i < v.size(); ++i)
                    if (v[i].first == tx) {
                        out = v[i].second;
                        v.erase(v.begin() + i);
                        return true;
                    }
            }
        }
    return false;
}

// this machine's address on its own network (the one a route to the internet goes out of)
uint32_t LanIp() {
    ENetSocket s = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
    if (s == ENET_SOCKET_NULL) return 0;
    ENetAddress to, me{};
    enet_address_set_host_ip(&to, "8.8.8.8");
    to.port = 53;
    uint32_t ip = 0;
    if (enet_socket_connect(s, &to) == 0 && enet_socket_get_address(s, &me) == 0) ip = me.host;
    enet_socket_destroy(s);
    return ip;
}

std::string Str(const ENetAddress& a) {
    char ip[64] = "?";
    enet_address_get_host_ip(&a, ip, sizeof ip);
    return std::string(ip) + ":" + std::to_string(a.port);
}

bool ParseAddr(const std::string& s, ENetAddress& a) {
    size_t c = s.rfind(':');
    if (c == std::string::npos) return false;
    if (enet_address_set_host_ip(&a, s.substr(0, c).c_str()) != 0) return false;
    a.port = (uint16_t)atoi(s.c_str() + c + 1);
    return a.port != 0;
}

// the card each side hands the other: "ver=18;pub=1.2.3.4:27615;lan=192.168.1.5:27615"
std::string Card(int protocol, bool hasPub, const ENetAddress& pub, const ENetAddress& lan) {
    std::string s = "ver=" + std::to_string(protocol);
    if (hasPub) s += ";pub=" + Str(pub);
    if (lan.host) s += ";lan=" + Str(lan);
    return s;
}

struct CardInfo {
    int ver = -1;
    bool hasPub = false, hasLan = false;
    ENetAddress pub{}, lan{};
};
CardInfo ParseCard(const std::string& s) {
    CardInfo c;
    size_t p = 0;
    while (p < s.size()) {
        size_t e = s.find(';', p);
        if (e == std::string::npos) e = s.size();
        std::string kv = s.substr(p, e - p);
        if (kv.rfind("ver=", 0) == 0) c.ver = atoi(kv.c_str() + 4);
        else if (kv.rfind("pub=", 0) == 0) c.hasPub = ParseAddr(kv.substr(4), c.pub);
        else if (kv.rfind("lan=", 0) == 0) c.hasLan = ParseAddr(kv.substr(4), c.lan);
        p = e + 1;
    }
    return c;
}

// a string field; "type" is the message's own (the last one: the server writes the payload, which has a "type" of its
// own, first)
std::string Field(const std::string& j, const char* key) {
    std::string k = std::string("\"") + key + "\":\"";
    size_t p = strcmp(key, "type") == 0 ? j.rfind(k) : j.find(k);
    if (p == std::string::npos) return "";
    p += k.size();
    size_t e = j.find('"', p);
    return e == std::string::npos ? "" : j.substr(p, e - p);
}

// The cards travel as a WebRTC "candidate": the server checks each message's shape (an OFFER must carry a real session
// description, or it hangs up), but a candidate's text is free. kind: "offer" (joiner -> room) or "answer".
std::string Msg(const char* kind, const std::string& dst, const std::string& card) {
    return std::string("{\"type\":\"CANDIDATE\",\"payload\":{\"candidate\":{\"candidate\":\"bs1;") + kind + ";" + card +
           "\",\"sdpMid\":\"0\",\"sdpMLineIndex\":0},\"type\":\"data\",\"connectionId\":\"dc_bs" + RandHex(8) + "\"},\"dst\":\"" + dst +
           "\"}";
}

// a card of this kind in a forwarded message ("" if it isn't one)
std::string CardOf(const std::string& msg, const char* kind) {
    std::string c = Field(msg, "candidate"), pre = std::string("bs1;") + kind + ";";
    return c.rfind(pre, 0) == 0 ? c.substr(pre.size()) : "";
}

// ---- the signalling server: PeerJS's, over a WebSocket ---------------------------------------------------------------
#ifdef _WIN32
struct Ws {
    HINTERNET ses = nullptr, con = nullptr, ws = nullptr;
    std::mutex sm;
    bool Open(const std::string& path, std::string& err) {
        ses = WinHttpOpen(L"Broadside", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!ses) { err = "No internet access"; return false; }
        WinHttpSetTimeouts(ses, 8000, 8000, 8000, 10000);
        con = WinHttpConnect(ses, L"0.peerjs.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
        std::wstring wp(path.begin(), path.end());
        HINTERNET req = con ? WinHttpOpenRequest(con, L"GET", wp.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                 WINHTTP_FLAG_SECURE)
                            : nullptr;
        if (!req) { err = "Can't reach the room server"; Close(); return false; }
        WinHttpSetOption(req, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0);
        DWORD status = 0, sz = sizeof status;
        if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(req, nullptr) ||
            !WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz,
                                 WINHTTP_NO_HEADER_INDEX) ||
            status != 101) {
            err = status ? "The room server said no (HTTP " + std::to_string(status) + ")" : "Can't reach the room server (0.peerjs.com)";
            WinHttpCloseHandle(req);
            Close();
            return false;
        }
        ws = WinHttpWebSocketCompleteUpgrade(req, 0);
        WinHttpCloseHandle(req);
        if (!ws) { err = "The room server's connection failed"; Close(); return false; }
        DWORD forever = 0;  // (a quiet room is normal: don't time out between messages)
        WinHttpSetOption(ws, WINHTTP_OPTION_RECEIVE_TIMEOUT, &forever, sizeof forever);
        return true;
    }
    bool Recv(std::string& out) {
        out.clear();
        char buf[8192];
        for (int idle = 0;;) {
            DWORD got = 0;
            WINHTTP_WEB_SOCKET_BUFFER_TYPE t;
            HINTERNET w = ws;
            if (!w) return false;
            DWORD r = WinHttpWebSocketReceive(w, buf, sizeof buf, &got, &t);
            if (r == ERROR_WINHTTP_TIMEOUT && ++idle < 1000) continue;  // (quiet is normal)
            if (r != NO_ERROR || t == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) return false;
            out.append(buf, got);
            if (t == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE || t == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE) return true;
        }
    }
    bool Send(const std::string& s) {
        std::lock_guard<std::mutex> l(sm);
        Log("-> %s", s.substr(0, 200));
        DWORD r = ws ? WinHttpWebSocketSend(ws, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE, (PVOID)s.data(), (DWORD)s.size()) : 1;
        Log("   sent (%s)", std::to_string(r));
        return r == NO_ERROR;
    }
    void Close() {
        std::lock_guard<std::mutex> l(sm);
        if (ws) WinHttpCloseHandle(ws);
        if (con) WinHttpCloseHandle(con);
        if (ses) WinHttpCloseHandle(ses);
        ws = con = ses = nullptr;
    }
};
#else
struct Ws {
    bool Open(const std::string&, std::string& err) { err = "Rooms need the Windows build"; return false; }
    bool Recv(std::string&) { return false; }
    bool Send(const std::string&) { return false; }
    void Close() {}
};
#endif

}  // namespace

// ---- shared by the host and the joiner ---------------------------------------------------------------------------
struct Common {
    std::atomic<bool> stop{false};
    mutable std::mutex m;
    State state = State::Working;
    std::string describe, name;
    ENetHost* host = nullptr;
    int protocol = 0;
    Ws ws;
    std::thread worker, reader;
    std::mutex qm;
    std::deque<std::string> inbox;
    std::atomic<bool> wsDown{false};
    std::string err;

    void Set(State s, const std::string& d) {
        std::lock_guard<std::mutex> l(m);
        state = s;
        describe = d;
    }
    bool Up(const std::string& id) {
        wsDown = false;
        Log("signalling as %s", id);
        std::string path = "/peerjs?key=peerjs&id=" + id + "&token=" + RandHex(10) + "&version=1.5.4";
        if (!ws.Open(path, err)) {
            Log("signalling failed: %s", err);
            return false;
        }
        reader = std::thread([this] {
            std::string msg;
            while (!stop && ws.Recv(msg)) {
                Log("<- %s", msg.substr(0, 300));
                std::lock_guard<std::mutex> l(qm);
                inbox.push_back(msg);
            }
            wsDown = true;
        });
        return true;
    }
    void Down() {
        ws.Close();
        if (reader.joinable()) reader.join();
        std::lock_guard<std::mutex> l(qm);
        inbox.clear();
    }
    bool Next(std::string& msg) {
        std::lock_guard<std::mutex> l(qm);
        if (inbox.empty()) return false;
        msg = inbox.front();
        inbox.pop_front();
        return true;
    }
    void Nap(int ms) {
        for (int t = 0; t < ms && !stop; t += 10) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
};

struct Host::Impl : Common {
    uint16_t port = 0;
    int visitors = 0;
};
struct Join::Impl : Common {
    bool have = false;
    ENetAddress found{};
};

// ---- the host -----------------------------------------------------------------------------------------------------
static void HostRun(std::shared_ptr<Host::Impl> me) {
    Host::Impl& I = *me;
    I.Set(State::Working, "Putting the room up...");
    ENetAddress pub{};
    bool hasPub = StunQuery(I.host, pub, I.stop);
    double stunAt = Now();
    ENetAddress lan{LanIp(), I.port};
    struct Target { ENetAddress a; double until; };
    std::vector<Target> targets;
    bool up = false;
    double retryAt = 0.0, beat = 0.0, punchAt = 0.0;
    auto addTarget = [&](const ENetAddress& a, double secs) {
        for (Target& t : targets)
            if (Same(t.a, a)) { t.until = Now() + secs; return; }
        targets.push_back({a, Now() + secs});
    };
    while (!I.stop) {
        double now = Now();
        if (!up && now >= retryAt) {
            std::string nm;
            { std::lock_guard<std::mutex> l(I.m); nm = I.name; }
            if (I.Up("broadside-" + nm)) {
                up = true;
                beat = now + 5.0;
            } else {
                I.Set(State::Failed, I.err + " - trying again");
                retryAt = now + 5.0;
            }
        }
        if (up && I.wsDown) {
            I.Down();
            up = false;
            retryAt = now + 2.0;
            I.Set(State::Working, "Lost the room server - reconnecting");
        }
        if (up && now >= beat) {
            I.ws.Send("{\"type\":\"HEARTBEAT\"}");
            beat = now + 5.0;
        }
        std::string msg;
        while (up && I.Next(msg)) {
            std::string type = Field(msg, "type");
            if (type == "OPEN") {
                I.Set(State::Open, hasPub ? "" : "(couldn't see the public address: only the same network may find it)");
                printf("[rooms] room up as %s (public %s)\n", I.name.c_str(), hasPub ? Str(pub).c_str() : "unknown");
            } else if (type == "ID-TAKEN") {  // someone has that name: take another
                I.Down();
                up = false;
                retryAt = now;
                std::lock_guard<std::mutex> l(I.m);
                I.name = NewName();
            } else if (type == "ERROR" || type == "INVALID-KEY") {
                I.Down();
                up = false;
                retryAt = now + 10.0;
                I.Set(State::Failed, "The room server refused: " + Field(msg, "msg"));
            } else if (type == "CANDIDATE" && !CardOf(msg, "offer").empty()) {
                std::string src = Field(msg, "src");
                CardInfo c = ParseCard(CardOf(msg, "offer"));
                if (now - stunAt > 30.0) {
                    ENetAddress p{};
                    if (StunQuery(I.host, p, I.stop)) pub = p, hasPub = true;
                    stunAt = Now();
                }
                I.ws.Send(Msg("answer", src, Card(I.protocol, hasPub, pub, lan)));
                if (c.ver == I.protocol) {
                    if (c.hasPub) addTarget(c.pub, 15.0);
                    if (c.hasLan) addTarget(c.lan, 15.0);
                    std::lock_guard<std::mutex> l(I.m);
                    ++I.visitors;
                }
                printf("[rooms] a captain asks the way in (%s, version %d)\n", c.hasPub ? Str(c.pub).c_str() : "?", c.ver);
            }
        }
        // the joiner's punches show where they really come from (a router that changes ports): answer there too
        for (const ENetAddress& a : TakePunched(I.host)) addTarget(a, 15.0);
        if (now >= punchAt) {
            punchAt = now + 0.2;
            targets.erase(std::remove_if(targets.begin(), targets.end(), [&](const Target& t) { return t.until < now; }), targets.end());
            for (const Target& t : targets) Punch(I.host, t.a);
        }
        I.Nap(40);
    }
    I.Down();
}

void Host::Start(const std::string& name, void* enetHost, uint16_t port, int protocol) {
    Stop();
    impl = std::make_shared<Impl>();
    impl->name = Clean(name);
    impl->host = (ENetHost*)enetHost;
    impl->port = port;
    impl->protocol = protocol;
    auto me = impl;
    impl->worker = std::thread([me] { HostRun(me); });
}

void Host::Stop() {
    if (!impl) return;
    impl->stop = true;
    impl->ws.Close();  // (wakes the reader)
    if (impl->worker.joinable()) impl->worker.join();
    impl.reset();
}

State Host::GetState() const {
    if (!impl) return State::Idle;
    std::lock_guard<std::mutex> l(impl->m);
    return impl->state;
}
std::string Host::Name() const {
    if (!impl) return "";
    std::lock_guard<std::mutex> l(impl->m);
    return impl->name;
}
std::string Host::Describe() const {
    if (!impl) return "";
    std::lock_guard<std::mutex> l(impl->m);
    return impl->describe;
}
int Host::Visitors() const {
    if (!impl) return 0;
    std::lock_guard<std::mutex> l(impl->m);
    return impl->visitors;
}

// ---- the joiner ---------------------------------------------------------------------------------------------------
static void JoinRun(std::shared_ptr<Join::Impl> me) {
    Join::Impl& I = *me;
    const std::string hostId = "broadside-" + I.name;
    I.Set(State::Working, "Finding the way out...");
    ENetAddress pub{};
    bool hasPub = StunQuery(I.host, pub, I.stop);
    Log("joiner's public address: %s", hasPub ? Str(pub) : "unknown");
    ENetAddress bound{};
    enet_socket_get_address(I.host->socket, &bound);
    ENetAddress lan{LanIp(), bound.port};
    TakePunched(I.host);
    I.Set(State::Working, "Looking for the room...");
    if (!I.Up("broadside-j" + RandHex(12))) {
        I.Set(State::Failed, I.err);
        return;
    }
    const bool forcePub = getenv("BS_ROOMPUB") != nullptr;  // (testing: through the router even on one network)
    double deadline = Now() + 15.0, beat = Now() + 5.0, answered = -1.0, punchAt = 0.0;
    std::vector<ENetAddress> targets;
    ENetAddress choice{};
    while (!I.stop) {
        double now = Now();
        if (I.wsDown && answered < 0.0) {
            I.Set(State::Failed, "Lost the room server");
            break;
        }
        if (now >= beat) {
            I.ws.Send("{\"type\":\"HEARTBEAT\"}");
            beat = now + 5.0;
        }
        std::string msg;
        while (I.Next(msg)) {
            std::string type = Field(msg, "type"), src = Field(msg, "src");
            if (type == "OPEN") {
                I.ws.Send(Msg("offer", hostId, Card(I.protocol, hasPub, pub, lan)));
            } else if (type == "EXPIRE" && answered < 0.0) {
                I.Set(State::Failed, "No room called \"" + I.name + "\" - check the name (the host's game must be open)");
                I.stop = true;
            } else if (type == "CANDIDATE" && src == hostId && answered < 0.0 && !CardOf(msg, "answer").empty()) {
                CardInfo c = ParseCard(CardOf(msg, "answer"));
                if (c.ver != I.protocol) {
                    I.Set(State::Failed, "That room runs another version of Broadside (" + std::to_string(c.ver) + ", this is " +
                                             std::to_string(I.protocol) + "): everyone needs the same one");
                    I.stop = true;
                    break;
                }
                answered = now;
                if (c.hasPub) targets.push_back(c.pub);
                if (c.hasLan && !forcePub) targets.push_back(c.lan);
                // the same public address: the same network (or the same PC), go straight in
                bool sameNet = hasPub && c.hasPub && c.pub.host == pub.host && c.hasLan;
                choice = (sameNet && !forcePub) || !c.hasPub ? c.lan : c.pub;
                I.Set(State::Working, "Found the room - getting through...");
            } else if (type == "ERROR") {
                I.Set(State::Failed, "The room server refused: " + Field(msg, "msg"));
                I.stop = true;
            }
        }
        if (I.stop) break;
        if (answered >= 0.0) {
            if (now >= punchAt) {
                punchAt = now + 0.2;
                for (const ENetAddress& a : targets) Punch(I.host, a);
            }
            bool have;
            { std::lock_guard<std::mutex> l(I.m); have = I.have; }
            if (!have) {
                // a punch from the host shows a way in that works: take it (or the best guess after a moment)
                for (const ENetAddress& a : TakePunched(I.host))
                    if (!forcePub || Same(a, targets.front())) { choice = a; have = true; }
                if (now - answered > 1.5) have = true;
                if (have) {
                    std::lock_guard<std::mutex> l(I.m);
                    I.found = choice;
                    I.have = true;
                    printf("[rooms] room %s: going in at %s\n", I.name.c_str(), Str(choice).c_str());
                }
            }
            if (now - answered > 10.0) break;  // (ENet has taken it from here)
        } else if (now > deadline) {
            I.Set(State::Failed, "The room didn't answer - try again, or ask the host for their address");
            break;
        }
        I.Nap(30);
    }
    I.Down();
}

void Join::Start(const std::string& name, void* enetHost, int protocol) {
    Stop();
    impl = std::make_shared<Impl>();
    impl->name = Clean(name);
    impl->host = (ENetHost*)enetHost;
    impl->protocol = protocol;
    auto me = impl;
    impl->worker = std::thread([me] { JoinRun(me); });
}

void Join::Stop() {
    if (!impl) return;
    impl->stop = true;
    impl->ws.Close();
    if (impl->worker.joinable()) impl->worker.join();
    impl.reset();
}

int Join::Result(uint32_t& host, uint16_t& port) const {
    if (!impl) return -1;
    std::lock_guard<std::mutex> l(impl->m);
    if (impl->state == State::Failed) return -1;
    if (!impl->have) return 0;
    host = impl->found.host;
    port = impl->found.port;
    return 1;
}

std::string Join::Describe() const {
    if (!impl) return "";
    std::lock_guard<std::mutex> l(impl->m);
    return impl->describe;
}

// ---- names --------------------------------------------------------------------------------------------------------
std::string NewName() {
    static const char* const kA[] = {"salty", "stormy", "rusty", "jolly", "grim", "golden", "black", "brave", "foggy", "sunken",
                                     "rolling", "crimson", "silent", "wild", "iron", "lucky"};
    static const char* const kB[] = {"kraken", "parrot", "anchor", "cutlass", "galleon", "barnacle", "walrus", "lantern", "squall", "reef",
                                     "shanty", "gull", "harpoon", "corsair", "tide", "mermaid"};
    std::random_device rd;
    return std::string(kA[rd() % 16]) + "-" + kB[rd() % 16] + "-" + std::to_string(10 + rd() % 90);
}

std::string Clean(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c == ' ' || c == '_') c = '-';
        bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
        if (!ok || (c == '-' && (o.empty() || o.back() == '-'))) continue;
        o += c;
    }
    while (!o.empty() && o.back() == '-') o.pop_back();
    if (o.size() > 40) o.resize(40);
    return o;
}

bool LooksLikeRoom(const std::string& typed) {
    if (typed.find('.') != std::string::npos || typed.find(':') != std::string::npos) return false;
    std::string c = Clean(typed);
    if (c.empty() || c == "localhost") return false;
    return std::any_of(c.begin(), c.end(), [](char ch) { return ch >= 'a' && ch <= 'z'; });
}

void Hook(void* enetHost) {
    if (enetHost) ((ENetHost*)enetHost)->intercept = Intercept;
}

}  // namespace rooms
