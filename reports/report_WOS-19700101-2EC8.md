# WATCH OS — SIMULATION REPORT

**Session ID:** `WOS-19700101-2EC8`  
**Run ID:** `RUN-000002`  
**Generated At:** 1970-01-01 00:02:17 UTC  
**Simulation Status:** COMPLETED  

---

## 1. Executive Summary
A full-system operating simulator analysis was conducted on workload of 3 configured processes. Subsystems evaluated include CPU Scheduling, Virtual Memory, Deadlock/Resource Allocation, Disk Arm Motion, and Access Protection.

## 2. Workload Configuration
| PID | Arrival Time | CPU Burst | Priority | Memory Demand |
| :--- | :--- | :--- | :--- | :--- |
| 2 | 2 | 3 | 1 | 64 units |
| 12 | 3 | 2 | 1 | Default (32 units) |
| 3 | 4 | 5 | 1 | Default (32 units) |

## 3. CPU Scheduling Results
| Algorithm | Avg Waiting Time | Avg Turnaround Time | Avg Response Time | Context Switches | CPU Utilization |
| :--- | :--- | :--- | :--- | :--- | :--- |
| FCFS | 1.67 | 5.00 | 1.67 | 2 | 83.33% |
| SJF | 1.67 | 5.00 | 1.67 | 2 | 83.33% |
| SRTF | 1.67 | 5.00 | 1.67 | 2 | 83.33% |
| Priority (Non-Preemptive) | 1.67 | 5.00 | 1.67 | 2 | 83.33% |
| Priority (Preemptive) | 1.67 | 5.00 | 1.67 | 2 | 83.33% |
| Round Robin (Q=2) | 2.67 | 6.00 | 1.00 | 4 | 83.33% |

## 4. Gantt Charts
```text
FCFS Gantt Chart:
| IDLE |  P2  | P12  |  P3  |
0      2      5      7     12

SJF Gantt Chart:
| IDLE |  P2  | P12  |  P3  |
0      2      5      7     12

SRTF Gantt Chart:
| IDLE |  P2  | P12  |  P3  |
0      2      5      7     12

Priority (Non-Preemptive) Gantt Chart:
| IDLE |  P2  | P12  |  P3  |
0      2      5      7     12

Priority (Preemptive) Gantt Chart:
| IDLE |  P2  | P12  |  P3  |
0      2      5      7     12

Round Robin (Q=2) Gantt Chart:
| IDLE |  P2  | P12  |  P3  |  P2  |  P3  |  P3  |
0      2      4      6      8      9     11     12

```

## 5. Memory Management & Page Replacement
| Algorithm | Total References | Page Faults | Page Hits | Fault Ratio | Thrashing Risk |
| :--- | :--- | :--- | :--- | :--- | :--- |
| FIFO | 45 | 22 | 23 | 48.89% | NORMAL |
| LRU | 45 | 19 | 26 | 42.22% | NORMAL |
| Optimal | 45 | 15 | 30 | 33.33% | NORMAL |

## 6. Deadlock & Banker's Algorithm Safe State
- **Banker's Algorithm State:** SAFE
- **Details:** Safe state verified. A valid safe execution sequence exists: <P2, P12, P3>
- **Deadlock Detection State:** DEADLOCK FREE

## 7. Disk Head Scheduling
- **Algorithm:** SSTF
- **Initial Head Position:** 53
- **Total Head Movement:** 236 cylinders
- **Average Movement:** 29.50 cylinders/request

## 8. Key Findings & Trade-Offs
1. Shortest remaining time / SJF algorithms optimize average waiting times at the cost of preemption overhead.
2. Round Robin provides bounded response time ideal for interactive systems.
3. Optimal page replacement establishes the theoretical minimum fault ceiling for comparison against FIFO and LRU.
4. Banker's state checks prevent resource allocation deadlocks.

## 9. Limitations & Assumptions
- Simulates user-space OS algorithms under controlled workloads.
- Integer simulated clock time units represent idealized execution cycles.
- Access control rules and memory protection boundaries are educational and separate from host OS security.
