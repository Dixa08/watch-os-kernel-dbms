#include "watchos/session/configuration_manager.hpp"
#include "watchos/process/workload_validator.hpp"
#include "watchos/cli/console_ui.hpp"
#include "watchos/security/access_policy.hpp"
#include <iomanip>
#include <limits>
#include <sstream>

#ifdef ARDUINO
#include <Arduino.h>
#define EOL "\r\n"
#else
#define EOL "\n"
#endif

namespace watchos {
namespace session {

#ifdef ARDUINO
namespace {

void writeJsonString(std::ostream& out, const std::string& value) {
    out << '"';
    for (unsigned char ch : value) {
        switch (ch) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (ch < 0x20) {
                    const char hex[] = "0123456789abcdef";
                    out << "\\u00" << hex[ch >> 4] << hex[ch & 0x0f];
                } else {
                    out << static_cast<char>(ch);
                }
                break;
        }
    }
    out << '"';
}

void writeCsvField(std::ostream& out, const std::string& value) {
    const bool quote = value.find_first_of(",\"\r\n") != std::string::npos;
    if (quote) out << '"';
    for (char character : value) {
        if (character == '"') out << "\"\"";
        else out << character;
    }
    if (quote) out << '"';
}

void writeSessionInputsSnapshot(const Session& session) {
    std::ostringstream json;
    json << "{\"session_id\":";
    writeJsonString(json, session.getSessionId());
    json << ",\"description\":";
    writeJsonString(json, session.getDescription());
    json << ",\"active_config_version\":" << session.getActiveVersion()
         << ",\"algorithm\":\"ALL_SCHEDULERS\""
         << ",\"quantum\":" << session.getSettings().defaultTimeQuantum
         << ",\"processes\":[";

    const auto& processes = session.getProcesses();
    for (size_t i = 0; i < processes.size(); ++i) {
        if (i != 0) json << ',';
        const auto& process = processes[i];
        json << "{\"pid\":" << process.getPid()
             << ",\"process_name\":";
        writeJsonString(json, process.getName());
        json << ",\"description\":";
        writeJsonString(json, process.getDescription());
        json << ",\"arrival_time\":" << process.getArrivalTime()
             << ",\"cpu_burst\":" << process.getCpuBurst()
             << ",\"priority\":" << process.getPriority()
             << ",\"memory\":{\"configured\":"
             << (process.getMemoryConfig().isConfigured ? "true" : "false")
             << ",\"demand_units\":" << process.getMemoryConfig().demandUnits
             << ",\"page_references\":[";
        const auto& pageReferences = process.getMemoryConfig().pageReferences;
        for (size_t j = 0; j < pageReferences.size(); ++j) {
            if (j != 0) json << ',';
            json << pageReferences[j];
        }
        json << "]},\"io_requests\":[";
        const auto& ioRequests = process.getIoRequests();
        for (size_t j = 0; j < ioRequests.size(); ++j) {
            if (j != 0) json << ',';
            json << "{\"request_time\":" << ioRequests[j].requestTime
                 << ",\"duration\":" << ioRequests[j].duration
                 << ",\"device\":";
            writeJsonString(json, ioRequests[j].device);
            json << '}';
        }
        json << "],\"resource_claims\":[";
        const auto& resourceClaims = process.getResourceClaims();
        for (size_t j = 0; j < resourceClaims.size(); ++j) {
            if (j != 0) json << ',';
            json << "{\"resource_type\":";
            writeJsonString(json, resourceClaims[j].resourceType);
            json << ",\"max_claim\":" << resourceClaims[j].maxClaim
                 << ",\"initial_allocation\":" << resourceClaims[j].initialAllocation << '}';
        }
        json << "],\"disk_requests\":[";
        const auto& diskRequests = process.getDiskRequests();
        for (size_t j = 0; j < diskRequests.size(); ++j) {
            if (j != 0) json << ',';
            json << "{\"block_or_cylinder\":" << diskRequests[j].blockOrCylinder
                 << ",\"operation_type\":";
            writeJsonString(json, diskRequests[j].operationType);
            json << ",\"data_volume_bytes\":" << diskRequests[j].dataVolumeBytes << '}';
        }
        json << "],\"sync_requirements\":[";
        const auto& syncRequirements = process.getSyncRequirements();
        for (size_t j = 0; j < syncRequirements.size(); ++j) {
            if (j != 0) json << ',';
            json << "{\"primitive_name\":";
            writeJsonString(json, syncRequirements[j].primitiveName);
            json << ",\"start_time\":" << syncRequirements[j].csStartTime
                 << ",\"duration\":" << syncRequirements[j].csDuration << '}';
        }
        const auto& access = process.getAccessConfig();
        json << "],\"access\":{\"configured\":"
             << (access.isConfigured ? "true" : "false")
             << ",\"user_or_role\":";
        writeJsonString(json, access.userOrRole);
        json << ",\"resource_name\":";
        writeJsonString(json, access.resourceName);
        json << ",\"requested_operation\":";
        writeJsonString(json, access.requestedOperation);
        json << "}}";
    }
    json << "]}";

    Serial.println("<<<FILE_EXPORT_START:data/session_inputs_log.jsonl>>>");
    Serial.println(json.str().c_str());
    Serial.println("<<<FILE_EXPORT_END>>>");

    std::ostringstream csv;
    csv << "session_id,session_description,config_version,process_id,process_name,"
           "process_description,arrival_time,cpu_burst,priority,memory_demand\n";
    for (const auto& process : processes) {
        writeCsvField(csv, session.getSessionId());
        csv << ',';
        writeCsvField(csv, session.getDescription());
        csv << ',' << session.getActiveVersion() << ',' << process.getPid() << ',';
        writeCsvField(csv, process.getName());
        csv << ',';
        writeCsvField(csv, process.getDescription());
        csv << ',' << process.getArrivalTime()
            << ',' << process.getCpuBurst()
            << ',' << process.getPriority() << ',';
        if (process.getMemoryConfig().isConfigured) {
            csv << process.getMemoryConfig().demandUnits;
        }
        csv << '\n';
    }
    Serial.println(("<<<FILE_EXPORT_START:reports/workload_" + session.getSessionId() + ".csv>>>").c_str());
    Serial.print(csv.str().c_str());
    Serial.println("<<<FILE_EXPORT_END>>>");
}

// Helper to normalize any \n in a formatted string to \r\n on Arduino
void printLine(std::ostream& out, const std::string& text = "") {
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\n' && (i == 0 || text[i - 1] != '\r')) {
            out << EOL;
        } else {
            out << text[i];
        }
    }
    out << EOL << std::flush;
}

} // namespace
#else
namespace {
void printLine(std::ostream& out, const std::string& text = "") {
    out << text << '\n' << std::flush;
}
} // namespace
#endif

