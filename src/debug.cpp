#include "debug.h"
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
namespace quiz {
namespace {
struct LogEntry { char level; char text[LOG_MESSAGE_LENGTH]; };
QueueHandle_t queue = nullptr;
std::atomic<uint32_t> dropped{0};
void loggerTask(void*) {
    LogEntry entry{};
    while (true) {
        if (xQueueReceive(queue, &entry, portMAX_DELAY) == pdTRUE)
            std::printf("[%c] %s\n", entry.level, entry.text);
    }
}
}
bool startLogger() {
    queue = xQueueCreate(LOG_QUEUE_LENGTH, sizeof(LogEntry));
    return queue && xTaskCreate(loggerTask, "quiz_log", 3072, nullptr, 1, nullptr) == pdPASS;
}
void logMessage(char level, const char* format, ...) {
    if (!queue) return;
    LogEntry entry{};
    entry.level = level;
    va_list args;
    va_start(args, format);
    std::vsnprintf(entry.text, sizeof(entry.text), format, args);
    va_end(args);
    if (xQueueSend(queue, &entry, 0) != pdTRUE) dropped.fetch_add(1, std::memory_order_relaxed);
}
uint32_t droppedLogs() { return dropped.load(std::memory_order_relaxed); }
}  // namespace quiz
