// Rooms: join a game by a name ("kraken-rum-42") instead of an address, the way qwop-row does it.
// The host's server puts its room up on PeerJS's free signalling server (wss://0.peerjs.com, the same one qwop-row's
// PeerJS uses) under the id "broadside-<name>". A joiner asks that id for the way in: both sides learn their public
// address from a STUN server (asked from the game's own UDP socket), swap addresses through the signalling server, and
// send each other a few UDP packets so both routers let the other side's game traffic in ("hole punching"). Then ENet
// connects as it always did. No relay: two routers that both refuse (symmetric NAT, some mobile carriers) can't meet
// this way, and the address is the way in then. Kept free of raylib and windows.h.
#pragma once
#include <cstdint>
#include <memory>
#include <string>

namespace rooms {

std::string NewName();                          // a fresh name: two sea words and a number
std::string Clean(const std::string& s);        // lower case, spaces to '-', only a-z 0-9 '-'
bool LooksLikeRoom(const std::string& typed);   // a name rather than an address ("1.2.3.4", "host:port", "localhost")
void Hook(void* enetHost);                      // let the rooms see this ENet host's raw UDP (STUN answers, punches)

enum class State { Idle, Working, Open, Failed };

// A server's room
class Host {
public:
    void Start(const std::string& name, void* enetHost, uint16_t port, int protocol);
    void Stop();
    State GetState() const;
    std::string Name() const;
    std::string Describe() const;  // for the HUD: what's happening, or what went wrong
    int Visitors() const;          // captains who found the room so far
    ~Host() { Stop(); }
    struct Impl;

private:
    std::shared_ptr<Impl> impl;
};

// Looking a room up from a client's ENet host
class Join {
public:
    void Start(const std::string& name, void* enetHost, int protocol);
    void Stop();
    // 0 still looking, 1 found (address and port as ENet takes them: host in network order), -1 failed (Describe)
    int Result(uint32_t& host, uint16_t& port) const;
    std::string Describe() const;
    ~Join() { Stop(); }
    struct Impl;

private:
    std::shared_ptr<Impl> impl;
};

}  // namespace rooms
