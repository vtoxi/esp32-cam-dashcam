#pragma once

#include <Arduino.h>

// Gateway-side, persistent registry of known camera/sensor nodes (Section 6). This is
// what makes adding/removing a node a zero-code operation: a new node's ESP-NOW
// heartbeat auto-creates an entry here (enabled by default); removing one is a serial
// command, not a firmware change. Distinct from PeerRegistry (Phase 5), which is
// in-memory-only radio-layer peer tracking for dedup/replay protection — DeviceRegistry
// is the durable "this is a device I manage" record, gateway-only.
namespace CarSentinel {

constexpr int DEVICE_REGISTRY_SCHEMA_VERSION = 2;

struct DeviceRegistryEntry {
    // Persisted:
    String nodeId;
    String displayName;
    String role;
    String mac;              // "AA:BB:CC:DD:EE:FF" — see MacAddress.h
    String hardwareProfile;
    String firmwareVersion;
    String ip;               // last observed Wi-Fi IP (when connected)
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
    // displayName defaults to nodeId), or refreshes mac/role/hardwareProfile on an
    // existing one. `displayName`, if non-empty, is adopted as-is — a node's own
    // heartbeat-reported name is authoritative (it's what RENAME actually changed on
    // the node itself; the registry mirrors it rather than diverging from it). Pass ""
    // when the caller has no displayName to report (e.g. legacy/partial heartbeats).
    static DeviceRegistryEntry* upsertFromDiscovery(const String& nodeId, const uint8_t mac[6],
                                                      const String& role,
                                                      const String& displayName = "",
                                                      const String& ip = "");
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
