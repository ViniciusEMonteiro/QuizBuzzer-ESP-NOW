#pragma once
#include "protocol.h"
namespace quiz {
enum class LedMode { INITIALIZING, READY, WAITING, WINNER, LOCKED, ERROR };
class LedManager {
public:
    void setMode(LedMode mode, uint32_t now) {
        if (mode_ != mode) { mode_ = mode; since_ = now; }
    }
    bool level(uint32_t now) const;
    static LedMode modeFor(SlaveState state);
private:
    LedMode mode_ = LedMode::INITIALIZING;
    uint32_t since_ = 0;
};
}  // namespace quiz
