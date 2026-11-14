#include "watchos/hardware/tft_dashboard.hpp"

#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <Arduino.h>
#include <SPI.h>
#include <cstdio>
#include <cstring>
#include <string>

namespace watchos {
namespace hardware {
namespace {

constexpr uint8_t TFT_CS_PIN = 15;
constexpr uint8_t TFT_DC_PIN = 2;
constexpr uint8_t TFT_RST_PIN = 4;
constexpr uint8_t TFT_MOSI_PIN = 23;
constexpr uint8_t TFT_MISO_PIN = 19;
constexpr uint8_t TFT_SCLK_PIN = 18;
constexpr uint8_t ACTIVE_LED_PIN = 25;
constexpr uint8_t DEADLOCK_LED_PIN = 26;
constexpr size_t EVENT_LINES = 5;
constexpr size_t GANTT_SEGMENTS_PER_PAGE = 4;
constexpr uint32_t TERMINAL_REDRAW_INTERVAL_MS = 80;

portMUX_TYPE snapshotMux = portMUX_INITIALIZER_UNLOCKED;
DashboardSnapshot sharedSnapshot;
SemaphoreHandle_t serialMutex = nullptr;
uint8_t ansiEscapeState = 0;
bool terminalMenuSuppressed = false;

void appendEventLocked(const char* tag, const char* message) {
    const uint32_t seconds = millis() / 1000;
    char entry[sizeof(sharedSnapshot.events[0])];
    std::snprintf(entry, sizeof(entry), "[%02lu:%02lu:%02lu] %.10s: %.40s",
                  static_cast<unsigned long>((seconds / 3600) % 24),
                  static_cast<unsigned long>((seconds / 60) % 60),
                  static_cast<unsigned long>(seconds % 60),
                  tag, message);

    const size_t slot = sharedSnapshot.eventCount % EVENT_LINES;
    std::snprintf(sharedSnapshot.events[slot], sizeof(sharedSnapshot.events[slot]), "%s", entry);
    ++sharedSnapshot.eventCount;
}

size_t currentTerminalSlotLocked() {
    return (sharedSnapshot.terminalHead + sharedSnapshot.terminalCount - 1) %
        DASHBOARD_TERMINAL_LINES;
}

void startTerminalLineLocked() {
    size_t slot;
    if (sharedSnapshot.terminalCount < DASHBOARD_TERMINAL_LINES) {
        slot = (sharedSnapshot.terminalHead + sharedSnapshot.terminalCount) %
            DASHBOARD_TERMINAL_LINES;
        ++sharedSnapshot.terminalCount;
    } else {
        sharedSnapshot.terminalHead =
            (sharedSnapshot.terminalHead + 1) % DASHBOARD_TERMINAL_LINES;
        slot = (sharedSnapshot.terminalHead + sharedSnapshot.terminalCount - 1) %
            DASHBOARD_TERMINAL_LINES;
    }
    sharedSnapshot.terminalLines[slot][0] = '\0';
    sharedSnapshot.terminalLineLength = 0;
}

void appendTerminalCharacterLocked(char character) {
    if (character == '\r') return;
    if (character == '\n') {
        if (sharedSnapshot.terminalCount == 0) startTerminalLineLocked();
        startTerminalLineLocked();
        return;
    }

    if (sharedSnapshot.terminalCount == 0) startTerminalLineLocked();
    size_t slot = currentTerminalSlotLocked();
    if (sharedSnapshot.terminalLineLength >= DASHBOARD_TERMINAL_CHARS) {
        char remainder[DASHBOARD_TERMINAL_CHARS + 1] = {};
        size_t remainderLength = 0;
        const char* currentLine = sharedSnapshot.terminalLines[slot];
        size_t split = DASHBOARD_TERMINAL_CHARS;
        while (split > 0 && currentLine[split - 1] != ' ') --split;
        if (split > 0) {
            const size_t suffixStart = split;
            remainderLength = DASHBOARD_TERMINAL_CHARS - suffixStart;
            std::memcpy(remainder, currentLine + suffixStart, remainderLength);
            sharedSnapshot.terminalLineLength = static_cast<uint8_t>(split - 1);
            sharedSnapshot.terminalLines[slot][split - 1] = '\0';
        }
        startTerminalLineLocked();
        slot = currentTerminalSlotLocked();
        if (remainderLength != 0) {
            std::memcpy(sharedSnapshot.terminalLines[slot], remainder, remainderLength);
            sharedSnapshot.terminalLineLength = static_cast<uint8_t>(remainderLength);
            sharedSnapshot.terminalLines[slot][remainderLength] = '\0';
        }
    }

    slot = currentTerminalSlotLocked();
    if (sharedSnapshot.terminalLineLength < DASHBOARD_TERMINAL_CHARS) {
        const size_t length = sharedSnapshot.terminalLineLength;
        sharedSnapshot.terminalLines[slot][length] = character;
        sharedSnapshot.terminalLines[slot][length + 1] = '\0';
        ++sharedSnapshot.terminalLineLength;
    } else {
        startTerminalLineLocked();
        slot = currentTerminalSlotLocked();
        sharedSnapshot.terminalLines[slot][0] = character;
        sharedSnapshot.terminalLines[slot][1] = '\0';
        sharedSnapshot.terminalLineLength = 1;
    }
}

void publishEvent(const char* tag, const char* message) {
    portENTER_CRITICAL(&snapshotMux);
    appendEventLocked(tag, message);
    portEXIT_CRITICAL(&snapshotMux);
    logSerial(tag, message);
}

DashboardSnapshot readSnapshot() {
    DashboardSnapshot copy;
    portENTER_CRITICAL(&snapshotMux);
    copy = sharedSnapshot;
    portEXIT_CRITICAL(&snapshotMux);
    return copy;
}

class DashboardDisplay {
public:
    DashboardDisplay() : tft_(TFT_CS_PIN, TFT_DC_PIN, TFT_RST_PIN) {}

