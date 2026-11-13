#include "upnp.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

namespace upnp {
namespace {

std::mutex mu;
std::atomic<int> state{(int)State::Idle};
std::string extIp, localIp, controlUrl, serviceType, routerIp;
uint16_t mappedPort = 0, wantPort = 0;
std::thread worker;

#ifdef _WIN32
struct Url { std::string host, path; uint16_t port = 80; };

bool ParseUrl(const std::string& u, Url& out) {
    const char* p = strstr(u.c_str(), "://");
    if (!p) return false;
    std::string rest = p + 3;
    size_t slash = rest.find('/');
    std::string hp = rest.substr(0, slash);
    out.path = slash == std::string::npos ? "/" : rest.substr(slash);
    size_t colon = hp.find(':');
    out.host = hp.substr(0, colon);
    out.port = colon == std::string::npos ? 80 : (uint16_t)atoi(hp.c_str() + colon + 1);
    return !out.host.empty();
}

std::string Tag(const std::string& xml, const std::string& tag, size_t from = 0, size_t* at = nullptr) {
    // first <tag>...</tag> (namespace prefixes on the tag are allowed)
    size_t i = from;
    while ((i = xml.find(tag + ">", i)) != std::string::npos) {
        size_t open = xml.rfind('<', i);
        if (open != std::string::npos && xml[open + 1] != '/') {
            size_t s = i + tag.size() + 1, e = xml.find("</", s);
            if (e == std::string::npos) return {};
            if (at) *at = e;
            return xml.substr(s, e - s);
        }
        i += tag.size();
    }
    return {};
}

// One HTTP request over TCP. Returns the whole response ("" on failure). `local` gets our LAN address.
std::string Http(const Url& u, const std::string& req, std::string* local = nullptr) {
    addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(u.host.c_str(), std::to_string(u.port).c_str(), &hints, &res) != 0 || !res) return {};
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    DWORD tmo = 3000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tmo, sizeof(tmo));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tmo, sizeof(tmo));
    std::string out;
    if (connect(s, res->ai_addr, (int)res->ai_addrlen) == 0) {
        if (local) {
            sockaddr_in me{};
            int len = sizeof(me);
            if (getsockname(s, (sockaddr*)&me, &len) == 0) {
                char buf[64];
                inet_ntop(AF_INET, &me.sin_addr, buf, sizeof(buf));
                *local = buf;
            }
        }
        send(s, req.c_str(), (int)req.size(), 0);
        char buf[4096];
        int n;
        while ((n = recv(s, buf, sizeof(buf), 0)) > 0 && out.size() < 512 * 1024) out.append(buf, n);
    }
    closesocket(s);
    freeaddrinfo(res);
    return out;
}

std::string Get(const std::string& url, std::string* local = nullptr) {
    Url u;
    if (!ParseUrl(url, u)) return {};
    std::string req = "GET " + u.path + " HTTP/1.1\r\nHost: " + u.host + ":" + std::to_string(u.port) + "\r\nConnection: close\r\n\r\n";
    return Http(u, req, local);
}

std::string Soap(const std::string& action, const std::string& args) {
    Url u;
    if (!ParseUrl(controlUrl, u)) return {};
    std::string body = "<?xml version=\"1.0\"?>\r\n"
                       "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
                       "<s:Body><u:" + action + " xmlns:u=\"" + serviceType + "\">" + args + "</u:" + action + "></s:Body></s:Envelope>\r\n";
    std::string req = "POST " + u.path + " HTTP/1.1\r\nHost: " + u.host + ":" + std::to_string(u.port) +
                      "\r\nContent-Type: text/xml; charset=\"utf-8\"\r\nSOAPAction: \"" + serviceType + "#" + action +
                      "\"\r\nContent-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
    return Http(u, req);
}

bool Ok(const std::string& resp) { return resp.compare(0, 12, "HTTP/1.1 200") == 0 || resp.compare(0, 12, "HTTP/1.0 200") == 0; }

