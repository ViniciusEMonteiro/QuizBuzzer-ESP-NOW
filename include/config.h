#pragma once
#include <cstddef>
#include <cstdint>
#include "pins.h"

namespace quiz {
constexpr uint8_t MASTER_ID = 0;
constexpr uint8_t MAX_SLAVES = 8;
constexpr uint8_t INVALID_ID = 0xFF;
// 0: identifica o SLAVE pelo MAC da tabela, permitindo um binario unico.
// 1..8: alternativa de ID fixo por compilacao; o MAC ainda e conferido.
constexpr uint8_t CONFIGURED_SLAVE_ID = 0;
constexpr uint8_t WIFI_CHANNEL = 6;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 30;
constexpr uint32_t MASTER_BUTTON_DEBOUNCE_MS = 30;
constexpr bool AUTO_ARM_ON_BOOT = true;
constexpr bool DEBUG_ENABLED = true;
constexpr bool VERBOSE_DEBUG_ENABLED = false;
constexpr bool SUPERVISION_ENABLED = true;
constexpr uint32_t SUPERVISION_POLL_MS = 250; // Um peer por vez; ciclo de 2s.
constexpr uint32_t ONLINE_TIMEOUT_MS = 8000;
constexpr uint32_t SYNC_INTERVAL_MS = 1000;
constexpr uint32_t STATE_RETRY_MS = 80;
constexpr uint32_t STATE_SLOW_RETRY_MS = 1000;
constexpr uint8_t STATE_FAST_ATTEMPTS = 6;
constexpr uint32_t BUTTON_RETRY_MS = 40;
constexpr uint8_t BUTTON_MAX_ATTEMPTS = 6;
constexpr uint32_t DECISION_TIMEOUT_MS = 1200;
constexpr uint32_t RADIO_CALLBACK_TIMEOUT_MS = 1000;
constexpr uint32_t RADIO_ERROR_BACKOFF_MS = 5;
constexpr uint32_t APP_POLL_MS = 1;
constexpr uint32_t LED_TASK_POLL_MS = 10;
constexpr size_t RX_QUEUE_LENGTH = 64;
constexpr size_t RX_BATCH_SIZE = 16;
constexpr size_t ACK_HISTORY_SIZE = 8;
constexpr uint32_t BOOT_BLINK_MS = 150;
constexpr uint32_t WINNER_BLINK_MS = 100;
constexpr uint32_t ERROR_PATTERN_PERIOD_MS = 2000;
constexpr bool LED_ACTIVE_HIGH = true;          // LED do botao (SLAVE e MASTER).
constexpr bool EXTERNAL_LED_ACTIVE_HIGH = true; // Gate/driver do MOSFET da fita 12 V.
static_assert(WINNER_BLINK_MS > 0 && BOOT_BLINK_MS > 0 && LED_TASK_POLL_MS > 0);
constexpr uint32_t LOG_QUEUE_LENGTH = 32;
constexpr size_t LOG_MESSAGE_LENGTH = 160;
constexpr uint32_t DIAGNOSTICS_INTERVAL_MS = 10000;
static_assert(MAX_SLAVES > 0 && MAX_SLAVES <= 19, "Limite de peers ESP-NOW");
static_assert(WIFI_CHANNEL >= 1 && WIFI_CHANNEL <= 11, "Canal deve estar entre 1 e 11");
static_assert(CONFIGURED_SLAVE_ID <= MAX_SLAVES, "ID fora da tabela");
static_assert(BUTTON_DEBOUNCE_MS >= 20 && BUTTON_DEBOUNCE_MS <= 40);
inline bool elapsed(uint32_t now, uint32_t since, uint32_t interval) {
    return static_cast<uint32_t>(now - since) >= interval;
}
}  // namespace quiz
