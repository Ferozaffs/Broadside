#include "server.h"
#include "protocol.h"
#include <chrono>
#include <cstdio>

static const char* kBotNames[] = {"Cpt. Bilgewater", "Mad Meg", "Admiral Soggy", "Barnacle Bess", "Old Salt", "Kraken Kate", "Dread Ned", "Salty Sue"};

bool GameServer::Start(uint16_t port, int bots, uint32_t seed, int timeOfDay, int weather, int mode, bool fleets) {
    Stop();
    if (!NetInit()) return false;
    if (!net.Start(port, 32)) {
        printf("[server] could not bind UDP port %u\n", port);
        return false;
    }
    for (int& p : peerShip) p = -1;
    world.Init(seed, (uint8_t)weather, (uint8_t)timeOfDay, (uint8_t)mode, fleets);
    for (int i = 0; i < bots; ++i)
        if (world.AddShip(kBotNames[i % 8], true) < 0) break;
    world.events.clear();
    for (int i = 0; i < MAX_SHIPS; ++i) announced[i] = world.ships[i].active;
    running = true;
    thread = std::thread([this] { Run(); });
    printf("[server] listening on UDP %u, seed %u, %d bot(s)\n", port, seed, bots);
    return true;
}

void GameServer::Stop() {
    if (!running) return;
    running = false;
    if (thread.joinable()) thread.join();
    net.Stop();
}

void GameServer::SendPlayerInfo(int id, int toPeer) {
    const Ship& s = world.ships[id];
    ByteWriter w;
    w.Put(MSG_PLAYER_INFO);
    w.Put((uint8_t)id);
    w.Put((uint8_t)(s.isBot ? 1 : 0));
    w.PutString(s.name);
    w.Put(s.leader);
    if (toPeer < 0) net.Broadcast(w.buf.data(), w.buf.size(), true);
    else net.Send((uint32_t)toPeer, w.buf.data(), w.buf.size(), true);
}

void GameServer::AnnounceShips() {
    for (int i = 0; i < MAX_SHIPS; ++i) {
        bool on = world.ships[i].active;
        if (on && !announced[i]) SendPlayerInfo(i, -1);
        if (!on && announced[i]) {
            ByteWriter w;
            w.Put(MSG_PLAYER_LEFT);
            w.Put((uint8_t)i);
            net.Broadcast(w.buf.data(), w.buf.size(), true);
        }
        announced[i] = on;
    }
}

void GameServer::HandlePacket(uint32_t peer, const std::vector<uint8_t>& data) {
    if (data.empty() || peer >= 64) return;
    ByteReader r(data);
    uint8_t type = r.Get<uint8_t>();
    if (type == MSG_HELLO) {
        uint16_t ver = r.Get<uint16_t>();
        std::string name = r.GetString();
        if (name.empty()) name = "Sailor";
        auto reject = [&](const char* why) {
            ByteWriter w;
            w.Put(MSG_REJECT);
            w.PutString(why);
            net.Send(peer, w.buf.data(), w.buf.size(), true);
            net.Kick(peer);
        };
        if (ver != PROTOCOL_VERSION) return reject("version mismatch");
        if (peerShip[peer] >= 0) return;
        int id = world.AddShip(name, false);
        if (id < 0) return reject("server full");
        peerShip[peer] = id;
        ByteWriter w;
        w.Put(MSG_WELCOME);
        w.Put((uint8_t)id);
        w.Put(world.oceanParams.seed);
        w.Put(world.timeOfDay);
        w.Put(world.weather);
        w.Put(world.mode);
        w.Put((uint8_t)(world.fleets ? 1 : 0));
        w.Put(world.time);
        net.Send(peer, w.buf.data(), w.buf.size(), true);
        for (int i = 0; i < MAX_SHIPS; ++i)
            if (world.ships[i].active && announced[i] && i != id) SendPlayerInfo(i, (int)peer);
        AnnounceShips();  // the new captain (and fleet) to everyone
        printf("[server] %s joined as ship %d\n", name.c_str(), id);
    } else if (type == MSG_INPUT) {
        int id = peerShip[peer];
        if (id < 0) return;
        PlayerInput in;
        Ship& s = world.ships[id];
        // unreliable channel: ignore inputs that arrive out of order
        if (ReadInput(r, in) && (in.seq > s.lastInputSeq || s.lastInputSeq - in.seq > 100000)) {
            s.input = in;
            s.lastInputSeq = in.seq;
        }
    }
}

void GameServer::Run() {
    using clock = std::chrono::steady_clock;
    NetHighResTimer();
    auto last = clock::now();
    double acc = 0.0;
    std::vector<NetEvent> events;
    while (running) {
        events.clear();
        net.Poll(events);
        for (auto& e : events) {
            if (e.type == NetEvent::Receive) HandlePacket(e.peer, e.data);
            else if (e.type == NetEvent::Disconnect && e.peer < 64 && peerShip[e.peer] >= 0) {
                int id = peerShip[e.peer];
                printf("[server] %s left\n", world.ships[id].name.c_str());
                world.RemoveShip(id);
                peerShip[e.peer] = -1;
                AnnounceShips();
            }
        }

        auto now = clock::now();
        acc += std::chrono::duration<double>(now - last).count();
        last = now;
        if (acc > 0.25) acc = 0.25;  // don't spiral after a hitch
        bool sent = false;
        while (acc >= SIM_DT) {
            acc -= SIM_DT;
            world.Step();
            AnnounceShips();
            for (const GameEvent& ge : world.events) {
                ByteWriter w;
                WriteEvent(w, ge);
                net.Broadcast(w.buf.data(), w.buf.size(), true);
            }
            world.events.clear();
            if (world.tick % SNAPSHOT_EVERY_TICKS == 0) {
                ByteWriter w;
                WriteSnapshot(w, world);
                net.Broadcast(w.buf.data(), w.buf.size(), false);
            }
            sent = true;
        }
        if (sent) net.Flush();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
