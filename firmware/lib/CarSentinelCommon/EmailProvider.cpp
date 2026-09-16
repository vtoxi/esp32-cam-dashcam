#include "EmailProvider.h"
#include "EmailConfig.h"
#include "Logger.h"
#include "Watchdog.h"

#include <WiFiClientSecure.h>
#include <base64.h>

namespace CarSentinel {

static const char* TAG = "EmailProvider";
static const unsigned long SMTP_TIMEOUT_MS = 10000;

// Reads one SMTP response (following continuation lines like "250-SIZE ...") and
// returns its numeric status code, or -1 on timeout. Blocking, bounded by
// SMTP_TIMEOUT_MS — acceptable here since email send already happens off the
// security-critical path (see NotificationManager), not inside loop().
static int readSmtpResponse(WiFiClientSecure& client) {
    int code = -1;
    unsigned long start = millis();
    while (millis() - start < SMTP_TIMEOUT_MS) {
        // Section 1/10's lesson repeated: a blocking wait like this must keep feeding
        // the watchdog, or a slow SMTP server turns into a watchdog panic instead of
        // just a failed send.
        Watchdog::feed();
        if (client.available()) {
            String line = client.readStringUntil('\n');
            line.trim();
            if (line.length() >= 3) {
                code = line.substring(0, 3).toInt();
            }
            bool continuation = line.length() >= 4 && line.charAt(3) == '-';
            if (!continuation) {
                return code;
            }
        } else {
            delay(10);
        }
    }
    Logger::warn(TAG, "SMTP response timeout");
    return -1;
}

static bool expectCode(WiFiClientSecure& client, int expected, const char* step) {
    int code = readSmtpResponse(client);
    if (code != expected) {
        Logger::error(TAG, String(step) + ": expected " + String(expected) +
                      ", got " + String(code));
        return false;
    }
    return true;
}

bool EmailProvider::send(const String& subject, const String& body) {
    const EmailConfigData& cfg = EmailConfig::get();
    if (!cfg.enabled) {
        Logger::info(TAG, "Email disabled; not sending");
        return false;
    }
    if (cfg.smtpHost.isEmpty() || cfg.recipient.isEmpty()) {
        Logger::warn(TAG, "Email config incomplete (host/recipient) — cannot send");
        return false;
    }

    WiFiClientSecure client;
    // See EmailProvider.h: certificate validation intentionally disabled for now —
    // documented limitation, not an oversight.
    client.setInsecure();

    Logger::info(TAG, "Connecting to " + cfg.smtpHost + ":" + String(cfg.smtpPort));
    if (!client.connect(cfg.smtpHost.c_str(), cfg.smtpPort)) {
        Logger::error(TAG, "SMTP connection failed");
        return false;
    }

    bool ok = true;
    ok = ok && expectCode(client, 220, "greeting");
    if (ok) {
        client.print("EHLO carsentinel\r\n");
        ok = expectCode(client, 250, "EHLO");
    }
    if (ok) {
        client.print("AUTH LOGIN\r\n");
        ok = expectCode(client, 334, "AUTH LOGIN");
    }
    if (ok) {
        client.print(base64::encode(cfg.username) + "\r\n");
        ok = expectCode(client, 334, "username");
    }
    if (ok) {
        client.print(base64::encode(cfg.password) + "\r\n");
        ok = expectCode(client, 235, "password");  // never logs the password itself
    }
    if (ok) {
        client.print("MAIL FROM:<" + cfg.sender + ">\r\n");
        ok = expectCode(client, 250, "MAIL FROM");
    }
    if (ok) {
        client.print("RCPT TO:<" + cfg.recipient + ">\r\n");
        ok = expectCode(client, 250, "RCPT TO");
    }
    if (ok) {
        client.print("DATA\r\n");
        ok = expectCode(client, 354, "DATA");
    }
    if (ok) {
        String message = "From: " + cfg.sender + "\r\n" +
                          "To: " + cfg.recipient + "\r\n" +
                          "Subject: " + subject + "\r\n" +
                          "\r\n" + body + "\r\n.\r\n";
        client.print(message);
        ok = expectCode(client, 250, "message body");
    }

    client.print("QUIT\r\n");
    client.stop();

    Logger::info(TAG, ok ? "Email sent to " + cfg.recipient : "Email send failed");
    return ok;
}

}  // namespace CarSentinel
