#include "led_manager.h"
namespace quiz {
LedLevels LedManager::levels(uint32_t now) const {
    const uint32_t phase = now - since_;
    switch (mode_) {
        case LedMode::READY:
        case LedMode::WAITING: return {true, false};
        case LedMode::WINNER: {
            // Um unico calculo de fase comanda os dois LEDs em sincronia.
            const bool on = (phase / WINNER_BLINK_MS) % 2 == 0;
            return {on, on};
        }
        case LedMode::INITIALIZING: return {(phase / BOOT_BLINK_MS) % 2 == 0, false};
        case LedMode::LOCKED:
        case LedMode::DISCONNECTED: return {false, false};
        case LedMode::MASTER_ARMED: return {false, true};
        case LedMode::MASTER_RESET_READY: return {true, false};
        case LedMode::ERROR: {
            const uint32_t errorPhase = phase % ERROR_PATTERN_PERIOD_MS;
            const bool on = errorPhase < 3 * BOOT_BLINK_MS &&
                            (errorPhase / BOOT_BLINK_MS) % 2 == 0;
            return {on, false};
        }
    }
    return {false, false};
}
LedMode LedManager::modeFor(SlaveState state, bool synchronized) {
    // Vitoria confirmada permanece ate a nova rodada. Fora disso, sem conexao
    // sincronizada nao ha luz de disponibilidade, inclusive durante WAITING_MASTER.
    if (state == SlaveState::WINNER) return LedMode::WINNER;
    if (state == SlaveState::LOCKED) return LedMode::LOCKED;
    if (!synchronized) return LedMode::DISCONNECTED;
    switch (state) {
        case SlaveState::INITIALIZING: return LedMode::INITIALIZING;
        case SlaveState::READY: return LedMode::READY;
        case SlaveState::WAITING_MASTER: return LedMode::WAITING;
        case SlaveState::WINNER: return LedMode::WINNER;
        case SlaveState::LOCKED: return LedMode::LOCKED;
    }
    return LedMode::ERROR;
}
LedMode LedManager::modeFor(MasterState state) {
    switch (state) {
        case MasterState::ARMED: return LedMode::MASTER_ARMED;
        // INITIALIZING aqui e o controlador pronto, aguardando liberacao manual.
        // O boot de hardware usa LedMode::INITIALIZING diretamente.
        case MasterState::INITIALIZING:
        case MasterState::LOCKED: return LedMode::MASTER_RESET_READY;
    }
    return LedMode::ERROR;
}
}  // namespace quiz
