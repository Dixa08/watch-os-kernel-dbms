#include "watchos/database/db_connection.hpp"
#include "watchos/database/schema.hpp"
#include "watchos/database/session_repository.hpp"
#include "watchos/database/run_repository.hpp"
#include "watchos/core/id_generator.hpp"
#include <cassert>
#include <iostream>
#include <set>
#include <string>
#include <vector>

using namespace watchos;

void testOnlyEightTablesExist() {
    std::cout << "[TEST] Database Schema Audit: Verifying the supported tables...\n";
    database::DbConnection db(":memory:");
    db.open();
    database::Schema::initializeSchema(db);

    const std::vector<std::string> expectedTables = {
        "sessions",
        "processes",
        "simulation_events",
        "process_state_history",
        "reports",
        "deadlock_results",
        "access_policies",
        "scheduling_results",
    };

    auto stmt = db.prepare("SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%';");
    std::set<std::string> actualTables;
    while (stmt.step()) {
        actualTables.insert(stmt.getText(0));
    }

    assert(actualTables.size() == expectedTables.size());
    for (const auto& table : expectedTables) {
        assert(actualTables.find(table) != actualTables.end());
    }
    auto sessionColumns = db.prepare("PRAGMA table_info(sessions);");
    while (sessionColumns.step()) {
        assert(sessionColumns.getText(1) != "metadata");
    }
    std::cout << "  -> Exactly eight tables exist and sessions has no metadata column.\n";
}

void testPragmaIntegrityAndForeignKeys() {
    std::cout << "[TEST] Database Schema Audit: PRAGMA integrity_check and foreign_key_check...\n";
    database::DbConnection db(":memory:");
    db.open();
    database::Schema::initializeSchema(db);

    // PRAGMA foreign_keys is ON
    auto fkStmt = db.prepare("PRAGMA foreign_keys;");
    assert(fkStmt.step());
    int fkEnabled = fkStmt.getInt(0);
    assert(fkEnabled == 1);
    std::cout << "  -> PRAGMA foreign_keys = 1 (Enforced).\n";

    // PRAGMA integrity_check
    auto icStmt = db.prepare("PRAGMA integrity_check;");
    assert(icStmt.step());
    std::string icResult = icStmt.getText(0);
    assert(icResult == "ok");
    std::cout << "  -> PRAGMA integrity_check = ok.\n";

    // PRAGMA foreign_key_check
    auto fkcStmt = db.prepare("PRAGMA foreign_key_check;");
    int fkViolations = 0;
    while (fkcStmt.step()) {
        fkViolations++;
    }
    assert(fkViolations == 0);
    std::cout << "  -> PRAGMA foreign_key_check = 0 violations.\n";
}

void testForeignKeyRejection() {
    std::cout << "[TEST] Database Schema Audit: Foreign Key Rejection on Invalid Parent...\n";
    database::DbConnection db(":memory:");
    db.open();
    database::Schema::initializeSchema(db);

    bool caught = false;
    try {
        // Try inserting a process with a non-existent session_id
        auto stmt = db.prepare(
            "INSERT INTO processes (session_id, config_version, pid, arrival_time, cpu_burst, priority) "
            "VALUES ('NON-EXISTENT-SESSION', 1, 101, 0, 5, 1);"
        );
        stmt.step();
    } catch (const std::exception& ex) {
        caught = true;
    }
    assert(caught);
    std::cout << "  -> Foreign key constraint successfully rejected invalid insertion.\n";
}

void testCascadeDelete() {
    std::cout << "[TEST] Database Schema Audit: Cascade Delete Integrity...\n";
    database::DbConnection db(":memory:");
    db.open();
    database::Schema::initializeSchema(db);

    database::SessionRepository sessionRepo(db);
    database::RunRepository runRepo(db);

    session::Session sess("WOS-20261016-0001", "Test Cascade");
    sessionRepo.createSession(sess);

    std::vector<process::Process> procs = {
        process::Process(101, 0, 5, 1),
        process::Process(102, 2, 4, 2)
    };
    sess.setProcesses(procs);
    sessionRepo.saveWorkload(sess, true);

    runRepo.recordProcessStateTransition({"RUN-001", 101, 0, "NEW", "READY", "init"});

    // Verify records exist before delete
    auto checkProc = db.prepare("SELECT COUNT(*) FROM processes WHERE session_id = 'WOS-20261016-0001';");
    assert(checkProc.step());
    assert(checkProc.getInt(0) == 2);

    // Delete session
    bool deleted = sessionRepo.deleteSession("WOS-20261016-0001");
    assert(deleted);

    // Verify cascade removed all associated entities
    auto postProc = db.prepare("SELECT COUNT(*) FROM processes WHERE session_id = 'WOS-20261016-0001';");
    assert(postProc.step());
    assert(postProc.getInt(0) == 0);

    std::cout << "  -> Session deletion cascades to its process records.\n";
}