ConfigurationManager::ConfigurationManager(std::istream& in, std::ostream& out)
    : in_(in), out_(out) {}

int ConfigurationManager::promptInt(const std::string& promptText, int minVal, int maxVal, int defaultVal) {
    while (true) {
        out_ << "\r  " << promptText;
        if (defaultVal >= 0) {
            out_ << " [" << defaultVal << "]";
        }
        out_ << ": " << std::flush;

        std::string line;
        if (!cli::ConsoleUi::readLine(in_, line)) {
            printLine(out_);
            return (defaultVal >= 0) ? defaultVal : minVal;
        }
        out_ << "\r";

        size_t first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            if (defaultVal >= 0) return defaultVal;
            printLine(out_, "  [ERROR] Input cannot be blank. Please enter a valid integer.");
            continue;
        }
        size_t last = line.find_last_not_of(" \t\r\n");
        line = line.substr(first, (last - first + 1));

        try {
            size_t idx = 0;
            int val = std::stoi(line, &idx);
            if (idx != line.length()) {
                printLine(out_, "  [ERROR] Invalid numeric input. Please enter a valid integer.");
                continue;
            }
            if (val < minVal || val > maxVal) {
                printLine(out_, "  [ERROR] Value must be between " + std::to_string(minVal) + " and " + std::to_string(maxVal) + ".");
                continue;
            }
            return val;
        } catch (...) {
            printLine(out_, "  [ERROR] Invalid number format. Please try again.");
        }
    }
}