// SSDP: ask the LAN for internet gateways, return the first device description URL
std::string Discover() {
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return {};
    DWORD tmo = 200;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tmo, sizeof(tmo));
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(1900);
    inet_pton(AF_INET, "239.255.255.250", &to.sin_addr);
    const char* targets[] = {"urn:schemas-upnp-org:device:InternetGatewayDevice:1", "urn:schemas-upnp-org:device:InternetGatewayDevice:2",
                             "urn:schemas-upnp-org:service:WANIPConnection:1", "urn:schemas-upnp-org:service:WANPPPConnection:1",
                             "upnp:rootdevice"};
    if (const char* t = getenv("BS_UPNP_SSDP")) {  // testing: a fake router at "ip:port"
        std::string v = t;
        size_t c = v.find(':');
        inet_pton(AF_INET, v.substr(0, c).c_str(), &to.sin_addr);
        if (c != std::string::npos) to.sin_port = htons((uint16_t)atoi(v.c_str() + c + 1));
    }
    std::string found;
    for (int round = 0; round < 2 && found.empty(); ++round) {
        for (const char* st : targets) {
            std::string m = std::string("M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 2\r\nST: ") + st + "\r\n\r\n";
            sendto(s, m.c_str(), (int)m.size(), 0, (sockaddr*)&to, sizeof(to));
        }
        DWORD until = GetTickCount() + (round == 0 ? 2000 : 1500);  // listen for a fixed window, however chatty the LAN is
        while (found.empty() && (int)(until - GetTickCount()) > 0) {
            char buf[2048];
            sockaddr_in from{};
            int fl = sizeof(from);
            int n = recvfrom(s, buf, sizeof(buf) - 1, 0, (sockaddr*)&from, &fl);
            if (n <= 0) continue;
            buf[n] = 0;
            {
                char ip[64];
                inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
                std::lock_guard<std::mutex> g(mu);
                if (routerIp.empty()) routerIp = ip;
            }
            std::string r = buf;
            for (auto& c : r) c = (char)tolower((unsigned char)c);
            if (r.find("internetgatewaydevice") == std::string::npos && r.find("wanipconnection") == std::string::npos &&
                r.find("wanpppconnection") == std::string::npos)
                continue;  // some other UPnP device (printers, the router's WPS service...)
            size_t at = r.find("\nlocation:");
            if (at == std::string::npos) continue;
            size_t s0 = at + 10, e = r.find('\r', s0);
            std::string loc = std::string(buf).substr(s0, e - s0);
            while (!loc.empty() && loc[0] == ' ') loc.erase(0, 1);
            found = loc;
        }
    }
    closesocket(s);
    return found;
}

// From the device description: the WAN connection service and its control URL
bool FindService(const std::string& descUrl) {
    std::string lan;
    std::string xml = Get(descUrl, &lan);
    if (xml.empty()) return false;
    std::string base = descUrl;
    std::string urlBase = Tag(xml, "URLBase");
    if (!urlBase.empty()) base = urlBase;
    Url b;
    if (!ParseUrl(base, b)) return false;
    const char* kinds[] = {"urn:schemas-upnp-org:service:WANIPConnection:2", "urn:schemas-upnp-org:service:WANIPConnection:1",
                           "urn:schemas-upnp-org:service:WANPPPConnection:1"};
    for (const char* k : kinds) {
        size_t at = xml.find(std::string(k) + "<");
        if (at == std::string::npos) continue;
        size_t end = 0;
        std::string ctl = Tag(xml, "controlURL", at, &end);
        if (ctl.empty()) continue;
        if (ctl.find("://") == std::string::npos) {
            if (ctl[0] != '/') ctl = "/" + ctl;
            ctl = "http://" + b.host + ":" + std::to_string(b.port) + ctl;
        }
        std::lock_guard<std::mutex> g(mu);
        controlUrl = ctl;
        serviceType = k;
        localIp = lan;
        return true;
    }
    return false;
}

bool QueryExternal() {
    std::string r = Soap("GetExternalIPAddress", "");
    std::string ip = Tag(r, "NewExternalIPAddress");
    std::lock_guard<std::mutex> g(mu);
    extIp = ip;
    return !ip.empty();
}

bool IsPrivate(const std::string& ip) {
    unsigned a = 0, b = 0;
    if (sscanf(ip.c_str(), "%u.%u", &a, &b) != 2) return false;
    return a == 10 || (a == 192 && b == 168) || (a == 172 && b >= 16 && b <= 31) || (a == 100 && b >= 64 && b <= 127) || a == 0;
}

bool Winsock() {
    static bool ok = [] {
        WSADATA w;
        return WSAStartup(MAKEWORD(2, 2), &w) == 0;
    }();
    return ok;
}

void GuessLocal() {
    std::string target;
    {
        std::lock_guard<std::mutex> g(mu);
        if (!localIp.empty()) return;
        target = routerIp.empty() ? "192.168.1.1" : routerIp;
    }
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(1900);
    inet_pton(AF_INET, target.c_str(), &to.sin_addr);
    if (connect(s, (sockaddr*)&to, sizeof(to)) == 0) {  // UDP connect sends nothing, it just picks the route
        sockaddr_in me{};
        int len = sizeof(me);
        if (getsockname(s, (sockaddr*)&me, &len) == 0) {
            char buf[64];
            inet_ntop(AF_INET, &me.sin_addr, buf, sizeof(buf));
            std::lock_guard<std::mutex> g(mu);
            localIp = buf;
        }
    }
    closesocket(s);
}

