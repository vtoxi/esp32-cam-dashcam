#pragma once

#include <Arduino.h>
#include "IncidentCorrelator.h"

// Gateway-only. Turns an incident reaching IncidentCorrelator's NOTIFICATION state into
// an actual outbound notification (Section 12/29/30). Deliberately depends on
// NotificationProvider (the abstraction), not EmailProvider directly, in the public
// send path — only begin() wires up the concrete provider, so a future provider swap
// touches one line.
namespace CarSentinel {

class NotificationManager {
public:
    static void begin();

    // Hooked into IncidentCorrelator::setNotificationHandler() — called once per
    // incident that reaches NOTIFICATION (i.e. it collected at least one piece of
    // evidence). Applies Section 29's cooldown: if a notification was already sent
    // within cooldownSeconds, this one is suppressed and logged, not queued or
    // aggregated — real Section 29 "incident aggregation" (combining several events
    // into one email) is a further step this phase doesn't take; every notification
    // still stands alone, just rate-limited.
    static void onIncidentReady(const IncidentRecord& incident);

    // Sends immediately, bypassing the cooldown — used by the gateway's TESTEMAIL
    // serial command to verify configuration without waiting for a real incident.
    static bool sendTest();

private:
    static unsigned long lastSentMs;
};

}  // namespace CarSentinel
