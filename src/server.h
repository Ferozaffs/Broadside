// Authoritative game server. Runs the World at 60 Hz on its own thread, receives player
// inputs and broadcasts 30 Hz snapshots + reliable events. Used for both "host" (listen
// server inside the game) and "--server" (dedicated, headless).
#pragma once
#include "net.h"
#include "rooms.h"
#include <string>
#include "sim.h"
#include <atomic>
#include <thread>

class GameServer {
public:
    bool Start(uint16_t port, int bots, uint32_t seed, int timeOfDay = 0, int weather = 0, int mode = 0, bool fleets = false,
               uint8_t allowedCls = SC_ALL, bool deckOnly = false, bool skills = false, int arena = ARENA_ISLES);
    void Stop();
    bool Running() const { return running; }
    // put the game up as a room (src/rooms.h) so friends join by its name
    void OpenRoom(const std::string& name);
    const rooms::Host& Room() const { return room; }
    ~GameServer() { Stop(); }

private:
    void Run();
    void HandlePacket(uint32_t peer, const std::vector<uint8_t>& data);
    void SendPlayerInfo(int shipId, int toPeer);  // toPeer < 0 = broadcast
    void AnnounceShips();  // tell everyone about ships that appeared (fleets) or went away

    std::thread thread;
    std::atomic<bool> running{false};
    NetServer net;
    rooms::Host room;
    uint16_t port = 0;
    World world;
    int peerShip[64];
    int peerCrew[64];
    std::vector<uint8_t> peerLivery[64];  // each captain's paint (Livery::Encode), passed on with their ship's info
    void SendCrewInfo(int crewId, int toPeer);
    bool announced[MAX_SHIPS] = {};
};
