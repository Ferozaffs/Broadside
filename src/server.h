// Authoritative game server. Runs the World at 60 Hz on its own thread, receives player
// inputs and broadcasts 30 Hz snapshots + reliable events. Used for both "host" (listen
// server inside the game) and "--server" (dedicated, headless).
#pragma once
#include "net.h"
#include "sim.h"
#include <atomic>
#include <thread>

class GameServer {
public:
    bool Start(uint16_t port, int bots, uint32_t seed, int timeOfDay = 0, int weather = 0, int mode = 0, bool fleets = false,
               uint8_t allowedCls = SC_ALL, bool deckOnly = false, bool skills = false);
    void Stop();
    bool Running() const { return running; }
    ~GameServer() { Stop(); }

private:
    void Run();
    void HandlePacket(uint32_t peer, const std::vector<uint8_t>& data);
    void SendPlayerInfo(int shipId, int toPeer);  // toPeer < 0 = broadcast
    void AnnounceShips();  // tell everyone about ships that appeared (fleets) or went away

    std::thread thread;
    std::atomic<bool> running{false};
    NetServer net;
    World world;
    int peerShip[64];
    bool announced[MAX_SHIPS] = {};
};
