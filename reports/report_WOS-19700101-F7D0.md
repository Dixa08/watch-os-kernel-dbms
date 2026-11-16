# WATCH OS — SIMULATION REPORT

**Session ID:** `WOS-19700101-F7D0`  
**Run ID:** `RUN-000001`  
**Generated At:** 1970-01-01 00:01:07 UTC  
**Simulation Status:** COMPLETED  

---

## 1. Executive Summary
A full-system operating simulator analysis was conducted on workload of 3 configured processes. Subsystems evaluated include CPU Scheduling, Virtual Memory, Deadlock/Resource Allocation, Disk Arm Motion, and Access Protection.

## 2. Workload Configuration
| PID | Arrival Time | CPU Burst | Priority | Memory Demand |
| :--- | :--- | :--- | :--- | :--- |
| 2 | 1 | 5 | 1 | Default (32 units) |
| 4 | 2 | 3 | 4 | Default (32 units) |
| 32 | 3 | 5 | 4 | Default (32 units) |

## 3. CPU Scheduling Results
| Algorithm | Avg Waiting Time | Avg Turnaround Time | Avg Response Time | Context Switches | CPU Utilization |
| :--- | :--- | :--- | :--- | :--- | :--- |
| FCFS | 3.33 | 7.67 | 3.33 | 2 | 92.86% |
| SJF | 3.33 | 7.67 | 3.33 | 2 | 92.86% |
| SRTF | 3.00 | 7.33 | 2.00 | 3 | 92.86% |
| Priority (Non-Preemptive) | 3.33 | 7.67 | 3.33 | 2 | 92.86% |
| Priority (Preemptive) | 3.33 | 7.67 | 3.33 | 2 | 92.86% |
| Round Robin (Q=2) | 6.00 | 10.33 | 1.00 | 7 | 92.86% |

## 4. Gantt Charts
```text
FCFS Gantt Chart:
| IDLE |  P2  |  P4  | P32  |
0      1      6      9     14

SJF Gantt Chart:
| IDLE |  P2  |  P4  | P32  |
0      1      6      9     14

SRTF Gantt Chart:
| IDLE |  P2  |  P4  |  P2  | P32  |
0      1      2      5      9     14

Priority (Non-Preemptive) Gantt Chart:
| IDLE |  P2  |  P4  | P32  |
0      1      6      9     14

Priority (Preemptive) Gantt Chart:
| IDLE |  P2  |  P4  | P32  |
0      1      6      9     14

Round Robin (Q=2) Gantt Chart:
| IDLE |  P2  |  P4  | P32  |  P2  |  P4  | P32  |  P2  | P32  |
0      1      3      5      7      9     10     12     13     14

```

## 5. Memory Management & Page Replacement
| Algorithm | Total References | Page Faults | Page Hits | Fault Ratio | Thrashing Risk |
| :--- | :--- | :--- | :--- | :--- | :--- |
| FIFO | 45 | 17 | 28 | 37.78% | NORMAL |
| LRU | 45 | 12 | 33 | 26.67% | NORMAL |
| Optimal | 45 | 11 | 34 | 24.44% | NORMAL |

## 6. Deadlock & Banker's Algorithm Safe State
- **Banker's Algorithm State:** SAFE
- **Details:** Safe state verified. A valid safe execution sequence exists: <P2, P4, P32>
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
