#!/usr/bin/env python3
"""Interactive, schema-checked DBMS viva queries for WATCH OS."""

from __future__ import annotations

import re
import sqlite3
import argparse
from pathlib import Path


ROOT = Path(__file__).resolve().parent
DATABASE = ROOT / "data" / "watchos_sessions.db"

QUERIES: dict[str, tuple[str, str]] = {
    "1": (
        "Recent sessions and process workloads",
        """
        SELECT s.session_id,
               COALESCE(s.title, s.description, '(untitled)') AS session_title,
               s.description AS session_description,
               s.created_at,
               s.status,
               s.active_config_version,
               COUNT(p.pid) AS process_count,
               COALESCE(
                   GROUP_CONCAT(
                       'PID=' || p.pid ||
                       ' name=' || COALESCE(NULLIF(p.process_name, ''), '(unnamed)') ||
                       ' description=' || COALESCE(p.description, '') ||
                       ' arrival=' || p.arrival_time ||
                       ' burst=' || p.cpu_burst ||
                       ' priority=' || p.priority,
                       ' | '
                   ),
                   '(no processes)'
               ) AS workload
        FROM sessions AS s
        LEFT JOIN processes AS p
          ON p.session_id = s.session_id
         AND p.config_version = s.active_config_version
        GROUP BY s.session_id, s.title, s.description, s.created_at,
                 s.status, s.active_config_version
        ORDER BY COALESCE(s.updated_at, s.created_at) DESC
        LIMIT 20
        """,
    ),
    "2": (
        "CPU scheduling results grouped by run",
        """
        SELECT run_id,
               COUNT(pid) AS process_count,
               ROUND(AVG(waiting_time), 2) AS avg_waiting_time,
               ROUND(AVG(turnaround_time), 2) AS avg_turnaround_time,
               ROUND(AVG(response_time), 2) AS avg_response_time
        FROM scheduling_results
        GROUP BY run_id
        ORDER BY run_id DESC
        LIMIT 30
        """,
    ),
    "3": (
        "Deadlock detection and Banker's safe-state results",
        """
        SELECT run_id,
               is_deadlocked,
               dr.is_safe,
               dr.safe_sequence,
               dr.deadlocked_processes,
               dr.frequency_trials,
               dr.frequency_deadlocks,
               dr.empirical_frequency
        FROM deadlock_results AS dr
        ORDER BY dr.id DESC
        LIMIT 30
        """,
    ),
    "4": (
        "Simulation events and process state transitions",
        """
        SELECT run_id, timestamp, entity_id, event_type, details
        FROM simulation_events
        UNION ALL
        SELECT run_id, timestamp, pid,
               from_state || ' -> ' || to_state AS event_type, reason AS details
        FROM process_state_history
        ORDER BY run_id DESC, timestamp
        LIMIT 100
        """,
    ),
    "5": (
        "Access policies and report exports",
        """
        SELECT session_id,
               'POLICY' AS record_type,
               user_or_role AS subject,
               resource_name AS resource_or_path,
               operation,
               policy AS detail
        FROM access_policies
        UNION ALL
        SELECT session_id,
               'REPORT' AS record_type,
               report_format AS subject,
               export_path AS resource_or_path,
               generated_at AS operation,
               run_id AS detail
        FROM reports
        ORDER BY session_id DESC
        LIMIT 100
        """,
    ),
}


def table_columns(connection: sqlite3.Connection, table: str) -> set[str]:
    return {
        row[1] for row in connection.execute(f'PRAGMA table_info("{table}")')
    }


def verify_schema(connection: sqlite3.Connection) -> None:
    allowed_tables = {
        "sessions", "processes", "simulation_events", "process_state_history",
        "reports", "deadlock_results", "access_policies", "scheduling_results",
    }
    actual_tables = {
        row[0]
        for row in connection.execute(
            "SELECT name FROM sqlite_master "
            "WHERE type = 'table' AND name NOT LIKE 'sqlite_%'"
        )
    }
    if actual_tables != allowed_tables:
        missing = sorted(allowed_tables - actual_tables)
        unexpected = sorted(actual_tables - allowed_tables)
        raise RuntimeError(
            f"Database must contain exactly the eight supported tables; "
            f"missing={missing}, unexpected={unexpected}"
        )

    required = {
        "sessions": {
            "session_id", "title", "description", "created_at", "updated_at",
            "status", "active_config_version",
        },
        "processes": {
            "session_id", "config_version", "pid", "process_name", "description",
            "arrival_time", "cpu_burst", "priority",
        },
        "simulation_events": {"run_id", "timestamp", "event_type"},
        "process_state_history": {
            "run_id", "pid", "timestamp", "from_state", "to_state",
        },
        "reports": {
            "report_id", "session_id", "run_id", "report_format", "export_path",
            "generated_at",
        },
        "scheduling_results": {
            "run_id", "pid", "waiting_time", "turnaround_time", "response_time",
        },
        "deadlock_results": {
            "run_id", "is_deadlocked", "is_safe", "safe_sequence",
            "deadlocked_processes", "frequency_trials", "frequency_deadlocks",
            "empirical_frequency",
        },
        "access_policies": {
            "session_id", "user_or_role", "resource_name", "operation", "policy",
        },
    }
    for table, expected in required.items():
        actual = table_columns(connection, table)
        missing = expected - actual
        if missing:
            raise RuntimeError(
                f"Database schema mismatch for {table}: missing {', '.join(sorted(missing))}"
            )
    if "metadata" in table_columns(connection, "sessions"):
        raise RuntimeError("The sessions table must not contain a metadata column")


