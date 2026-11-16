#include "watchos/database/db_connection.hpp"
#include "watchos/database/run_repository.hpp"
#include "watchos/database/schema.hpp"
#include "watchos/database/session_repository.hpp"
#include "watchos/reporting/report_generator.hpp"
#include "watchos/security/access_controller.hpp"
#include "tests/fixtures/test_workloads.hpp"
#include <cstdio>
#include <fstream>
#include <iostream>
#include <cassert>
#include <iterator>

void testSchemaAndSessionCrud() {
    std::cout << "[TEST] Running testSchemaAndSessionCrud...\n";
    watchos::database::DbConnection db(":memory:");
    db.open();
    watchos::database::Schema::initializeSchema(db);

    watchos::database::SessionRepository repo(db);

    // Create session
    watchos::session::Session sess("WOS-20261016-TEST", "Unit test session");
    sess.setProcesses(watchos::test::getGoldenWorkload());
    repo.createSession(sess);

    assert(repo.sessionExists("WOS-20261016-TEST"));

    // Save workload
    repo.saveWorkload(sess, true);

    // Load session back
    watchos::session::Session loaded;
    bool ok = repo.loadSession("WOS-20261016-TEST", loaded);
    assert(ok);
    assert(loaded.getSessionId() == "WOS-20261016-TEST");
    assert(loaded.getStatus() == watchos::core::SessionStatus::FINALIZED);
    assert(loaded.getProcesses().size() == 3);

    // Verify process 1 details
    const auto& p1 = loaded.getProcesses()[0];
    assert(p1.getPid() == 1);
    assert(p1.getArrivalTime() == 0);
    assert(p1.getCpuBurst() == 5);
    assert(p1.getPriority() == 2);
    assert(p1.getMemoryConfig().isConfigured == true);
    assert(p1.getMemoryConfig().demandUnits == 128);
    // Verify session listing
    auto list = repo.listSessions();
    assert(list.size() == 1);
    assert(list[0].processCount == 3);

    std::cout << "[PASS] Session CRUD and relational mapping verified.\n";
}

void testForeignKeyIntegrity() {
    std::cout << "[TEST] Running testForeignKeyIntegrity...\n";
    watchos::database::DbConnection db(":memory:");
    db.open();
    watchos::database::Schema::initializeSchema(db);

    // Attempt to insert a process pointing to a non-existent session
    bool caughtFkError = false;
    try {
        auto stmt = db.prepare(
            "INSERT INTO processes "
            "(session_id, config_version, pid, arrival_time, cpu_burst, priority) "
            "VALUES ('NON_EXISTENT_SESSION', 1, 101, 0, 5, 1);"
        );
        stmt.step();
    } catch (const watchos::database::DbException& ex) {
        caughtFkError = true;
    }
    assert(caughtFkError);
    std::cout << "[PASS] SQLite Foreign Key constraint enforced successfully.\n";
}

void testTransactionRollback() {
    std::cout << "[TEST] Running testTransactionRollback...\n";
    watchos::database::DbConnection db(":memory:");
    db.open();
    watchos::database::Schema::initializeSchema(db);

    watchos::database::SessionRepository repo(db);
    watchos::session::Session sess("WOS-20261016-TX01", "Transaction test");
    repo.createSession(sess);

    try {
        watchos::database::TransactionGuard tx(db);
        auto stmt = db.prepare("UPDATE sessions SET description = 'Updated in TX' WHERE session_id = 'WOS-20261016-TX01';");
        stmt.step();
        // Intentional exception without commit
        throw std::runtime_error("Simulated crash during transaction");
    } catch (...) {}

    watchos::session::Session checkSess;
    repo.loadSession("WOS-20261016-TX01", checkSess);
    assert(checkSess.getDescription() == "Transaction test");
    std::cout << "[PASS] Transaction rollback restored original state cleanly.\n";
}

void testAccessPolicyNormalizationAndUpsert() {
    std::cout << "[TEST] Running testAccessPolicyNormalizationAndUpsert...\n";
    watchos::database::DbConnection db(":memory:");
    db.open();
    db.execute("CREATE TABLE sessions (session_id TEXT PRIMARY KEY);");
    db.execute(
        "CREATE TABLE access_policies ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, session_id TEXT NOT NULL, "
        "user_or_role TEXT NOT NULL, resource_name TEXT NOT NULL, "
        "operation TEXT NOT NULL, policy TEXT NOT NULL DEFAULT 'ALLOW', "
        "FOREIGN KEY (session_id) REFERENCES sessions(session_id) ON DELETE CASCADE);"
    );
    db.execute("INSERT INTO sessions VALUES ('WOS-20261116-ACL');");
    db.execute(
        "INSERT INTO access_policies "
        "(session_id, user_or_role, resource_name, operation, policy) VALUES "
        "('WOS-20261116-ACL', 'analyst', 'report', 'read', 'deny'), "
        "('WOS-20261116-ACL', 'analyst', 'report', 'READ', 'allow');"
    );

    watchos::database::Schema::initializeSchema(db);
    auto migrated = db.prepare(
        "SELECT operation, policy FROM access_policies "
        "WHERE session_id = 'WOS-20261116-ACL';"
    );
    assert(migrated.step());
    assert(migrated.getText(0) == "READ");
    assert(migrated.getText(1) == "ALLOW");
    assert(!migrated.step());

    watchos::database::RunRepository repository(db);
    repository.saveAccessPolicy("WOS-20261116-ACL", "analyst", "report", "write", "deny");
    repository.saveAccessPolicy("WOS-20261116-ACL", "analyst", "report", "WRITE", "allow");
    auto persisted = db.prepare(
        "SELECT operation, policy FROM access_policies "
        "WHERE session_id = 'WOS-20261116-ACL' AND operation = 'WRITE';"
    );
    assert(persisted.step());
    assert(persisted.getText(0) == "WRITE");
    assert(persisted.getText(1) == "ALLOW");
    assert(!persisted.step());

    watchos::security::AccessController controller;
    controller.addPolicy("analyst", "report", "write", true);
    controller.assignRole(7, "analyst");
    const auto decision = controller.checkAccess(7, "report", "wRiTe", 1);
    assert(decision.granted);
    assert(decision.operation == "WRITE");
    std::cout << "[PASS] Access policies are normalized, migrated, and upserted.\n";
}

void testReportExportStripsLeadingWhitespace() {
    std::cout << "[TEST] Running testReportExportStripsLeadingWhitespace...\n";
    const std::string path = "report_export_whitespace_test.tmp";
    const std::string content =
        "  # Heading\n"
        "\t## Subheading\n"
        "\xC2\xA0" "| Column,Value\n"
        " \trow,value\n";
    assert(watchos::reporting::ReportGenerator::exportToFile(path, content));

    std::ifstream input(path, std::ios::binary);
    assert(input.good());
    const std::string exported(
        (std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>()
    );
    assert(exported == "# Heading\n## Subheading\n| Column,Value\nrow,value\n");
    std::remove(path.c_str());
    std::cout << "[PASS] Report exports strip indentation and non-breaking spaces.\n";
}

int main() {
    try {
        testSchemaAndSessionCrud();
        testForeignKeyIntegrity();
        testTransactionRollback();
        testAccessPolicyNormalizationAndUpsert();
        testReportExportStripsLeadingWhitespace();
        std::cout << "All Database Persistence unit tests passed successfully!\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "[FAIL] Test failed with exception: " << ex.what() << "\n";
        return 1;
    }
}
