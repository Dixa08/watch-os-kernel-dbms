#!/usr/bin/env python3
"""Capture Wokwi RFC2217 serial exports into local reports and SQLite."""

from __future__ import annotations

import argparse
import csv
import hashlib
import io
import json
import re
import secrets
import sqlite3
import sys
from datetime import datetime, timezone
from pathlib import Path, PurePosixPath
from typing import Any, Iterable


ROOT = Path(__file__).resolve().parent
START_MARKER = re.compile(r"^<<<FILE_EXPORT_START:(.+)>>>$")
END_MARKER = "<<<FILE_EXPORT_END>>>"
EPOCH_SESSION_PREFIX = "WOS-19700101-"


def table_columns(connection: sqlite3.Connection, table: str) -> dict[str, sqlite3.Row]:
    rows = connection.execute(f'PRAGMA table_info("{table}")').fetchall()
    if not rows:
        raise RuntimeError(f"Required database table {table!r} does not exist")
    return {row["name"]: row for row in rows}


def checkpoint_wal(connection: sqlite3.Connection) -> None:
    connection.execute("PRAGMA wal_checkpoint(TRUNCATE)").fetchone()


def normalize_access_operation(operation: str) -> str:
    normalized = operation.strip().upper()
    if normalized == "EXEC":
        normalized = "EXECUTE"
    if normalized not in {"READ", "WRITE", "EXECUTE"}:
        raise ValueError("Access operation must be READ, WRITE, or EXECUTE")
    return normalized


def normalize_access_policy(policy: str) -> str:
    normalized = policy.strip().upper()
    if normalized not in {"ALLOW", "DENY"}:
        raise ValueError("Access policy must be ALLOW or DENY")
    return normalized


def upsert_access_policy(
    connection: sqlite3.Connection,
    session_id: str,
    access: dict[str, Any],
    policy: str = "ALLOW",
) -> None:
    user_or_role = str(access.get("user_or_role", "")).strip()
    resource_name = str(access.get("resource_name", "")).strip()
    operation = normalize_access_operation(str(access.get("requested_operation", "")))
    if not user_or_role or not resource_name:
        raise ValueError("Configured access entries need a user/role and resource")
    normalized_policy = normalize_access_policy(policy)
    connection.execute(
        "INSERT INTO access_policies "
        "(session_id, user_or_role, resource_name, operation, policy) "
        "VALUES (?, ?, ?, ?, ?) "
        "ON CONFLICT(session_id, user_or_role, resource_name, operation) "
        "DO UPDATE SET policy = excluded.policy",
        (session_id, user_or_role, resource_name, operation, normalized_policy),
    )


def ensure_process_columns(connection: sqlite3.Connection) -> None:
    columns = table_columns(connection, "processes")
    if "process_name" not in columns:
        connection.execute("ALTER TABLE processes ADD COLUMN process_name TEXT NOT NULL DEFAULT ''")
    if "description" not in columns:
        connection.execute("ALTER TABLE processes ADD COLUMN description TEXT NOT NULL DEFAULT ''")


def compatible_row(
    values: dict[str, Any], columns: dict[str, sqlite3.Row], table: str
) -> tuple[list[str], list[Any]]:
    selected = [(name, value) for name, value in values.items() if name in columns]
    included = {name for name, _ in selected}
    for name, column in columns.items():
        is_integer_primary_key = (
            column["pk"] and column["type"].upper() == "INTEGER"
        )
        if (
            column["notnull"]
            and column["dflt_value"] is None
            and not is_integer_primary_key
            and name not in included
        ):
            raise RuntimeError(
                f"Cannot import into {table}: required column {name!r} "
                "has no value or database default"
            )
    return [name for name, _ in selected], [value for _, value in selected]