def display_rows(cursor: sqlite3.Cursor) -> None:
    headers = [column[0] for column in cursor.description or ()]
    rows = cursor.fetchmany(100)
    if not rows:
        print("(no rows)")
        return

    print(" | ".join(headers))
    print("-+-".join("-" * min(max(len(name), 8), 24) for name in headers))
    for row in rows:
        print(" | ".join(str(value) if value is not None else "NULL" for value in row))


def run_custom_sql(connection: sqlite3.Connection) -> None:
    statement = input(
        "Enter one SELECT, or a narrowly-scoped UPDATE/DELETE sessions statement:\n> "
    ).strip()
    if not statement:
        return

    normalized = statement.lstrip()
    if re.match(r"(?is)^(select|with)\b", normalized):
        try:
            cursor = connection.execute(statement)
            display_rows(cursor)
        except sqlite3.Error as exc:
            print(f"SQL error: {exc}")
        return

    string_literal = r"'((?:''|[^'])*)'"
    update = re.fullmatch(
        rf"(?is)\s*UPDATE\s+sessions\s+SET\s+"
        rf"(title|description|notes|status)\s*=\s*{string_literal}\s+"
        rf"WHERE\s+session_id\s*=\s*{string_literal}\s*;?\s*",
        statement,
    )
    delete = re.fullmatch(
        rf"(?is)\s*DELETE\s+FROM\s+sessions\s+WHERE\s+"
        rf"session_id\s*=\s*{string_literal}\s*;?\s*",
        statement,
    )
    if update:
        column, value, session_id = update.groups()
        value = value.replace("''", "'")
        mutation_sql = f'UPDATE sessions SET "{column}" = ? WHERE session_id = ?'
        parameters = (value, session_id.replace("''", "'"))
        operation = "update"
    elif delete:
        (session_id,) = delete.groups()
        mutation_sql = "DELETE FROM sessions WHERE session_id = ?"
        parameters = (session_id.replace("''", "'"),)
        operation = "delete"
    else:
        print(
            "Allowed changes are a single quoted-value UPDATE of title/description/"
            "notes/status or DELETE FROM sessions, each with WHERE session_id = '...'."
        )
        return

    session_id = parameters[-1]
    target = connection.execute(
        "SELECT session_id, title, status FROM sessions WHERE session_id = ?",
        (session_id,),
    ).fetchone()
    if target is None:
        print(f"No session found with session_id {session_id!r}; nothing changed.")
        return
    print(f"Target session: {dict(target)}")

    print(f"Mutation to execute:\n{statement}")
    if input("Type APPLY to execute this change in a transaction: ").strip() != "APPLY":
        print("Cancelled.")
        return

    try:
        connection.execute("BEGIN IMMEDIATE")
        cursor = connection.execute(mutation_sql, parameters)
        affected = cursor.rowcount
        connection.commit()
        print(f"Committed {operation}; affected rows: {affected}")
    except sqlite3.Error as exc:
        connection.rollback()
        print(f"SQL error; transaction rolled back: {exc}")


def open_database(database: Path) -> sqlite3.Connection:
    if not database.exists():
        raise FileNotFoundError(f"Database not found: {database}")
    connection = sqlite3.connect(database)
    connection.row_factory = sqlite3.Row
    connection.execute("PRAGMA foreign_keys = ON")
    try:
        verify_schema(connection)
    except Exception:
        connection.close()
        raise
    return connection


def verify_all(database: Path) -> int:
    try:
        connection = open_database(database)
    except (OSError, sqlite3.Error, RuntimeError) as exc:
        print(f"Cannot use the database: {exc}")
        return 1

    failures = 0
    try:
        for key in ("1", "2", "3", "4", "5"):
            title, sql = QUERIES[key]
            print(f"\n[{key}] {title}")
            try:
                cursor = connection.execute(sql)
                display_rows(cursor)
            except sqlite3.Error as exc:
                failures += 1
                print(f"Query failed: {exc}")
    finally:
        connection.close()
    if failures:
        print(f"\nVerification failed: {failures} read-only query(s) had SQL errors.")
        return 1
    print("\nVerification passed: all read-only queries executed successfully.")
    return 0


def menu(database: Path = DATABASE) -> int:
    try:
        connection = open_database(database)
    except (OSError, sqlite3.Error, RuntimeError) as exc:
        print(f"Cannot use the database: {exc}")
        return 1

    try:
        while True:
            print(
                "\nWATCH OS DBMS / OS Viva Queries\n"
                "1) Recent sessions and workloads (JOIN, GROUP BY, ORDER BY)\n"
                "2) CPU scheduling result sets grouped by run ID\n"
                "3) Deadlock and Banker's safe-state results\n"
                "4) Simulation events and process state transitions\n"
                "5) Access policies and report exports\n"
                "6) Custom SELECT / safely update or delete a test session\n"
                "0) Exit"
            )
            choice = input("Select: ").strip()
            if choice == "0":
                break
            if choice == "6":
                run_custom_sql(connection)
                continue
            query = QUERIES.get(choice)
            if query is None:
                print("Choose one of the listed options.")
                continue
            title, sql = query
            print(f"\n{title}")
            try:
                display_rows(connection.execute(sql))
            except sqlite3.Error as exc:
                print(f"Query error: {exc}")
    finally:
        connection.close()
    return 0


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--verify-all",
        action="store_true",
        help="execute and display every read-only viva query, then exit",
    )
    parser.add_argument("--database", type=Path, default=DATABASE)
    arguments = parser.parse_args()
    raise SystemExit(
        verify_all(arguments.database)
        if arguments.verify_all
        else menu(arguments.database)
    )
