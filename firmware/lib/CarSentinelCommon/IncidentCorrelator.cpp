#include "IncidentCorrelator.h"
#include "Logger.h"

#include <LittleFS.h>
#include <ArduinoJson.h>

namespace CarSentinel {

static const char* TAG = "IncidentCorrelator";
static const char* INCIDENTS_DIR = "/incidents";
static const char* COUNTER_PATH = "/incidents/.next_incident_number";

IncidentRecord IncidentCorrelator::incidents[IncidentCorrelator::MAX_INCIDENTS];
uint32_t IncidentCorrelator::nextIncidentNumber = 1;

const char* incidentStateToString(IncidentState state) {
    switch (state) {
        case IncidentState::DETECTED: return "DETECTED";
        case IncidentState::CONFIRMING: return "CONFIRMING";
        case IncidentState::ACTIVE: return "ACTIVE";
        case IncidentState::EVIDENCE_COLLECTION: return "EVIDENCE_COLLECTION";
        case IncidentState::NOTIFICATION: return "NOTIFICATION";
        default: return "CLOSED";
    }
}

static uint32_t nextIncidentNumberFromDisk() {
    uint32_t n = 1;
    if (LittleFS.exists(COUNTER_PATH)) {
        File f = LittleFS.open(COUNTER_PATH, "r");
        if (f) {
            n = f.parseInt();
            f.close();
            if (n < 1) n = 1;
        }
    }
    File out = LittleFS.open(COUNTER_PATH, "w");
    if (out) {
        out.print(n + 1);
        out.close();
    }
    return n;
}

void IncidentCorrelator::begin() {
    for (uint8_t i = 0; i < MAX_INCIDENTS; i++) {
        incidents[i] = IncidentRecord();
    }
    if (!LittleFS.exists(INCIDENTS_DIR)) {
        LittleFS.mkdir(INCIDENTS_DIR);
    }
    // Peek the counter without consuming it (begin() shouldn't burn a number) — read
    // it directly rather than via nextIncidentNumberFromDisk(), which also increments.
    if (LittleFS.exists(COUNTER_PATH)) {
        File f = LittleFS.open(COUNTER_PATH, "r");
        if (f) {
            uint32_t n = f.parseInt();
            f.close();
            if (n >= 1) nextIncidentNumber = n;
        }
    }
    Logger::info(TAG, "Incident store ready at " + String(INCIDENTS_DIR) +
                 ", next incident number=" + String(nextIncidentNumber));
}

IncidentRecord* IncidentCorrelator::findOpen(const String& triggerNodeId, const String& triggerEventId) {
    for (uint8_t i = 0; i < MAX_INCIDENTS; i++) {
        if (incidents[i].active && incidents[i].triggerNodeId == triggerNodeId &&
            incidents[i].triggerEventId == triggerEventId) {
            return &incidents[i];
        }
    }
    return nullptr;
}

bool IncidentCorrelator::persist(const IncidentRecord& inc) {
    JsonDocument doc;
    doc["incidentId"] = inc.incidentId;
    doc["state"] = incidentStateToString(inc.state);
    doc["triggerNodeId"] = inc.triggerNodeId;
    doc["triggerEventId"] = inc.triggerEventId;
    doc["createdAtMs"] = inc.createdAtMs;

    JsonObject trig = doc["trigger"].to<JsonObject>();
    trig["triggerType"] = inc.trigger.triggerType;
    trig["severity"] = inc.trigger.severity;
    trig["hasImage"] = inc.trigger.hasImage;
    if (inc.trigger.hasGps) {
        JsonObject gps = trig["gps"].to<JsonObject>();
        gps["lat"] = inc.trigger.gpsLat;
        gps["lon"] = inc.trigger.gpsLon;
        gps["speedKmph"] = inc.trigger.gpsSpeedKmph;
    }
    if (inc.trigger.hasImu) {
        JsonObject imu = trig["imu"].to<JsonObject>();
        imu["accelG"] = inc.trigger.imuAccelG;
        imu["gyroDps"] = inc.trigger.imuGyroDps;
    }
    if (inc.trigger.hasEnv) {
        JsonObject env = trig["env"].to<JsonObject>();
        env["tempC"] = inc.trigger.envTempC;
        env["humidity"] = inc.trigger.envHumidity;
    }

    JsonArray evidenceArr = doc["evidence"].to<JsonArray>();
    for (uint8_t i = 0; i < inc.evidenceCount; i++) {
        JsonObject e = evidenceArr.add<JsonObject>();
        e["nodeId"] = inc.evidence[i].nodeId;
        e["localEventId"] = inc.evidence[i].localEventId;
        e["hasImage"] = inc.evidence[i].hasImage;
    }

    String path = String(INCIDENTS_DIR) + "/" + inc.incidentId + ".json";
    File f = LittleFS.open(path, "w");
    if (!f) {
        Logger::error(TAG, "Failed to open " + path + " for writing");
        return false;
    }
    bool ok = serializeJson(doc, f) > 0;
    f.close();
    return ok;
}

void IncidentCorrelator::enforceRetention() {
    File dir = LittleFS.open(INCIDENTS_DIR);
    if (!dir || !dir.isDirectory()) return;

    uint16_t count = 0;
    String oldestPath;
    uint32_t oldestNumber = UINT32_MAX;

    File entry = dir.openNextFile();
    while (entry) {
        String name = String(entry.name());
        if (name.endsWith(".json")) {
            count++;
            // Filenames are "INCIDENT-NNNNNN.json" — extract NNNNNN to find the oldest.
            int dashIdx = name.indexOf('-');
            int dotIdx = name.indexOf(".json");
            if (dashIdx >= 0 && dotIdx > dashIdx) {
                uint32_t num = name.substring(dashIdx + 1, dotIdx).toInt();
                if (num < oldestNumber) {
                    oldestNumber = num;
                    oldestPath = String(INCIDENTS_DIR) + "/" + name;
                }
            }
        }
        entry = dir.openNextFile();
    }

    if (count > MAX_STORED_INCIDENTS && !oldestPath.isEmpty()) {
        Logger::info(TAG, "Retention: " + String(count) + " stored incidents exceeds " +
                     String(MAX_STORED_INCIDENTS) + ", removing " + oldestPath);
        LittleFS.remove(oldestPath);
    }
}

String IncidentCorrelator::startIncident(const String& triggerNodeId, const String& triggerEventId,
                                          const IncidentTriggerInfo& info) {
    IncidentRecord* existing = findOpen(triggerNodeId, triggerEventId);
    if (existing) {
        return existing->incidentId;
    }

    uint8_t slot = MAX_INCIDENTS;
    for (uint8_t i = 0; i < MAX_INCIDENTS; i++) {
        if (!incidents[i].active) { slot = i; break; }
    }
    if (slot == MAX_INCIDENTS) {
        // Full — close the oldest to make room rather than silently drop the new
        // trigger. MAX_INCIDENTS=4 concurrent open incidents is generous for a small
        // camera set; this eviction path is a safety net, not expected in normal use.
        uint8_t oldest = 0;
        for (uint8_t i = 1; i < MAX_INCIDENTS; i++) {
            if (incidents[i].createdAtMs < incidents[oldest].createdAtMs) oldest = i;
        }
        Logger::warn(TAG, "Incident table full; force-closing oldest to make room");
        closeIncident(incidents[oldest]);
        slot = oldest;
    }

    uint32_t number = nextIncidentNumberFromDisk();
    nextIncidentNumber = number + 1;
    char idBuf[24];
    snprintf(idBuf, sizeof(idBuf), "INCIDENT-%06u", (unsigned int)number);

    IncidentRecord& inc = incidents[slot];
    inc = IncidentRecord();
    inc.active = true;
    inc.incidentId = String(idBuf);
    inc.triggerNodeId = triggerNodeId;
    inc.triggerEventId = triggerEventId;
    inc.createdAtMs = millis();
    inc.trigger = info;

    // Section 19: DETECTED -> ACTIVE is immediate here — by the time the gateway hears
    // about a trigger, the originating node has already finished its own confirmation
    // (Section 17's debounce/confirmation window for RCWL, or the IMU threshold check),
    // so there's no separate "confirming" wait on the gateway side to represent.
    inc.state = IncidentState::ACTIVE;

    // The trigger's own capture (if any) counts as the first evidence entry.
    if (inc.evidenceCount < IncidentRecord::MAX_EVIDENCE) {
        inc.evidence[inc.evidenceCount].nodeId = triggerNodeId;
        inc.evidence[inc.evidenceCount].localEventId = triggerEventId;
        inc.evidence[inc.evidenceCount].hasImage = info.hasImage;
        inc.evidenceCount++;
    }
    inc.state = IncidentState::EVIDENCE_COLLECTION;

    persist(inc);
    Logger::info(TAG, inc.incidentId + " opened (" + info.triggerType + "), trigger=" +
                 triggerNodeId + " eventId=" + triggerEventId +
                 " gps=" + String(info.hasGps) + " imu=" + String(info.hasImu) +
                 " env=" + String(info.hasEnv));
    return inc.incidentId;
}

bool IncidentCorrelator::addRelated(const String& triggerNodeId, const String& triggerEventId,
                                     const String& relatedNodeId, const String& localEventId,
                                     bool hasImage) {
    IncidentRecord* inc = findOpen(triggerNodeId, triggerEventId);
    if (!inc) {
        Logger::warn(TAG, "CAPTURE_RESULT from " + relatedNodeId + " for trigger " +
                     triggerNodeId + "/" + triggerEventId +
                     " — no open incident (correlation window may have already closed)");
        return false;
    }
    for (uint8_t i = 0; i < inc->evidenceCount; i++) {
        if (inc->evidence[i].nodeId == relatedNodeId) {
            return true;  // already recorded, not an error
        }
    }
    if (inc->evidenceCount >= IncidentRecord::MAX_EVIDENCE) {
        Logger::warn(TAG, inc->incidentId + ": evidence list full, dropping " + relatedNodeId);
        return false;
    }
    inc->evidence[inc->evidenceCount].nodeId = relatedNodeId;
    inc->evidence[inc->evidenceCount].localEventId = localEventId;
    inc->evidence[inc->evidenceCount].hasImage = hasImage;
    inc->evidenceCount++;
    persist(*inc);
    Logger::info(TAG, inc->incidentId + ": related capture from " + relatedNodeId +
                 " (" + String(inc->evidenceCount) + " evidence entries so far)");
    return true;
}

void IncidentCorrelator::closeIncident(IncidentRecord& inc) {
    if (!inc.active) return;

    if (inc.evidenceCount > 0) {
        inc.state = IncidentState::NOTIFICATION;
        Logger::info(TAG, inc.incidentId + " -> NOTIFICATION (would trigger email/alert "
                     "pipeline here — Phase 12)");
    }
    inc.state = IncidentState::CLOSED;
    persist(inc);

    String evidenceSummary = "";
    for (uint8_t i = 0; i < inc.evidenceCount; i++) {
        if (i > 0) evidenceSummary += ", ";
        evidenceSummary += inc.evidence[i].nodeId + (inc.evidence[i].hasImage ? "(img)" : "(no img)");
    }
    Logger::info(TAG, inc.incidentId + " CLOSED — trigger=" + inc.triggerNodeId +
                 " type=" + inc.trigger.triggerType + " evidence=[" + evidenceSummary + "] (" +
                 String(inc.evidenceCount) + " total)");

    inc.active = false;
    enforceRetention();
}

void IncidentCorrelator::loop() {
    unsigned long now = millis();
    for (uint8_t i = 0; i < MAX_INCIDENTS; i++) {
        if (incidents[i].active && now - incidents[i].createdAtMs >= CORRELATION_WINDOW_MS) {
            closeIncident(incidents[i]);
        }
    }
}

}  // namespace CarSentinel
