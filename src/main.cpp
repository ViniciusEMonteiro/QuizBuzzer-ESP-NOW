#include <cinttypes>
#include "button_manager.h"
#include "debug.h"
#include "espnow_manager.h"
#include "hardware.h"
#include "master_controller.h"
#include "slave_controller.h"
#include "esp_mac.h"
#include "esp_log.h"

namespace quiz {
namespace {
// Falha de configuracao/armazenamento/radio impede o jogo. LED/log continuam ativos.
[[noreturn]] void halt(const char* operation, esp_err_t error) {
    LOG_ERROR("%s: %s. Corrija a causa e reinicie.", operation, esp_err_to_name(error));
    setLedMode(LedMode::ERROR);
    // Preserva o TaskHandle que os callbacks/ISR ainda podem notificar.
    while (true) ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
}
void check(esp_err_t result, const char* operation) { if (result != ESP_OK) halt(operation, result); }
bool buttonEvent(ButtonManager& button, uint32_t now) {
    const bool edge = consumeButtonEdge();
    const bool pressed = buttonIsPressed();
    // Tambem captura pulsos curtos entre duas iteracoes; bouncing fica no debounce.
    const bool event = button.update(edge || pressed, now);
    if (edge && !pressed) button.update(false, now);
    return event;
}
void diagnostics(EspNowManager& radio, uint32_t now, uint32_t& last) {
    if (!elapsed(now, last, DIAGNOSTICS_INTERVAL_MS)) return;
    last = now;
    const auto stats = radio.stats();
    LOG_DEBUG("Radio: RX invalido=%" PRIu32 " overflow=%" PRIu32 " TX ok=%" PRIu32
              " falha=%" PRIu32 " erro=%" PRIu32 " logs perdidos=%" PRIu32,
              stats.invalidRx, stats.droppedRx, stats.txSuccess, stats.txFailure, stats.sendErrors, droppedLogs());
    if (stats.droppedRx) LOG_WARNING("RX overflow acumulado=%" PRIu32 "; verificar interferencia/carga", stats.droppedRx);
}
void runMaster(EspNowManager& radio, uint64_t bootID) {
    MasterController master(radio, bootID);
    ButtonManager button(MASTER_BUTTON_DEBOUNCE_MS);
    button.begin(buttonIsPressed(), nowMs());
    master.begin();
    uint32_t lastDiagnostics = nowMs();
    std::array<bool, MAX_SLAVES + 1> online{};
    LOG_INFO("[ROUND %u] System %s", master.roundID(), master.acceptButtons() ? "ARMED" : "INITIALIZING");
    for (uint8_t id = 1; id <= MAX_SLAVES; ++id) LOG_INFO("SLAVE %02u - OFFLINE (aguardando)", id);
    while (true) {
        const uint32_t now = nowMs();
        radio.poll(now);
        if (!radio.healthy()) halt("Falha persistente ESP-NOW", radio.faultReason());
        const bool reset = buttonEvent(button, now);
        // RESET tem precedencia sobre a fila pendente; pacotes antigos falham no roundID.
        if (reset && !master.newRound()) halt("Contador de rodada esgotado", ESP_ERR_INVALID_STATE);
        const uint8_t before = master.winnerID();
        QuizMessage message{};
        for (size_t i = 0; i < RX_BATCH_SIZE && radio.receive(message); ++i) {
            master.onMessage(message, now);
            // Se houve vencedor, envia antes de consumir os proximos pacotes.
            if (!before && master.winnerID()) { master.tick(now); break; }
        }
        master.tick(now);
        // Logs e LEDs somente depois da tentativa de envio da decisao.
        if (reset) LOG_INFO("[ROUND %u] New round; System ARMED", master.roundID());
        if (!before && master.winnerID()) {
            LOG_INFO("[ROUND %u] BUTTON received from SLAVE %02u", master.roundID(), master.winnerID());
            LOG_INFO("[ROUND %u] WINNER: SLAVE %02u; System LOCKED", master.roundID(), master.winnerID());
        }
        setLedMode(LedManager::modeFor(master.state()));
        for (uint8_t id = 1; id <= MAX_SLAVES; ++id) {
            if (online[id] != master.online(id)) {
                online[id] = master.online(id);
                LOG_INFO("SLAVE %02u - %s", id, online[id] ? "ONLINE" : "OFFLINE");
            }
        }
        diagnostics(radio, now, lastDiagnostics);
        // Espera interrompivel por RX, TX ou borda de GPIO. Nao e debounce/delay ativo.
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(APP_POLL_MS));
    }
}
void runSlave(EspNowManager& radio, uint8_t id, uint64_t bootID) {
    SlaveController slave(radio, id, bootID);
    ButtonManager button(BUTTON_DEBOUNCE_MS);
    button.begin(buttonIsPressed(), nowMs());
    uint32_t lastDiagnostics = nowMs();
    while (true) {
        const uint32_t now = nowMs();
        radio.poll(now);
        if (!radio.healthy()) halt("Falha persistente ESP-NOW", radio.faultReason());
        // Primeiro caminho util da iteracao: botao -> esp_now_send.
        if (buttonEvent(button, now)) slave.onButton(now);
        QuizMessage message{};
        for (size_t i = 0; i < RX_BATCH_SIZE && radio.receive(message); ++i) {
            slave.onMessage(message, now);
            // Interrompe trabalho secundario se uma nova borda chegou durante RX.
            if (buttonEvent(button, nowMs())) slave.onButton(nowMs());
        }
        // Uma borda dentro do lote pode ter horario posterior ao inicio da iteracao.
        // Releia o tempo para nao transformar essa diferenca em timeout por underflow.
        slave.tick(nowMs());
        setLedMode(LedManager::modeFor(slave.state(), slave.synchronized()));
        diagnostics(radio, now, lastDiagnostics);
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(APP_POLL_MS));
    }
}
void gameTask(void*) {
    DeviceRole role = DeviceRole::SLAVE;
    check(readDeviceRole(role), "GPIO ROLE_SELECT");
    uint8_t mac[6]{};
    check(esp_read_mac(mac, ESP_MAC_WIFI_STA), "Leitura do MAC STA");
    const ConfigDeviceIdProvider idProvider;
    const uint8_t id = role == DeviceRole::MASTER ? MASTER_ID : idProvider.slaveId(mac);
    LOG_INFO("QuizBuzzer ESP-NOW / ESP-IDF 6.x");
    LOG_INFO("MAC local: %02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    LOG_INFO("Funcao: %s; ID: %02u", role == DeviceRole::MASTER ? "MASTER" : "SLAVE", id);
    if (!validateDeviceTable() || id == INVALID_ID || !macMatches(id, mac))
        halt("Substitua MACs ficticios em src/device_config.cpp e confira ID/jumper", ESP_ERR_INVALID_ARG);
    check(initializeStorage(), "Inicializacao NVS (sem apagamento automatico)");
    uint64_t bootID = 0;
    check(nextBootID(bootID), "Contador persistente de boot");
    LOG_INFO("Boot: %" PRIu64 "; canal ESP-NOW: %u", bootID, WIFI_CHANNEL);
    static EspNowManager radio;
    check(radio.begin(role, id, xTaskGetCurrentTaskHandle()), "Inicializacao ESP-NOW/peers");
    check(initializeButton(xTaskGetCurrentTaskHandle()), "GPIO botao unico");
    // Somente o controlador correspondente ao jumper e construido/executado.
    if (role == DeviceRole::MASTER) runMaster(radio, bootID);
    else runSlave(radio, id, bootID);
}
}
}  // namespace quiz
extern "C" void app_main() {
    using namespace quiz;
    ESP_ERROR_CHECK(startLogger() ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(initializeIndicators());
    esp_log_level_set("wifi", ESP_LOG_ERROR);
#if CONFIG_FREERTOS_UNICORE
    constexpr BaseType_t gameCore = 0;
#else
    constexpr BaseType_t gameCore = 1;
#endif
    ESP_ERROR_CHECK(xTaskCreatePinnedToCore(gameTask, "quiz_game", 8192, nullptr, 5, nullptr, gameCore)
                       == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}
