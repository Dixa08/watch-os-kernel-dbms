#ifndef WATCHOS_DATABASE_RUN_REPOSITORY_HPP
#define WATCHOS_DATABASE_RUN_REPOSITORY_HPP

#include "watchos/database/db_connection.hpp"
#include "watchos/core/types.hpp"
#include <string>
#include <vector>

namespace watchos {
namespace database {

struct EventRecord {
    int64_t id = 0;
    std::string runId;
    core::TimeUnits timestamp = 0;
    std::string eventType;
    core::ProcessId entityId = 0;
    std::string details;
};

struct SchedulingResultRecord {
    std::string runId;
    core::ProcessId pid = 0;
    core::TimeUnits arrivalTime = 0;
    core::TimeUnits cpuBurst = 0;
    core::Priority priority = 0;
    core::TimeUnits completionTime = 0;
    core::TimeUnits turnaroundTime = 0;
    core::TimeUnits waitingTime = 0;
    core::TimeUnits responseTime = 0;
};

struct StateHistoryRecord {
    std::string runId;
    core::ProcessId pid = 0;
    core::TimeUnits timestamp = 0;
    std::string fromState;
    std::string toState;
    std::string reason;
};

class RunRepository {
public:
    explicit RunRepository(DbConnection& db);

    void recordSimulationEvent(
        const std::string& runId,
        core::TimeUnits timestamp,
        const std::string& eventType,
        core::ProcessId entityId,
        const std::string& details
    );
    void recordSchedulingResult(const SchedulingResultRecord& record);
    void recordDeadlockResult(
        const std::string& runId,
        bool isDeadlocked,
        bool isSafe,
        const std::string& safeSequence,
        const std::string& deadlockedProcesses,
        int trials,
        int deadlockedTrials,
        double frequency
    );
    void recordProcessStateTransition(const StateHistoryRecord& record);

    std::vector<EventRecord> getEventsForRun(const std::string& runId);
    std::vector<SchedulingResultRecord> getSchedulingResultsForRun(const std::string& runId);
    std::vector<StateHistoryRecord> getProcessStateHistory(const std::string& runId);

    void saveReportRecord(
        const std::string& reportId,
        const std::string& sessionId,
        const std::string& runId,
        const std::string& format,
        const std::string& path
    );
    void saveAccessPolicy(
        const std::string& sessionId,
        const std::string& userOrRole,
        const std::string& resourceName,
        const std::string& operation,
        const std::string& policy
    );

private:
    DbConnection& db_;
};

} // namespace database
} // namespace watchos

#endif // WATCHOS_DATABASE_RUN_REPOSITORY_HPP
