#pragma once
#include <cstdint>

namespace quiz {
// ESP32 classico / ESP32-WROOM-32. TODO: confirmar GPIO conforme PCB definitiva.
// Evitamos strapping, flash e os GPIOs 34..39 (sem pull-up interno).
constexpr int ROLE_SELECT_PIN = 27;          // Pull-up; jumper para GND = MASTER.
constexpr int BUTTON_PIN = 25;              // Botao unico: resposta SLAVE / nova rodada MASTER.
constexpr int SLAVE_LED_PIN = 26;            // LED do botao, tambem usado no MASTER.
constexpr int EXTERNAL_LED_PIN = 32;         // MOSFET da fita LED 12 V da bancada.
static_assert(ROLE_SELECT_PIN != BUTTON_PIN && ROLE_SELECT_PIN != SLAVE_LED_PIN &&
              BUTTON_PIN != SLAVE_LED_PIN &&
              EXTERNAL_LED_PIN != ROLE_SELECT_PIN && EXTERNAL_LED_PIN != BUTTON_PIN &&
              EXTERNAL_LED_PIN != SLAVE_LED_PIN,
              "GPIOs devem ser distintos");
}  // namespace quiz
