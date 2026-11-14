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
All 9 test suites will execute:
1. `TestIdGenerator`
2. `TestWorkloadValidator`
3. `TestDbPersistence`
4. `TestCpuSchedulers`
5. `TestDeadlockBankers`
6. `TestMemoryPaging`
7. `TestDiskScheduling`
8. `TestSessionLifecycle`
9. `TestCompleteKernelAnalysis`

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