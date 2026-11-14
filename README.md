# WATCH OS — Hybrid Embedded OS Kernel & Relational DBMS Simulation Engine

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://isocpp.org/)
[![ESP32](https://img.shields.io/badge/Hardware-ESP32%20%2F%20Wokwi-red.svg)](https://wokwi.com/)
[![CMake](https://img.shields.io/badge/CMake-3.15%2B-orange.svg)](https://cmake.org/)
[![PlatformIO](https://img.shields.io/badge/Build-PlatformIO-orange.svg)](https://platformio.org/)
[![Database](https://img.shields.io/badge/Database-SQLite%203%20(25%20Tables)-lightgrey.svg)](https://www.sqlite.org/)
[![Python](https://img.shields.io/badge/DBMS%20Bridge-Python%203-yellow.svg)](https://www.python.org/)
[![Status](https://img.shields.io/badge/OS%20%2B%20DBMS%20PBL-Complete-brightgreen.svg)]()

> A complete, modular operating system simulator and relational DBMS analysis engine built in **C++17**, **ESP32 (FreeRTOS / Wokwi)**, **CMake**, **SQLite 3**, and **Python**. Configure workloads once, simulate kernel subsystems with live **ILI9341 TFT** hardware visualization, persist telemetry across restarts into a **25-table normalized SQLite database**, and generate comprehensive Markdown and CSV reports.

---

## Capabilities Overview

### 1. CPU Scheduling Engine & Analytics
- **Algorithms**: FCFS, Shortest Job First (SJF), Shortest Remaining Time First (SRTF), Priority (both Preemptive and Non-Preemptive modes), and Round Robin (configurable quantum).
- **Comprehensive Metrics**: Turnaround Time ($TAT = CT - AT$), Waiting Time ($WT = TAT - BT$), Response Time ($RT = FST - AT$), Throughput, CPU Utilization, and Context Switches.
- **Visual & ASCII Gantt Charts**: Rendered in the serial terminal and plotted as color-coded execution blocks (`GANTT: <algorithm>`) on the 320×240 ILI9341 TFT display.
- **Recommendation Engine**: Multi-criteria weighted normalization ranking the optimal scheduling algorithm for the active workload.
- **Starvation & Aging**: Starvation detection threshold analysis and dynamic priority aging before/after comparison.

### 2. Virtual Memory Management
- **Page Replacement Algorithms**: FIFO, Least Recently Used (LRU), and Optimal Page Replacement.
- **Metrics**: Page references, page hits, page faults, fault ratio, frame allocations, and thrashing indicators.
- **Memory Modeling**: Locality of reference simulation and page table mappings.

### 3. Deadlock Analysis & Banker's Algorithm
- **Resource Allocation Graph (RAG)**: Formatted terminal graph representation of process-to-resource allocation and request edges.
- **Banker's Algorithm**: Safe state validation, remaining need calculation, and safe sequence determination.
- **Deadlock Detection**: Multiple-instance resource reduction identifying actual deadlocks.
- **Monte Carlo Risk Estimation**: 1,000 stochastic trials computing empirical deadlock frequency and per-process involvement.

### 4. I/O & Disk Scheduling
- **Disk Arm Algorithms**: FCFS, Shortest Seek Time First (SSTF), SCAN (Elevator with configurable direction), and C-SCAN.
- **Metrics**: Total head movement, average seek time, and service sequence.
- **I/O Queue Management**: Process blocking on I/O operations and latency measurement.

### 5. Process Synchronization & Access Protection
- **Primitives**: Mutex locks, counting semaphores, bounded IPC message queues, and producer-consumer simulation.
- **Race Condition Detection**: Flags unsynchronized concurrent writes to shared variables.
- **Simulated Access Control**: Role-based permissions (root, user, guest), memory partition boundary protection, and segmentation fault tracking.

### 6. Embedded Hardware Simulation & TFT Dashboard (Wokwi + ESP32)
- **Simulated Peripherals (`diagram.json`)**: ESP32 wired to a **320×240 ILI9341 SPI TFT Display**, status LEDs, and hardware pushbuttons (START button on GPIO 32).
- **Multi-View TFT Dashboard (`TftDashboard`)**: Press the **START** button to cycle through three live hardware views:
  1. **Live Serial Terminal Mirror**: Streams ANSI-stripped CLI menus, process inputs, and subsystem tables directly onto the TFT screen.
  2. **Visual Gantt Chart**: Displays color-coded process blocks and execution timestamps for the top-ranked or selected scheduler.
  3. **Kernel Summary**: Shows active session state, workload count, and live system telemetry.

### 7. Persistence, Relational DBMS & Reports
- **Embedded SQLite 3 (25 Normalized Tables)**: Complete ACID transactional persistence with foreign key enforcement (`PRAGMA foreign_keys = ON;`) stored in `data/watchos_sessions.db`.
- **Live Wokwi Serial Bridge (`sync_wokwi_local.py`)**: Connects to Wokwi RFC2217 serial (`localhost:4000`), automatically exports Markdown (`reports/report_<session>.md`) and CSV (`reports/report_<session>.csv`) reports, and commits session records into SQLite.
- **DBMS Query & Verification Suite (`dbms_queries.py`, `show_tables.py`, `demo.py`)**: Interactive SQL runner and `--verify-all` suite supporting multi-table `JOIN`s, `GROUP BY` aggregations, schema audits, and guarded `UPDATE`/`DELETE` operations.
- **JARVIS Explain Mode**: Generates evidence-backed WHY, COMPARE, and SUMMARY explanations grounded in actual recorded events.

---

## Project Structure

```text
KERNEL-MAIN/
├── CMakeLists.txt              # CMake configuration
├── CMakePresets.json           # Presets for building and testing
├── platformio.ini              # ESP32 & Adafruit ILI9341 PlatformIO configuration
├── diagram.json                # Wokwi circuit wiring (ESP32, ILI9341 TFT, LEDs, Buttons)
├── wokwi.toml                  # Wokwi firmware path & RFC2217 serial port (4000)
├── demo.py                     # Unified interactive demo launcher
├── sync_wokwi_local.py         # Wokwi serial-to-SQLite bridge & report exporter
├── dbms_queries.py             # Interactive SQL & --verify-all DBMS query suite
├── show_tables.py              # 25-table schema & live row count inspector
├── README.md                   # Project documentation
├── .gitignore                  # Git ignore rules
│
├── third_party/
│   └── sqlite3/                # Embedded SQLite 3 amalgamation
│
├── include/watchos/
│   ├── hardware/               # TftDashboard (ILI9341 Terminal Mirror + Gantt renderer)
│   ├── core/                   # Event engine, event queue, ID generator, types
│   ├── database/               # DbConnection, Schema, SessionRepository, RunRepository
│   ├── process/                # Process entities, validator, lifecycle simulator
│   ├── session/                # Session manager, configuration manager
│   ├── scheduler/              # FCFS, SJF, SRTF, Priority, RR, Gantt, Recommendation, Aging
│   ├── memory/                 # Page replacement (FIFO, LRU, Optimal), memory manager
│   ├── deadlock/               # Resource manager, Banker's, Deadlock detector, Monte Carlo
│   ├── io/                     # Disk scheduler (FCFS, SSTF, SCAN, CSCAN), I/O manager
│   ├── synchronization/        # Mutex, Semaphore, IPC, Producer-Consumer
│   ├── security/               # Access controller, partition memory protection
│   ├── experiments/            # What-if experiments, fault injector
│   ├── reporting/              # Jarvis explainer, Markdown & CSV report generator
│   └── cli/                    # Console UI, AnsiTerminal, AnalysisMenu, CliApp
│
├── src/                        # Implementation files corresponding to include headers
│   ├── main.cpp                # Application entry point (ESP32 & Native)
│   ├── hardware/
│   ├── core/
│   ├── database/
│   ├── process/
│   ├── session/
│   ├── scheduler/
│   ├── memory/
│   ├── deadlock/
│   ├── io/
│   ├── synchronization/
│   ├── security/
│   ├── experiments/
│   ├── reporting/
│   └── cli/
│
├── tests/
│   ├── unit/                   # Unit test executables (ID, Validator, DB, Schedulers, Deadlock, Memory, Disk)
│   ├── integration/            # Integration tests (Lifecycle restart, complete kernel analysis)
│   ├── regression/             # Regression tests
│   └── fixtures/               # Golden workload test fixtures
│
├── docs/                       # Architecture, database schema, and CLI guides
│   ├── architecture.md
│   ├── database-schema.md
│   ├── DATABASE_DATA_DICTIONARY.md
│   └── cli-guide.md
│
├── data/                       # Default storage for watchos_sessions.db
└── reports/                    # Export directory for generated simulation reports
```

---

## Build and Run Instructions

### Prerequisites
- C++17 compiler (`g++`, `clang++`, or `MSVC`)
- CMake 3.15 or newer
- PlatformIO & VS Code Wokwi Extension (for ESP32 + ILI9341 hardware simulation)
- Python 3.10+ (for live Wokwi-to-SQLite bridge and DBMS query tools)

### Building the Project (Native CMake)
```powershell
cmake -B build -S .
cmake --build build
```

### Running All Test Suites
```powershell
ctest --test-dir build --output-on-failure
```
All 13 test suites declared in `CMakeLists.txt` will execute:
1. `TestIdGenerator`
2. `TestWorkloadValidator`
3. `TestDbPersistence`
4. `TestCpuSchedulers`
5. `TestDeadlockBankers`
6. `TestMemoryPaging`
7. `TestDiskScheduling`
8. `TestSessionLifecycle`
9. `TestCompleteKernelAnalysis`
10. `TestDbSchemaAudit`
11. `TestEdgeCasesSchedulers`
12. `TestEndToEndWorkflows`
13. `TestDiscoveredRegressions`

### Running WATCH OS (Native CLI)
```powershell
.\build\watchos.exe
```
Or specify a custom database:
```powershell
.\build\watchos.exe my_custom_sessions.db
```

### Building & Running ESP32 Hardware Simulation (Wokwi + TFT)
1. Build the ESP32 firmware:
   ```powershell
   pio run -j1
   ```
2. Open `diagram.json` in VS Code with the Wokwi extension and click **Start Simulation**.
3. Start the unified demo launcher in your terminal:
   ```powershell
   python demo.py
   ```
   - Choose **`[1]`** (`sync_wokwi_local.py`) to capture live serial telemetry into `reports/` and `data/watchos_sessions.db`.
   - Press the virtual **START** button (GPIO 32) on the breadboard to activate the kernel and toggle TFT views (**Terminal Mirror → Gantt Chart → Kernel Summary**).

### Inspecting the 25-Table SQLite Database
```powershell
python show_tables.py               # Inspect all 25 normalized tables and row counts
python dbms_queries.py --verify-all # Run automated SQL verification queries
python dbms_queries.py              # Launch interactive SQL & guarded DML console
```

---

## Fresh Laptop Setup & Step-by-Step Run Guide

This section describes a clean Windows setup for the ESP32/Wokwi firmware and
the Python DBMS tools. The repository's Python scripts use SQLite and other
Python standard-library modules; `sync_wokwi_local.py` additionally requires
PySerial to connect to Wokwi's RFC2217 serial server.

### Prerequisites

Install the following software:

| Software | Required for | Notes |
|---|---|---|
| Windows 10/11 and Git | Clone and work with the repository | Install Git for Windows and use PowerShell or a VS Code terminal. |
| Python 3.10 or newer | `demo.py`, `sync_wokwi_local.py`, `dbms_queries.py`, `show_tables.py` | Include `python` and `pip` on PATH. The scripts use `sqlite3` from the Python standard library. |
| Visual Studio Code | Wokwi/PlatformIO IDE workflow | Not required if using PlatformIO Core and another compatible editor from the command line. |
| PlatformIO Core | Build ESP32 firmware | Install it through the PlatformIO IDE extension or install PlatformIO Core separately and make `pio` available on PATH. |
| C++17 toolchain and CMake 3.15+ | Optional native CMake targets/tests | On Windows, the checked-in CMake preset uses MinGW Makefiles, so install MinGW-w64 with `g++` and `mingw32-make` to use that preset. |

Install these VS Code extensions for the integrated firmware/simulation flow:

- **PlatformIO IDE** (`platformio.platformio-ide`) — provides the PlatformIO project environment and firmware build integration.
- **Wokwi Simulator** (`wokwi.wokwi-vscode`) — opens `diagram.json` and runs the ESP32 simulation.

Optional editor extensions:

- **CMake Tools** (`ms-vscode.cmake-tools`) and **C/C++** (`ms-vscode.cpptools`) for CMake configuration, builds, tests, and C++ navigation.
- **Python** (`ms-python.python`) for Python editing and virtual-environment selection. It is not needed to execute the scripts.

No separate SQLite installation is needed for the project database: CMake compiles
the bundled SQLite amalgamation under `third_party/sqlite3`, and Python includes
the `sqlite3` module. No separate Arduino IDE or manually installed ESP32
toolchain/libraries are required for PlatformIO; the first PlatformIO build
downloads the configured Espressif platform, Arduino framework, compiler, and
libraries.

The firmware dependencies declared in `platformio.ini` are:

- `adafruit/Adafruit ILI9341@^1.6.2`
- `adafruit/Adafruit GFX Library@^1.13.0`
- `SPI` is supplied by the ESP32 Arduino framework and is not an extra
  PlatformIO library dependency.

The only third-party Python package is listed in `requirements.txt`:

- `pyserial>=3.5,<4.0` — required by the live serial bridge for
  `rfc2217://localhost:4000`.

### Clone and prepare Python tools

Open PowerShell in the folder where you want the project, then replace
`<repository-url>` with the repository's clone URL:

```powershell
git clone <repository-url>
Set-Location .\Kernel-main

py -3.10 -m venv .venv
.\.venv\Scripts\Activate.ps1
python -m pip install --upgrade pip
python -m pip install -r requirements.txt
```

If PowerShell blocks activation, allow scripts for the current user or invoke
the virtual-environment interpreter directly, for example:
`.\.venv\Scripts\python.exe demo.py`.

### Build the ESP32 firmware

Open the cloned project folder in VS Code (with PlatformIO IDE installed), or
use a terminal where PlatformIO Core's `pio` command is available:

```powershell
pio run -j1
```

The first build may take longer while PlatformIO downloads the Espressif
platform, ESP32 Arduino framework, toolchain, and the Adafruit libraries. The
firmware and ELF files are written to `.pio/build/esp32dev/`; these paths match
the firmware entries in `wokwi.toml`.

### Start Wokwi and capture serial exports

1. Open the project folder in VS Code and start the Wokwi simulation for
   `diagram.json`. The diagram contains the ESP32, ILI9341 TFT, LEDs, and
   buttons; firmware pin assignments are defined in the source.
2. Leave the simulation running so its RFC2217 serial server is available on
   the port configured by `wokwi.toml` (`4000`).
3. In a separate terminal, launch the utilities:

   ```powershell
   python demo.py
   ```

4. Choose **1** to run the live Wokwi serial bridge. It connects to
   `rfc2217://localhost:4000`, saves marked Markdown/CSV exports under
   `reports/`, and imports session data into `data/watchos_sessions.db`.
   Press **Ctrl+C** in that terminal to stop capture.
5. Press the virtual START button (GPIO 32) in Wokwi to launch the CLI. The
   button also cycles the TFT views while a session is active.

The bridge imports existing `reports/report_WOS-*.csv` files when it starts,
so offline CSV exports can be recovered. The `reports/` directory is created
by the bridge if it does not exist. Keep the simulation running while the
bridge is capturing.

### Run the database utilities

Run these commands from the repository root with the Python environment
activated:

```powershell
python show_tables.py
python dbms_queries.py --verify-all
python dbms_queries.py
```

Alternatively, choose **2**, **3**, or **4** in `demo.py` to launch those
utilities. They use the project database at `data/watchos_sessions.db`.

### Optional native CMake workflow

The project also declares CMake targets and CTest tests. With CMake 3.15+ and
a C++17 compiler installed, configure and build with:

```powershell
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The checked-in `CMakePresets.json` selects the `MinGW Makefiles` generator, so
the preset workflow specifically needs MinGW-w64 and `mingw32-make`. The native
CMake target is separate from the ESP32 PlatformIO firmware. In particular,
the current `src/main.cpp` contains ESP32/Arduino entry-point code; if it does
not compile with a native toolchain, use the PlatformIO build and Wokwi steps
above for the supported firmware run rather than treating CMake as a
replacement for the ESP32 build.