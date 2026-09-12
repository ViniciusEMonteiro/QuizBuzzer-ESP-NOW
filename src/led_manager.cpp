#include "led_manager.h"
namespace quiz {
bool LedManager::level(uint32_t now) const {
    const uint32_t phase = now - since_;
    switch (mode_) {
        case LedMode::READY: return true;
        case LedMode::WINNER: return !WINNER_LED_BLINK || (phase / WINNER_BLINK_MS) % 2 == 0;
        case LedMode::INITIALIZING: return (phase / BOOT_BLINK_MS) % 2 == 0;
        case LedMode::WAITING: return (phase / WAITING_BLINK_MS) % 2 == 0;
        case LedMode::LOCKED: return phase % HEARTBEAT_PERIOD_MS < HEARTBEAT_ON_MS;
        case LedMode::ERROR: return phase % HEARTBEAT_PERIOD_MS < 3 * BOOT_BLINK_MS &&
                                   (phase / BOOT_BLINK_MS) % 2 == 0;
    }
    return false;
}
LedMode LedManager::modeFor(SlaveState state) {
    switch (state) {
        case SlaveState::INITIALIZING: return LedMode::INITIALIZING;
        case SlaveState::READY: return LedMode::READY;
        case SlaveState::WAITING_MASTER: return LedMode::WAITING;
        case SlaveState::WINNER: return LedMode::WINNER;
        case SlaveState::LOCKED: return LedMode::LOCKED;
    }
    return LedMode::ERROR;
}
}  // namespace quiz
