#pragma once

#include <Arduino.h>

// Gateway-side, persistent registry of known camera/sensor nodes (Section 6). This is
// what makes adding/removing a node a zero-code operation: a new node's ESP-NOW
// heartbeat auto-creates an entry here (enabled by default); removing one is a serial
// command, not a firmware change. Distinct from PeerRegistry (Phase 5), which is
// in-memory-only radio-layer peer tracking for dedup/replay protection — DeviceRegistry
// is the durable "this is a device I manage" record, gateway-only.
namespace CarSentinel {

constexpr int DEVICE_REGISTRY_SCHEMA_VERSION = 1;

struct DeviceRegistryEntry {
    // Persisted:
    String nodeId;
    String displayName;
    String role;
    String mac;              // "AA:BB:CC:DD:EE:FF" — see MacAddress.h
    String hardwareProfile;
    String firmwareVersion;
    bool enabled = true;

    // Runtime-only (repopulated from live heartbeats, never written to disk — Section 13
    // style "last seen" freshness, not a durable record):
    unsigned long lastSeenMs = 0;
    uint32_t lastFreeHeap = 0;
    unsigned long lastUptimeMs = 0;
};

class DeviceRegistry {
public:
    static const uint8_t MAX_DEVICES = 16;

    static bool begin();

    // Called on every HELLO/HEARTBEAT: creates the entry if unknown (enabled=true,
    // displayName defaults to nodeId), or refreshes mac/role/hardwareProfile/
    // firmwareVersion on an existing one without touching admin-set fields
    // (displayName, enabled).
    static DeviceRegistryEntry* upsertFromDiscovery(const String& nodeId, const uint8_t mac[6],
                                                      const String& role);
    static void updateHealth(const String& nodeId, uint32_t freeHeap, unsigned long uptimeMs);

    static DeviceRegistryEntry* find(const String& nodeId);
    static bool setEnabled(const String& nodeId, bool enabled);
    static bool rename(const String& nodeId, const String& newDisplayName);
    static bool remove(const String& nodeId);

    static uint8_t count();
    static DeviceRegistryEntry* get(uint8_t index);

private:
    static DeviceRegistryEntry devices[MAX_DEVICES];
    static uint8_t deviceCount;

    static bool save();
    static bool loadFromDisk();
};

}  // namespace CarSentinel
