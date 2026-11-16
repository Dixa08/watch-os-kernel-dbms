#!/usr/bin/env python3
"""Populate a representative WATCH OS session using the supported tables."""

import sqlite3
from datetime import datetime, timezone
from pathlib import Path


DB_PATH = Path(__file__).resolve().parent / "data" / "watchos_sessions.db"


def populate() -> None:
    timestamp = datetime.now(timezone.utc).isoformat(timespec="seconds")
    suffix = datetime.now(timezone.utc).strftime("%Y%m%d%H%M%S")
    session_id = f"WOS-{suffix}-ALLFEAT"
    run_id = f"RUN-{suffix}-ALLFEAT"

    conn = sqlite3.connect(DB_PATH)
    try:
        conn.execute("PRAGMA foreign_keys = ON")
        with conn:
            conn.execute(
                "INSERT INTO sessions "
                "(session_id, title, created_at, updated_at, status, "
                "active_config_version, description, notes) "
                "VALUES (?, ?, ?, ?, 'FINALIZED', 1, ?, ?)",
                (
                    session_id,
                    "WATCH OS representative kernel session",
                    timestamp,
                    timestamp,
                    "Representative CPU scheduling and deadlock workload",
                    "Populated using the supported eight-table database schema.",
                ),
            )

            processes = [
                (session_id, 1, 101, 0, 6, 1, "Web Server", "HTTP workload", 64),
                (session_id, 1, 102, 1, 8, 2, "Database", "Query workload", 128),
                (session_id, 1, 103, 2, 4, 3, "Log Worker", "Log processing", 32),
                (session_id, 1, 104, 4, 3, 4, "Cache Cleaner", "Maintenance", 16),
            ]
            conn.executemany(
                "INSERT INTO processes "
                "(session_id, config_version, pid, arrival_time, cpu_burst, priority, "
                "process_name, description, memory_demand) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)",
                processes,
            )

            conn.executemany(
                "INSERT INTO access_policies "
                "(session_id, user_or_role, resource_name, operation, policy) "
                "VALUES (?, ?, ?, ?, ?)",
                [
                    (session_id, "root", "system_config", "WRITE", "ALLOW"),
                    (session_id, "guest", "system_config", "WRITE", "DENY"),
                    (session_id, "web_service", "storage_pool", "READ", "ALLOW"),
                ],
            )

            events = [
                (run_id, 0, "PROCESS_ARRIVE", 101, "P101 arrived in the ready queue"),
                (run_id, 1, "PROCESS_ARRIVE", 102, "P102 arrived in the ready queue"),
                (run_id, 2, "CPU_DISPATCH", 101, "P101 dispatched to CPU"),
                (run_id, 6, "PROCESS_TERMINATE", 101, "P101 completed"),
            ]
            conn.executemany(
                "INSERT INTO simulation_events "
                "(run_id, timestamp, event_type, entity_id, details) "
                "VALUES (?, ?, ?, ?, ?)",
                events,
            )

            history = [
                (run_id, 101, 0, "NEW", "READY", "Process created"),
                (run_id, 101, 2, "READY", "RUNNING", "Scheduler dispatch"),
                (run_id, 101, 8, "RUNNING", "TERMINATED", "CPU burst completed"),
            ]
            conn.executemany(
                "INSERT INTO process_state_history "
                "(run_id, pid, timestamp, from_state, to_state, reason) "
                "VALUES (?, ?, ?, ?, ?, ?)",
                history,
            )

            schedule = [
                (run_id, 101, 0, 6, 1, 6, 6, 0, 0),
                (run_id, 102, 1, 8, 2, 14, 13, 5, 5),
                (run_id, 103, 2, 4, 3, 18, 16, 12, 12),
                (run_id, 104, 4, 3, 4, 21, 17, 14, 14),
            ]
            conn.executemany(
                "INSERT INTO scheduling_results "
                "(run_id, pid, arrival_time, cpu_burst, priority, completion_time, "
                "turnaround_time, waiting_time, response_time) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)",
                schedule,
            )

            conn.execute(
                "INSERT INTO deadlock_results "
                "(run_id, is_deadlocked, is_safe, safe_sequence, deadlocked_processes, "
                "frequency_trials, frequency_deadlocks, empirical_frequency) "
                "VALUES (?, 0, 1, ?, '', 1000, 0, 0.0)",
                (run_id, "P101 -> P103 -> P104 -> P102"),
            )
            conn.executemany(
                "INSERT INTO reports "
                "(report_id, session_id, run_id, report_format, export_path, generated_at) "
                "VALUES (?, ?, ?, ?, ?, ?)",
                [
                    (
                        f"RPT-{session_id}",
                        session_id,
                        run_id,
                        "MARKDOWN",
                        f"reports/report_{session_id}.md",
                        timestamp,
                    ),
                    (
                        f"RPT-CSV-{session_id}",
                        session_id,
                        run_id,
                        "CSV",
                        f"reports/report_{session_id}.csv",
                        timestamp,
                    ),
                ],
            )
    finally:
        conn.close()

    print(f"[SUCCESS] Populated supported tables for session {session_id}.")


if __name__ == "__main__":
    populate()
