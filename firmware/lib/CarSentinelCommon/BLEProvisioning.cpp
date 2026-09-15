#include "BLEProvisioning.h"
#include "Logger.h"
#include "NetworkConfig.h"
#include "DeviceConfig.h"

#include <NimBLEDevice.h>

namespace CarSentinel {

static const char* TAG = "BLEProvisioning";

// Custom 128-bit UUIDs, arbitrary but fixed for this project. Not a registered/reserved
// range — fine for a private provisioning service that isn't published for interop.
static const char* SERVICE_UUID           = "5cae0000-c5e1-4a4a-8f1e-000000000001";
static const char* CHAR_SSID_UUID         = "5cae0000-c5e1-4a4a-8f1e-000000000002";  // write
static const char* CHAR_PASSWORD_UUID     = "5cae0000-c5e1-4a4a-8f1e-000000000003";  // write-only
static const char* CHAR_DISPLAY_NAME_UUID = "5cae0000-c5e1-4a4a-8f1e-000000000004";  // read/write
static const char* CHAR_ROLE_UUID         = "5cae0000-c5e1-4a4a-8f1e-000000000005";  // read/write
static const char* CHAR_COMMIT_UUID       = "5cae0000-c5e1-4a4a-8f1e-000000000006";  // write
static const char* CHAR_STATUS_UUID       = "5cae0000-c5e1-4a4a-8f1e-000000000007";  // read/notify

bool BLEProvisioning::active = false;
bool BLEProvisioning::committed = false;

static NimBLEServer* server = nullptr;
static NimBLECharacteristic* statusChar = nullptr;

// Staged values, applied to NetworkConfig/DeviceConfig only when Commit is written —
// avoids persisting a half-typed SSID if a client disconnects mid-configuration.
static String pendingSsid;
static String pendingPassword;
static String pendingDisplayName;
static String pendingRole;

class SsidCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c) override {
        pendingSsid = String(c->getValue().c_str());
        Logger::info(TAG, "BLE: SSID staged");
    }
};

class PasswordCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c) override {
        pendingPassword = String(c->getValue().c_str());
        Logger::info(TAG, "BLE: password staged (value not logged)");
    }
};

class DisplayNameCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c) override {
        pendingDisplayName = String(c->getValue().c_str());
        Logger::info(TAG, "BLE: displayName staged: " + pendingDisplayName);
    }
};

class RoleCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c) override {
        pendingRole = String(c->getValue().c_str());
        Logger::info(TAG, "BLE: role staged: " + pendingRole);
    }
};

class CommitCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c) override {
        if (pendingSsid.isEmpty()) {
            Logger::warn(TAG, "BLE: commit received with no SSID staged — ignoring");
            return;
        }

        NetworkConfigData net = NetworkConfig::get();
        net.ssid = pendingSsid;
        if (!pendingPassword.isEmpty()) {
            net.password = pendingPassword;
        }
        NetworkConfig::save(net);

        DeviceConfigData dev = DeviceConfig::get();
        if (!pendingDisplayName.isEmpty()) {
            dev.displayName = pendingDisplayName;
        }
        if (!pendingRole.isEmpty()) {
            dev.role = roleFromString(pendingRole);
        }
        DeviceConfig::save(dev);

        Logger::info(TAG, "BLE: commit applied — ssid=" + net.ssid +
                     " displayName=" + dev.displayName +
                     " role=" + String(roleToString(dev.role)));
        BLEProvisioning::committed = true;
    }
};

void BLEProvisioning::begin(const String& deviceName) {
    pendingSsid = "";
    pendingPassword = "";
    pendingDisplayName = "";
    pendingRole = "";
    committed = false;

    NimBLEDevice::init(deviceName.c_str());
    server = NimBLEDevice::createServer();

    NimBLEService* service = server->createService(SERVICE_UUID);

    NimBLECharacteristic* ssidChar = service->createCharacteristic(
        CHAR_SSID_UUID, NIMBLE_PROPERTY::WRITE);
    ssidChar->setCallbacks(new SsidCallbacks());

    NimBLECharacteristic* passwordChar = service->createCharacteristic(
        CHAR_PASSWORD_UUID, NIMBLE_PROPERTY::WRITE);
    passwordChar->setCallbacks(new PasswordCallbacks());

    NimBLECharacteristic* displayNameChar = service->createCharacteristic(
        CHAR_DISPLAY_NAME_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE);
    displayNameChar->setValue(DeviceConfig::get().displayName.c_str());
    displayNameChar->setCallbacks(new DisplayNameCallbacks());

    NimBLECharacteristic* roleChar = service->createCharacteristic(
        CHAR_ROLE_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE);
    roleChar->setValue(roleToString(DeviceConfig::get().role));
    roleChar->setCallbacks(new RoleCallbacks());

    NimBLECharacteristic* commitChar = service->createCharacteristic(
        CHAR_COMMIT_UUID, NIMBLE_PROPERTY::WRITE);
    commitChar->setCallbacks(new CommitCallbacks());

    statusChar = service->createCharacteristic(
        CHAR_STATUS_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    String status = String("{\"nodeId\":\"") + DeviceConfig::get().nodeId + "\"}";
    statusChar->setValue(status.c_str());

    service->start();

    NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
    advertising->addServiceUUID(SERVICE_UUID);
    advertising->start();

    active = true;
    Logger::info(TAG, "BLE provisioning advertising as \"" + deviceName + "\"");
}

void BLEProvisioning::stop() {
    NimBLEDevice::deinit(true);
    server = nullptr;
    statusChar = nullptr;
    active = false;
    Logger::info(TAG, "BLE provisioning stopped");
}

bool BLEProvisioning::isActive() {
    return active;
}

bool BLEProvisioning::isCommitted() {
    return committed;
}

}  // namespace CarSentinel
