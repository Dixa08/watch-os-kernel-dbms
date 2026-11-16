#include "watchos/database/run_repository.hpp"
#include "watchos/core/id_generator.hpp"
#include "watchos/security/access_policy.hpp"
#include <stdexcept>

namespace watchos {
namespace database {

RunRepository::RunRepository(DbConnection& db) : db_(db) {}

void RunRepository::recordSimulationEvent(
    const std::string& runId,
    core::TimeUnits timestamp,
    const std::string& eventType,
    core::ProcessId entityId,
    const std::string& details
) {
    auto stmt = db_.prepare(
        "INSERT INTO simulation_events (run_id, timestamp, event_type, entity_id, details) "
        "VALUES (?, ?, ?, ?, ?);"
    );
    stmt.bindText(1, runId);
    stmt.bindInt64(2, timestamp);
    stmt.bindText(3, eventType);
    stmt.bindInt(4, entityId);
    stmt.bindText(5, details);
    stmt.step();
}

void RunRepository::recordSchedulingResult(const SchedulingResultRecord& record) {
    auto stmt = db_.prepare(
        "INSERT INTO scheduling_results "
        "(run_id, pid, arrival_time, cpu_burst, priority, completion_time, "
        "turnaround_time, waiting_time, response_time) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);"
    );
    stmt.bindText(1, record.runId);
    stmt.bindInt(2, record.pid);
    stmt.bindInt64(3, record.arrivalTime);
    stmt.bindInt64(4, record.cpuBurst);
    stmt.bindInt(5, record.priority);
    stmt.bindInt64(6, record.completionTime);
    stmt.bindInt64(7, record.turnaroundTime);
    stmt.bindInt64(8, record.waitingTime);
    stmt.bindInt64(9, record.responseTime);
    stmt.step();
}

void RunRepository::recordDeadlockResult(
    const std::string& runId,
    bool isDeadlocked,
    bool isSafe,
    const std::string& safeSequence,
    const std::string& deadlockedProcesses,
    int trials,
    int deadlockedTrials,
    double frequency
) {
    auto stmt = db_.prepare(
        "INSERT INTO deadlock_results "
        "(run_id, is_deadlocked, is_safe, safe_sequence, deadlocked_processes, "
        "frequency_trials, frequency_deadlocks, empirical_frequency) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?);"
    );
    stmt.bindText(1, runId);
    stmt.bindInt(2, isDeadlocked ? 1 : 0);
    stmt.bindInt(3, isSafe ? 1 : 0);
    stmt.bindText(4, safeSequence);
    stmt.bindText(5, deadlockedProcesses);
    stmt.bindInt(6, trials);
    stmt.bindInt(7, deadlockedTrials);
    stmt.bindDouble(8, frequency);
    stmt.step();
}

std::vector<EventRecord> RunRepository::getEventsForRun(const std::string& runId) {
    std::vector<EventRecord> records;
    auto stmt = db_.prepare(
        "SELECT id, run_id, timestamp, event_type, entity_id, details "
        "FROM simulation_events WHERE run_id = ? ORDER BY timestamp, id;"
    );
    stmt.bindText(1, runId);
    while (stmt.step()) {
        EventRecord record;
        record.id = stmt.getInt64(0);
        record.runId = stmt.getText(1);
        record.timestamp = stmt.getInt64(2);
        record.eventType = stmt.getText(3);
        record.entityId = stmt.getInt(4);
        record.details = stmt.getText(5);
        records.push_back(record);
    }
    return records;
}

std::vector<SchedulingResultRecord> RunRepository::getSchedulingResultsForRun(
    const std::string& runId
) {
    std::vector<SchedulingResultRecord> records;
    auto stmt = db_.prepare(
        "SELECT run_id, pid, arrival_time, cpu_burst, priority, completion_time, "
        "turnaround_time, waiting_time, response_time FROM scheduling_results "
        "WHERE run_id = ? ORDER BY pid;"
    );
    stmt.bindText(1, runId);
    while (stmt.step()) {
        SchedulingResultRecord record;
        record.runId = stmt.getText(0);
        record.pid = stmt.getInt(1);
        record.arrivalTime = stmt.getInt64(2);
        record.cpuBurst = stmt.getInt64(3);
        record.priority = stmt.getInt(4);
        record.completionTime = stmt.getInt64(5);
        record.turnaroundTime = stmt.getInt64(6);
        record.waitingTime = stmt.getInt64(7);
        record.responseTime = stmt.getInt64(8);
        records.push_back(record);
    }
    return records;
}

void RunRepository::recordProcessStateTransition(const StateHistoryRecord& record) {
    auto stmt = db_.prepare(
        "INSERT INTO process_state_history "
        "(run_id, pid, timestamp, from_state, to_state, reason) "
        "VALUES (?, ?, ?, ?, ?, ?);"
    );
    stmt.bindText(1, record.runId);
    stmt.bindInt(2, record.pid);
    stmt.bindInt64(3, record.timestamp);
    stmt.bindText(4, record.fromState);
    stmt.bindText(5, record.toState);
    stmt.bindText(6, record.reason);
    stmt.step();
}

std::vector<StateHistoryRecord> RunRepository::getProcessStateHistory(const std::string& runId) {
    std::vector<StateHistoryRecord> records;
    auto stmt = db_.prepare(
        "SELECT run_id, pid, timestamp, from_state, to_state, reason "
        "FROM process_state_history WHERE run_id = ? ORDER BY timestamp, id;"
    );
    stmt.bindText(1, runId);
    while (stmt.step()) {
        StateHistoryRecord record;
        record.runId = stmt.getText(0);
        record.pid = stmt.getInt(1);
        record.timestamp = stmt.getInt64(2);
        record.fromState = stmt.getText(3);
        record.toState = stmt.getText(4);
        record.reason = stmt.getText(5);
        records.push_back(record);
    }
    return records;
}

void RunRepository::saveReportRecord(
    const std::string& reportId,
    const std::string& sessionId,
    const std::string& runId,
    const std::string& format,
    const std::string& path
) {
    auto stmt = db_.prepare(
        "INSERT INTO reports "
        "(report_id, session_id, run_id, report_format, export_path, generated_at) "
        "VALUES (?, ?, ?, ?, ?, ?);"
    );
    stmt.bindText(1, reportId);
    stmt.bindText(2, sessionId);
    stmt.bindText(3, runId);
    stmt.bindText(4, format);
    stmt.bindText(5, path);
    stmt.bindText(6, core::IdGenerator::getCurrentTimestampIso());
    stmt.step();
}

void RunRepository::saveAccessPolicy(
    const std::string& sessionId,
    const std::string& userOrRole,
    const std::string& resourceName,
    const std::string& operation,
    const std::string& policy
) {
    const std::string normalizedOperation = security::normalizeAccessOperation(operation);
    if (!security::isSupportedAccessOperation(normalizedOperation)) {
        throw std::invalid_argument("Access operation must be READ, WRITE, or EXECUTE");
    }
    const std::string normalizedPolicy = security::normalizeAccessPolicy(policy);

    auto stmt = db_.prepare(
        "INSERT INTO access_policies "
        "(session_id, user_or_role, resource_name, operation, policy) "
        "VALUES (?, ?, ?, ?, ?) "
        "ON CONFLICT(session_id, user_or_role, resource_name, operation) "
        "DO UPDATE SET policy = excluded.policy;"
    );
    stmt.bindText(1, sessionId);
    stmt.bindText(2, userOrRole);
    stmt.bindText(3, resourceName);
    stmt.bindText(4, normalizedOperation);
    stmt.bindText(5, normalizedPolicy);
    stmt.step();
}

} // namespace database
} // namespace watchos
