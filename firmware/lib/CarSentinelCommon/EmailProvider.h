#pragma once

#include "NotificationProvider.h"

// Minimal SMTP client (implicit TLS, port 465 by default — e.g. Gmail's
// smtp.gmail.com:465 with an app password) implemented directly over
// WiFiClientSecure rather than a third-party mail library: the SMTP dialogue itself
// (EHLO/AUTH LOGIN/MAIL FROM/RCPT TO/DATA) is a short, well-documented, stable text
// protocol, and every earlier phase that pulled in a new external library needed at
// least one guessed-API compile-fix pass — not worth that risk for something this
// small. `base64::encode()` (used for AUTH LOGIN) and `WiFiClientSecure` were both
// confirmed against this toolchain's actual installed headers before writing this.
//
// KNOWN SECURITY LIMITATION, stated plainly: TLS certificate validation is currently
// disabled (`setInsecure()`) rather than pinned to a CA bundle — accepts whatever
// certificate the server presents. Acceptable for a first working version on a
// project's own hardware talking to a known mail provider; not a substitute for real
// certificate pinning in a security-focused product. Revisit before treating this as
// hardened (Section 41).
namespace CarSentinel {

class EmailProvider : public NotificationProvider {
public:
    bool send(const String& subject, const String& body) override;
    const char* name() const override { return "Email"; }
};

}  // namespace CarSentinel
