#include "device_config.h"
#include <cstring>
namespace quiz {
// MACs FICTICIOS: substituir TODOS pelos MACs STA impressos no boot.
// Nao sobrescrevemos o MAC de fabrica. A tabela deve ser igual nas nove placas.
const DeviceConfig DEVICES[MAX_SLAVES + 1] = {
    {0, {0x02, 0x00, 0x00, 0x00, 0x00, 0x10}},
    {1, {0x02, 0x00, 0x00, 0x00, 0x00, 0x11}},
    {2, {0x02, 0x00, 0x00, 0x00, 0x00, 0x12}},
    {3, {0x02, 0x00, 0x00, 0x00, 0x00, 0x13}},
    {4, {0x02, 0x00, 0x00, 0x00, 0x00, 0x14}},
    {5, {0x02, 0x00, 0x00, 0x00, 0x00, 0x15}},
    {6, {0x02, 0x00, 0x00, 0x00, 0x00, 0x16}},
    {7, {0x02, 0x00, 0x00, 0x00, 0x00, 0x17}},
    {8, {0x02, 0x00, 0x00, 0x00, 0x00, 0x18}},
};
bool macMatches(uint8_t id, const uint8_t* mac) {
    return mac && id <= MAX_SLAVES && std::memcmp(DEVICES[id].mac, mac, 6) == 0;
}
bool validateDeviceTable() {
    const uint8_t zero[6]{};
    for (uint8_t i = 0; i <= MAX_SLAVES; ++i) {
        if (DEVICES[i].id != i || (DEVICES[i].mac[0] & 1) ||
            std::memcmp(DEVICES[i].mac, zero, 6) == 0) return false;
        for (uint8_t j = 0; j < i; ++j) if (macMatches(j, DEVICES[i].mac)) return false;
    }
    return true;
}
uint8_t ConfigDeviceIdProvider::slaveId(const uint8_t* mac) const {
    if (CONFIGURED_SLAVE_ID) return CONFIGURED_SLAVE_ID;
    for (uint8_t i = 1; i <= MAX_SLAVES; ++i) if (macMatches(i, mac)) return i;
    return INVALID_ID;
}
}  // namespace quiz