int64_t ConfigurationManager::promptInt64(const std::string& promptText, int64_t minVal, int64_t maxVal, int64_t defaultVal) {
    while (true) {
        out_ << "\r  " << promptText;
        if (defaultVal >= 0) {
            out_ << " [" << defaultVal << "]";
        }
        out_ << ": " << std::flush;

        std::string line;
        if (!cli::ConsoleUi::readLine(in_, line)) {
            printLine(out_);
            return (defaultVal >= 0) ? defaultVal : minVal;
        }
        out_ << "\r";

        size_t first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            if (defaultVal >= 0) return defaultVal;
            printLine(out_, "  [ERROR] Input cannot be blank. Please enter a valid integer.");
            continue;
        }
        size_t last = line.find_last_not_of(" \t\r\n");
        line = line.substr(first, (last - first + 1));

        try {
            size_t idx = 0;
            long long val = std::stoll(line, &idx);
            if (idx != line.length()) {
                printLine(out_, "  [ERROR] Invalid numeric input. Please enter a valid integer.");
                continue;
            }
            if (val < minVal || val > maxVal) {
                printLine(out_, "  [ERROR] Value must be between " + std::to_string(minVal) + " and " + std::to_string(maxVal) + ".");
                continue;
            }
            return static_cast<int64_t>(val);
        } catch (...) {
            printLine(out_, "  [ERROR] Invalid number format. Please try again.");
        }
    }
}

bool ConfigurationManager::promptYesNo(const std::string& promptText, bool defaultVal) {
    while (true) {
        out_ << "\r  " << promptText << (defaultVal ? " (Y/n): " : " (y/N): ") << std::flush;
        std::string line;
        if (!cli::ConsoleUi::readLine(in_, line)) {
            printLine(out_);
            return defaultVal;
        }
        out_ << "\r";

        size_t first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            return defaultVal;
        }
        char c = std::tolower(line[first]);
        if (c == 'y') return true;
        if (c == 'n') return false;
        printLine(out_, "  [ERROR] Please enter 'y' for yes or 'n' for no.");
    }
}

std::string ConfigurationManager::promptString(const std::string& promptText, const std::string& defaultVal) {
    out_ << "\r  " << promptText;
    if (!defaultVal.empty()) {
        out_ << " [" << defaultVal << "]";
    }
    out_ << ": " << std::flush;

    std::string line;
    if (!cli::ConsoleUi::readLine(in_, line)) {
        printLine(out_);
        return defaultVal;
    }
    out_ << "\r";

    size_t first = line.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return defaultVal;
    }
    size_t last = line.find_last_not_of(" \t\r\n");
    return line.substr(first, (last - first + 1));
}

bool ConfigurationManager::runInteractiveConfiguration(Session& session) {
    printLine(out_);
    printLine(out_, "============================================================");
    printLine(out_, "           INTERACTIVE PROCESS CONFIGURATION");
    printLine(out_, "============================================================");
    printLine(out_, "  Session ID : " + session.getSessionId());
    printLine(out_);

    int count = promptInt("How many processes do you want to configure?", 1, 100, 3);
    std::vector<process::Process> procs;

    for (int i = 1; i <= count; ++i) {
        auto p = promptSingleProcess(i, count, procs);
        procs.push_back(p);
    }

    session.setProcesses(procs);
    return runReviewAndEditMenu(session);
}