def insert_session(
    connection: sqlite3.Connection, session: dict[str, Any], columns: dict[str, sqlite3.Row]
) -> None:
    session_id = str(session["session_id"])
    now = datetime.now(timezone.utc).isoformat(timespec="seconds")
    values = {
        "session_id": session_id,
        "title": session.get(
            "title", session.get("description") or "Wokwi imported session"
        ),
        "created_at": session.get("created_at", now),
        "updated_at": now,
        "status": session.get("status", "DRAFT"),
        "active_config_version": int(session.get("active_config_version", 1)),
        "description": session.get("description", "Imported from Wokwi serial export"),
    }
    names, parameters = compatible_row(values, columns, "sessions")
    quoted_names = ", ".join(f'"{name}"' for name in names)
    placeholders = ", ".join("?" for _ in names)
    connection.execute(
        f"INSERT OR IGNORE INTO sessions ({quoted_names}) VALUES ({placeholders})",
        parameters,
    )


def upsert_process(
    connection: sqlite3.Connection,
    session_id: str,
    version: int,
    process: dict[str, Any],
    columns: dict[str, sqlite3.Row],
) -> None:
    values = {
        "session_id": session_id,
        "config_version": version,
        "pid": int(process["pid"]),
        "arrival_time": int(process["arrival_time"]),
        "cpu_burst": int(process["cpu_burst"]),
        "priority": int(process["priority"]),
        "process_name": process.get("process_name", process.get("name", "")),
        "description": process.get("description", ""),
        "memory_demand": process.get("memory", {}).get("demand_units"),
    }
    names, parameters = compatible_row(values, columns, "processes")
    keys = [name for name in ("session_id", "config_version", "pid") if name in columns]
    if "session_id" not in keys or "pid" not in keys:
        raise RuntimeError("The processes table must expose session_id and pid columns")

    where = " AND ".join(f'"{key}" = ?' for key in keys)
    key_values = [values[key] for key in keys]
    existing = connection.execute(
        f'SELECT 1 FROM processes WHERE {where} LIMIT 1', key_values
    ).fetchone()
    if existing:
        update_names = [name for name in names if name not in keys]
        if update_names:
            assignments = ", ".join(f'"{name}" = ?' for name in update_names)
            update_values = [values[name] for name in update_names]
            connection.execute(
                f"UPDATE processes SET {assignments} WHERE {where}",
                update_values + key_values,
            )
        return

    quoted_names = ", ".join(f'"{name}"' for name in names)
    placeholders = ", ".join("?" for _ in names)
    connection.execute(
        f"INSERT INTO processes ({quoted_names}) VALUES ({placeholders})", parameters
    )


def import_session_snapshot(
    connection: sqlite3.Connection,
    payload: str,
    session_columns: dict[str, sqlite3.Row],
    process_columns: dict[str, sqlite3.Row],
) -> int:
    session = json.loads(payload)
    if not isinstance(session, dict) or not session.get("session_id"):
        raise ValueError("session snapshot needs a non-empty session_id")
    processes = session.get("processes", [])
    if not isinstance(processes, list):
        raise ValueError("session snapshot processes must be a JSON array")

    version = int(session.get("active_config_version", 1))
    with connection:
        insert_session(connection, session, session_columns)
        if "description" in session_columns:
            connection.execute(
                "UPDATE sessions SET description = ?, updated_at = ? WHERE session_id = ?",
                (
                    session.get("description", ""),
                    datetime.now(timezone.utc).isoformat(timespec="seconds"),
                    str(session["session_id"]),
                ),
            )
        for process in processes:
            if not isinstance(process, dict):
                raise ValueError("each process entry must be a JSON object")
            for required in ("pid", "arrival_time", "cpu_burst", "priority"):
                if required not in process:
                    raise ValueError(f"process entry is missing {required!r}")
            upsert_process(
                connection,
                str(session["session_id"]),
                version,
                process,
                process_columns,
            )
            access = process.get("access")
            if isinstance(access, dict) and access.get("configured"):
                upsert_access_policy(
                    connection, str(session["session_id"]), access
                )
    checkpoint_wal(connection)
    return len(processes)


