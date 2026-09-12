#pragma once
#include <cstdint>

namespace quiz {
// ESP32 classico / ESP32-WROOM-32. TODO: confirmar GPIO conforme PCB definitiva.
// Evitamos strapping, flash e os GPIOs 34..39 (sem pull-up interno).
constexpr int ROLE_SELECT_PIN = 27;          // Pull-up; jumper para GND = MASTER.
constexpr int SLAVE_BUTTON_PIN = 25;         // Botao normalmente aberto para GND.
constexpr int SLAVE_LED_PIN = 26;            // Acionamento externo, ativo em HIGH.
constexpr int MASTER_RESET_BUTTON_PIN = 33;  // Botao normalmente aberto para GND.
static_assert(ROLE_SELECT_PIN != SLAVE_BUTTON_PIN && ROLE_SELECT_PIN != SLAVE_LED_PIN &&
              ROLE_SELECT_PIN != MASTER_RESET_BUTTON_PIN &&
              SLAVE_BUTTON_PIN != SLAVE_LED_PIN &&
              SLAVE_BUTTON_PIN != MASTER_RESET_BUTTON_PIN &&
              SLAVE_LED_PIN != MASTER_RESET_BUTTON_PIN, "GPIOs devem ser distintos");
}  // namespace quiz
