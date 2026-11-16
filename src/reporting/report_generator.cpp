#include "watchos/reporting/report_generator.hpp"
#include "watchos/scheduler/gantt_chart.hpp"
#include <cctype>
#include <fstream>
#include <sstream>
#include <iomanip>

#if defined(ARDUINO) || defined(ESP32)
#include <Arduino.h>
#endif

namespace watchos {
namespace reporting {

namespace {

std::string escapeCsvField(const std::string& value) {
    if (value.find_first_of(",\"\r\n") == std::string::npos) {
        return value;
    }

    std::string escaped = "\"";
    for (char character : value) {
        if (character == '"') {
            escaped += "\"\"";
        } else {
            escaped += character;
        }
    }
    escaped += '"';
    return escaped;
}

void writeCsvRow(
    std::ostream& output,
    const std::string& section,
    const std::string& algorithm,
    const std::string& metric,
    const std::string& value
) {
    output << escapeCsvField(section) << ','
           << escapeCsvField(algorithm) << ','
           << escapeCsvField(metric) << ','
           << escapeCsvField(value) << '\n';
}

template <typename Value>
void writeCsvMetric(
    std::ostream& output,
    const std::string& section,
    const std::string& algorithm,
    const std::string& metric,
    const Value& value
) {
    std::ostringstream valueStream;
    valueStream << value;
    writeCsvRow(output, section, algorithm, metric, valueStream.str());
}

template <typename Value>
std::string joinValues(const std::vector<Value>& values) {
    std::ostringstream joined;
    for (size_t index = 0; index < values.size(); ++index) {
        if (index != 0) {
            joined << ';';
        }
        joined << values[index];
    }
    return joined.str();
}

std::string stripLineIndentation(const std::string& content) {
    std::string cleaned;
    cleaned.reserve(content.size());
    size_t lineStart = 0;
    while (lineStart < content.size()) {
        const size_t lineEnd = content.find('\n', lineStart);
        const size_t end = lineEnd == std::string::npos ? content.size() : lineEnd;
        size_t first = lineStart;
        while (first < end) {
            const unsigned char character = static_cast<unsigned char>(content[first]);
            if (std::isspace(character) != 0) {
                ++first;
            } else if (first + 1 < end
                && character == 0xC2
                && static_cast<unsigned char>(content[first + 1]) == 0xA0) {
                first += 2;
            } else {
                break;
            }
        }
        cleaned.append(content, first, end - first);
        if (lineEnd == std::string::npos) {
            break;
        }
        cleaned.push_back('\n');
        lineStart = lineEnd + 1;
    }
    return cleaned;
}

} // namespace

std::string ReportGenerator::generateMarkdownReport(const CompleteReportData& data) {
    std::ostringstream oss;
    oss << "# WATCH OS — SIMULATION REPORT\n\n";
    oss << "**Session ID:** `" << data.sessionId << "`  \n";
    oss << "**Run ID:** `" << data.runId << "`  \n";
    oss << "**Generated At:** " << data.generatedAt << "  \n";
    oss << "**Simulation Status:** COMPLETED  \n\n";
    oss << "---\n\n";

    oss << "## 1. Executive Summary\n";
    oss << "A full-system operating simulator analysis was conducted on workload of "
        << data.processes.size() << " configured processes. Subsystems evaluated include CPU Scheduling, Virtual Memory, Deadlock/Resource Allocation, Disk Arm Motion, and Access Protection.\n\n";

    oss << "## 2. Workload Configuration\n";
    oss << "| PID | Arrival Time | CPU Burst | Priority | Memory Demand |\n";
    oss << "| :--- | :--- | :--- | :--- | :--- |\n";
    for (const auto& p : data.processes) {
        std::string memStr = p.getMemoryConfig().isConfigured ? (std::to_string(p.getMemoryConfig().demandUnits) + " units") : "Default (32 units)";
        oss << "| " << p.getPid() << " | " << p.getArrivalTime() << " | " << p.getCpuBurst() << " | " << p.getPriority() << " | " << memStr << " |\n";
    }
    oss << "\n";

    oss << "## 3. CPU Scheduling Results\n";
    oss << "| Algorithm | Avg Waiting Time | Avg Turnaround Time | Avg Response Time | Context Switches | CPU Utilization |\n";
    oss << "| :--- | :--- | :--- | :--- | :--- | :--- |\n";
    for (const auto& m : data.schedulingMetrics) {
        oss << "| " << m.algorithmName << " | "
            << std::fixed << std::setprecision(2) << m.avgWaitingTime << " | "
            << m.avgTurnaroundTime << " | "
            << m.avgResponseTime << " | "
            << m.totalContextSwitches << " | "
            << m.cpuUtilization << "% |\n";
    }
    oss << "\n";

    oss << "## 4. Gantt Charts\n```text\n";
    for (const auto& m : data.schedulingMetrics) {
        oss << scheduler::GanttChart::render(m.algorithmName, m.ganttSegments);
    }
    oss << "```\n\n";

    oss << "## 5. Memory Management & Page Replacement\n";
    oss << "| Algorithm | Total References | Page Faults | Page Hits | Fault Ratio | Thrashing Risk |\n";
    oss << "| :--- | :--- | :--- | :--- | :--- | :--- |\n";
    for (const auto& mem : data.memoryResults) {
        oss << "| " << mem.algorithmName << " | "
            << mem.totalReferences << " | "
            << mem.pageFaults << " | "
            << mem.pageHits << " | "
            << std::fixed << std::setprecision(2) << (mem.faultRatio * 100.0) << "% | "
            << (mem.thrashingDetected ? "HIGH" : "NORMAL") << " |\n";
    }
    oss << "\n";

    oss << "## 6. Deadlock & Banker's Algorithm Safe State\n";
    oss << "- **Banker's Algorithm State:** " << (data.bankersResult.isSafe ? "SAFE" : "UNSAFE") << "\n";
    oss << "- **Details:** " << data.bankersResult.explanation << "\n";
    oss << "- **Deadlock Detection State:** " << (data.deadlockResult.isDeadlocked ? "DEADLOCK PRESENT" : "DEADLOCK FREE") << "\n\n";

    oss << "## 7. Disk Head Scheduling\n";
    oss << "- **Algorithm:** " << data.diskResult.algorithmName << "\n";
    oss << "- **Initial Head Position:** " << data.diskResult.initialHead << "\n";
    oss << "- **Total Head Movement:** " << data.diskResult.totalHeadMovement << " cylinders\n";
    oss << "- **Average Movement:** " << std::fixed << std::setprecision(2) << data.diskResult.averageHeadMovement << " cylinders/request\n\n";

    oss << "## 8. Key Findings & Trade-Offs\n";
    oss << "1. Shortest remaining time / SJF algorithms optimize average waiting times at the cost of preemption overhead.\n";
    oss << "2. Round Robin provides bounded response time ideal for interactive systems.\n";
    oss << "3. Optimal page replacement establishes the theoretical minimum fault ceiling for comparison against FIFO and LRU.\n";
    oss << "4. Banker's state checks prevent resource allocation deadlocks.\n\n";

    oss << "## 9. Limitations & Assumptions\n";
    oss << "- Simulates user-space OS algorithms under controlled workloads.\n";
    oss << "- Integer simulated clock time units represent idealized execution cycles.\n";
    oss << "- Access control rules and memory protection boundaries are educational and separate from host OS security.\n";

    return stripLineIndentation(oss.str());
}

std::string ReportGenerator::generateCsvReport(const CompleteReportData& data) {
    std::ostringstream oss;
    oss << "# Session ID: " << data.sessionId << "\n";
    oss << "# Run ID: " << data.runId << "\n";
    oss << "# Generated At: " << data.generatedAt << "\n";
    oss << "Section,Algorithm,Metric,Value\n";

    writeCsvRow(oss, "Session", "Metadata", "Description", data.sessionDescription);
    writeCsvMetric(oss, "Session", "Metadata", "ProcessCount", data.processes.size());
    writeCsvMetric(oss, "Configuration", "System", "DefaultTimeQuantum", data.settings.defaultTimeQuantum);
    writeCsvMetric(oss, "Configuration", "System", "DefaultMemoryFrames", data.settings.defaultMemoryFrames);
    writeCsvMetric(oss, "Configuration", "System", "AgingInterval", data.settings.agingInterval);
    writeCsvMetric(oss, "Configuration", "System", "AgingIncrement", data.settings.agingIncrement);
    writeCsvMetric(oss, "Configuration", "System", "ContextSwitchOverhead", data.settings.contextSwitchOverhead);
    writeCsvMetric(oss, "Configuration", "System", "EnableAnsiColor", data.settings.enableAnsiColor ? "true" : "false");
    writeCsvRow(oss, "Configuration", "System", "ReportFormat", data.settings.reportFormat);

    for (const auto& process : data.processes) {
        const std::string processName = "Process " + std::to_string(process.getPid());
        writeCsvRow(oss, "Input", processName, "ProcessName", process.getName());
        writeCsvRow(oss, "Input", processName, "ProcessDescription", process.getDescription());
        writeCsvMetric(oss, "Input", processName, "ArrivalTime", process.getArrivalTime());
        writeCsvMetric(oss, "Input", processName, "CpuBurst", process.getCpuBurst());
        writeCsvMetric(oss, "Input", processName, "Priority", process.getPriority());

        const auto& memory = process.getMemoryConfig();
        writeCsvMetric(oss, "Input", processName, "MemoryConfigured", memory.isConfigured ? "true" : "false");
        writeCsvMetric(oss, "Input", processName, "MemoryDemandUnits", memory.demandUnits);
        writeCsvRow(oss, "Input", processName, "PageReferences", joinValues(memory.pageReferences));

        for (size_t index = 0; index < process.getIoRequests().size(); ++index) {
            const auto& request = process.getIoRequests()[index];
            const std::string prefix = "IoRequest[" + std::to_string(index + 1) + "].";
            writeCsvMetric(oss, "Input", processName, prefix + "RequestTime", request.requestTime);
            writeCsvMetric(oss, "Input", processName, prefix + "Duration", request.duration);
            writeCsvRow(oss, "Input", processName, prefix + "Device", request.device);
        }

        for (size_t index = 0; index < process.getResourceClaims().size(); ++index) {
            const auto& claim = process.getResourceClaims()[index];
            const std::string prefix = "ResourceClaim[" + std::to_string(index + 1) + "].";
            writeCsvRow(oss, "Input", processName, prefix + "ResourceType", claim.resourceType);
            writeCsvMetric(oss, "Input", processName, prefix + "MaxClaim", claim.maxClaim);
            writeCsvMetric(oss, "Input", processName, prefix + "InitialAllocation", claim.initialAllocation);
        }

        for (size_t index = 0; index < process.getDiskRequests().size(); ++index) {
            const auto& request = process.getDiskRequests()[index];
            const std::string prefix = "DiskRequest[" + std::to_string(index + 1) + "].";
            writeCsvMetric(oss, "Input", processName, prefix + "BlockOrCylinder", request.blockOrCylinder);
            writeCsvRow(oss, "Input", processName, prefix + "OperationType", request.operationType);
            writeCsvMetric(oss, "Input", processName, prefix + "DataVolumeBytes", request.dataVolumeBytes);
        }

        for (size_t index = 0; index < process.getSyncRequirements().size(); ++index) {
            const auto& requirement = process.getSyncRequirements()[index];
            const std::string prefix = "SyncRequirement[" + std::to_string(index + 1) + "].";
            writeCsvRow(oss, "Input", processName, prefix + "PrimitiveName", requirement.primitiveName);
            writeCsvMetric(oss, "Input", processName, prefix + "StartTime", requirement.csStartTime);
            writeCsvMetric(oss, "Input", processName, prefix + "Duration", requirement.csDuration);
        }

        const auto& access = process.getAccessConfig();
        writeCsvMetric(oss, "Input", processName, "AccessConfigured", access.isConfigured ? "true" : "false");
        writeCsvRow(oss, "Input", processName, "AccessUserOrRole", access.userOrRole);
        writeCsvRow(oss, "Input", processName, "AccessResourceName", access.resourceName);
        writeCsvRow(oss, "Input", processName, "AccessRequestedOperation", access.requestedOperation);
    }

    for (const auto& m : data.schedulingMetrics) {
        writeCsvMetric(oss, "CPU", m.algorithmName, "AvgWaitingTime", m.avgWaitingTime);
        writeCsvMetric(oss, "CPU", m.algorithmName, "AvgTurnaroundTime", m.avgTurnaroundTime);
        writeCsvMetric(oss, "CPU", m.algorithmName, "AvgResponseTime", m.avgResponseTime);
        writeCsvMetric(oss, "CPU", m.algorithmName, "Throughput", m.throughput);
        writeCsvMetric(oss, "CPU", m.algorithmName, "ContextSwitches", m.totalContextSwitches);
        writeCsvMetric(oss, "CPU", m.algorithmName, "CpuUtilization", m.cpuUtilization);
        writeCsvMetric(oss, "CPU", m.algorithmName, "TotalElapsedTime", m.totalElapsedTime);
        writeCsvMetric(oss, "CPU", m.algorithmName, "TotalBusyTime", m.totalBusyTime);
        writeCsvMetric(oss, "CPU", m.algorithmName, "TotalIdleTime", m.totalIdleTime);
        writeCsvMetric(oss, "CPU", m.algorithmName, "StarvationBreaches", m.starvationBreaches);
        writeCsvMetric(oss, "CPU", m.algorithmName, "MaxWaitingTime", m.maxWaitingTime);

        for (const auto& result : m.processResults) {
            const std::string processName = "Process " + std::to_string(result.pid);
            writeCsvMetric(oss, "CPU Process Result", m.algorithmName, processName + ".FirstStartTime", result.firstStartTime);
            writeCsvMetric(oss, "CPU Process Result", m.algorithmName, processName + ".CompletionTime", result.completionTime);
            writeCsvMetric(oss, "CPU Process Result", m.algorithmName, processName + ".TurnaroundTime", result.turnaroundTime);
            writeCsvMetric(oss, "CPU Process Result", m.algorithmName, processName + ".WaitingTime", result.waitingTime);
            writeCsvMetric(oss, "CPU Process Result", m.algorithmName, processName + ".ResponseTime", result.responseTime);
            writeCsvMetric(oss, "CPU Process Result", m.algorithmName, processName + ".ContextSwitches", result.contextSwitches);
        }

        for (size_t index = 0; index < m.ganttSegments.size(); ++index) {
            const auto& segment = m.ganttSegments[index];
            const std::string prefix = "Segment[" + std::to_string(index + 1) + "].";
            writeCsvMetric(oss, "CPU Gantt", m.algorithmName, prefix + "StartTime", segment.startTime);
            writeCsvMetric(oss, "CPU Gantt", m.algorithmName, prefix + "EndTime", segment.endTime);
            writeCsvMetric(oss, "CPU Gantt", m.algorithmName, prefix + "ProcessId", segment.pid);
            writeCsvMetric(oss, "CPU Gantt", m.algorithmName, prefix + "IsIdle", segment.isIdle ? "true" : "false");
            writeCsvMetric(oss, "CPU Gantt", m.algorithmName, prefix + "IsContextSwitch", segment.isContextSwitch ? "true" : "false");
        }
    }
    for (const auto& mem : data.memoryResults) {
        writeCsvMetric(oss, "Memory", mem.algorithmName, "FrameCount", mem.frameCount);
        writeCsvMetric(oss, "Memory", mem.algorithmName, "TotalReferences", mem.totalReferences);
        writeCsvMetric(oss, "Memory", mem.algorithmName, "PageFaults", mem.pageFaults);
        writeCsvMetric(oss, "Memory", mem.algorithmName, "PageHits", mem.pageHits);
        writeCsvMetric(oss, "Memory", mem.algorithmName, "Replacements", mem.replacements);
        writeCsvMetric(oss, "Memory", mem.algorithmName, "FaultRatio", mem.faultRatio);
        writeCsvMetric(oss, "Memory", mem.algorithmName, "ThrashingDetected", mem.thrashingDetected ? "true" : "false");
    }

    writeCsvMetric(oss, "Deadlock", "Bankers", "IsSafe", data.bankersResult.isSafe ? "true" : "false");
    writeCsvRow(oss, "Deadlock", "Bankers", "Explanation", data.bankersResult.explanation);
    writeCsvRow(oss, "Deadlock", "Bankers", "SafeSequence", joinValues(data.bankersResult.safeSequence));
    for (size_t index = 0; index < data.bankersResult.resourceNames.size(); ++index) {
        const std::string resourceName = data.bankersResult.resourceNames[index];
        writeCsvRow(oss, "Deadlock", "Bankers", "Resource[" + std::to_string(index + 1) + "].Name", resourceName);
        if (index < data.bankersResult.availableAtStart.size()) {
            writeCsvMetric(oss, "Deadlock", "Bankers", "Resource[" + std::to_string(index + 1) + "].AvailableAtStart", data.bankersResult.availableAtStart[index]);
        }
    }
    writeCsvMetric(oss, "Deadlock", "Detection", "IsDeadlocked", data.deadlockResult.isDeadlocked ? "true" : "false");
    writeCsvRow(oss, "Deadlock", "Detection", "DeadlockedProcesses", joinValues(data.deadlockResult.deadlockedProcesses));
    writeCsvRow(oss, "Deadlock", "Detection", "Report", data.deadlockResult.report);

    writeCsvMetric(oss, "Disk", data.diskResult.algorithmName, "InitialHead", data.diskResult.initialHead);
    writeCsvMetric(oss, "Disk", data.diskResult.algorithmName, "MinCylinder", data.diskResult.minCylinder);
    writeCsvMetric(oss, "Disk", data.diskResult.algorithmName, "MaxCylinder", data.diskResult.maxCylinder);
    writeCsvRow(oss, "Disk", data.diskResult.algorithmName, "ServiceOrder", joinValues(data.diskResult.serviceOrder));
    writeCsvMetric(oss, "Disk", data.diskResult.algorithmName, "TotalHeadMovement", data.diskResult.totalHeadMovement);
    writeCsvMetric(oss, "Disk", data.diskResult.algorithmName, "AverageHeadMovement", data.diskResult.averageHeadMovement);
    writeCsvRow(oss, "Disk", data.diskResult.algorithmName, "Direction", data.diskResult.direction);

    for (size_t index = 0; index < data.auditLogs.size(); ++index) {
        writeCsvRow(oss, "Audit", "Log", "Entry[" + std::to_string(index + 1) + "]", data.auditLogs[index]);
    }

    return stripLineIndentation(oss.str());
}

bool ReportGenerator::exportToFile(const std::string& filePath, const std::string& content) {
    const std::string cleanedContent = stripLineIndentation(content);
#if defined(ARDUINO) || defined(ESP32)
    if (filePath.compare(0, 8, "reports/") != 0
        || (filePath.size() < 3
            || (filePath.compare(filePath.size() - 3, 3, ".md") != 0
                && filePath.compare(filePath.size() - 4, 4, ".csv") != 0))) {
        return false;
    }

    Serial.println(("<<<FILE_EXPORT_START:" + filePath + ">>>").c_str());
    Serial.print(cleanedContent.c_str());
    Serial.println("<<<FILE_EXPORT_END>>>");
    return true;
#else
    std::ofstream out(filePath);
    if (!out.is_open()) return false;
    out << cleanedContent;
    return out.good();
#endif
}

} // namespace reporting
} // namespace watchos
