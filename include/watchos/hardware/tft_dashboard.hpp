#ifndef WATCHOS_HARDWARE_TFT_DASHBOARD_HPP
#define WATCHOS_HARDWARE_TFT_DASHBOARD_HPP

#include "watchos/deadlock/bankers_algorithm.hpp"
#include "watchos/deadlock/deadlock_detector.hpp"
#include "watchos/scheduler/scheduling_metrics.hpp"
#include <cstddef>
#include <cstdint>
#include <string>

namespace watchos {
namespace hardware {

constexpr size_t MAX_DASHBOARD_GANTT_SEGMENTS = 64;
constexpr size_t DASHBOARD_TERMINAL_LINES = 22;
constexpr size_t DASHBOARD_TERMINAL_CHARS = 50;

struct DashboardGanttSegment {
    int64_t startTime = 0;
    int64_t endTime = 0;
    int32_t pid = 0;
    bool isIdle = false;
    bool isContextSwitch = false;
};

struct DashboardSnapshot {
    bool active = false;
    bool deadlocked = false;
    bool safeState = true;
    bool emergencyStopped = false;
    int processCount = 0;
    int completedCount = 0;
    double avgWaitingTime = 0.0;
    double avgTurnaroundTime = 0.0;
    char algorithm[24] = "Idle";
    char sessionId[32] = "No active session";
    char ganttAlgorithm[24] = "No schedule";
    DashboardGanttSegment ganttSegments[MAX_DASHBOARD_GANTT_SEGMENTS] = {};
    uint8_t ganttSegmentCount = 0;
    uint8_t ganttPage = 0;
    uint32_t ganttRevision = 0;
    uint8_t viewMode = 2;
    char terminalLines[DASHBOARD_TERMINAL_LINES][DASHBOARD_TERMINAL_CHARS + 1] = {};
    uint8_t terminalHead = 0;
    uint8_t terminalCount = 0;
    uint8_t terminalLineLength = 0;
    uint32_t terminalRevision = 0;
    char events[5][64] = {};
    uint32_t eventCount = 0;
};

class TftDashboard {
public:
    TftDashboard();

    static TftDashboard& instance();
    bool begin();
    void service();
};

void appendTerminalText(const std::string& text);
void setTerminalMenuSuppressed(bool suppressed);
void setDashboardActive(bool active);
void setDashboardWaiting(bool waiting);
void setDashboardEmergencyStopped(bool stopped);
void setDashboardSessionId(const std::string& sessionId);
void publishSchedulerMetrics(const scheduler::AggregateMetrics& metrics, int processCount);
void publishGanttChart(const scheduler::AggregateMetrics& metrics);
void cycleDashboardView();
void publishDeadlockResults(
    const deadlock::DeadlockDetectionResult& detection,
    const deadlock::BankersResult& bankers
);
void logSerial(const char* tag, const char* message);

} // namespace hardware
} // namespace watchos

#endif // WATCHOS_HARDWARE_TFT_DASHBOARD_HPP
