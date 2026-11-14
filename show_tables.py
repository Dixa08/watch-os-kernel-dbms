import sqlite3
from pathlib import Path

database = Path(__file__).resolve().parent / "data" / "watchos_sessions.db"
conn = sqlite3.connect(database)
cur = conn.cursor()

tables_query = """
SELECT name 
FROM sqlite_master 
WHERE type = 'table' AND name NOT LIKE 'sqlite_%' 
ORDER BY name;
"""

print(f"{'#':<3} | {'TABLE NAME':<26} | {'COLUMNS':<8} | {'ROW COUNT'}")
print("=" * 55)

tables = [r[0] for r in cur.execute(tables_query).fetchall()]
for i, table in enumerate(tables, start=1):
    quoted_table = table.replace('"', '""')
    cols = len(cur.execute(f'PRAGMA table_info("{quoted_table}");').fetchall())
    rows = cur.execute(f'SELECT COUNT(*) FROM "{quoted_table}";').fetchone()[0]
    print(f"{i:<3} | {table:<26} | {cols:<8} | {rows}")

print("=" * 55)
print(f"Total Tables: {len(tables)}")

print("\nMost Recent Sessions (newest inserted first)")
print("-" * 100)
for row in cur.execute(
    """
    SELECT rowid, session_id, created_at, status
    FROM sessions
    ORDER BY rowid DESC
    LIMIT 10;
    """
):
    print(f"rowid={row[0]} | session_id={row[1]} | created_at={row[2]} | status={row[3]}")

print("\nMost Recent Process Workloads (newest inserted first)")
print("-" * 100)
for row in cur.execute(
    """
    SELECT rowid, session_id, config_version, pid, arrival_time, cpu_burst, priority
    FROM processes
    ORDER BY rowid DESC
    LIMIT 10;
    """
):
    print(
        f"rowid={row[0]} | session_id={row[1]} | config_version={row[2]} "
        f"| PID={row[3]} | arrival={row[4]} | burst={row[5]} | priority={row[6]}"
    )

conn.close()