def import_workload_csv(
    connection: sqlite3.Connection,
    path: Path,
    session_columns: dict[str, sqlite3.Row],
    process_columns: dict[str, sqlite3.Row],
) -> int:
    required = {
        "session_id", "session_description", "config_version", "process_id",
        "process_name", "process_description", "arrival_time", "cpu_burst",
        "priority", "memory_demand",
    }
    with path.open("r", encoding="utf-8-sig", newline="") as workload:
        reader = csv.DictReader(workload)
        if not reader.fieldnames or not required.issubset(reader.fieldnames):
            raise ValueError(f"{path.name}: unexpected workload CSV columns")
        rows = list(reader)
    if not rows:
        raise ValueError(f"{path.name}: workload CSV contains no process rows")

    session_id = rows[0]["session_id"]
    session_description = rows[0]["session_description"]
    if not session_id:
        raise ValueError(f"{path.name}: session_id is required")
    if any(row["session_id"] != session_id for row in rows):
        raise ValueError(f"{path.name}: rows contain multiple session IDs")
    version = int(rows[0]["config_version"])
    if any(int(row["config_version"]) != version for row in rows):
        raise ValueError(f"{path.name}: rows contain multiple config versions")

    with connection:
        insert_session(
            connection,
            {
                "session_id": session_id,
                "description": session_description,
                "active_config_version": version,
            },
            session_columns,
        )
        if "description" in session_columns:
            connection.execute(
                "UPDATE sessions SET description = ?, active_config_version = ?, updated_at = ? "
                "WHERE session_id = ?",
                (
                    session_description,
                    version,
                    datetime.now(timezone.utc).isoformat(timespec="seconds"),
                    session_id,
                ),
            )
        for row in rows:
            memory_demand = row["memory_demand"]
            upsert_process(
                connection,
                session_id,
                version,
                {
                    "pid": row["process_id"],
                    "process_name": row["process_name"],
                    "description": row["process_description"],
                    "arrival_time": row["arrival_time"],
                    "cpu_burst": row["cpu_burst"],
                    "priority": row["priority"],
                    "memory": {
                        "demand_units": int(memory_demand) if memory_demand else None
                    },
                },
                process_columns,
            )
    checkpoint_wal(connection)
    return len(rows)


def parse_report_metadata_csv(content: str) -> tuple[str | None, dict[int, dict[str, str]]]:
    lines = content.splitlines(keepends=True)
    header_index = None
    for index, line in enumerate(lines):
        stripped = line.strip()
        if stripped and not stripped.startswith("#"):
            header_index = index
            break
    if header_index is None:
        raise ValueError("report CSV is missing its data header")

    reader = csv.DictReader(lines[header_index:])
    expected = {"Section", "Algorithm", "Metric", "Value"}
    if not reader.fieldnames or not expected.issubset(reader.fieldnames):
        raise ValueError("unexpected report CSV columns")

    session_description: str | None = None
    processes: dict[int, dict[str, str]] = {}
    for row in reader:
        section = row.get("Section", "")
        algorithm = row.get("Algorithm", "")
        metric = row.get("Metric", "")
        value = row.get("Value", "") or ""
        if section == "Session" and algorithm == "Metadata" and metric == "Description":
            session_description = value
            continue
        if section != "Input":
            continue

        process_match = re.fullmatch(r"Process\s+(\d+)", algorithm.strip())
        if not process_match:
            continue
        pid = int(process_match.group(1))
        process = processes.setdefault(pid, {})
        if metric in {
            "ProcessName",
            "ProcessDescription",
            "ArrivalTime",
            "CpuBurst",
            "Priority",
            "MemoryDemandUnits",
            "AccessConfigured",
            "AccessUserOrRole",
            "AccessResourceName",
            "AccessRequestedOperation",
        }:
            process[metric] = value
    return session_description, processes


def session_id_from_export(path: Path, content: str) -> str | None:
    if path.name.startswith("workload_"):
        reader = csv.DictReader(io.StringIO(content))
        if reader.fieldnames and "session_id" in reader.fieldnames:
            row = next(reader, None)
            if row:
                return row.get("session_id") or None

    if path.suffix.lower() == ".md":
        match = re.search(r"(?m)^\*\*Session ID:\*\*\s*`([^`]+)`", content)
    else:
        match = re.search(r"(?m)^# Session ID:\s*(\S+)\s*$", content)
    return match.group(1) if match else None


