#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

// WiFi serial log bridge.
//
// Core 1 calls dlog() / dlogs() — messages go to USB Serial AND are queued for
// forwarding to any TCP client connected on TCP_LOG_PORT (8890).
// Core 0 (networkTask) drains the queue and writes to the log client.
//
// Usage:
//   dlog("Voice: captured %zu bytes", n);   // printf-style
//   dlogs("Voice: tap received");            // string literal (faster, no format parse)
//
// To receive from a Mac/Linux machine:
//   nc quadruped.local 8890
//
// Or use software/serial_monitor.py

#define LOG_QUEUE_LEN 20
#define LOG_MSG_MAX  120

extern QueueHandle_t logQueue;

// printf-style log — sends to USB Serial + WiFi log queue.
// Safe to call from Core 1 (loop task) only. xQueueSend is ISR-safe from any core
// but we only write from Core 1 to match the rest of the sketch's pattern.
static inline void __attribute__((format(printf, 1, 2))) dlog(const char* fmt, ...) {
    char buf[LOG_MSG_MAX];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    Serial.println(buf);
    if (logQueue) xQueueSend(logQueue, buf, 0);
}

// Fast path for string literals — avoids format parsing overhead.
static inline void dlogs(const char* msg) {
    Serial.println(msg);
    if (logQueue) {
        char buf[LOG_MSG_MAX];
        strncpy(buf, msg, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        xQueueSend(logQueue, buf, 0);
    }
}
