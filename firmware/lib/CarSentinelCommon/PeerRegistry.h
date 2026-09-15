#pragma once

#include <Arduino.h>

// In-memory only (does not survive reboot) tracking of ESP-NOW peers this device has
// heard from — the minimal piece Phase 5 needs for duplicate/replay detection and
// discovering the gateway's MAC by role. This is NOT the persistent, gateway-side
// device registry with enable/disable/rename (Section 6) — that's Phase 6's job, built
// on top of this.
namespace CarSentinel {

struct PeerInfo {
    uint8_t mac[6] = {0};
    String nodeId;
    String role;
    unsigned long lastSeenMs = 0;
    uint32_t lastSeqNum = 0;
    bool seqInitialized = false;
};

class PeerRegistry {
public:
    static const uint8_t MAX_PEERS = 10;

    static PeerInfo* findByMac(const uint8_t mac[6]);
    static PeerInfo* findByRole(const String& role);  // first match, e.g. "GATEWAY"

    // Creates the peer if unknown, updates nodeId/role/lastSeen if known. role may be
    // empty (unknown yet) without overwriting a previously-learned role.
    static PeerInfo* registerOrUpdate(const uint8_t mac[6], const String& nodeId, const String& role);

    // Monotonic-sequence check for duplicate/replay protection (Section 41): a peer's
    // first-seen message is always accepted (and initializes the counter — handles the
    // peer having rebooted, since we have no persisted cross-reboot state either);
    // afterward only strictly increasing sequence numbers are accepted. This is a
    // deliberately simple scheme, not hardened against a sophisticated attacker who can
    // observe and re-inject packets before the legitimate next one arrives — documented
    // as a Phase 5 scope limit, not a claim of strong replay resistance.
    static bool acceptSequence(PeerInfo* peer, uint32_t seq);

    static uint8_t count();
    static PeerInfo* get(uint8_t index);

private:
    static PeerInfo peers[MAX_PEERS];
    static uint8_t peerCount;
};

}  // namespace CarSentinel
