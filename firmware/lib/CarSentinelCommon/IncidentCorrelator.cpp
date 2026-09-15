#include "IncidentCorrelator.h"
#include "Logger.h"

namespace CarSentinel {

static const char* TAG = "IncidentCorrelator";

IncidentRecord IncidentCorrelator::incidents[IncidentCorrelator::MAX_INCIDENTS];
uint32_t IncidentCorrelator::nextIncidentNumber = 1;

void IncidentCorrelator::begin() {
    for (uint8_t i = 0; i < MAX_INCIDENTS; i++) {
        incidents[i] = IncidentRecord();
    }
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

String IncidentCorrelator::startIncident(const String& triggerNodeId, const String& triggerEventId) {
    IncidentRecord* existing = findOpen(triggerNodeId, triggerEventId);
    if (existing) {
        return existing->incidentId;
    }

    uint8_t slot = MAX_INCIDENTS;
    for (uint8_t i = 0; i < MAX_INCIDENTS; i++) {
        if (!incidents[i].active) { slot = i; break; }
    }
    if (slot == MAX_INCIDENTS) {
        // Full — close/log the oldest to make room rather than silently drop the new
        // trigger. MAX_INCIDENTS=4 concurrent open incidents is generous for a small
        // camera set; this eviction path is a safety net, not expected in normal use.
        uint8_t oldest = 0;
        for (uint8_t i = 1; i < MAX_INCIDENTS; i++) {
            if (incidents[i].createdAtMs < incidents[oldest].createdAtMs) oldest = i;
        }
        Logger::warn(TAG, "Incident table full; force-closing oldest to make room");
        closeAndLog(incidents[oldest]);
        slot = oldest;
    }

    char idBuf[24];
    snprintf(idBuf, sizeof(idBuf), "INCIDENT-%06u", (unsigned int)(nextIncidentNumber++));

    IncidentRecord& inc = incidents[slot];
    inc = IncidentRecord();
    inc.active = true;
    inc.incidentId = String(idBuf);
    inc.triggerNodeId = triggerNodeId;
    inc.triggerEventId = triggerEventId;
    inc.createdAtMs = millis();

    Logger::info(TAG, inc.incidentId + " opened, trigger=" + triggerNodeId +
                 " eventId=" + triggerEventId);
    return inc.incidentId;
}

bool IncidentCorrelator::addRelated(const String& triggerNodeId, const String& triggerEventId,
                                     const String& relatedNodeId) {
    IncidentRecord* inc = findOpen(triggerNodeId, triggerEventId);
    if (!inc) {
        Logger::warn(TAG, "CAPTURE_RESULT from " + relatedNodeId + " for trigger " +
                     triggerNodeId + "/" + triggerEventId +
                     " — no open incident (correlation window may have already closed)");
        return false;
    }
    for (uint8_t i = 0; i < inc->relatedCount; i++) {
        if (inc->relatedNodeIds[i] == relatedNodeId) {
            return true;  // already recorded, not an error
        }
    }
    if (inc->relatedCount >= IncidentRecord::MAX_RELATED) {
        Logger::warn(TAG, inc->incidentId + ": related-node list full, dropping " + relatedNodeId);
        return false;
    }
    inc->relatedNodeIds[inc->relatedCount++] = relatedNodeId;
    Logger::info(TAG, inc->incidentId + ": related capture from " + relatedNodeId +
                 " (" + String(inc->relatedCount) + " so far)");
    return true;
}

void IncidentCorrelator::closeAndLog(IncidentRecord& inc) {
    if (!inc.active) return;

    String related = "";
    for (uint8_t i = 0; i < inc.relatedCount; i++) {
        if (i > 0) related += ", ";
        related += inc.relatedNodeIds[i];
    }
    Logger::info(TAG, inc.incidentId + " closed — trigger=" + inc.triggerNodeId +
                 " related=[" + related + "] (" + String(inc.relatedCount) +
                 " camera(s) responded)");
    inc.active = false;
}

void IncidentCorrelator::loop() {
    unsigned long now = millis();
    for (uint8_t i = 0; i < MAX_INCIDENTS; i++) {
        if (incidents[i].active && now - incidents[i].createdAtMs >= CORRELATION_WINDOW_MS) {
            closeAndLog(incidents[i]);
        }
    }
}

}  // namespace CarSentinel
