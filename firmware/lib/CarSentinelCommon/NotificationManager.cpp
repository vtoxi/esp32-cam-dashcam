#include "NotificationManager.h"
#include "NotificationProvider.h"
#include "EmailProvider.h"
#include "EmailConfig.h"
#include "Logger.h"

namespace CarSentinel {

static const char* TAG = "NotificationManager";
static EmailProvider emailProvider;
static NotificationProvider* provider = &emailProvider;

unsigned long NotificationManager::lastSentMs = 0;

void NotificationManager::begin() {
    EmailConfig::begin();
    Logger::info(TAG, "Notification manager ready (provider=" + String(provider->name()) + ")");
}

static String buildSubject(const IncidentRecord& inc) {
    return String("CarSentinel Alert - ") + inc.trigger.triggerType + " (" + inc.incidentId + ")";
}

static String buildBody(const IncidentRecord& inc) {
    String body = "Incident: " + inc.incidentId + "\r\n";
    body += "Type: " + inc.trigger.triggerType + "\r\n";
    body += "Severity: " + inc.trigger.severity + "\r\n";
    body += "Trigger node: " + inc.triggerNodeId + "\r\n";
    if (inc.trigger.hasGps) {
        body += "Location: " + String(inc.trigger.gpsLat, 6) + ", " + String(inc.trigger.gpsLon, 6) +
                " (speed " + String(inc.trigger.gpsSpeedKmph, 1) + " km/h)\r\n";
    }
    if (inc.trigger.hasImu) {
        body += "IMU: accel=" + String(inc.trigger.imuAccelG, 2) + "g gyro=" +
                String(inc.trigger.imuGyroDps, 1) + " deg/s\r\n";
    }
    if (inc.trigger.hasEnv) {
        body += "Environment: " + String(inc.trigger.envTempC, 1) + "C / " +
                String(inc.trigger.envHumidity, 1) + "%\r\n";
    }
    body += "Evidence (" + String(inc.evidenceCount) + " node(s)):\r\n";
    for (uint8_t i = 0; i < inc.evidenceCount; i++) {
        body += "  - " + inc.evidence[i].nodeId + " / " + inc.evidence[i].localEventId +
                (inc.evidence[i].hasImage ? " (image saved on that node's SD card)" : " (no image)") + "\r\n";
    }
    body += "\r\nThis is an automated message from CarSentinel. Evidence images are not "
            "attached — they remain on each camera node's own SD card.";
    return body;
}

void NotificationManager::onIncidentReady(const IncidentRecord& incident) {
    const EmailConfigData& cfg = EmailConfig::get();
    if (!cfg.enabled) {
        Logger::info(TAG, incident.incidentId + ": notification skipped (email disabled)");
        return;
    }

    unsigned long now = millis();
    if (lastSentMs != 0 && now - lastSentMs < cfg.cooldownSeconds * 1000UL) {
        Logger::info(TAG, incident.incidentId + ": notification suppressed by cooldown (" +
                     String((cfg.cooldownSeconds * 1000UL - (now - lastSentMs)) / 1000) +
                     "s remaining) — Section 29 aggregation isn't implemented yet, this "
                     "event is simply not emailed");
        return;
    }

    bool sent = provider->send(buildSubject(incident), buildBody(incident));
    if (sent) {
        lastSentMs = now;
    }
    Logger::info(TAG, incident.incidentId + ": notification " + (sent ? "sent" : "failed"));
}

bool NotificationManager::sendTest() {
    IncidentRecord fake;
    fake.incidentId = "TEST";
    fake.triggerNodeId = "TEST-NODE";
    fake.trigger.triggerType = "TEST";
    fake.trigger.severity = "INFO";
    bool sent = provider->send("CarSentinel Test Email",
                                "This is a test message confirming your SMTP configuration works.");
    lastSentMs = millis();
    return sent;
}

}  // namespace CarSentinel
