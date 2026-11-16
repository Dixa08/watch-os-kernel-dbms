#include "watchos/database/session_repository.hpp"
#include "watchos/core/id_generator.hpp"

namespace watchos {
namespace database {

SessionRepository::SessionRepository(DbConnection& db) : db_(db) {}

void SessionRepository::createSession(const session::Session& session) {
    auto stmt = db_.prepare(
        "INSERT INTO sessions (session_id, created_at, status, active_config_version, description) "
        "VALUES (?, ?, ?, ?, ?);"
    );
    stmt.bindText(1, session.getSessionId());
    stmt.bindText(2, session.getCreatedAt());
    stmt.bindText(3, core::sessionStatusToString(session.getStatus()));
    stmt.bindInt(4, session.getActiveVersion());
    stmt.bindText(5, session.getDescription());
    stmt.step();
}

void SessionRepository::saveWorkload(const session::Session& session, bool finalize) {
    TransactionGuard tx(db_);

    const int32_t version = session.getActiveVersion();
    const std::string& sessionId = session.getSessionId();
    auto stmt = db_.prepare(
        "UPDATE sessions SET active_config_version = ?, status = ?, updated_at = ? "
        "WHERE session_id = ?;"
    );
    stmt.bindInt(1, version);
    stmt.bindText(2, core::sessionStatusToString(
        finalize ? core::SessionStatus::FINALIZED : session.getStatus()));
    stmt.bindText(3, core::IdGenerator::getCurrentTimestampIso());
    stmt.bindText(4, sessionId);
    stmt.step();

    saveProcesses(sessionId, version, session.getProcesses());
    tx.commit();
}

void SessionRepository::saveProcesses(
    const std::string& sessionId,
    int32_t version,
    const std::vector<process::Process>& processes
) {
    auto del = db_.prepare(
        "DELETE FROM processes WHERE session_id = ? AND config_version = ?;"
    );
    del.bindText(1, sessionId);
    del.bindInt(2, version);
    del.step();

    auto insert = db_.prepare(
        "INSERT INTO processes "
        "(session_id, config_version, pid, arrival_time, cpu_burst, priority, "
        "process_name, description, memory_demand) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);"
    );
    for (const auto& proc : processes) {
        insert.reset();
        insert.bindText(1, sessionId);
        insert.bindInt(2, version);
        insert.bindInt(3, proc.getPid());
        insert.bindInt64(4, proc.getArrivalTime());
        insert.bindInt64(5, proc.getCpuBurst());
        insert.bindInt(6, proc.getPriority());
        insert.bindText(7, proc.getName());
        insert.bindText(8, proc.getDescription());
        if (proc.getMemoryConfig().isConfigured) {
            insert.bindInt64(9, proc.getMemoryConfig().demandUnits);
        } else {
            insert.bindNull(9);
        }
        insert.step();
    }
}

bool SessionRepository::loadSession(const std::string& sessionId, session::Session& outSession) {
    auto stmt = db_.prepare(
        "SELECT session_id, created_at, status, description, active_config_version "
        "FROM sessions WHERE session_id = ?;"
    );
    stmt.bindText(1, sessionId);
    if (!stmt.step()) {
        return false;
    }

    outSession.setSessionId(stmt.getText(0));
    outSession.setCreatedAt(stmt.getText(1));
    outSession.setStatus(core::stringToSessionStatus(stmt.getText(2)));
    outSession.setDescription(stmt.getText(3));
    outSession.setActiveVersion(stmt.getInt(4));

    std::vector<process::Process> processes;
    loadProcesses(sessionId, outSession.getActiveVersion(), processes);
    outSession.setProcesses(processes);
    return true;
}

void SessionRepository::loadProcesses(
    const std::string& sessionId,
    int32_t version,
    std::vector<process::Process>& outProcesses
) {
    outProcesses.clear();
    auto stmt = db_.prepare(
        "SELECT pid, arrival_time, cpu_burst, priority, process_name, description, memory_demand "
        "FROM processes WHERE session_id = ? AND config_version = ? ORDER BY pid ASC;"
    );
    stmt.bindText(1, sessionId);
    stmt.bindInt(2, version);

    while (stmt.step()) {
        process::Process proc(
            stmt.getInt(0),
            stmt.getInt64(1),
            stmt.getInt64(2),
            stmt.getInt(3)
        );
        proc.setName(stmt.getText(4));
        proc.setDescription(stmt.getText(5));
        if (!stmt.isNull(6)) {
            process::MemoryConfig memory;
            memory.isConfigured = true;
            memory.demandUnits = stmt.getInt64(6);
            proc.setMemoryConfig(memory);
        }
        outProcesses.push_back(proc);
    }
}

std::vector<session::SessionMetadata> SessionRepository::listSessions() {
    std::vector<session::SessionMetadata> list;
    auto stmt = db_.prepare(
        "SELECT s.session_id, s.created_at, s.status, s.description, "
        "s.active_config_version, "
        "COALESCE((SELECT COUNT(*) FROM processes p "
        "WHERE p.session_id = s.session_id "
        "AND p.config_version = s.active_config_version), 0) "
        "FROM sessions s ORDER BY COALESCE(s.updated_at, s.created_at) DESC;"
    );

    while (stmt.step()) {
        session::SessionMetadata meta;
        meta.sessionId = stmt.getText(0);
        meta.createdAt = stmt.getText(1);
        meta.status = core::stringToSessionStatus(stmt.getText(2));
        meta.description = stmt.getText(3);
        meta.activeVersion = stmt.getInt(4);
        meta.processCount = static_cast<size_t>(stmt.getInt64(5));
        list.push_back(meta);
    }
    return list;
}

bool SessionRepository::sessionExists(const std::string& sessionId) {
    auto stmt = db_.prepare("SELECT 1 FROM sessions WHERE session_id = ?;");
    stmt.bindText(1, sessionId);
    return stmt.step();
}

void SessionRepository::updateSessionStatus(const std::string& sessionId, core::SessionStatus status) {
    auto stmt = db_.prepare("UPDATE sessions SET status = ? WHERE session_id = ?;");
    stmt.bindText(1, core::sessionStatusToString(status));
    stmt.bindText(2, sessionId);
    stmt.step();
}

bool SessionRepository::deleteSession(const std::string& sessionId) {
    auto stmt = db_.prepare("DELETE FROM sessions WHERE session_id = ?;");
    stmt.bindText(1, sessionId);
    stmt.step();
    return true;
}

} // namespace database
} // namespace watchos