    bool begin() {
        SPI.begin(18, 19, 23, 15);
        tft_.begin();
        tft_.setRotation(1);
        tft_.fillScreen(ILI9341_BLACK);
        drawFrame();
        return true;
    }

    void service() {
        const DashboardSnapshot state = readSnapshot();
        const bool viewChanged = !initialized_ || state.viewMode != last_.viewMode;
        if (state.viewMode == 0) {
            if (viewChanged || state.terminalRevision != renderedTerminalRevision_) {
                const uint32_t now = millis();
                if (viewChanged || now - lastTerminalDrawMs_ >= TERMINAL_REDRAW_INTERVAL_MS) {
                    drawTerminal(state);
                    lastTerminalDrawMs_ = now;
                    renderedTerminalRevision_ = state.terminalRevision;
                }
            }
        } else if (state.viewMode == 1) {
            if (viewChanged || state.ganttRevision != last_.ganttRevision ||
                state.ganttPage != last_.ganttPage) {
                drawGanttChart(state);
            }
        } else if (viewChanged || state.active != last_.active ||
                   state.deadlocked != last_.deadlocked ||
                   state.safeState != last_.safeState ||
                   state.emergencyStopped != last_.emergencyStopped ||
                   state.processCount != last_.processCount ||
                   state.completedCount != last_.completedCount ||
                   state.avgWaitingTime != last_.avgWaitingTime ||
                   state.avgTurnaroundTime != last_.avgTurnaroundTime ||
                   std::strcmp(state.algorithm, last_.algorithm) != 0 ||
                   std::strcmp(state.sessionId, last_.sessionId) != 0) {
            drawSummary(state);
        }

        digitalWrite(ACTIVE_LED_PIN, state.active ? HIGH : LOW);
        digitalWrite(DEADLOCK_LED_PIN, state.deadlocked ? HIGH : LOW);
        last_ = state;
        initialized_ = true;
    }

private:
    Adafruit_ILI9341 tft_;
    DashboardSnapshot last_{};
    bool initialized_ = false;
    uint32_t renderedTerminalRevision_ = 0;
    uint32_t lastTerminalDrawMs_ = 0;

    void drawTerminal(const DashboardSnapshot& state) {
        tft_.fillRect(0, 0, 320, 20, ILI9341_NAVY);
        tft_.setTextSize(1);
        tft_.setTextColor(ILI9341_CYAN, ILI9341_NAVY);
        tft_.setCursor(5, 6);
        tft_.print("JARVIS WATCH OS [TERM]");
        tft_.fillRect(0, 20, 320, 220, ILI9341_BLACK);
        tft_.setTextColor(ILI9341_GREEN, ILI9341_BLACK);
        for (size_t row = 0; row < state.terminalCount; ++row) {
            const size_t slot = (state.terminalHead + row) % DASHBOARD_TERMINAL_LINES;
            tft_.setCursor(2, static_cast<int16_t>(20 + row * 10));
            tft_.print(state.terminalLines[slot]);
        }
    }

