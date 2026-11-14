#!/usr/bin/env python3
"""Capture Wokwi RFC2217 serial exports into local reports and SQLite."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import re
import sqlite3
import sys
from datetime import datetime, timezone
from pathlib import Path, PurePosixPath
from typing import Any, Iterable


ROOT = Path(__file__).resolve().parent
START_MARKER = re.compile(r"^<<<FILE_EXPORT_START:(.+)>>>$")
END_MARKER = "<<<FILE_EXPORT_END>>>"


def table_columns(connection: sqlite3.Connection, table: str) -> dict[str, sqlite3.Row]:
    rows = connection.execute(f'PRAGMA table_info("{table}")').fetchall()
    if not rows:
        raise RuntimeError(f"Required database table {table!r} does not exist")
    return {row["name"]: row for row in rows}


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
        "title": session.get("title", "Wokwi imported session"),
        "created_at": session.get("created_at", now),
        "updated_at": now,
        "status": session.get("status", "DRAFT"),
        "active_config_version": int(session.get("active_config_version", 1)),
        "description": session.get("description", "Imported from Wokwi serial export"),
        "metadata": json.dumps(
            {"algorithm": session.get("algorithm"), "quantum": session.get("quantum")},
            ensure_ascii=False,
        ),
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
    return len(processes)


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
    run_columns: dict[str, sqlite3.Row],
    report_columns: dict[str, sqlite3.Row],
) -> None:
    metadata, rows = parse_report_csv(path)
    session_id = metadata["session_id"]
    source_run_id = metadata["run_id"]
    generated_at = metadata.get(
        "generated_at", datetime.fromtimestamp(path.stat().st_mtime, timezone.utc).isoformat()
    )
    now = datetime.now(timezone.utc).isoformat(timespec="seconds")
    offline_run_id = f"OFFLINE-{session_id}-{source_run_id}"
    report_id = "RPT-CSV-IMPORT-" + hashlib.sha256(
        path.name.encode("utf-8")
    ).hexdigest()[:16]
    try:
        relative_path = path.resolve().relative_to(reports_dir.resolve()).as_posix()
    except ValueError as exc:
        raise ValueError(f"{path} is not inside {reports_dir}") from exc

    parameters = json.dumps(
        {
            "source": "offline_csv_report",
            "source_run_id": source_run_id,
            "source_file": relative_path,
            "metrics": rows,
        },
        ensure_ascii=False,
        separators=(",", ":"),
    )

    with connection:
        if connection.execute(
            "SELECT 1 FROM sessions WHERE session_id = ?", (session_id,)
        ).fetchone() is None:
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
                    "description": f"Session restored from offline report {path.name}",
                    "metadata": json.dumps({"imported_from": relative_path}),
                },
                session_columns,
            )

        if connection.execute(
            "SELECT 1 FROM simulation_runs WHERE run_id = ?", (offline_run_id,)
        ).fetchone() is None:
            insert_compatible_record(
                connection,
                "simulation_runs",
                {
                    "run_id": offline_run_id,
                    "session_id": session_id,
                    "module": "OFFLINE_CSV_REPORT_IMPORT",
                    "status": "COMPLETED",
                    "parameters": parameters,
                    "created_at": generated_at,
                },
                run_columns,
            )

        insert_compatible_record(
            connection,
            "reports",
            {
                "report_id": report_id,
                "session_id": session_id,
                "run_id": offline_run_id,
                "report_format": "CSV",
                "export_path": relative_path,
                "generated_at": generated_at,
            },
            report_columns,
        )


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

    connection = sqlite3.connect(database)
    connection.row_factory = sqlite3.Row
    connection.execute("PRAGMA foreign_keys = ON")
    sessions = table_columns(connection, "sessions")
    processes = table_columns(connection, "processes")
    runs = table_columns(connection, "simulation_runs")
    reports = table_columns(connection, "reports")

    for report_path in sorted(reports_dir.glob("report_WOS-*.csv")):
        try:
            import_report_csv(
                connection, report_path, reports_dir, sessions, runs, reports
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
                        count = import_session_snapshot(
                            connection, content, sessions, processes
                        )
                        print(f"Imported session snapshot with {count} process(es)")
                    else:
                        destination = safe_report_destination(active_path, reports_dir)
                        destination.write_text(content + "\n", encoding="utf-8")
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
