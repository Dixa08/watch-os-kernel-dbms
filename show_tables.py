import sqlite3
from pathlib import Path


DATABASE = Path(__file__).resolve().parent / "data" / "watchos_sessions.db"
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


def main() -> int:
    if not DATABASE.is_file():
        raise FileNotFoundError(f"Database not found: {DATABASE}")

    conn = sqlite3.connect(DATABASE)
    try:
        cur = conn.cursor()
        tables = {
            row[0]
            for row in cur.execute(
                "SELECT name FROM sqlite_master "
                "WHERE type = 'table' AND name NOT LIKE 'sqlite_%'"
            )
        }
        if tables != ALLOWED_TABLES:
            missing = sorted(ALLOWED_TABLES - tables)
            unexpected = sorted(tables - ALLOWED_TABLES)
            raise RuntimeError(
                f"Unexpected database schema; missing={missing}, unexpected={unexpected}"
            )

        print(f"{'#':<3} | {'TABLE NAME':<26} | {'COLUMNS':<8} | {'ROW COUNT'}")
        print("=" * 55)
        for index, table in enumerate(sorted(ALLOWED_TABLES), start=1):
            quoted_table = table.replace('"', '""')
            columns = cur.execute(
                f'PRAGMA table_info("{quoted_table}")'
            ).fetchall()
            rows = cur.execute(
                f'SELECT COUNT(*) FROM "{quoted_table}"'
            ).fetchone()[0]
            print(f"{index:<3} | {table:<26} | {len(columns):<8} | {rows}")

        print("=" * 55)
        print(f"Total Tables: {len(tables)}")

        print("\nMost Recent Sessions (newest inserted first)")
        print("-" * 100)
        for row in cur.execute(
            "SELECT rowid, session_id, created_at, status "
            "FROM sessions ORDER BY rowid DESC LIMIT 10"
        ):
            print(
                f"rowid={row[0]} | session_id={row[1]} | "
                f"created_at={row[2]} | status={row[3]}"
            )

        print("\nMost Recent Process Workloads (newest inserted first)")
        print("-" * 100)
        for row in cur.execute(
            "SELECT rowid, session_id, config_version, pid, process_name, "
            "description, arrival_time, cpu_burst, priority "
            "FROM processes ORDER BY rowid DESC LIMIT 10"
        ):
            print(
                f"rowid={row[0]} | session_id={row[1]} | config_version={row[2]} "
                f"| PID={row[3]} | name={row[4]} | description={row[5]} "
                f"| arrival={row[6]} | burst={row[7]} | priority={row[8]}"
            )
    finally:
        conn.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
