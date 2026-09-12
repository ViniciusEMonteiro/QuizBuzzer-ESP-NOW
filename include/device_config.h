#pragma once
#include "protocol.h"
namespace quiz {
struct DeviceConfig { uint8_t id; uint8_t mac[6]; };
extern const DeviceConfig DEVICES[MAX_SLAVES + 1];
bool validateDeviceTable();
bool macMatches(uint8_t id, const uint8_t* mac);
class DeviceIdProvider {
public:
    virtual ~DeviceIdProvider() = default;
    virtual uint8_t slaveId(const uint8_t* localMac) const = 0;
};
// Ponto de extensao: implementar DeviceIdProvider para DIP, jumpers, NVS ou UI.
class ConfigDeviceIdProvider final : public DeviceIdProvider {
public:
    uint8_t slaveId(const uint8_t* localMac) const override;
};
}  // namespace quiz
