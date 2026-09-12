#include "espnow_manager.h"
#include <cstring>
#include "debug.h"
#include "hardware.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_idf_version.h"
static_assert(ESP_IDF_VERSION_MAJOR == 6, "Callbacks implementados para ESP-IDF 6.x");
namespace quiz {
EspNowManager* EspNowManager::instance_ = nullptr;
esp_err_t EspNowManager::begin(DeviceRole role, uint8_t id, TaskHandle_t owner) {
    if (instance_ || !validateDeviceTable() || id > MAX_SLAVES ||
        (role == DeviceRole::MASTER) != (id == MASTER_ID)) return ESP_ERR_INVALID_ARG;
    owner_ = owner; localID_ = id;
    rxQueue_ = xQueueCreate(RX_QUEUE_LENGTH, sizeof(QuizMessage));
    txQueue_ = xQueueCreate(1, sizeof(esp_now_send_status_t));
    if (!rxQueue_ || !txQueue_) return ESP_ERR_NO_MEM;
    esp_err_t result = esp_netif_init();
    if (result != ESP_OK) return result;
    result = esp_event_loop_create_default();
    if (result != ESP_OK) return result;
    const wifi_init_config_t wifiConfig = WIFI_INIT_CONFIG_DEFAULT();
    result = esp_wifi_init(&wifiConfig);
    if (result != ESP_OK) return result;
    result = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (result != ESP_OK) return result;
    result = esp_wifi_set_mode(WIFI_MODE_STA);
    if (result != ESP_OK) return result;
    result = esp_wifi_start();
    if (result != ESP_OK) return result;
    result = esp_wifi_set_ps(WIFI_PS_NONE);
    if (result != ESP_OK) return result;
    result = esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
    if (result != ESP_OK) return result;
    result = esp_now_init();
    if (result != ESP_OK) return result;
    // Nenhum AP, scan, associacao, DHCP ou roteador. Somente STA e canal fixo.
    for (uint8_t peerID = 0; peerID <= MAX_SLAVES; ++peerID) {
        if (peerID == localID_ || (role == DeviceRole::SLAVE && peerID != MASTER_ID)) continue;
        esp_now_peer_info_t peer{};
        std::memcpy(peer.peer_addr, DEVICES[peerID].mac, ESP_NOW_ETH_ALEN);
        peer.channel = WIFI_CHANNEL; peer.ifidx = WIFI_IF_STA;
        peer.encrypt = false; // Ver docs/protocolo.md: whitelist nao e autenticacao.
        result = esp_now_add_peer(&peer);
        if (result != ESP_OK) return result;
        LOG_DEBUG("Peer %02u cadastrado", static_cast<unsigned>(peerID));
    }
    instance_ = this;
    result = esp_now_register_recv_cb(onReceive);
    if (result != ESP_OK) return result;
    result = esp_now_register_send_cb(onSend);
    if (result == ESP_OK) initialized_ = true;
    return result;
}
void EspNowManager::onReceive(const esp_now_recv_info_t* info, const uint8_t* data, int length) {
    auto* self = instance_;
    if (!self) return;
    QuizMessage message{};
    if (!info || !info->src_addr || !info->des_addr || length != static_cast<int>(WIRE_MESSAGE_SIZE) ||
        !decodeMessage(data, static_cast<size_t>(length), message) ||
        !macMatches(message.senderID, info->src_addr) ||
        !macMatches(self->localID_, info->des_addr) || message.targetID != self->localID_) {
        self->invalidRx_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    // Copia por valor: pointers do SDK nao sobrevivem ao callback. FIFO preserva ordem.
    // Sem logs, heap, mutex ou arbitragem dentro da tarefa Wi-Fi.
    if (xQueueSend(self->rxQueue_, &message, 0) != pdTRUE)
        self->droppedRx_.fetch_add(1, std::memory_order_relaxed);
    xTaskNotifyGive(self->owner_);
}
void EspNowManager::onSend(const esp_now_send_info_t*, esp_now_send_status_t status) {
    if (!instance_) return;
    // Uma unica transmissao em voo; mailbox e consumida somente pela tarefa do jogo.
    xQueueOverwrite(instance_->txQueue_, &status);
    xTaskNotifyGive(instance_->owner_);
}
bool EspNowManager::trySend(const QuizMessage& m) {
    if (!initialized_ || busy_ || fault_ || m.senderID != localID_ || !validMessage(m)) return false;
    const uint32_t now = nowMs();
    if (backoff_ && !elapsed(now, lastError_, RADIO_ERROR_BACKOFF_MS)) return false;
    backoff_ = false;
    const auto data = encodeMessage(m);
    const esp_err_t result = esp_now_send(DEVICES[m.targetID].mac, data.data(), data.size());
    if (result != ESP_OK) {
        ++sendErrors_;
        backoff_ = true; lastError_ = now;
        if (result != ESP_ERR_ESPNOW_NO_MEM) { fault_ = true; faultReason_ = result; }
        return false;
    }
    busy_ = true; sentAt_ = now;
    return true;
}
bool EspNowManager::receive(QuizMessage& m) { return xQueueReceive(rxQueue_, &m, 0) == pdTRUE; }
void EspNowManager::poll(uint32_t now) {
    esp_now_send_status_t status{};
    if (xQueueReceive(txQueue_, &status, 0) == pdTRUE) {
        busy_ = false;
        if (status == ESP_NOW_SEND_SUCCESS) ++txSuccess_; else ++txFailure_;
    }
    // Nao soltar busy cegamente: um callback tardio seria atribuido ao proximo envio.
    if (busy_ && elapsed(now, sentAt_, RADIO_CALLBACK_TIMEOUT_MS)) {
        fault_ = true; faultReason_ = ESP_ERR_TIMEOUT;
    }
}
RadioStats EspNowManager::stats() const {
    return {invalidRx_.load(std::memory_order_relaxed), droppedRx_.load(std::memory_order_relaxed),
            txSuccess_, txFailure_, sendErrors_};
}
}  // namespace quiz
