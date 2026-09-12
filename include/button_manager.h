#pragma once
#include "config.h"
namespace quiz {
// Debounce independente de GPIO, para teste e reutilizacao.
class ButtonManager {
public:
    explicit ButtonManager(uint32_t debounceMs) : debounceMs_(debounceMs) {}
    void begin(bool pressed, uint32_t now);
    bool update(bool pressed, uint32_t now);
private:
    uint32_t debounceMs_;
    uint32_t pressedAt_ = 0;
    uint32_t releasedAt_ = 0;
    bool latched_ = true;
    bool releasing_ = false;
};
}  // namespace quiz