void testCheckConstraints() {
    std::cout << "[TEST] Database Schema Audit: CHECK Constraints for Domain Validation...\n";
    database::DbConnection db(":memory:");
    db.open();
    database::Schema::initializeSchema(db);

    database::SessionRepository sessionRepo(db);
    session::Session sess("WOS-20261016-0002", "Test Constraints");
    sessionRepo.createSession(sess);

    // 1. Negative arrival time must fail
    bool negativeArrivalFailed = false;
    try {
        auto stmt = db.prepare("INSERT INTO processes (session_id, config_version, pid, arrival_time, cpu_burst, priority) VALUES ('WOS-20261016-0002', 1, 101, -5, 5, 1);");
        stmt.step();
    } catch (...) {
        negativeArrivalFailed = true;
    }
    assert(negativeArrivalFailed);

    // 2. Zero or negative CPU burst must fail
    bool zeroBurstFailed = false;
    try {
        auto stmt = db.prepare("INSERT INTO processes (session_id, config_version, pid, arrival_time, cpu_burst, priority) VALUES ('WOS-20261016-0002', 1, 102, 0, 0, 1);");
        stmt.step();
    } catch (...) {
        zeroBurstFailed = true;
    }
    assert(zeroBurstFailed);

    // Deadlock frequency outside the [0, 1] range must fail.
    database::RunRepository runRepo(db);
    bool invalidFrequencyFailed = false;
    try {
        auto stmt = db.prepare(
            "INSERT INTO deadlock_results "
            "(run_id, empirical_frequency) VALUES ('RUN-002', 1.5);"
        );
        stmt.step();
    } catch (...) {
        invalidFrequencyFailed = true;
    }
    assert(invalidFrequencyFailed);

    std::cout << "  -> CHECK constraints enforce non-negative arrival, positive burst, and valid deadlock frequency.\n";
}

void testTransactionRollback() {
    std::cout << "[TEST] Database Schema Audit: Transaction Rollback on Failure...\n";
    database::DbConnection db(":memory:");
    db.open();
    database::Schema::initializeSchema(db);

    database::SessionRepository sessionRepo(db);
    session::Session sess("WOS-20261016-0003", "Test Transaction");
    sessionRepo.createSession(sess);

    // Run transaction where step 1 succeeds and step 2 fails
    db.beginTransaction();
    auto stmt1 = db.prepare("INSERT INTO processes (session_id, config_version, pid, arrival_time, cpu_burst, priority) VALUES ('WOS-20261016-0003', 1, 101, 0, 5, 1);");
    stmt1.step();

    bool errorOccurred = false;
    try {
        // Step 2 fails constraint
        auto stmt2 = db.prepare("INSERT INTO processes (session_id, config_version, pid, arrival_time, cpu_burst, priority) VALUES ('WOS-20261016-0003', 1, 102, 0, -1, 1);");
        stmt2.step();
        db.commitTransaction();
    } catch (...) {
        errorOccurred = true;
        db.rollbackTransaction();
    }
    assert(errorOccurred);

    // Verify step 1 was rolled back
    auto check = db.prepare("SELECT COUNT(*) FROM processes WHERE session_id = 'WOS-20261016-0003';");
    assert(check.step());
    assert(check.getInt(0) == 0);

    std::cout << "  -> Transaction rollback successfully restored clean state on multi-step failure.\n";
}

int main() {
    std::cout << "============================================================\n";
    std::cout << "       WATCH OS — DATABASE SCHEMA & DBMS READINESS AUDIT\n";
    std::cout << "============================================================\n";

    testOnlyEightTablesExist();
    testPragmaIntegrityAndForeignKeys();
    testForeignKeyRejection();
    testCascadeDelete();
    testCheckConstraints();
    testTransactionRollback();

    std::cout << "All database schema audit tests PASSED successfully!\n";
    return 0;
}
