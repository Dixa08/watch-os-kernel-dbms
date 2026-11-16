#!/usr/bin/env python3
"""Migrate WATCH OS SQLite storage to the supported eight-table schema."""

from __future__ import annotations

import argparse
import sqlite3
from pathlib import Path


ROOT = Path(__file__).resolve().parent
DATABASE = ROOT / "data" / "watchos_sessions.db"

TABLE_DEFINITIONS = {
    "sessions": """
        CREATE TABLE sessions__new (
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
        )
    """,
    "simulation_events": """
        CREATE TABLE simulation_events__new (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            run_id TEXT NOT NULL,
            timestamp INTEGER NOT NULL CHECK (timestamp >= 0),
            event_type TEXT NOT NULL,
            entity_id INTEGER,
            details TEXT
        )
    """,
    "process_state_history": """
        CREATE TABLE process_state_history__new (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            run_id TEXT NOT NULL,
            pid INTEGER NOT NULL,
            timestamp INTEGER NOT NULL CHECK (timestamp >= 0),
            from_state TEXT NOT NULL,
            to_state TEXT NOT NULL,
            reason TEXT
        )
    """,
    "reports": """
        CREATE TABLE reports__new (
            report_id TEXT PRIMARY KEY,
            session_id TEXT NOT NULL,
            run_id TEXT NOT NULL,
            report_format TEXT NOT NULL,
            export_path TEXT NOT NULL,
            generated_at TEXT NOT NULL,
            FOREIGN KEY (session_id) REFERENCES sessions(session_id) ON DELETE CASCADE
        )
    """,
    "deadlock_results": """
        CREATE TABLE deadlock_results__new (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            run_id TEXT NOT NULL,
            is_deadlocked INTEGER NOT NULL DEFAULT 0,
            is_safe INTEGER NOT NULL DEFAULT 1,
            safe_sequence TEXT,
            deadlocked_processes TEXT,
            frequency_trials INTEGER DEFAULT 0 CHECK (frequency_trials >= 0),
            frequency_deadlocks INTEGER DEFAULT 0 CHECK (frequency_deadlocks >= 0),
            empirical_frequency REAL DEFAULT 0.0
                CHECK (empirical_frequency >= 0.0 AND empirical_frequency <= 1.0)
        )
    """,
    "scheduling_results": """
        CREATE TABLE scheduling_results__new (
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
        )
    """,
}

ALLOWED_TABLES = {
    "sessions",
    "processes",
    "simulation_events",
    "process_state_history",
    "reports",
    "deadlock_results",
    "access_policies",
    "scheduling_results",
}


def rebuild_table(connection: sqlite3.Connection, table: str, definition: str) -> None:
    old_columns = {
        row[1] for row in connection.execute(f'PRAGMA table_info("{table}")')
    }
    if not old_columns:
        raise RuntimeError(f"Required table {table!r} does not exist")

    connection.execute(definition)
    new_table = f"{table}__new"
    new_columns = [
        row[1] for row in connection.execute(f'PRAGMA table_info("{new_table}")')
    ]
    copied_columns = [column for column in new_columns if column in old_columns]
    quoted = ", ".join(f'"{column}"' for column in copied_columns)
    connection.execute(
        f'INSERT INTO "{new_table}" ({quoted}) SELECT {quoted} FROM "{table}"'
    )
    connection.execute(f'DROP TABLE "{table}"')
    connection.execute(f'ALTER TABLE "{new_table}" RENAME TO "{table}"')


def migrate(database: Path) -> None:
    if not database.is_file():
        raise FileNotFoundError(f"Database not found: {database}")

    connection = sqlite3.connect(database)
    try:
        connection.execute("PRAGMA foreign_keys = OFF")
        connection.execute("BEGIN IMMEDIATE")

        for table, definition in TABLE_DEFINITIONS.items():
            rebuild_table(connection, table, definition)

        tables = [
            row[0]
            for row in connection.execute(
                "SELECT name FROM sqlite_master "
                "WHERE type = 'table' AND name NOT LIKE 'sqlite_%'"
            )
        ]
        for table in tables:
            if table not in ALLOWED_TABLES:
                escaped = table.replace('"', '""')
                connection.execute(f'DROP TABLE "{escaped}"')

        views = [
            row[0]
            for row in connection.execute(
                "SELECT name FROM sqlite_master WHERE type = 'view'"
            )
        ]
        for view in views:
            escaped = view.replace('"', '""')
            connection.execute(f'DROP VIEW "{escaped}"')

        connection.execute(
            "CREATE INDEX IF NOT EXISTS idx_processes_lookup "
            "ON processes(session_id, config_version)"
        )
        connection.execute(
            "CREATE INDEX IF NOT EXISTS idx_events_run_time "
            "ON simulation_events(run_id, timestamp)"
        )
        connection.execute(
            "CREATE INDEX IF NOT EXISTS idx_state_history_run "
            "ON process_state_history(run_id, timestamp)"
        )
        connection.execute(
            "CREATE INDEX IF NOT EXISTS idx_reports_session ON reports(session_id)"
        )
        connection.execute(
            "CREATE INDEX IF NOT EXISTS idx_deadlock_results_run "
            "ON deadlock_results(run_id)"
        )
        connection.execute(
            "CREATE INDEX IF NOT EXISTS idx_scheduling_results_run "
            "ON scheduling_results(run_id)"
        )
        connection.commit()

        connection.execute("PRAGMA foreign_keys = ON")
        if connection.execute("PRAGMA foreign_keys").fetchone()[0] != 1:
            raise RuntimeError("Could not enable SQLite foreign-key enforcement")
        violations = connection.execute("PRAGMA foreign_key_check").fetchall()
        if violations:
            raise RuntimeError(f"Foreign-key violations remain: {violations!r}")

        connection.execute("PRAGMA wal_checkpoint(TRUNCATE)").fetchone()
        connection.execute("VACUUM")
    except Exception:
        connection.rollback()
        raise
    finally:
        connection.close()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--database", type=Path, default=DATABASE)
    args = parser.parse_args()
    migrate(args.database)
    print(f"Migrated database to the eight-table schema: {args.database}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
