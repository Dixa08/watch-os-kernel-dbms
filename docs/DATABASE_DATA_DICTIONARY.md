# WATCH OS Database Data Dictionary

The SQLite database contains exactly eight tables. This document describes
the current schema created by `src/database/schema.cpp`.

## Tables

### `sessions`

Stores a session's identity, lifecycle, and active workload version.

| Column | Type | Constraints |
|---|---|---|
| `session_id` | `TEXT` | Primary key |
| `title` | `TEXT` | Nullable |
| `created_at` | `TEXT` | Not null |
| `updated_at` | `TEXT` | Nullable |
| `last_opened_at` | `TEXT` | Nullable |
| `ended_at` | `TEXT` | Nullable |
| `status` | `TEXT` | Not null, defaults to `DRAFT` |
| `active_config_version` | `INTEGER` | Not null, defaults to `1` |
| `description` | `TEXT` | Nullable |
| `notes` | `TEXT` | Nullable |

There is deliberately no `metadata` or `meta_data` column.

### `processes`

Stores configured processes for each session and workload version.

| Column | Type | Constraints |
|---|---|---|
| `id` | `INTEGER` | Auto-increment primary key |
| `session_id` | `TEXT` | Not null; references `sessions` with cascade delete |
| `config_version` | `INTEGER` | Not null, defaults to `1` |
| `pid` | `INTEGER` | Not null |
| `arrival_time` | `INTEGER` | Not null, at least `0` |
| `cpu_burst` | `INTEGER` | Not null, greater than `0` |
| `priority` | `INTEGER` | Not null |
| `process_name` | `TEXT` | Not null, defaults to an empty string |
| `description` | `TEXT` | Not null, defaults to an empty string |
| `memory_demand` | `INTEGER` | Nullable; if present, greater than `0` |

The `(session_id, config_version, pid)` tuple is unique.

### `simulation_events`

Stores timestamped simulation event records.

| Column | Type | Constraints |
|---|---|---|
| `id` | `INTEGER` | Auto-increment primary key |
| `run_id` | `TEXT` | Not null correlation identifier |
| `timestamp` | `INTEGER` | Not null, at least `0` |
| `event_type` | `TEXT` | Not null |
| `entity_id` | `INTEGER` | Nullable |
| `details` | `TEXT` | Nullable |

### `process_state_history`

Stores process state transitions.

| Column | Type | Constraints |
|---|---|---|
| `id` | `INTEGER` | Auto-increment primary key |
| `run_id` | `TEXT` | Not null correlation identifier |
| `pid` | `INTEGER` | Not null |
| `timestamp` | `INTEGER` | Not null, at least `0` |
| `from_state` | `TEXT` | Not null |
| `to_state` | `TEXT` | Not null |
| `reason` | `TEXT` | Nullable |

### `reports`

Stores the file path and format for each generated report.

| Column | Type | Constraints |
|---|---|---|
| `report_id` | `TEXT` | Primary key |
| `session_id` | `TEXT` | Not null; references `sessions` with cascade delete |
| `run_id` | `TEXT` | Not null correlation identifier |
| `report_format` | `TEXT` | Not null |
| `export_path` | `TEXT` | Not null |
| `generated_at` | `TEXT` | Not null |

### `deadlock_results`

Stores deadlock detection and safe-state results.

| Column | Type | Constraints |
|---|---|---|
| `id` | `INTEGER` | Auto-increment primary key |
| `run_id` | `TEXT` | Not null correlation identifier |
| `is_deadlocked` | `INTEGER` | Not null, defaults to `0` |
| `is_safe` | `INTEGER` | Not null, defaults to `1` |
| `safe_sequence` | `TEXT` | Nullable |
| `deadlocked_processes` | `TEXT` | Nullable |
| `frequency_trials` | `INTEGER` | Defaults to `0`, at least `0` |
| `frequency_deadlocks` | `INTEGER` | Defaults to `0`, at least `0` |
| `empirical_frequency` | `REAL` | Defaults to `0`; constrained to `[0, 1]` |

### `access_policies`

Stores session-specific role/resource permissions.

| Column | Type | Constraints |
|---|---|---|
| `id` | `INTEGER` | Auto-increment primary key |
| `session_id` | `TEXT` | Not null; references `sessions` with cascade delete |
| `user_or_role` | `TEXT` | Not null |
| `resource_name` | `TEXT` | Not null |
| `operation` | `TEXT` | Not null; uppercase `READ`, `WRITE`, or `EXECUTE` |
| `policy` | `TEXT` | Not null; uppercase `ALLOW` or `DENY`, defaults to `ALLOW` |

Rules are unique per session, identity, resource, and operation. Re-evaluating
a rule updates its policy rather than inserting a duplicate.

### `scheduling_results`

Stores per-process CPU scheduling metrics.

| Column | Type | Constraints |
|---|---|---|
| `id` | `INTEGER` | Auto-increment primary key |
| `run_id` | `TEXT` | Not null correlation identifier |
| `pid` | `INTEGER` | Not null |
| `arrival_time` | `INTEGER` | Not null, at least `0` |
| `cpu_burst` | `INTEGER` | Not null, greater than `0` |
| `priority` | `INTEGER` | Not null |
| `completion_time` | `INTEGER` | Not null, at least `arrival_time` |
| `turnaround_time` | `INTEGER` | Not null, at least `cpu_burst` |
| `waiting_time` | `INTEGER` | Not null, at least `0` |
| `response_time` | `INTEGER` | Not null, at least `0` |

## Removed persistence

Configuration-version registries, run registries, Gantt segments, memory and
disk result tables, checkpoints, and operational event logs are not persisted
by the eight-table schema. Their algorithms and interactive output remain
available in the kernel; only the listed relational records are stored.
