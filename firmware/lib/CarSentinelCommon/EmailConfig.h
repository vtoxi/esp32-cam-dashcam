#pragma once

#include <Arduino.h>

// Persisted SMTP settings (Section 29). Never logged, never transmitted anywhere except
// to the configured SMTP server itself — kept in its own file (like NetworkConfig and
// EspNowSecurity's key) so a diagnostic dump of other config never risks leaking it.
namespace CarSentinel {

struct EmailConfigData {
    bool enabled = false;
    String smtpHost;
    uint16_t smtpPort = 465;  // implicit-TLS SMTPS default; see EmailProvider
    String username;
    String password;
    String sender;
    String recipient;
    unsigned long cooldownSeconds = 300;  // Section 29: avoid spamming on repeated triggers
};

class EmailConfig {
public:
    static bool begin();
    static const EmailConfigData& get();
    static bool save(const EmailConfigData& data);

private:
    static EmailConfigData current;
};

}  // namespace CarSentinel