    void drawFrame() {
        tft_.drawRect(0, 0, 320, 30, ILI9341_CYAN);
        tft_.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
        tft_.setTextSize(2);
        tft_.setCursor(8, 7);
        tft_.print("WATCH OS KERNEL");

        tft_.drawFastHLine(0, 60, 320, ILI9341_DARKGREY);
        tft_.drawFastHLine(0, 140, 320, ILI9341_DARKGREY);
        tft_.drawFastHLine(0, 216, 320, ILI9341_DARKGREY);

        tft_.setTextSize(1);
        tft_.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
        tft_.setCursor(8, 66);
        tft_.print("PROCESS / SCHEDULER METRICS");
        tft_.setCursor(8, 146);
        tft_.print("SCROLLING EVENT LOG");
        tft_.setCursor(8, 224);
        tft_.print("START: GPIO32   EMERGENCY EXIT: GPIO33");
    }

    void drawStatus(const DashboardSnapshot& state) {
        tft_.fillRect(1, 31, 318, 28, ILI9341_BLACK);
        tft_.setTextSize(2);
        tft_.setCursor(8, 36);
        if (state.emergencyStopped) {
            tft_.setTextColor(ILI9341_RED, ILI9341_BLACK);
            tft_.print("EMERGENCY STOPPED");
        } else {
            tft_.setTextColor(state.active ? ILI9341_GREEN : ILI9341_DARKGREY, ILI9341_BLACK);
            tft_.print(state.active ? "ACTIVE" : "IDLE");
            tft_.setCursor(155, 36);
            tft_.setTextColor(state.deadlocked ? ILI9341_RED :
                              (state.safeState ? ILI9341_GREEN : ILI9341_ORANGE), ILI9341_BLACK);
            tft_.print(state.deadlocked ? "DEADLOCK" : (state.safeState ? "SAFE" : "UNSAFE"));
        }
    }

    void drawMetrics(const DashboardSnapshot& state) {
        tft_.fillRect(1, 80, 318, 58, ILI9341_BLACK);
        tft_.setTextSize(1);
        tft_.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
        tft_.setCursor(8, 84);
        tft_.print("Algorithm: ");
        tft_.print(state.algorithm);

        char line[64];
        std::snprintf(line, sizeof(line), "Processes: %d   Completed: %d",
                      state.processCount, state.completedCount);
        tft_.setCursor(8, 101);
        tft_.print(line);

        std::snprintf(line, sizeof(line), "Avg waiting: %.2f   Avg turnaround: %.2f",
                      state.avgWaitingTime, state.avgTurnaroundTime);
        tft_.setCursor(8, 118);
        tft_.print(line);
    }

    void drawEventLog(const DashboardSnapshot& state) {
        tft_.fillRect(1, 160, 318, 54, ILI9341_BLACK);
        tft_.setTextSize(1);
        tft_.setTextColor(ILI9341_WHITE, ILI9341_BLACK);

        const size_t count = state.eventCount < EVENT_LINES ? state.eventCount : EVENT_LINES;
        const size_t first = state.eventCount > EVENT_LINES
            ? state.eventCount % EVENT_LINES
            : 0;
        for (size_t row = 0; row < count; ++row) {
            const size_t slot = (first + row) % EVENT_LINES;
            tft_.setCursor(8, static_cast<int16_t>(160 + row * 10));
            tft_.print(state.events[slot]);
        }
    }