process::Process ConfigurationManager::promptSingleProcess(int processIndex, int totalProcesses, const std::vector<process::Process>& existing) {
    printLine(out_);
    printLine(out_, "+----------------------------------------------------------+");
    printLine(out_, "| Process " + std::to_string(processIndex) + " of " + std::to_string(totalProcesses));
    printLine(out_, "+----------------------------------------------------------+");

    int suggestedPid = 100 + processIndex;
    int pid = 0;
    while (true) {
        pid = promptInt("Process ID (PID)", 1, 999999, suggestedPid);
        bool duplicate = false;
        for (const auto& other : existing) {
            if (other.getPid() == pid) {
                printLine(out_, "  [ERROR] PID " + std::to_string(pid) + " is already in use. Enter a unique PID.");
                duplicate = true;
                break;
            }
        }
        if (!duplicate) break;
    }

    std::string name        = promptString("Process Name", "Process " + std::to_string(pid));
    std::string description = promptString("Process Description", "");
    int64_t arrival         = promptInt64("Arrival Time (>= 0)", 0, 1000000, 0);
    int64_t burst           = promptInt64("CPU Burst Time (> 0)", 1, 1000000, 5);
    int priority            = promptInt("Priority (1 = highest)", -1000, 1000, 1);

    process::Process proc(pid, arrival, burst, priority);
    proc.setName(name);
    proc.setDescription(description);

    printLine(out_);
    printLine(out_, "  -- Optional Subsystems (PID " + std::to_string(pid) + ") --");

    if (promptYesNo("Configure memory requirements?", false)) {
        configureMemory(proc);
    }
    if (promptYesNo("Configure I/O requests?", false)) {
        configureIo(proc);
    }
    if (promptYesNo("Configure resource claims?", false)) {
        configureResources(proc);
    }
    if (promptYesNo("Configure disk workload?", false)) {
        configureDisk(proc);
    }
    if (promptYesNo("Configure synchronization?", false)) {
        configureSync(proc);
    }
    if (promptYesNo("Configure access permissions?", false)) {
        configureAccess(proc);
    }

    return proc;
}

void ConfigurationManager::configureMemory(process::Process& proc) {
    int64_t demand = promptInt64("  Memory demand (units > 0)", 1, 1000000, 64);
    process::MemoryConfig mem;
    mem.isConfigured = true;
    mem.demandUnits = demand;
    proc.setMemoryConfig(mem);
}

void ConfigurationManager::configureIo(process::Process& proc) {
    int numIo = promptInt("  Number of I/O requests", 1, 20, 1);
    for (int i = 1; i <= numIo; ++i) {
        printLine(out_, "    [I/O Op " + std::to_string(i) + " of " + std::to_string(numIo) + "]");
        int64_t maxReqTime = (proc.getCpuBurst() > 1) ? (proc.getCpuBurst() - 1) : 0;
        int64_t reqTime = promptInt64("    Request time (0 <= t < " + std::to_string(proc.getCpuBurst()) + ")", 0, maxReqTime, 0);
        int64_t duration = promptInt64("    I/O duration (> 0)", 1, 1000000, 2);
        std::string device = promptString("    Device name", "DISK");
        proc.addIoRequest({reqTime, duration, device});
    }
}

void ConfigurationManager::configureResources(process::Process& proc) {
    int numRes = promptInt("  Number of resource types to claim", 1, 10, 1);
    for (int i = 1; i <= numRes; ++i) {
        printLine(out_, "    [Resource " + std::to_string(i) + " of " + std::to_string(numRes) + "]");
        std::string rname = promptString("    Resource name (e.g., R1, MUTEX_A)", "R" + std::to_string(i));
        int maxClaim = promptInt("    Maximum claim (>= 0)", 0, 1000, 1);
        int initAlloc = promptInt("    Initial allocation (0 <= alloc <= " + std::to_string(maxClaim) + ")", 0, maxClaim, 0);
        proc.addResourceClaim({rname, maxClaim, initAlloc});
    }
}

void ConfigurationManager::configureDisk(process::Process& proc) {
    int numReq = promptInt("  Number of disk operations", 1, 20, 1);
    for (int i = 1; i <= numReq; ++i) {
        printLine(out_, "    [Disk Op " + std::to_string(i) + " of " + std::to_string(numReq) + "]");
        int cyl = promptInt("    Target cylinder/block (>= 0)", 0, 1000000, 50);
        std::string op = promptString("    Operation type (READ/WRITE)", "READ");
        int64_t vol = promptInt64("    Transfer volume bytes (> 0)", 1, 100000000, 4096);
        proc.addDiskRequest({cyl, op, vol});
    }
}

