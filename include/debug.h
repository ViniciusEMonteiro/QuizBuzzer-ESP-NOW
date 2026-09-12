#pragma once
#include "config.h"
namespace quiz {
bool startLogger();
void logMessage(char level, const char* format, ...) __attribute__((format(printf, 2, 3)));
uint32_t droppedLogs();
}
#define LOG_INFO(...) do { if (quiz::DEBUG_ENABLED) quiz::logMessage('I', __VA_ARGS__); } while (0)
#define LOG_WARNING(...) do { if (quiz::DEBUG_ENABLED) quiz::logMessage('W', __VA_ARGS__); } while (0)
#define LOG_ERROR(...) quiz::logMessage('E', __VA_ARGS__)
#define LOG_DEBUG(...) do { if (quiz::DEBUG_ENABLED && quiz::VERBOSE_DEBUG_ENABLED) quiz::logMessage('D', __VA_ARGS__); } while (0)