    uint16_t ganttColor(const DashboardSnapshot& state, size_t segmentIndex) const {
        const DashboardGanttSegment& segment = state.ganttSegments[segmentIndex];
        if (segment.isIdle) return ILI9341_DARKGREY;
        if (segment.isContextSwitch) return ILI9341_ORANGE;
        static const uint16_t colors[] = {
            ILI9341_CYAN, ILI9341_GREEN, ILI9341_YELLOW,
            ILI9341_MAGENTA, ILI9341_ORANGE, ILI9341_BLUE
        };
        size_t processRank = 0;
        for (size_t i = 0; i < segmentIndex; ++i) {
            const auto& previous = state.ganttSegments[i];
            if (previous.isIdle || previous.isContextSwitch || previous.pid == segment.pid) {
                continue;
            }
            bool seenEarlier = false;
            for (size_t j = 0; j < i; ++j) {
                const auto& earlier = state.ganttSegments[j];
                if (!earlier.isIdle && !earlier.isContextSwitch &&
                    earlier.pid == previous.pid) {
                    seenEarlier = true;
                    break;
                }
            }
            if (!seenEarlier) {
                ++processRank;
            }
        }
        const size_t colorCount = sizeof(colors) / sizeof(colors[0]);
        return colors[processRank % colorCount];
    }

    void drawGanttChart(const DashboardSnapshot& state) {
        tft_.fillRect(1, 31, 318, 184, ILI9341_BLACK);
        tft_.setTextSize(1);
        tft_.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
        tft_.setCursor(8, 37);
        tft_.print("GANTT: ");
        tft_.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
        tft_.print(state.ganttAlgorithm);

        const size_t total = state.ganttSegmentCount;
        const size_t pageCount = total == 0 ? 1 :
            (total + GANTT_SEGMENTS_PER_PAGE - 1) / GANTT_SEGMENTS_PER_PAGE;
        const size_t page = state.ganttPage < pageCount ? state.ganttPage : pageCount - 1;
        const size_t first = page * GANTT_SEGMENTS_PER_PAGE;
        const size_t last = total < first + GANTT_SEGMENTS_PER_PAGE
            ? total
            : first + GANTT_SEGMENTS_PER_PAGE;

        if (total == 0) {
            tft_.setCursor(8, 80);
            tft_.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
            tft_.print("No Gantt segments available.");
        } else {
            const int64_t pageStart = state.ganttSegments[first].startTime;
            const int64_t pageEnd = state.ganttSegments[last - 1].endTime;
            const int64_t span = pageEnd > pageStart ? pageEnd - pageStart : 1;
            constexpr int16_t chartLeft = 8;
            constexpr int16_t chartWidth = 304;
            constexpr int16_t chartY = 76;
            constexpr int16_t chartHeight = 42;

            tft_.drawFastHLine(chartLeft, chartY + chartHeight + 4, chartWidth, ILI9341_LIGHTGREY);
            for (size_t i = first; i < last; ++i) {
                const DashboardGanttSegment& segment = state.ganttSegments[i];
                int16_t x1 = chartLeft + static_cast<int16_t>(
                    (segment.startTime - pageStart) * chartWidth / span);
                int16_t x2 = chartLeft + static_cast<int16_t>(
                    (segment.endTime - pageStart) * chartWidth / span);
                if (x2 <= x1) x2 = x1 + 1;
                if (x1 >= chartLeft + chartWidth) x1 = chartLeft + chartWidth - 1;
                if (x2 > chartLeft + chartWidth) x2 = chartLeft + chartWidth;
                const int16_t width = x2 - x1;

                const uint16_t color = ganttColor(state, i);
                tft_.fillRect(x1, chartY, width, chartHeight, color);
                tft_.drawRect(x1, chartY, width, chartHeight, ILI9341_WHITE);

                char label[16];
                if (segment.isIdle) {
                    std::snprintf(label, sizeof(label), "IDLE");
                } else if (segment.isContextSwitch) {
                    std::snprintf(label, sizeof(label), "CS");
                } else {
                    std::snprintf(label, sizeof(label), "P%ld", static_cast<long>(segment.pid));
                }
                if (width >= static_cast<int16_t>(std::strlen(label) * 6 + 4)) {
                    tft_.setTextColor(ILI9341_BLACK, color);
                    tft_.setCursor(x1 + 2, chartY + 17);
                    tft_.print(label);
                }
            }

            tft_.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
            char axis[32];
            std::snprintf(axis, sizeof(axis), "t=%lld", static_cast<long long>(pageStart));
            tft_.setCursor(chartLeft, 126);
            tft_.print(axis);
            std::snprintf(axis, sizeof(axis), "t=%lld", static_cast<long long>(pageEnd));
            tft_.setCursor(260, 126);
            tft_.print(axis);

            for (size_t i = first; i < last; ++i) {
                const DashboardGanttSegment& segment = state.ganttSegments[i];
                const size_t row = i - first;
                char timing[48];
                if (segment.isIdle) {
                    std::snprintf(timing, sizeof(timing), "IDLE: %lld -> %lld",
                                  static_cast<long long>(segment.startTime),
                                  static_cast<long long>(segment.endTime));
                } else if (segment.isContextSwitch) {
                    std::snprintf(timing, sizeof(timing), "CS: %lld -> %lld",
                                  static_cast<long long>(segment.startTime),
                                  static_cast<long long>(segment.endTime));
                } else {
                    std::snprintf(timing, sizeof(timing), "P%ld: %lld -> %lld",
                                  static_cast<long>(segment.pid),
                                  static_cast<long long>(segment.startTime),
                                  static_cast<long long>(segment.endTime));
                }
                tft_.setCursor(8, static_cast<int16_t>(145 + row * 15));
                tft_.setTextColor(ganttColor(state, i), ILI9341_BLACK);
                tft_.print(timing);
            }
        }

        tft_.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
        tft_.setCursor(8, 198);
        tft_.printf("Page %u/%u  START: next page / return",
                    static_cast<unsigned int>(page + 1),
                    static_cast<unsigned int>(pageCount));
    }