void Run(uint16_t port) {
    if (!Winsock()) { state = (int)State::NoRouter; return; }
    std::string desc = Discover();
    if (desc.empty() || !FindService(desc)) {
        GuessLocal();
        state = (int)State::NoRouter;
        printf("[upnp] %s\n", Describe().c_str());
        fflush(stdout);
        return;
    }
    QueryExternal();
    std::string p = std::to_string(port), lan;
    {
        std::lock_guard<std::mutex> g(mu);
        lan = localIp;
    }
    auto add = [&](int lease) {
        return Soap("AddPortMapping", "<NewRemoteHost></NewRemoteHost><NewExternalPort>" + p + "</NewExternalPort><NewProtocol>UDP</NewProtocol>"
                                      "<NewInternalPort>" + p + "</NewInternalPort><NewInternalClient>" + lan + "</NewInternalClient>"
                                      "<NewEnabled>1</NewEnabled><NewPortMappingDescription>Broadside</NewPortMappingDescription>"
                                      "<NewLeaseDuration>" + std::to_string(lease) + "</NewLeaseDuration>");
    };
    std::string r = add(7200);           // two hours, renewed by re-hosting; some routers only take permanent ones
    if (!Ok(r)) r = add(0);
    if (Ok(r)) {
        mappedPort = port;
        state = (int)State::Mapped;
        printf("[upnp] UDP %u forwarded to %s, public address %s\n", port, lan.c_str(), ExternalIp().c_str());
    } else {
        state = (int)State::Refused;
        printf("[upnp] the router refused the port mapping\n");
    }
    fflush(stdout);
}
#endif

}  // namespace

void Start(uint16_t port) {
#ifdef _WIN32
    Stop();
    wantPort = port;
    state = (int)State::Working;
    worker = std::thread(Run, port);
#else
    (void)port;
    state = (int)State::NoRouter;
#endif
}

void Stop() {
#ifdef _WIN32
    if (worker.joinable()) worker.join();
    if (mappedPort) {
        std::string p = std::to_string(mappedPort);
        Soap("DeletePortMapping", "<NewRemoteHost></NewRemoteHost><NewExternalPort>" + p + "</NewExternalPort><NewProtocol>UDP</NewProtocol>");
        printf("[upnp] UDP %u mapping removed\n", mappedPort);
        mappedPort = 0;
    }
#endif
    state = (int)State::Idle;
}

State GetState() { return (State)state.load(); }
std::string ExternalIp() { std::lock_guard<std::mutex> g(mu); return extIp; }
std::string LocalIp() { std::lock_guard<std::mutex> g(mu); return localIp; }
bool CarrierNat() {
#ifdef _WIN32
    std::string ip = ExternalIp();
    return !ip.empty() && IsPrivate(ip);
#else
    return false;
#endif
}

std::string Describe() {
    switch (GetState()) {
        case State::Working: return "Opening the port on your router...";
        case State::Mapped: return CarrierNat() ? "Port opened, but your internet provider shares your address (CGNAT): friends outside your network can't reach you"
                                                : "Port opened on your router";
        case State::NoRouter: {
            std::lock_guard<std::mutex> g(mu);
            std::string to = localIp.empty() ? "this PC" : localIp;
            if (!routerIp.empty())
                return "Your router (" + routerIp + ") has UPnP port forwarding switched off: turn it on in its settings, or forward UDP " +
                       std::to_string(wantPort) + " to " + to + " by hand";
            return "No router answered: for internet play forward UDP " + std::to_string(wantPort) + " to " + to + " by hand";
        }
        case State::Refused: {
            std::lock_guard<std::mutex> g(mu);
            return "The router refused to open the port: forward UDP " + std::to_string(wantPort) + " to " + localIp + " by hand";
        }
        default: return "";
    }
}

std::string Probe() {
#ifdef _WIN32
    if (!Winsock()) return "winsock failed";
    std::string desc = Discover();
    if (desc.empty()) return routerIp.empty() ? "no UPnP device answered" : "UPnP devices answered (first: " + routerIp + ") but none offers port forwarding (IGD)";
    if (!FindService(desc)) return "router found (" + desc + ") but no WAN connection service";
    bool ok = QueryExternal();
    std::string ip = ExternalIp();
    return std::string("router ") + desc + "\n  service " + serviceType + "\n  this PC on the LAN " + LocalIp() + "\n  public address " +
           (ok ? ip : std::string("(router would not say)")) + (ok && IsPrivate(ip) ? "  <- private: carrier-grade NAT" : "");
#else
    return "not supported on this platform";
#endif
}

}  // namespace upnp
