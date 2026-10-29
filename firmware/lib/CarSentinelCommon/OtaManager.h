#pragma once

#include <Arduino.h>

// Shared (gateway self-updates and each node self-updates independently — Section 38:
// gateway and camera firmware are separate targets, never assumed identical). MANUAL
// mode only this phase (Section 36 explicitly warns "never update every node blindly" —
// AUTO/STAGED modes that trigger without a human pressing a button are exactly that,
// deferred rather than rushed). Triggered by a serial command with a manifest URL;
// nothing here runs on a timer or reacts to anything automatically.
//
// Update.h/HTTPClient APIs (begin/GET/getStreamPtr, Update.begin/writeStream/setMD5/end)
// confirmed against this toolchain's installed headers before writing any of this.
namespace CarSentinel {

struct OtaManifest {
    String version;
    String url;
    String md5;              // Update.setMD5() — Section 37 "validate checksum"
    String hardwareProfile;  // must match this device's own profile, or the update is refused
};

class OtaManager {
public:
    // Fetches manifestUrl (http or https — https uses the same setInsecure() trust
    // posture as EmailProvider, same documented limitation) and parses
    // {"version","url","md5","hardwareProfile"}. Returns false on any network/parse
    // failure.
    static bool fetchManifest(const String& manifestUrl, OtaManifest& outManifest);

    // Section 37 pre-update validation: hardware profile must match (never flash the
    // wrong board's binary) and the version must actually differ from what's running.
    static bool isUpdateNeeded(const OtaManifest& manifest);

    // Downloads manifest.url, streams it directly into Update (no full-image buffering
    // — the ESP32 doesn't have RAM for that), verifies the MD5 the library itself
    // checks against before Update.end() finalizes, and restarts on success. Returns
    // false (device unchanged, nothing partially applied) on any failure along the way.
    // Blocking — call only in response to an explicit MANUAL trigger, never from loop().
    static bool performUpdate(const OtaManifest& manifest);

    // Call once near the end of setup(), after Diagnostics::selfTest() passes — this is
    // the "report healthy" half of Section 37's post-update health check. Only has a
    // real rollback-cancelling effect if the bootloader was actually built with
    // app-rollback support enabled; if not, this call is a harmless no-op (logged, not
    // hidden) rather than something this project can honestly claim always works.
    static void confirmHealthyBoot();
};

}  // namespace CarSentinel
