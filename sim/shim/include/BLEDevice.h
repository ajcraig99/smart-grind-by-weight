// Declarations-only Bluetooth LE library surface. The twin has no radio: the firmware's
// BluetoothManager is replaced by a never-connected stub (sim/shim/stubs/comms_stubs.cpp).
#pragma once
#include <Arduino.h>
#include <cstdint>
#include <string>

class BLEServer;
class BLEService;
class BLECharacteristic;
class BLEAdvertising;
class BLEDescriptor;

class BLEServerCallbacks {
public:
    virtual ~BLEServerCallbacks() = default;
    virtual void onConnect(BLEServer*) {}
    virtual void onDisconnect(BLEServer*) {}
};

class BLECharacteristicCallbacks {
public:
    virtual ~BLECharacteristicCallbacks() = default;
    virtual void onWrite(BLECharacteristic*) {}
    virtual void onRead(BLECharacteristic*) {}
};

class BLEDevice {
public:
    static void init(const std::string&) {}
    static void deinit(bool = false) {}
    static BLEServer* createServer() { return nullptr; }
    static BLEAdvertising* getAdvertising() { return nullptr; }
    static uint16_t getMTU() { return 23; }
    static void setMTU(uint16_t) {}
};
