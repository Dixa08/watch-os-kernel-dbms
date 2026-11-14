#!/usr/bin/env python3
"""Unified launcher for the WATCH OS Wokwi and DBMS presentation tools."""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent
OPTIONS = {
    "1": ("Start Wokwi Live Sync & Report Exporter", ("sync_wokwi_local.py",)),
    "2": ("Show All 25 Normalized Database Tables", ("show_tables.py",)),
    "3": ("Run All DBMS Verification Queries", ("dbms_queries.py", "--verify-all")),
    "4": ("Open Interactive SQL Query & Live UPDATE/DELETE Menu", ("dbms_queries.py",)),
}


def run_tool(arguments: tuple[str, ...]) -> None:
    command = [sys.executable, str(ROOT / arguments[0]), *arguments[1:]]
    try:
        completed = subprocess.run(command, cwd=ROOT, check=False)
    except OSError as exc:
        print(f"Could not launch {arguments[0]}: {exc}")
        return
    if completed.returncode:
        print(f"{arguments[0]} exited with status {completed.returncode}.")


def main() -> int:
    while True:
        print(
            "\nWATCH OS + DBMS Viva Launcher\n"
            "[1] Start Wokwi Live Sync & Report Exporter\n"
            "[2] Show All 25 Normalized Database Tables\n"
            "[3] Run All DBMS Verification Queries\n"
            "[4] Open Interactive SQL Query & Live UPDATE/DELETE Menu\n"
            "[0] Exit"
        )
        try:
            choice = input("Select an option: ").strip()
        except (EOFError, KeyboardInterrupt):
            print()
            return 0

        if choice == "0":
            return 0
        option = OPTIONS.get(choice)
        if option is None:
            print("Please select an option from 0 to 4.")
            continue

        print(f"\nStarting: {option[0]}")
        run_tool(option[1])


if __name__ == "__main__":
    raise SystemExit(main())