void ConfigurationManager::configureSync(process::Process& proc) {
    int numSync = promptInt("  Number of critical sections", 1, 10, 1);
    for (int i = 1; i <= numSync; ++i) {
        printLine(out_, "    [Critical Section " + std::to_string(i) + " of " + std::to_string(numSync) + "]");
        std::string prim = promptString("    Primitive name (e.g. MUTEX_1)", "MUTEX_1");
        int64_t start = promptInt64("    Start time offset (>= 0)", 0, 1000000, 0);
        int64_t dur = promptInt64("    Duration (> 0)", 1, 1000000, 2);
        proc.addSyncRequirement({prim, start, dur});
    }
}

void ConfigurationManager::configureAccess(process::Process& proc) {
    std::string role = promptString("  User / Role identity", "user_1");
    std::string res = promptString("  Target resource name", "FILE_A");
    std::string op;
    do {
        op = security::normalizeAccessOperation(
            promptString("  Requested operation (READ/WRITE/EXECUTE)", "READ")
        );
        if (!security::isSupportedAccessOperation(op)) {
            printLine(out_, "  [ERROR] Choose READ, WRITE, or EXECUTE.");
        }
    } while (!security::isSupportedAccessOperation(op));
    proc.setAccessConfig({true, role, res, op});
}

bool ConfigurationManager::runReviewAndEditMenu(Session& session) {
    while (true) {
        printLine(out_);
        printLine(out_, "============================================================");
        printLine(out_, "             WORKLOAD CONFIGURATION REVIEW");
        printLine(out_, "============================================================");
        printLine(out_, "  Session ID      : " + session.getSessionId());
        printLine(out_, "  Status          : " + core::sessionStatusToString(session.getStatus()));
        printLine(out_, "  Total Processes : " + std::to_string(session.getProcesses().size()));
        printLine(out_);

        printLine(out_, "+-----+---------+-----------+----------+----------+-----------------------------+");
        printLine(out_, "| PID | Arrival | CPU Burst | Priority | Memory   | Subsystems Configured       |");
        printLine(out_, "+-----+---------+-----------+----------+----------+-----------------------------+");

        if (session.getProcesses().empty()) {
            printLine(out_, "|                     No processes configured.                              |");
        } else {
            for (const auto& p : session.getProcesses()) {
                std::string memStr = p.getMemoryConfig().isConfigured ? (std::to_string(p.getMemoryConfig().demandUnits) + " u") : "None";
                std::string subStr = "";
                if (!p.getIoRequests().empty()) subStr += "I/O(" + std::to_string(p.getIoRequests().size()) + ") ";
                if (!p.getResourceClaims().empty()) subStr += "Res(" + std::to_string(p.getResourceClaims().size()) + ") ";
                if (!p.getDiskRequests().empty()) subStr += "Disk(" + std::to_string(p.getDiskRequests().size()) + ") ";
                if (!p.getSyncRequirements().empty()) subStr += "Sync(" + std::to_string(p.getSyncRequirements().size()) + ") ";
                if (p.getAccessConfig().isConfigured) subStr += "Access ";
                if (subStr.empty()) subStr = "CPU-only";

                std::ostringstream row;
                row << "| " << std::setw(3) << p.getPid()
                    << " | " << std::setw(7) << p.getArrivalTime()
                    << " | " << std::setw(9) << p.getCpuBurst()
                    << " | " << std::setw(8) << p.getPriority()
                    << " | " << std::setw(8) << memStr
                    << " | " << std::left << std::setw(27) << subStr << std::right << " |";
                printLine(out_, row.str());
            }
        }
        printLine(out_, "+-----+---------+-----------+----------+----------+-----------------------------+");
        printLine(out_);

        auto issues = process::WorkloadValidator::validateWorkload(session.getProcesses());
        int errorCount = 0;
        for (const auto& issue : issues) {
            if (issue.severity == process::ValidationIssue::Severity::ERROR) {
                printLine(out_, "  [VALIDATION ERROR] PID " + std::to_string(issue.pid) + " (" + issue.field + "): " + issue.message);
                errorCount++;
            }
        }
        if (errorCount > 0) {
            printLine(out_, "  " + std::to_string(errorCount) + " error(s) must be resolved before finalizing.");
            printLine(out_);
        }

        printLine(out_, "  Workload Options:");
        printLine(out_, "    1. Edit a Process");
        printLine(out_, "    2. Add a New Process");
        printLine(out_, "    3. Remove a Process");
        printLine(out_, "    4. Finalize Configuration & Persist");
        printLine(out_, "    5. Return to Main Menu (Save Draft)");
        printLine(out_);

        int choice = promptInt("Select an option", 1, 5);

        if (choice == 1) {
            if (session.getProcesses().empty()) {
                printLine(out_, "  [WARNING] No processes to edit.");
                continue;
            }
            int targetPid = promptInt("Enter PID of process to edit", 1, 999999);
            auto& procs = session.getProcesses();
            bool found = false;
            for (auto& proc : procs) {
                if (proc.getPid() == targetPid) {
                    editProcess(proc, procs);
                    found = true;
                    break;
                }
            }
            if (!found) {
                printLine(out_, "  [ERROR] Process PID " + std::to_string(targetPid) + " not found in session.");
            }
        } else if (choice == 2) {
            int newIdx = static_cast<int>(session.getProcesses().size()) + 1;
            auto p = promptSingleProcess(newIdx, newIdx, session.getProcesses());
            session.addProcess(p);
            printLine(out_, "  [OK] Process PID " + std::to_string(p.getPid()) + " added.");
        } else if (choice == 3) {
            if (session.getProcesses().empty()) {
                printLine(out_, "  [WARNING] No processes to remove.");
                continue;
            }
            int targetPid = promptInt("Enter PID of process to remove", 1, 999999);
            auto& procs = session.getProcesses();
            bool removed = false;
            for (auto it = procs.begin(); it != procs.end(); ++it) {
                if (it->getPid() == targetPid) {
                    procs.erase(it);
                    removed = true;
                    printLine(out_, "  [OK] Process PID " + std::to_string(targetPid) + " removed.");
                    break;
                }
            }
            if (!removed) {
                printLine(out_, "  [ERROR] Process PID " + std::to_string(targetPid) + " not found.");
            }
        } else if (choice == 4) {
            std::string err;
            if (!process::WorkloadValidator::isWorkloadValid(session.getProcesses(), err)) {
                printLine(out_);
                printLine(out_, "  [CANNOT FINALIZE] " + err);
                printLine(out_, "  Please resolve the validation issue before finalizing.");
                continue;
            }
#ifdef ARDUINO
            writeSessionInputsSnapshot(session);
#endif
            return true;
        } else if (choice == 5) {
#ifdef ARDUINO
            writeSessionInputsSnapshot(session);
#endif
            return false;
        }
    }
}

