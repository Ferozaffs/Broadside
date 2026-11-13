// Automatic port forwarding for hosts: find the router (UPnP IGD over SSDP), ask it to forward our UDP
// port to this PC, and read back the public address friends should join. Runs on a background thread;
// the mapping is removed again on Stop. Kept free of raylib includes (it pulls in winsock).
#pragma once
#include <cstdint>
#include <string>

namespace upnp {

enum class State { Idle, Working, Mapped, NoRouter, Refused };

void Start(uint16_t port);  // map UDP `port` -> this PC (async)
void Stop();                // remove the mapping (blocks briefly)
State GetState();
std::string ExternalIp();   // from the router; empty if unknown
std::string LocalIp();      // this PC on the LAN
bool CarrierNat();          // the router's own public address is private: friends outside can't reach us
std::string Describe();     // one line for the HUD / log

// Read-only check for testing: find the router and ask for its public address, no mapping.
std::string Probe();

}  // namespace upnp
