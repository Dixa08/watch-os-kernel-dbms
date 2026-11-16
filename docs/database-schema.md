# WATCH OS SQLite Schema

The WATCH OS persistence layer creates exactly these eight tables:

1. `sessions`
2. `processes`
3. `simulation_events`
4. `process_state_history`
5. `reports`
6. `deadlock_results`
7. `access_policies`
8. `scheduling_results`

The `sessions` table has no `metadata`/`meta_data` column. Configuration
versions, detailed I/O/memory workloads, run registries, Gantt segments, and
operational log tables are not part of this schema.

`processes`, `reports`, and `access_policies` reference `sessions` and cascade
when a session is deleted. The `run_id` values in events and analysis results
are correlation identifiers only; there is no `simulation_runs` table or
foreign key to one.

To migrate an existing database while preserving rows in the supported tables,
run:

```powershell
python migrate_watchos_db.py
```

The migration disables foreign-key enforcement only while rebuilding the
tables, then enables it, checks referential integrity, checkpoints the WAL, and
vacuum-compacts the database.