void ConfigurationManager::editProcess(process::Process& proc, const std::vector<process::Process>& existing) {
    (void)existing;
    printLine(out_);
    printLine(out_, "  Editing Process PID " + std::to_string(proc.getPid()) + ":");
    proc.setName(promptString("New Process Name", proc.getName()));
    proc.setDescription(promptString("New Process Description", proc.getDescription()));
    int newArrival = static_cast<int>(promptInt64("New Arrival Time", 0, 1000000, proc.getArrivalTime()));
    int newBurst = static_cast<int>(promptInt64("New CPU Burst Time", 1, 1000000, proc.getCpuBurst()));
    int newPriority = promptInt("New Priority", -1000, 1000, proc.getPriority());

    proc.setArrivalTime(newArrival);
    proc.setCpuBurst(newBurst);
    proc.setPriority(newPriority);

    if (promptYesNo("Reconfigure subsystem parameters for this process?", false)) {
        if (promptYesNo("Configure memory requirements?", proc.getMemoryConfig().isConfigured)) {
            configureMemory(proc);
        } else {
            proc.setMemoryConfig(process::MemoryConfig());
        }

        if (promptYesNo("Configure I/O requests?", !proc.getIoRequests().empty())) {
            proc.clearIoRequests();
            configureIo(proc);
        } else {
            proc.clearIoRequests();
        }

        if (promptYesNo("Configure resource claims?", !proc.getResourceClaims().empty())) {
            proc.clearResourceClaims();
            configureResources(proc);
        } else {
            proc.clearResourceClaims();
        }
    }

    printLine(out_, "  [OK] Process PID " + std::to_string(proc.getPid()) + " updated successfully.");
}

} // namespace session
} // namespace watchos