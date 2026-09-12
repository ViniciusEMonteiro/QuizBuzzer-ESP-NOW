#include "button_manager.h"
namespace quiz {
void ButtonManager::begin(bool pressed, uint32_t now) {
    latched_ = pressed; // Botao mantido no boot nao dispara.
    releasing_ = false;
    pressedAt_ = releasedAt_ = now;
}
bool ButtonManager::update(bool pressed, uint32_t now) {
    if (!latched_ && pressed) {
        latched_ = true; releasing_ = false; pressedAt_ = now;
        return true; // Primeira borda: nao aguarda o debounce.
    }
    if (latched_) {
        if (pressed) releasing_ = false;
        else if (!releasing_) { releasing_ = true; releasedAt_ = now; }
        else if (elapsed(now, releasedAt_, debounceMs_) && elapsed(now, pressedAt_, debounceMs_)) {
            latched_ = false; releasing_ = false;
        }
    }
    return false;
}
}  // namespace quiz