def host_session_id(
    connection: sqlite3.Connection,
    session_id: str,
    reports_dir: Path,
    aliases: dict[str, str],
) -> str:
    if not session_id.startswith(EPOCH_SESSION_PREFIX):
        return session_id
    if session_id in aliases:
        return aliases[session_id]

    while True:
        timestamp = datetime.now().strftime("%Y%m%d-%H%M%S")
        candidate = f"WOS-{timestamp}-{secrets.token_hex(2).upper()}"
        exists_in_database = connection.execute(
            "SELECT 1 FROM sessions WHERE session_id = ?", (candidate,)
        ).fetchone()
        exists_in_reports = any(
            (reports_dir / f"{prefix}{candidate}{extension}").exists()
            for prefix in ("report_", "workload_")
            for extension in (".csv", ".md")
        )
        if not exists_in_database and not exists_in_reports:
            aliases[session_id] = candidate
            return candidate


def normalize_snapshot_session_id(
    connection: sqlite3.Connection,
    payload: str,
    reports_dir: Path,
    aliases: dict[str, str],
) -> str:
    session = json.loads(payload)
    if not isinstance(session, dict) or not session.get("session_id"):
        raise ValueError("session snapshot needs a non-empty session_id")
    original_id = str(session["session_id"])
    session["session_id"] = host_session_id(
        connection, original_id, reports_dir, aliases
    )
    return json.dumps(session, ensure_ascii=False, separators=(",", ":"))


def insert_compatible_record(
    connection: sqlite3.Connection,
    table: str,
    values: dict[str, Any],
    columns: dict[str, sqlite3.Row],
) -> None:
    names, parameters = compatible_row(values, columns, table)
    if not names:
        raise RuntimeError(f"No compatible columns supplied for {table}")
    quoted_names = ", ".join(f'"{name}"' for name in names)
    placeholders = ", ".join("?" for _ in names)
    connection.execute(
        f'INSERT OR IGNORE INTO "{table}" ({quoted_names}) VALUES ({placeholders})',
        parameters,
    )


def parse_report_csv(path: Path) -> tuple[dict[str, str], list[dict[str, str]]]:
    metadata: dict[str, str] = {}
    data_rows: list[dict[str, str]] = []
    with path.open("r", encoding="utf-8-sig", newline="") as report:
        lines = report.readlines()

    header_index = None
    for index, line in enumerate(lines):
        stripped = line.strip()
        if stripped.startswith("#") and ":" in stripped:
            key, value = stripped[1:].split(":", 1)
            metadata[key.strip().lower().replace(" ", "_")] = value.strip()
        elif stripped:
            header_index = index
            break
    if header_index is None:
        raise ValueError(f"{path.name}: missing CSV header")

    reader = csv.DictReader(lines[header_index:])
    if not reader.fieldnames or not {"Section", "Algorithm", "Metric", "Value"}.issubset(
        reader.fieldnames
    ):
        raise ValueError(f"{path.name}: unexpected report CSV columns")
    for row in reader:
        if row.get("Section") and row.get("Algorithm") and row.get("Metric"):
            data_rows.append(
                {
                    "section": row["Section"],
                    "algorithm": row["Algorithm"],
                    "metric": row["Metric"],
                    "value": row.get("Value", ""),
                }
            )

    if not metadata.get("session_id") or not metadata.get("run_id"):
        raise ValueError(f"{path.name}: missing Session ID or Run ID metadata")
    return metadata, data_rows


