#pragma once

#include <Arduino.h>

// Section 30: notification delivery must not be hardwired into the security/incident
// logic — IncidentCorrelator and NotificationManager depend only on this interface, so
// a future provider (Telegram, webhook, push) can be added without touching either.
namespace CarSentinel {

class NotificationProvider {
public:
    virtual ~NotificationProvider() {}
    virtual bool send(const String& subject, const String& body) = 0;
    virtual const char* name() const = 0;
};

}  // namespace CarSentinel
