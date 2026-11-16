#include "watchos/database/schema.hpp"

namespace watchos {
namespace database {

namespace {

bool hasProcessColumn(DbConnection& db, const std::string& columnName) {
    auto columns = db.prepare("PRAGMA table_info(processes);");
    while (columns.step()) {
        if (columns.getText(1) == columnName) {
            return true;
        }
    }
    return false;
}

} // namespace

void Schema::initializeSchema(DbConnection& db) {
    const std::string ddl = R"SQL(
    CREATE TABLE IF NOT EXISTS sessions (
        session_id TEXT PRIMARY KEY,
        title TEXT,
        created_at TEXT NOT NULL,
        updated_at TEXT,
        last_opened_at TEXT,
        ended_at TEXT,
        status TEXT NOT NULL DEFAULT 'DRAFT',
        active_config_version INTEGER NOT NULL DEFAULT 1,
        description TEXT,
        notes TEXT
    );

    CREATE TABLE IF NOT EXISTS processes (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        session_id TEXT NOT NULL,
        config_version INTEGER NOT NULL DEFAULT 1,
        pid INTEGER NOT NULL,
        arrival_time INTEGER NOT NULL CHECK (arrival_time >= 0),
        cpu_burst INTEGER NOT NULL CHECK (cpu_burst > 0),
        priority INTEGER NOT NULL,
        process_name TEXT NOT NULL DEFAULT '',
        description TEXT NOT NULL DEFAULT '',
        memory_demand INTEGER DEFAULT NULL CHECK (memory_demand IS NULL OR memory_demand > 0),
        FOREIGN KEY (session_id) REFERENCES sessions(session_id) ON DELETE CASCADE,
        UNIQUE(session_id, config_version, pid)
    );

    CREATE TABLE IF NOT EXISTS simulation_events (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        run_id TEXT NOT NULL,
        timestamp INTEGER NOT NULL CHECK (timestamp >= 0),
        event_type TEXT NOT NULL,
        entity_id INTEGER,
        details TEXT
    );

    CREATE TABLE IF NOT EXISTS process_state_history (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        run_id TEXT NOT NULL,
        pid INTEGER NOT NULL,
        timestamp INTEGER NOT NULL CHECK (timestamp >= 0),
        from_state TEXT NOT NULL,
        to_state TEXT NOT NULL,
        reason TEXT
    );

    CREATE TABLE IF NOT EXISTS reports (
        report_id TEXT PRIMARY KEY,
        session_id TEXT NOT NULL,
        run_id TEXT NOT NULL,
        report_format TEXT NOT NULL,
        export_path TEXT NOT NULL,
        generated_at TEXT NOT NULL,
        FOREIGN KEY (session_id) REFERENCES sessions(session_id) ON DELETE CASCADE
    );

    CREATE TABLE IF NOT EXISTS deadlock_results (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        run_id TEXT NOT NULL,
        is_deadlocked INTEGER NOT NULL DEFAULT 0,
        is_safe INTEGER NOT NULL DEFAULT 1,
        safe_sequence TEXT,
        deadlocked_processes TEXT,
        frequency_trials INTEGER DEFAULT 0 CHECK (frequency_trials >= 0),
        frequency_deadlocks INTEGER DEFAULT 0 CHECK (frequency_deadlocks >= 0),
        empirical_frequency REAL DEFAULT 0.0 CHECK (empirical_frequency >= 0.0 AND empirical_frequency <= 1.0)
    );

    CREATE TABLE IF NOT EXISTS access_policies (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        session_id TEXT NOT NULL,
        user_or_role TEXT NOT NULL,
        resource_name TEXT NOT NULL,
        operation TEXT NOT NULL CHECK (operation IN ('READ', 'WRITE', 'EXECUTE')),
        policy TEXT NOT NULL DEFAULT 'ALLOW' CHECK (policy IN ('ALLOW', 'DENY')),
        FOREIGN KEY (session_id) REFERENCES sessions(session_id) ON DELETE CASCADE
    );

    CREATE TABLE IF NOT EXISTS scheduling_results (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        run_id TEXT NOT NULL,
        pid INTEGER NOT NULL,
        arrival_time INTEGER NOT NULL CHECK (arrival_time >= 0),
        cpu_burst INTEGER NOT NULL CHECK (cpu_burst > 0),
        priority INTEGER NOT NULL,
        completion_time INTEGER NOT NULL CHECK (completion_time >= arrival_time),
        turnaround_time INTEGER NOT NULL CHECK (turnaround_time >= cpu_burst),
        waiting_time INTEGER NOT NULL CHECK (waiting_time >= 0),
        response_time INTEGER NOT NULL CHECK (response_time >= 0)
    );

    CREATE INDEX IF NOT EXISTS idx_processes_lookup ON processes(session_id, config_version);
    CREATE INDEX IF NOT EXISTS idx_events_run_time ON simulation_events(run_id, timestamp);
    CREATE INDEX IF NOT EXISTS idx_state_history_run ON process_state_history(run_id, timestamp);
    CREATE INDEX IF NOT EXISTS idx_reports_session ON reports(session_id);
    CREATE INDEX IF NOT EXISTS idx_deadlock_results_run ON deadlock_results(run_id);
    CREATE INDEX IF NOT EXISTS idx_scheduling_results_run ON scheduling_results(run_id);
    )SQL";

    db.execute(ddl);

    if (!hasProcessColumn(db, "process_name")) {
        db.execute("ALTER TABLE processes ADD COLUMN process_name TEXT NOT NULL DEFAULT '';");
    }
    if (!hasProcessColumn(db, "description")) {
        db.execute("ALTER TABLE processes ADD COLUMN description TEXT NOT NULL DEFAULT '';");
    }

    db.execute(
        "UPDATE access_policies SET "
        "operation = CASE UPPER(TRIM(operation)) "
        "WHEN 'EXEC' THEN 'EXECUTE' ELSE UPPER(TRIM(operation)) END, "
        "policy = UPPER(TRIM(policy));"
    );
    db.execute(
        "DELETE FROM access_policies WHERE id NOT IN ("
        "SELECT MAX(id) FROM access_policies "
        "GROUP BY session_id, user_or_role, resource_name, operation);"
    );
    db.execute(
        "CREATE UNIQUE INDEX IF NOT EXISTS idx_access_policies_rule "
        "ON access_policies(session_id, user_or_role, resource_name, operation);"
    );
}

} // namespace database
} // namespace watchos
