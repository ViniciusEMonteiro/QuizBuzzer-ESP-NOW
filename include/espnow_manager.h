#pragma once
#include <atomic>
#include "device_config.h"
#include "esp_now.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
namespace quiz {
struct RadioStats {
    uint32_t invalidRx;
    uint32_t droppedRx;
    uint32_t txSuccess;
    uint32_t txFailure;
    uint32_t sendErrors;
};
class EspNowManager final : public Transport {
public:
    esp_err_t begin(DeviceRole role, uint8_t localID, TaskHandle_t owner);
    bool trySend(const QuizMessage& message) override;
    bool receive(QuizMessage& message);
    void poll(uint32_t now);
    bool healthy() const { return !fault_; }
    esp_err_t faultReason() const { return faultReason_; }
    RadioStats stats() const;
private:
    static void onReceive(const esp_now_recv_info_t* info, const uint8_t* data, int length);
    static void onSend(const esp_now_send_info_t* info, esp_now_send_status_t status);
    static EspNowManager* instance_;
    QueueHandle_t rxQueue_ = nullptr;
    QueueHandle_t txQueue_ = nullptr;
    TaskHandle_t owner_ = nullptr;
    uint8_t localID_ = INVALID_ID;
    bool initialized_ = false;
    bool busy_ = false;
    bool fault_ = false;
    esp_err_t faultReason_ = ESP_OK;
    uint32_t sentAt_ = 0;
    uint32_t lastError_ = 0;
    bool backoff_ = false;
    std::atomic<uint32_t> invalidRx_{0};
    std::atomic<uint32_t> droppedRx_{0};
    uint32_t txSuccess_ = 0;
    uint32_t txFailure_ = 0;
    uint32_t sendErrors_ = 0;
};
}  // namespace quiz