def import_report_csv(
    connection: sqlite3.Connection,
    path: Path,
    reports_dir: Path,
    session_columns: dict[str, sqlite3.Row],
    report_columns: dict[str, sqlite3.Row],
) -> None:
    metadata, rows = parse_report_csv(path)
    content = path.read_text(encoding="utf-8-sig")
    session_description, imported_processes = parse_report_metadata_csv(content)
    session_id = metadata["session_id"]
    source_run_id = metadata["run_id"]
    generated_at = metadata.get(
        "generated_at", datetime.fromtimestamp(path.stat().st_mtime, timezone.utc).isoformat()
    )
    now = datetime.now(timezone.utc).isoformat(timespec="seconds")
    report_id = "RPT-CSV-IMPORT-" + hashlib.sha256(
        path.name.encode("utf-8")
    ).hexdigest()[:16]
    try:
        relative_path = path.resolve().relative_to(reports_dir.resolve()).as_posix()
    except ValueError as exc:
        raise ValueError(f"{path} is not inside {reports_dir}") from exc

    with connection:
        existing_session = connection.execute(
            "SELECT 1 FROM sessions WHERE session_id = ?", (session_id,)
        ).fetchone()
        if existing_session is None:
            insert_compatible_record(
                connection,
                "sessions",
                {
                    "session_id": session_id,
                    "title": f"Imported report: {session_id}",
                    "created_at": generated_at,
                    "updated_at": now,
                    "status": "FINALIZED",
                    "active_config_version": 1,
                    "description": (
                        session_description
                        if session_description is not None
                        else f"Session restored from offline report {path.name}"
                    ),
                },
                session_columns,
            )
        elif session_description is not None:
            connection.execute(
                "UPDATE sessions SET description = ?, updated_at = ? WHERE session_id = ?",
                (session_description, now, session_id),
            )

        session_row = connection.execute(
            "SELECT active_config_version FROM sessions WHERE session_id = ?",
            (session_id,),
        ).fetchone()
        version = int(session_row["active_config_version"]) if session_row else 1
        process_columns = table_columns(connection, "processes")
        for pid, imported in imported_processes.items():
            existing_process = connection.execute(
                "SELECT arrival_time, cpu_burst, priority, process_name, description, memory_demand "
                "FROM processes WHERE session_id = ? AND config_version = ? AND pid = ?",
                (session_id, version, pid),
            ).fetchone()
            process = {
                "pid": pid,
                "process_name": imported.get(
                    "ProcessName",
                    existing_process["process_name"] if existing_process else "",
                ),
                "description": imported.get(
                    "ProcessDescription",
                    existing_process["description"] if existing_process else "",
                ),
                "arrival_time": imported.get(
                    "ArrivalTime",
                    existing_process["arrival_time"] if existing_process else 0,
                ),
                "cpu_burst": imported.get(
                    "CpuBurst",
                    existing_process["cpu_burst"] if existing_process else 1,
                ),
                "priority": imported.get(
                    "Priority",
                    existing_process["priority"] if existing_process else 1,
                ),
                "memory": {"demand_units": None},
            }
            memory_demand = imported.get("MemoryDemandUnits")
            if memory_demand:
                parsed_demand = int(memory_demand)
                if parsed_demand > 0:
                    process["memory"]["demand_units"] = parsed_demand
            elif existing_process:
                process["memory"]["demand_units"] = existing_process["memory_demand"]
            upsert_process(
                connection, session_id, version, process, process_columns
            )
            if imported.get("AccessConfigured", "").strip().lower() == "true":
                upsert_access_policy(
                    connection,
                    session_id,
                    {
                        "user_or_role": imported.get("AccessUserOrRole", ""),
                        "resource_name": imported.get("AccessResourceName", ""),
                        "requested_operation": imported.get(
                            "AccessRequestedOperation", ""
                        ),
                    },
                )
        insert_compatible_record(
            connection,
            "reports",
            {
                "report_id": report_id,
                "session_id": session_id,
                "run_id": source_run_id,
                "report_format": "CSV",
                "export_path": relative_path,
                "generated_at": generated_at,
            },
            report_columns,
        )
    checkpoint_wal(connection)


def safe_report_destination(marker_path: str, reports_dir: Path) -> Path:
    relative = PurePosixPath(marker_path)
    if (
        len(relative.parts) != 2
        or relative.parts[0] != "reports"
        or relative.name in ("", ".", "..")
        or relative.suffix.lower() not in (".md", ".csv")
    ):
        raise ValueError(f"Refusing unexpected report export path: {marker_path!r}")
    return reports_dir / relative.name


def connect_serial(port: str, baud: int):
    try:
        import serial
    except ImportError as exc:
        raise RuntimeError(
            "pyserial is required; install it with: python -m pip install pyserial"
        ) from exc
    return serial.serial_for_url(port, baudrate=baud, timeout=1)


