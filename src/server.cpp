#include "server.h"
#include "protocol.h"
#include "livery.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>

static const char* kBotNames[] = {"Cpt. Bilgewater", "Mad Meg", "Admiral Soggy", "Barnacle Bess", "Old Salt", "Kraken Kate", "Dread Ned", "Salty Sue",
                                  "Peg-leg Pete", "Rusty Anne", "Cpt. Scurvy", "One-eyed Jack", "Bosun Bart", "Stormy Stella", "Gunner Gus", "Long Tom",
                                  "Smoky Joe", "Mistress Marlin", "Cpt. Cutlass", "Nell the Knot", "Squid Vicious", "Lady Leeward", "Fishface Finn", "Black Molly"};
static constexpr int kBotNameCount = sizeof(kBotNames) / sizeof(kBotNames[0]);

bool GameServer::Start(uint16_t port, int bots, uint32_t seed, int timeOfDay, int weather, int mode, bool fleets, uint8_t allowedCls,
                       bool deckOnly, bool skills, int arena, bool realistic, bool friendlyFire, int difficulty, int boss) {
    Stop();
    if (!NetInit()) return false;
    if (!net.Start(port, kMaxPeers)) {
        printf("[server] could not bind UDP port %u\n", port);
        return false;
    }
    for (int& p : peerShip) p = -1;
    for (int& p : peerCrew) p = -1;
    for (auto& l : peerLivery) l.clear();
    if (const char* e = getenv("BS_ROUNDLEN")) world.roundLength = (float)atof(e);  // testing: short rounds
    world.Init(seed, (uint8_t)weather, (uint8_t)timeOfDay, (uint8_t)mode, fleets, (uint8_t)arena);
    world.allowedCls = (allowedCls & SC_ALL) ? (uint8_t)(allowedCls & SC_ALL) : SC_ALL;
    world.deckOnly = deckOnly;
    world.skills = skills || mode == MODE_RACE;  // (dash and hook are what a race is about)
    world.realistic = realistic;
    world.friendlyFire = friendlyFire;
    world.difficulty = (uint8_t)(difficulty >= 0 && difficulty < voyage::DIFFICULTIES ? difficulty : voyage::NORMAL);
    world.bossChoice = (uint8_t)(boss >= 0 && boss < BOSS_KINDS ? boss : BOSS_RANDOM);
    for (int i = 0; i < bots; ++i) {  // (as many as there are berths: past the list the names come round again, numbered)
        std::string nm = kBotNames[i % kBotNameCount];
        if (i >= kBotNameCount) nm += " " + std::to_string(i / kBotNameCount + 1);
        if (world.AddShip(nm, true) < 0) break;
    }
    world.events.clear();
    announced.assign(world.ships.size(), false);
    for (int i = 0; i < world.ShipCount(); ++i) announced[i] = world.ships[i].active;
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
    w.Put((uint16_t)id);
    w.Put((uint8_t)(s.isBot ? 1 : 0));
    w.PutString(s.name);
    w.Put(s.leader);
    std::vector<uint8_t> none;
    const std::vector<uint8_t>* liv = &none;
    for (int p = 0; p < kMaxPeers; ++p)
        if (peerShip[p] == id) liv = &peerLivery[p];
    PutBlob(w.buf, *liv);
    if (toPeer < 0) net.Broadcast(w.buf.data(), w.buf.size(), true);
    else net.Send((uint32_t)toPeer, w.buf.data(), w.buf.size(), true);
}

void GameServer::SendCrewInfo(int id, int toPeer) {
    ByteWriter w;
    w.Put(MSG_CREW_INFO);
    w.Put((uint16_t)id);
    w.PutString(world.crew[id].name);
    if (toPeer < 0) net.Broadcast(w.buf.data(), w.buf.size(), true);
    else net.Send((uint32_t)toPeer, w.buf.data(), w.buf.size(), true);
}

void GameServer::AnnounceShips() {
    if ((int)announced.size() < world.ShipCount()) announced.resize(world.ShipCount(), false);
    for (int i = 0; i < world.ShipCount(); ++i) {
        bool on = world.ships[i].active;
        if (on && !announced[i]) SendPlayerInfo(i, -1);
        if (!on && announced[i]) {
            ByteWriter w;
            w.Put(MSG_PLAYER_LEFT);
            w.Put((uint16_t)i);
            net.Broadcast(w.buf.data(), w.buf.size(), true);
        }
        announced[i] = on;
    }
}