    void drawSummary(const DashboardSnapshot& state) {
        tft_.fillScreen(ILI9341_BLACK);
        tft_.drawRect(0, 0, 320, 26, ILI9341_CYAN);
        tft_.setTextSize(2);
        tft_.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
        tft_.setCursor(8, 6);
        tft_.print("KERNEL SUMMARY");
        tft_.setTextSize(1);

        tft_.setCursor(8, 40);
        tft_.setTextColor(state.emergencyStopped ? ILI9341_RED :
                          (state.active ? ILI9341_GREEN : ILI9341_YELLOW), ILI9341_BLACK);
        tft_.print(state.emergencyStopped ? "State: EMERGENCY STOPPED" :
                   (state.active ? "State: ACTIVE" : "State: WAITING / IDLE"));

        tft_.setTextColor(state.deadlocked ? ILI9341_RED :
                          (state.safeState ? ILI9341_GREEN : ILI9341_ORANGE), ILI9341_BLACK);
        tft_.setCursor(8, 58);
        tft_.print(state.deadlocked ? "Resources: DEADLOCK" :
                   (state.safeState ? "Resources: SAFE" : "Resources: UNSAFE"));

        tft_.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
        tft_.setCursor(8, 84);
        tft_.print("Session: ");
        tft_.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
        tft_.print(state.sessionId);

        char line[64];
        tft_.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
        tft_.setCursor(8, 112);
        tft_.print("Scheduler: ");
        tft_.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
        tft_.print(state.algorithm);

        std::snprintf(line, sizeof(line), "Processes: %d   Completed: %d",
                      state.processCount, state.completedCount);
        tft_.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
        tft_.setCursor(8, 136);
        tft_.print(line);
        std::snprintf(line, sizeof(line), "Avg waiting: %.2f", state.avgWaitingTime);
        tft_.setCursor(8, 154);
        tft_.print(line);
        std::snprintf(line, sizeof(line), "Avg turnaround: %.2f", state.avgTurnaroundTime);
        tft_.setCursor(8, 172);
        tft_.print(line);

        tft_.setTextColor(ILI9341_DARKGREY, ILI9341_BLACK);
        tft_.setCursor(8, 222);
        tft_.print("START cycles views   0 TERM  1 GANTT  2 SUMMARY");
    }
};

DashboardDisplay& display() {
    static DashboardDisplay instance;
    return instance;
}

} // namespace

TftDashboard::TftDashboard() = default;

TftDashboard& TftDashboard::instance() {
    static TftDashboard dashboard;
    return dashboard;
}

bool TftDashboard::begin() {
    pinMode(ACTIVE_LED_PIN, OUTPUT);
    pinMode(DEADLOCK_LED_PIN, OUTPUT);
    digitalWrite(ACTIVE_LED_PIN, LOW);
    digitalWrite(DEADLOCK_LED_PIN, LOW);

    if (serialMutex == nullptr) {
        serialMutex = xSemaphoreCreateMutex();
    }
    if (serialMutex == nullptr) {
        return false;
    }
    const bool ready = display().begin();
    service();
    return ready;
}

void TftDashboard::service() {
    display().service();
}

void appendTerminalText(const std::string& text) {
    if (text.empty()) return;

    portENTER_CRITICAL(&snapshotMux);
    const bool suppressed = terminalMenuSuppressed;
    if (suppressed) {
        ansiEscapeState = 0;
        portEXIT_CRITICAL(&snapshotMux);
        return;
    }
    portEXIT_CRITICAL(&snapshotMux);

    for (size_t offset = 0; offset < text.size(); offset += 64) {
        const size_t end = text.size() < offset + 64 ? text.size() : offset + 64;
        portENTER_CRITICAL(&snapshotMux);
        for (size_t i = offset; i < end; ++i) {
            const unsigned char character = static_cast<unsigned char>(text[i]);
            if (ansiEscapeState == 1) {
                ansiEscapeState = character == '[' ? 2 : 0;
                continue;
            }
            if (ansiEscapeState == 2) {
                if (character >= '@' && character <= '~') {
                    ansiEscapeState = 0;
                }
                continue;
            }
            if (character == 0x1b) {
                ansiEscapeState = 1;
                continue;
            }
            appendTerminalCharacterLocked(static_cast<char>(character));
        }
        ++sharedSnapshot.terminalRevision;
        portEXIT_CRITICAL(&snapshotMux);
    }
}

void setTerminalMenuSuppressed(bool suppressed) {
    portENTER_CRITICAL(&snapshotMux);
    terminalMenuSuppressed = suppressed;
    if (suppressed) {
        ansiEscapeState = 0;
    }
    portEXIT_CRITICAL(&snapshotMux);
}

void setDashboardActive(bool active) {
    portENTER_CRITICAL(&snapshotMux);
    sharedSnapshot.active = active;
    if (active) {
        sharedSnapshot.emergencyStopped = false;
        sharedSnapshot.viewMode = 0;
    }
    portEXIT_CRITICAL(&snapshotMux);
    publishEvent(active ? "RUN" : "RUN", active ? "Execution active" : "Execution idle");
}

void setDashboardSessionId(const std::string& sessionId) {
    portENTER_CRITICAL(&snapshotMux);
    std::snprintf(sharedSnapshot.sessionId, sizeof(sharedSnapshot.sessionId), "%.31s",
                  sessionId.c_str());
    portEXIT_CRITICAL(&snapshotMux);
}

void setDashboardWaiting(bool waiting) {
    portENTER_CRITICAL(&snapshotMux);
    if (waiting) {
        sharedSnapshot.active = false;
        sharedSnapshot.deadlocked = false;
        sharedSnapshot.safeState = true;
        sharedSnapshot.emergencyStopped = false;
        sharedSnapshot.processCount = 0;
        sharedSnapshot.completedCount = 0;
        sharedSnapshot.avgWaitingTime = 0.0;
        sharedSnapshot.avgTurnaroundTime = 0.0;
        std::snprintf(sharedSnapshot.algorithm, sizeof(sharedSnapshot.algorithm), "%s", "Idle");
        std::snprintf(sharedSnapshot.sessionId, sizeof(sharedSnapshot.sessionId), "%s",
                      "No active session");
        sharedSnapshot.viewMode = 2;
    }
    portEXIT_CRITICAL(&snapshotMux);
    if (waiting) {
        publishEvent("STATE", "Waiting for START button");
    }
}

void setDashboardEmergencyStopped(bool stopped) {
    portENTER_CRITICAL(&snapshotMux);
    sharedSnapshot.emergencyStopped = stopped;
    if (stopped) {
        sharedSnapshot.active = false;
        sharedSnapshot.deadlocked = false;
    }
    portEXIT_CRITICAL(&snapshotMux);
    if (stopped) {
        publishEvent("EMERGENCY", "Execution stopped by emergency button");
    }
}

void publishSchedulerMetrics(const scheduler::AggregateMetrics& metrics, int processCount) {
    portENTER_CRITICAL(&snapshotMux);
    sharedSnapshot.processCount = processCount;
    sharedSnapshot.completedCount = static_cast<int>(metrics.processResults.size());
    sharedSnapshot.avgWaitingTime = metrics.avgWaitingTime;
    sharedSnapshot.avgTurnaroundTime = metrics.avgTurnaroundTime;
    std::snprintf(sharedSnapshot.algorithm, sizeof(sharedSnapshot.algorithm), "%.23s",
                  metrics.algorithmName.c_str());
    portEXIT_CRITICAL(&snapshotMux);

    char message[120];
    std::snprintf(message, sizeof(message),
                  "%.23s: processes=%d completed=%d avg_wait=%.2f avg_turnaround=%.2f",
                  metrics.algorithmName.c_str(), processCount,
                  static_cast<int>(metrics.processResults.size()),
                  metrics.avgWaitingTime, metrics.avgTurnaroundTime);
    publishEvent("SCHED", message);
}

void publishGanttChart(const scheduler::AggregateMetrics& metrics) {
    portENTER_CRITICAL(&snapshotMux);
    const size_t count = metrics.ganttSegments.size() < MAX_DASHBOARD_GANTT_SEGMENTS
        ? metrics.ganttSegments.size()
        : MAX_DASHBOARD_GANTT_SEGMENTS;
    sharedSnapshot.ganttSegmentCount = static_cast<uint8_t>(count);
    std::snprintf(sharedSnapshot.ganttAlgorithm, sizeof(sharedSnapshot.ganttAlgorithm), "%.23s",
                  metrics.algorithmName.c_str());
    for (size_t i = 0; i < count; ++i) {
        const auto& segment = metrics.ganttSegments[i];
        sharedSnapshot.ganttSegments[i] = {
            segment.startTime,
            segment.endTime,
            segment.pid,
            segment.isIdle,
            segment.isContextSwitch
        };
    }
    sharedSnapshot.ganttPage = 0;
    ++sharedSnapshot.ganttRevision;
    portEXIT_CRITICAL(&snapshotMux);

    char message[64];
    std::snprintf(message, sizeof(message), "%.23s Gantt chart: %u segments",
                  metrics.algorithmName.c_str(), static_cast<unsigned int>(count));
    publishEvent("GANTT", message);
}

void cycleDashboardView() {
    portENTER_CRITICAL(&snapshotMux);
    if (sharedSnapshot.viewMode == 1) {
        const size_t pageCount = sharedSnapshot.ganttSegmentCount == 0 ? 1 :
            (sharedSnapshot.ganttSegmentCount + GANTT_SEGMENTS_PER_PAGE - 1) /
                GANTT_SEGMENTS_PER_PAGE;
        if (sharedSnapshot.ganttPage + 1 < pageCount) {
            ++sharedSnapshot.ganttPage;
        } else {
            sharedSnapshot.viewMode = 2;
        }
    } else {
        sharedSnapshot.viewMode = static_cast<uint8_t>((sharedSnapshot.viewMode + 1) % 3);
        if (sharedSnapshot.viewMode == 1) sharedSnapshot.ganttPage = 0;
    }
    portEXIT_CRITICAL(&snapshotMux);
}

void publishDeadlockResults(const deadlock::DeadlockDetectionResult& detection,
                            const deadlock::BankersResult& bankers) {
    portENTER_CRITICAL(&snapshotMux);
    sharedSnapshot.deadlocked = detection.isDeadlocked;
    sharedSnapshot.safeState = bankers.isSafe;
    portEXIT_CRITICAL(&snapshotMux);

    char message[96];
    std::snprintf(message, sizeof(message), "deadlocked=%s bankers_safe=%s",
                  detection.isDeadlocked ? "true" : "false",
                  bankers.isSafe ? "true" : "false");
    publishEvent("DEADLOCK", message);
}

void logSerial(const char* tag, const char* message) {
    const uint32_t seconds = millis() / 1000;
    char line[160];
    std::snprintf(line, sizeof(line), "[%02lu:%02lu:%02lu] %.16s: %.120s",
                  static_cast<unsigned long>((seconds / 3600) % 24),
                  static_cast<unsigned long>((seconds / 60) % 60),
                  static_cast<unsigned long>(seconds % 60), tag, message);

    if (serialMutex != nullptr) {
        xSemaphoreTake(serialMutex, portMAX_DELAY);
    }
    Serial.println(line);
    if (serialMutex != nullptr) {
        xSemaphoreGive(serialMutex);
    }
    appendTerminalText(std::string(line) + "\n");
}

} // namespace hardware
} // namespace watchos