def capture(port: str, baud: int, database: Path, reports_dir: Path) -> None:
    database.parent.mkdir(parents=True, exist_ok=True)
    reports_dir.mkdir(parents=True, exist_ok=True)

    connection = sqlite3.connect(database, timeout=30.0)
    connection.row_factory = sqlite3.Row
    connection.execute("PRAGMA foreign_keys = ON")
    connection.execute("PRAGMA journal_mode = WAL").fetchone()
    ensure_process_columns(connection)
    sessions = table_columns(connection, "sessions")
    processes = table_columns(connection, "processes")
    reports = table_columns(connection, "reports")

    for workload_path in sorted(reports_dir.glob("workload_*.csv")):
        try:
            count = import_workload_csv(
                connection, workload_path, sessions, processes
            )
            print(f"Imported {count} process(es) from {workload_path.name}")
        except (OSError, sqlite3.Error, ValueError, TypeError, KeyError, RuntimeError) as exc:
            print(f"Could not import workload CSV {workload_path.name}: {exc}", file=sys.stderr)

    for report_path in sorted(reports_dir.glob("report_WOS-*.csv")):
        try:
            import_report_csv(
                connection, report_path, reports_dir, sessions, reports
            )
            print(f"Imported offline report {report_path.name} into SQLite")
        except (OSError, sqlite3.Error, ValueError, TypeError, KeyError, RuntimeError) as exc:
            print(f"Could not import offline report {report_path.name}: {exc}", file=sys.stderr)

    try:
        serial_port = connect_serial(port, baud)
    except Exception:
        connection.close()
        raise

    print(f"Listening on {port}; database: {database}")
    session_id_aliases: dict[str, str] = {}
    active_path: str | None = None
    captured_lines: list[str] = []
    try:
        while True:
            raw = serial_port.readline()
            if not raw:
                continue
            line = raw.decode("utf-8", errors="replace").rstrip("\r\n")
            start = START_MARKER.match(line)
            if start:
                active_path = start.group(1)
                captured_lines = []
                continue
            if active_path is None:
                print(line)
                continue
            if line == END_MARKER:
                content = "\n".join(captured_lines)
                try:
                    if active_path == "data/session_inputs_log.jsonl":
                        content = normalize_snapshot_session_id(
                            connection, content, reports_dir, session_id_aliases
                        )
                        count = import_session_snapshot(
                            connection, content, sessions, processes
                        )
                        print(f"Imported session snapshot with {count} process(es)")
                    else:
                        destination = safe_report_destination(active_path, reports_dir)
                        original_session_id = session_id_from_export(
                            destination, content
                        )
                        if original_session_id:
                            normalized_session_id = host_session_id(
                                connection,
                                original_session_id,
                                reports_dir,
                                session_id_aliases,
                            )
                            if normalized_session_id != original_session_id:
                                content = content.replace(
                                    original_session_id, normalized_session_id
                                )
                                destination = destination.with_name(
                                    destination.name.replace(
                                        original_session_id, normalized_session_id
                                    )
                                )
                        destination.write_text(content + "\n", encoding="utf-8")
                        if PurePosixPath(active_path).name.startswith("workload_"):
                            count = import_workload_csv(
                                connection, destination, sessions, processes
                            )
                            print(f"Saved {destination}; imported {count} process(es)")
                        elif destination.name.startswith("report_") and destination.suffix.lower() == ".csv":
                            import_report_csv(
                                connection,
                                destination,
                                reports_dir,
                                sessions,
                                reports,
                            )
                            print(f"Saved {destination}; imported report metadata into SQLite")
                        else:
                            print(f"Saved {destination}")
                except (OSError, sqlite3.Error, ValueError, TypeError, KeyError, RuntimeError) as exc:
                    print(f"Export import failed for {active_path}: {exc}", file=sys.stderr)
                active_path = None
                captured_lines = []
                continue
            captured_lines.append(line)
    except KeyboardInterrupt:
        print("\nSerial capture stopped.")
    finally:
        serial_port.close()
        connection.close()


def main(argv: Iterable[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="rfc2217://localhost:4000")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--database", type=Path, default=ROOT / "data" / "watchos_sessions.db")
    parser.add_argument("--reports-dir", type=Path, default=ROOT / "reports")
    args = parser.parse_args(argv)
    try:
        capture(args.port, args.baud, args.database, args.reports_dir)
    except (OSError, sqlite3.Error, RuntimeError) as exc:
        print(f"Could not start Wokwi capture: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
