#include <Arduino.h>
#include <atomic>
#include <cstdio>
#include <memory>

#include "watchos/cli/cli_app.hpp"
#include "watchos/cli/console_ui.hpp"
#include "watchos/hardware/tft_dashboard.hpp"

namespace {
constexpr uint8_t START_BUTTON_PIN = 32;
constexpr uint8_t EMERGENCY_BUTTON_PIN = 33;
constexpr uint32_t START_DEBOUNCE_MS = 30;
constexpr uint32_t LOOP_DELAY_MS = 10;

std::unique_ptr<watchos::cli::CliApp> cliApp;
TaskHandle_t cliTaskHandle = nullptr;
std::atomic<bool> cliTaskFinished{false};
portMUX_TYPE emergencyMux = portMUX_INITIALIZER_UNLOCKED;
volatile bool emergencyIsrFlag = false;

bool cliRunning = false;
bool emergencyStopped = false;
bool startPending = false;
int lastStartReading = HIGH;
int stableStartReading = HIGH;
uint32_t lastStartChangeMs = 0;

void IRAM_ATTR emergencyExitIsr() {
    portENTER_CRITICAL_ISR(&emergencyMux);
    emergencyIsrFlag = true;
    portEXIT_CRITICAL_ISR(&emergencyMux);
}

bool takeEmergencyRequest() {
    bool requested;
    portENTER_CRITICAL(&emergencyMux);
    requested = emergencyIsrFlag;
    emergencyIsrFlag = false;
    portEXIT_CRITICAL(&emergencyMux);
    return requested;
}

void cliTask(void* argument) {
    auto* app = static_cast<watchos::cli::CliApp*>(argument);
    const int result = app->run();
    char message[48];
    std::snprintf(message, sizeof(message), "CLI session ended with status %d", result);
    watchos::hardware::logSerial("CLI", message);
    cliTaskFinished.store(true, std::memory_order_release);
    vTaskDelete(nullptr);
}

void startCliSession() {
    if (cliRunning) {
        return;
    }

    watchos::cli::ConsoleUi::clearEmergencyStop();
    cliTaskFinished.store(false, std::memory_order_release);
    cliApp.reset(new watchos::cli::CliApp("data/watchos_sessions.db"));
    watchos::hardware::setDashboardWaiting(true);
    watchos::hardware::setDashboardActive(true);

    const BaseType_t created = xTaskCreatePinnedToCore(
        cliTask, "watchos-cli", 16384, cliApp.get(), 1, &cliTaskHandle, 0);
    if (created != pdPASS) {
        cliApp.reset();
        cliTaskHandle = nullptr;
        watchos::hardware::setDashboardActive(false);
        watchos::hardware::logSerial("ERROR", "Failed to create CLI task");
        return;
    }

    cliRunning = true;
    emergencyStopped = false;
    watchos::hardware::logSerial("BUTTON", "START accepted; CLI session launched");
}

void serviceButtons() {
    const bool emergencyRequested = takeEmergencyRequest();
    if (emergencyRequested) {
        emergencyStopped = true;
        startPending = false;
        digitalWrite(25, LOW);
        watchos::hardware::setDashboardEmergencyStopped(true);
        if (cliApp) {
            cliApp->requestEmergencyStop();
        }
    }

    const int reading = digitalRead(START_BUTTON_PIN);
    const uint32_t now = millis();
    if (reading != lastStartReading) {
        lastStartReading = reading;
        lastStartChangeMs = now;
    }
    if (now - lastStartChangeMs >= START_DEBOUNCE_MS &&
        reading != stableStartReading) {
        stableStartReading = reading;
        if (stableStartReading == LOW && !emergencyRequested) {
            startPending = true;
        }
    }

    if (cliRunning && cliTaskFinished.load(std::memory_order_acquire)) {
        cliRunning = false;
        cliTaskHandle = nullptr;
        cliApp.reset();
        if (!emergencyStopped) {
            watchos::hardware::setDashboardWaiting(true);
        }
    }

    if (startPending && !emergencyRequested) {
        startPending = false;
        if (cliRunning) {
            watchos::hardware::cycleDashboardView();
            watchos::hardware::logSerial("BUTTON", "Dashboard view advanced");
        } else {
            startCliSession();
        }
    }
}
}

void setup() {
    Serial.begin(115200);
    pinMode(START_BUTTON_PIN, INPUT_PULLUP);
    pinMode(EMERGENCY_BUTTON_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(EMERGENCY_BUTTON_PIN), emergencyExitIsr, FALLING);

    auto& dashboard = watchos::hardware::TftDashboard::instance();
    if (!dashboard.begin()) {
        watchos::hardware::logSerial("ERROR", "TFT dashboard initialization failed");
    }
    watchos::cli::ConsoleUi::installOutputMirror();

    watchos::hardware::setDashboardWaiting(true);
    watchos::hardware::logSerial("BOOT", "Waiting for START button");
}

void loop() {
    serviceButtons();
    watchos::hardware::TftDashboard::instance().service();
    vTaskDelay(pdMS_TO_TICKS(LOOP_DELAY_MS));
}
