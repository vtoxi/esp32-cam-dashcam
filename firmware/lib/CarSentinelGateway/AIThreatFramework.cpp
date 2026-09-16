#include "AIThreatFramework.h"
#include "ImuManager.h"

namespace CarSentinel {

ThreatAnalyzer AIThreatFramework::activeAnalyzer = AIThreatFramework::heuristicAnalyzer;

const char* threatSeverityToString(ThreatSeverity s) {
    switch (s) {
        case ThreatSeverity::THREAT_LOW: return "LOW";
        case ThreatSeverity::THREAT_HIGH: return "HIGH";
        default: return "SUSPICIOUS";
    }
}

ThreatAssessment AIThreatFramework::heuristicAnalyzer(const IncidentTriggerInfo& trigger,
                                                        uint8_t corroboratingNodes) {
    ThreatAssessment result;
    uint8_t score = 30;  // baseline: a single motion trigger, nothing corroborating it yet
    String reasons = "";

    if (trigger.triggerType == "IMPACT_EVENT" && trigger.hasImu) {
        const ImuThresholdsData& t = ImuManager::getThresholds();
        float overThreshold = t.accelMagnitudeG > 0 ? trigger.imuAccelG / t.accelMagnitudeG : 1.0f;
        if (overThreshold >= 2.0f) {
            score += 40;
            reasons += "impact well above threshold (" + String(trigger.imuAccelG, 2) + "g); ";
        } else {
            score += 20;
            reasons += "impact above threshold (" + String(trigger.imuAccelG, 2) + "g); ";
        }
    }

    // A vehicle that should be stationary (this incident only opens in PARKED mode —
    // Section 16) but has a nonzero GPS speed is a strong signal (being driven/towed).
    if (trigger.hasGps && trigger.gpsSpeedKmph > 5.0f) {
        score += 25;
        reasons += "GPS shows movement (" + String(trigger.gpsSpeedKmph, 1) + " km/h) while parked; ";
    }

    if (corroboratingNodes >= 2) {
        score += 20;
        reasons += String(corroboratingNodes) + " cameras corroborate this event; ";
    } else if (corroboratingNodes == 1) {
        score += 10;
        reasons += "1 other camera corroborates this event; ";
    }

    if (!trigger.hasImage) {
        score = score > 15 ? score - 15 : 0;
        reasons += "no image captured, lower confidence; ";
    }

    if (reasons.isEmpty()) {
        reasons = "single uncorroborated motion trigger, no additional signals";
    }

    if (score > 100) score = 100;
    result.confidencePercent = score;
    result.severity = score >= 70 ? ThreatSeverity::THREAT_HIGH
                       : score >= 40 ? ThreatSeverity::THREAT_SUSPICIOUS
                                     : ThreatSeverity::THREAT_LOW;
    result.reasoning = reasons;
    return result;
}

ThreatAssessment AIThreatFramework::assess(const IncidentTriggerInfo& trigger, uint8_t corroboratingNodes) {
    return activeAnalyzer(trigger, corroboratingNodes);
}

void AIThreatFramework::setAnalyzer(ThreatAnalyzer analyzer) {
    activeAnalyzer = analyzer ? analyzer : heuristicAnalyzer;
}

}  // namespace CarSentinel