void GameServer::HandlePacket(uint32_t peer, const std::vector<uint8_t>& data) {
    if (data.empty() || peer >= (uint32_t)kMaxPeers) return;
    ByteReader r(data);
    uint8_t type = r.Get<uint8_t>();
    if (type == MSG_HELLO) {
        uint16_t ver = r.Get<uint16_t>();
        std::string name = r.GetString();
        uint8_t cls = r.Get<uint8_t>();
        if (!r.ok || cls >= SC_PLAYABLE) cls = SC_FRIGATE;
        uint8_t role = r.Get<uint8_t>();
        if (!r.ok) role = ROLE_CAPTAIN;
        std::vector<uint8_t> livery = r.GetBlob();
        if (!r.ok || livery.size() > (size_t)kLiveryMaxBytes) livery.clear();
        if (name.empty()) name = "Sailor";
        auto reject = [&](const char* why) {
            ByteWriter w;
            w.Put(MSG_REJECT);
            w.PutString(why);
            net.Send(peer, w.buf.data(), w.buf.size(), true);
            net.Kick(peer);
        };
        if (ver != PROTOCOL_VERSION) return reject("version mismatch");
        if (peerShip[peer] >= 0 || peerCrew[peer] >= 0) return;
        peerLivery[peer] = livery;
        int id = -1, crewId = -1;
        if (role == ROLE_CREW) {  // sign on aboard someone's ship (a bot takes the helm if nobody's sailing)
            crewId = world.AddCrew(name);
            if (crewId < 0) return reject("no room aboard");
            id = world.crew[crewId].ship;
            peerCrew[peer] = crewId;
        } else {
            id = world.AddShip(name, false, cls);
            if (id >= 0) world.ships[id].fixedCls = false;
            if (id < 0) return reject("server full");
            peerShip[peer] = id;
        }
        ByteWriter w;
        w.Put(MSG_WELCOME);
        w.Put((uint16_t)id);
        w.Put(world.oceanParams.seed);
        w.Put(world.timeOfDay);
        w.Put(world.weather);
        w.Put(world.mode);
        w.Put((uint8_t)(world.fleets ? 1 : 0));
        w.Put(world.time);
        w.Put(world.allowedCls);
        w.Put((uint8_t)(world.deckOnly ? 1 : 0));
        w.Put((uint8_t)(world.skills ? 1 : 0));
        w.Put(world.arena);
        w.Put((uint16_t)(crewId >= 0 ? crewId : NO_SHIP));
        w.Put((uint8_t)(world.realistic ? 1 : 0));
        w.Put(world.difficulty);
        net.Send(peer, w.buf.data(), w.buf.size(), true);
        for (int i = 0; i < world.ShipCount() && i < (int)announced.size(); ++i)
            if (world.ships[i].active && announced[i] && (i != id || crewId >= 0)) SendPlayerInfo(i, (int)peer);
        AnnounceShips();  // the new captain (and fleet) to everyone
        for (int i = 0; i < world.CrewCount(); ++i)
            if (world.crew[i].active && i != crewId) SendCrewInfo(i, (int)peer);
        if (crewId >= 0) {
            SendCrewInfo(crewId, -1);
            printf("[server] %s signed on as crew aboard ship %d\n", name.c_str(), id);
        } else printf("[server] %s joined as ship %d\n", name.c_str(), id);
    } else if (type == MSG_SET_CLASS) {
        int id = peerShip[peer];
        uint8_t cls = r.Get<uint8_t>();
        if (id >= 0 && r.ok && cls < SC_PLAYABLE) world.ships[id].nextCls = (uint8_t)world.AllowedClass(cls);  // takes effect at the next respawn
    } else if (type == MSG_PICK_PERK) {  // the Odyssey: an upgrade, between rounds
        int id = peerShip[peer];
        uint8_t choice = r.Get<uint8_t>();
        if (id >= 0 && r.ok) world.PickPerk(id, choice);
    } else if (type == MSG_SET_ROLE) {
        uint8_t want = r.Get<uint8_t>();
        if (!r.ok) return;
        auto tell = [&](int crewId, int ship) {
            ByteWriter w;
            w.Put(MSG_ROLE);
            w.Put((uint16_t)(crewId >= 0 ? crewId : NO_SHIP));
            w.Put((uint16_t)ship);
            net.Send(peer, w.buf.data(), w.buf.size(), true);
        };
        if (want == 2 && peerCrew[peer] >= 0) {  // crew: over to another ship
            world.MoveCrew(peerCrew[peer], -1);
            tell(peerCrew[peer], world.crew[peerCrew[peer]].ship);
        } else if (want == ROLE_CREW && peerShip[peer] >= 0) {  // captain -> crew: the ship (and fleet) goes
            int id = peerShip[peer];
            std::string name = world.ships[id].name;
            world.RemoveShip(id);
            peerShip[peer] = -1;
            AnnounceShips();
            int c = world.AddCrew(name);
            if (c < 0) {  // (no room aboard anywhere: back to a ship of our own)
                int s = world.AddShip(name, false, SC_FRIGATE);
                peerShip[peer] = s;
                AnnounceShips();
                tell(-1, s);
                return;
            }
            peerCrew[peer] = c;
            AnnounceShips();  // (a bot captain may have come with it)
            SendCrewInfo(c, -1);
            tell(c, world.crew[c].ship);
            printf("[server] %s signed on as crew aboard ship %d\n", name.c_str(), world.crew[c].ship);
        } else if (want == ROLE_CAPTAIN && peerCrew[peer] >= 0) {  // crew -> captain of a new ship
            int c = peerCrew[peer];
            std::string name = world.crew[c].name;
            world.RemoveCrew(c);
            peerCrew[peer] = -1;
            ByteWriter lw;
            lw.Put(MSG_CREW_LEFT);
            lw.Put((uint16_t)c);
            net.Broadcast(lw.buf.data(), lw.buf.size(), true);
            int s = world.AddShip(name, false, SC_FRIGATE);
            if (s < 0) return;
            world.ships[s].fixedCls = false;
            peerShip[peer] = s;
            AnnounceShips();
            tell(-1, s);
            printf("[server] %s takes a ship of their own (%d)\n", name.c_str(), s);
        }
    } else if (type == MSG_ORDER) {  // a captain shouting to the crew
        int id = peerShip[peer];
        uint8_t order = r.Get<uint8_t>();
        if (id < 0 || !r.ok || order >= ORDER_COUNT) return;
        GameEvent e{EventType::OrderGiven};
        e.a = (uint16_t)id;
        e.b = order;
        e.pos = world.ships[id].pos;
        world.events.push_back(e);
    } else if (type == MSG_CREW_INPUT) {
        int id = peerCrew[peer];
        if (id < 0) return;
        CrewInput in;
        Crewman& c = world.crew[id];
        if (ReadCrewInput(r, in) && (in.seq > c.lastSeq || c.lastSeq - in.seq > 100000)) {
            c.input = in;
            c.lastSeq = in.seq;
        }
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
            else if (e.type == NetEvent::Disconnect && e.peer < (uint32_t)kMaxPeers && peerCrew[e.peer] >= 0) {
                int id = peerCrew[e.peer];
                printf("[server] %s (crew) left\n", world.crew[id].name.c_str());
                world.RemoveCrew(id);
                peerCrew[e.peer] = -1;
                ByteWriter w;
                w.Put(MSG_CREW_LEFT);
                w.Put((uint16_t)id);
                net.Broadcast(w.buf.data(), w.buf.size(), true);
            } else if (e.type == NetEvent::Disconnect && e.peer < (uint32_t)kMaxPeers && peerShip[e.peer] >= 0) {
                int id = peerShip[e.peer];
                printf("[server] %s left\n", world.ships[id].name.c_str());
                world.RemoveShip(id);
                peerShip[e.peer] = -1;
                AnnounceShips();
            }
            if (e.type == NetEvent::Disconnect && e.peer < (uint32_t)kMaxPeers) peerLivery[e.peer].clear();
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
