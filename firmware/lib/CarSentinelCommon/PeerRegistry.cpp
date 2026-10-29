#include "PeerRegistry.h"
#include "Logger.h"

namespace CarSentinel {

static const char* TAG = "PeerRegistry";

PeerInfo PeerRegistry::peers[PeerRegistry::MAX_PEERS];
uint8_t PeerRegistry::peerCount = 0;

PeerInfo* PeerRegistry::findByMac(const uint8_t mac[6]) {
    for (uint8_t i = 0; i < peerCount; i++) {
        if (memcmp(peers[i].mac, mac, 6) == 0) {
            return &peers[i];
        }
    }
    return nullptr;
}

PeerInfo* PeerRegistry::findByRole(const String& role) {
    for (uint8_t i = 0; i < peerCount; i++) {
        if (peers[i].role == role) {
            return &peers[i];
        }
    }
    return nullptr;
}

PeerInfo* PeerRegistry::registerOrUpdate(const uint8_t mac[6], const String& nodeId, const String& role) {
    PeerInfo* existing = findByMac(mac);
    if (existing) {
        if (!nodeId.isEmpty()) existing->nodeId = nodeId;
        if (!role.isEmpty()) existing->role = role;
        existing->lastSeenMs = millis();
        return existing;
    }

    if (peerCount >= MAX_PEERS) {
        // Evict the least-recently-seen peer rather than silently refuse new ones —
        // MAX_PEERS is a small fixed bound (10), generous for the "few cameras + one
        // gateway" scale this phase targets (Section 6's full dynamic registry with
        // persistence is Phase 6).
        uint8_t oldestIdx = 0;
        for (uint8_t i = 1; i < peerCount; i++) {
            if (peers[i].lastSeenMs < peers[oldestIdx].lastSeenMs) oldestIdx = i;
        }
        Logger::warn(TAG, "Peer table full; evicting " + peers[oldestIdx].nodeId);
        peers[oldestIdx] = PeerInfo();
        memcpy(peers[oldestIdx].mac, mac, 6);
        peers[oldestIdx].nodeId = nodeId;
        peers[oldestIdx].role = role;
        peers[oldestIdx].lastSeenMs = millis();
        return &peers[oldestIdx];
    }

    PeerInfo& slot = peers[peerCount++];
    memcpy(slot.mac, mac, 6);
    slot.nodeId = nodeId;
    slot.role = role;
    slot.lastSeenMs = millis();
    Logger::info(TAG, "New peer: " + nodeId + " role=" + role);
    return &slot;
}

bool PeerRegistry::acceptSequence(PeerInfo* peer, uint32_t seq) {
    if (!peer) return false;
    if (!peer->seqInitialized) {
        peer->seqInitialized = true;
        peer->lastSeqNum = seq;
        return true;
    }
    if (seq > peer->lastSeqNum) {
        peer->lastSeqNum = seq;
        return true;
    }
    return false;
}

uint8_t PeerRegistry::count() {
    return peerCount;
}

PeerInfo* PeerRegistry::get(uint8_t index) {
    if (index >= peerCount) return nullptr;
    return &peers[index];
}

}  // namespace CarSentinel
