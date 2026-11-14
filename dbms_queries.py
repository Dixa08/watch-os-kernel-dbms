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
               s.created_at,
               s.status,
               s.active_config_version,
               COUNT(p.pid) AS process_count,
               COALESCE(
                   GROUP_CONCAT(
                       'PID=' || p.pid ||
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
        "CPU scheduling results compared across simulation runs",
        """
        SELECT r.run_id,
               r.session_id,
               r.module,
               r.parameters,
               r.created_at,
               COUNT(sr.pid) AS process_count,
               ROUND(AVG(sr.waiting_time), 2) AS avg_waiting_time,
               ROUND(AVG(sr.turnaround_time), 2) AS avg_turnaround_time,
               ROUND(AVG(sr.response_time), 2) AS avg_response_time
        FROM simulation_runs AS r
        LEFT JOIN scheduling_results AS sr ON sr.run_id = r.run_id
        GROUP BY r.run_id, r.session_id, r.module, r.parameters, r.created_at
        ORDER BY r.created_at DESC
        LIMIT 30
        """,
    ),
    "3": (
        "Deadlock detection and Banker's safe-state results",
        """
        SELECT dr.run_id,
               r.session_id,
               r.created_at,
               dr.is_deadlocked,
               dr.is_safe,
               dr.safe_sequence,
               dr.deadlocked_processes,
               dr.frequency_trials,
               dr.frequency_deadlocks,
               dr.empirical_frequency
        FROM deadlock_results AS dr
        JOIN simulation_runs AS r ON r.run_id = dr.run_id
        ORDER BY r.created_at DESC
        LIMIT 30
        """,
    ),
    "4": (
        "Virtual memory and disk I/O performance",
        """
        SELECT mr.run_id,
               mr.algorithm,
               'MEMORY' AS subsystem,
               mr.total_references AS workload_size,
               mr.page_faults AS primary_metric,
               mr.page_hits AS secondary_metric,
               mr.fault_ratio AS ratio,
               mr.thrashing_detected AS status
        FROM memory_results AS mr
        UNION ALL
        SELECT ir.run_id,
               ir.algorithm,
               'DISK_IO' AS subsystem,
               NULL AS workload_size,
               ir.total_head_movement AS primary_metric,
               ir.average_head_movement AS secondary_metric,
               NULL AS ratio,
               ir.service_order AS status
        FROM io_results AS ir
        ORDER BY run_id DESC, subsystem, algorithm
        LIMIT 100
        """,
    ),
    "5": (
        "Security and permission audit events",
        """
        SELECT pe.run_id,
               r.session_id,
               r.created_at,
               pe.actor,
               pe.resource,
               pe.operation,
               pe.decision,
               pe.reason
        FROM permission_events AS pe
        JOIN simulation_runs AS r ON r.run_id = pe.run_id
        ORDER BY r.created_at DESC
        LIMIT 100
        """,
    ),
}


def table_columns(connection: sqlite3.Connection, table: str) -> set[str]:
    return {
        row[1] for row in connection.execute(f'PRAGMA table_info("{table}")')
    }


def verify_schema(connection: sqlite3.Connection) -> None:
    required = {
        "sessions": {
            "session_id", "title", "description", "created_at", "updated_at",
            "status", "active_config_version",
        },
        "processes": {
            "session_id", "config_version", "pid", "arrival_time", "cpu_burst", "priority",
        },
        "simulation_runs": {
            "run_id", "session_id", "module", "parameters", "created_at",
        },
        "scheduling_results": {
            "run_id", "pid", "waiting_time", "turnaround_time", "response_time",
        },
        "deadlock_results": {
            "run_id", "is_deadlocked", "is_safe", "safe_sequence",
            "deadlocked_processes", "frequency_trials", "frequency_deadlocks",
            "empirical_frequency",
        },
        "memory_results": {
            "run_id", "algorithm", "total_references", "page_faults", "page_hits",
            "fault_ratio", "thrashing_detected",
        },
        "io_results": {
            "run_id", "algorithm", "total_head_movement", "average_head_movement",
            "service_order",
        },
        "permission_events": {
            "run_id", "actor", "resource", "operation", "decision", "reason",
        },
    }
    for table, expected in required.items():
        actual = table_columns(connection, table)
        missing = expected - actual
        if missing:
            raise RuntimeError(
                f"Database schema mismatch for {table}: missing {', '.join(sorted(missing))}"
            )


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
                "2) Compare CPU scheduling results across runs\n"
                "3) Deadlock and Banker's safe-state results\n"
                "4) Virtual memory and disk I/O performance\n"
                "5) Security and permission audit events\n"
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
