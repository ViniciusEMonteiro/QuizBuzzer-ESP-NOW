#pragma once
#include "protocol.h"
namespace quiz {
enum class LedMode {
    INITIALIZING, READY, WAITING, WINNER, LOCKED, DISCONNECTED,
    MASTER_ARMED, MASTER_RESET_READY, ERROR
};
// Estados logicos de iluminacao. A polaridade eletrica e aplicada pelo hardware.
struct LedLevels {
    bool button;
    bool external;
};
class LedManager {
public:
    void setMode(LedMode mode, uint32_t now) {
        if (mode_ != mode) { mode_ = mode; since_ = now; }
    }
    LedLevels levels(uint32_t now) const;
    static LedMode modeFor(SlaveState state, bool synchronized);
    static LedMode modeFor(MasterState state);
private:
    LedMode mode_ = LedMode::INITIALIZING;
    uint32_t since_ = 0;
};
}  // namespace quiz
