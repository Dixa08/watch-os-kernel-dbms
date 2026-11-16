#include "watchos/cli/analysis_menu.hpp"
#include "watchos/cli/console_ui.hpp"
#include "watchos/cli/ansi_terminal.hpp"
#include "watchos/hardware/tft_dashboard.hpp"
#include "watchos/core/id_generator.hpp"
#include "watchos/scheduler/fcfs.hpp"
#include "watchos/scheduler/sjf.hpp"
#include "watchos/scheduler/srtf.hpp"
#include "watchos/scheduler/priority_scheduler.hpp"
#include "watchos/scheduler/round_robin.hpp"
#include "watchos/scheduler/gantt_chart.hpp"
#include "watchos/scheduler/recommendation_engine.hpp"
#include "watchos/scheduler/aging_analyzer.hpp"
#include "watchos/process/lifecycle_simulator.hpp"
#include "watchos/memory/memory_manager.hpp"
#include "watchos/deadlock/resource_manager.hpp"
#include "watchos/deadlock/bankers_algorithm.hpp"
#include "watchos/deadlock/deadlock_detector.hpp"
#include "watchos/deadlock/deadlock_frequency.hpp"
#include "watchos/io/disk_scheduler.hpp"
#include "watchos/io/io_manager.hpp"
#include "watchos/synchronization/sync_manager.hpp"
#include "watchos/security/access_controller.hpp"
#include "watchos/reporting/jarvis_explainer.hpp"
#include "watchos/reporting/report_generator.hpp"
#include "watchos/security/access_policy.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <utility>

#ifdef ARDUINO
#define EOL "\r\n"
#else
#define EOL "\n"
#endif

namespace watchos {
namespace cli {

namespace {

// Normalizes standalone '\n' to "\r\n" on ARDUINO so multi-line sub-module reports
// and ASCII tables align cleanly at column 0.
void printFormatted(const std::string& text) {
#ifdef ARDUINO
    std::string out;
    out.reserve(text.size() + 32);
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\n' && (i == 0 || text[i - 1] != '\r')) {
            out += "\r\n";
        } else {
            out += text[i];
        }
    }
    std::cout << out << std::flush;
#else
    std::cout << text << std::flush;
#endif
}

void printLine(const std::string& text = "") {
    printFormatted(text + "\n");
}

void pauseBeforeMenu() {
#ifdef ARDUINO
    printFormatted("\n\r[Press Enter to continue...] ");
    std::string ignored;
    ConsoleUi::readLine(std::cin, ignored);
    printLine();
#endif
}

} // namespace

AnalysisMenu::AnalysisMenu(session::Session& session, database::DbConnection& db)
    : session_(session), db_(db), runRepo_(db) {}

void AnalysisMenu::runMenuLoop() {
    bool inLoop = true;
    while (inLoop) {
        ConsoleUi::throwIfEmergencyStopRequested();
        printAnalysisMenu();
        printFormatted("\n\rWATCH OS > ");
        std::string line;
        if (!ConsoleUi::readLine(std::cin, line)) {
            break;
        }
        std::cout << "\r" << std::flush;

        size_t first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) continue;
        size_t last = line.find_last_not_of(" \t\r\n");
        line = line.substr(first, (last - first + 1));

        // Check if numeric menu choice
        try {
            size_t idx = 0;
            int opt = std::stoi(line, &idx);
            if (idx == line.length()) {
                if (opt == 18) {
                    inLoop = false;
                    break;
                }
                executeOption(opt);
                continue;
            }
        } catch (...) {}

        // Command parser
        if (line == "exit" || line == "quit" || line == "back") {
            inLoop = false;
        } else {
            executeCommand(line);
        }
        ConsoleUi::throwIfEmergencyStopRequested();
    }
}

void AnalysisMenu::printAnalysisMenu() {
    hardware::setTerminalMenuSuppressed(true);
    std::ostringstream menu;
    menu << "\n" << AnsiTerminal::cyan() << AnsiTerminal::bold()
         << "============================================================\n"
         << "            WATCH OS — KERNEL ANALYSIS CONSOLE\n"
         << "============================================================\n"
         << AnsiTerminal::reset()
         << "  Active Session : " << session_.getSessionId()
         << " | Workload: " << session_.getProcesses().size() << " Processes\n\n"
         << "   01  Find Best CPU Scheduling Algorithm\n"
         << "   02  Run All Scheduling Algorithms\n"
         << "   03  Display Gantt Charts\n"
         << "   04  Show Process Lifecycle\n"
         << "   05  Analyze Context Switching\n"
         << "   06  Detect Starvation\n"
         << "   07  Apply Aging and Compare\n"
         << "   08  Run Deadlock Analysis\n"
         << "   09  Estimate Deadlock Frequency\n"
         << "   10  Virtual Memory Analysis\n"
         << "   11  Disk Usage & I/O Analysis\n"
         << "   12  Process Synchronization & IPC\n"
         << "   13  Permissions & Protection\n"
         << "   14  Run Complete Kernel Analysis\n"
         << "   15  Compare Previous Runs\n"
         << "   16  View Session Logs\n"
         << "   17  Generate Final Report\n"
         << "   18  Return to Main Menu\n";
    printFormatted(menu.str());
    hardware::setTerminalMenuSuppressed(false);
}

void AnalysisMenu::executeCommand(const std::string& cmd) {
    std::string lowerCmd = cmd;
    std::transform(lowerCmd.begin(), lowerCmd.end(), lowerCmd.begin(), ::tolower);
    bool actionExecuted = true;

    if (lowerCmd == "help" || lowerCmd == "?") {
        printAnalysisMenu();
    } else if (lowerCmd == "schedule all" || lowerCmd == "run all") {
        opt02_runAllSchedulers();
    } else if (lowerCmd == "best" || lowerCmd == "find best" || lowerCmd == "best scheduler") {
        opt01_findBestScheduler();
    } else if (lowerCmd == "gantt" || lowerCmd == "display gantt") {
        opt03_displayGanttCharts();
    } else if (lowerCmd == "show lifecycle" || lowerCmd == "lifecycle") {
        opt04_showProcessLifecycle();
    } else if (lowerCmd == "context switch" || lowerCmd == "context") {
        opt05_analyzeContextSwitching();
    } else if (lowerCmd == "starvation" || lowerCmd == "detect starvation") {
        opt06_detectStarvation();
    } else if (lowerCmd == "aging" || lowerCmd == "apply aging") {
        opt07_applyAgingAndCompare();
    } else if (lowerCmd == "deadlock analyze" || lowerCmd == "deadlock") {
        opt08_runDeadlockAnalysis();
    } else if (lowerCmd == "deadlock frequency" || lowerCmd == "monte carlo") {
        opt09_estimateDeadlockFrequency();
    } else if (lowerCmd == "memory analyze" || lowerCmd == "memory" || lowerCmd == "paging") {
        opt10_virtualMemoryAnalysis();
    } else if (lowerCmd == "disk analyze" || lowerCmd == "disk" || lowerCmd == "io") {
        opt11_diskUsageAndIoAnalysis();
    } else if (lowerCmd == "sync" || lowerCmd == "ipc") {
        opt12_processSynchronizationAndIpc();
    } else if (lowerCmd == "permissions" || lowerCmd == "security" || lowerCmd == "protection") {
        opt13_permissionsAndProtection();
    } else if (lowerCmd == "complete" || lowerCmd == "all analysis") {
        opt14_runCompleteKernelAnalysis();
    } else if (lowerCmd == "compare runs" || lowerCmd == "compare") {
        opt15_comparePreviousRuns();
    } else if (lowerCmd == "view logs" || lowerCmd == "logs") {
        opt16_viewSessionLogs();
    } else if (lowerCmd == "report generate" || lowerCmd == "report") {
        opt17_generateFinalReport();
    } else {
        actionExecuted = false;
        ConsoleUi::printWarning("Unknown command '" + cmd + "'. Supported commands: 'schedule all', 'show lifecycle', 'deadlock analyze', 'memory analyze', 'disk analyze', 'compare runs', 'report generate' or enter options 01-18.");
    }
    if (actionExecuted && lowerCmd != "help" && lowerCmd != "?") {
        pauseBeforeMenu();
    }
}

void AnalysisMenu::executeOption(int option) {
    switch (option) {
        case 1: opt01_findBestScheduler(); break;
        case 2: opt02_runAllSchedulers(); break;
        case 3: opt03_displayGanttCharts(); break;
        case 4: opt04_showProcessLifecycle(); break;
        case 5: opt05_analyzeContextSwitching(); break;
        case 6: opt06_detectStarvation(); break;
        case 7: opt07_applyAgingAndCompare(); break;
        case 8: opt08_runDeadlockAnalysis(); break;
        case 9: opt09_estimateDeadlockFrequency(); break;
        case 10: opt10_virtualMemoryAnalysis(); break;
        case 11: opt11_diskUsageAndIoAnalysis(); break;
        case 12: opt12_processSynchronizationAndIpc(); break;
        case 13: opt13_permissionsAndProtection(); break;
        case 14: opt14_runCompleteKernelAnalysis(); break;
        case 15: opt15_comparePreviousRuns(); break;
        case 16: opt16_viewSessionLogs(); break;
        case 17: opt17_generateFinalReport(); break;
        default:
            ConsoleUi::printWarning("Invalid option number: " + std::to_string(option) + ". Choose between 01 and 18.");
            break;
    }
    if (option >= 1 && option <= 17) {
        pauseBeforeMenu();
    }
}

void AnalysisMenu::ensureSchedulersRun() {
    if (cachedMetrics_.empty()) {
        opt02_runAllSchedulers();
    }
}

void AnalysisMenu::opt01_findBestScheduler() {
    if (session_.getProcesses().empty()) {
        ConsoleUi::printWarning("No processes configured in this session. Add processes in Configuration Menu before running simulation.");
        return;
    }
    ensureSchedulersRun();
    if (cachedMetrics_.empty()) {
        ConsoleUi::printWarning("No scheduling metrics available to evaluate.");
        return;
    }
    printLine("\nEvaluating best scheduling algorithm using multi-objective normalization...");
    auto ranking = scheduler::RecommendationEngine::evaluateAndRank(cachedMetrics_);
    printLine(scheduler::RecommendationEngine::formatComparisonTable(ranking));
    if (!ranking.empty()) {
        hardware::publishGanttChart(ranking.front().metrics);
    }
}

void AnalysisMenu::opt02_runAllSchedulers(bool showOutput) {
    if (session_.getProcesses().empty()) {
        ConsoleUi::printWarning("No processes configured in this session. Add processes in Configuration Menu before running simulation.");
        return;
    }

    std::string runId = core::IdGenerator::generateRunId();
    latestRunId_ = runId;
    if (showOutput) {
        printLine("\n[RUN] Creating new analysis run...");
        printLine("[RUN] Session : " + session_.getSessionId());
        printLine("[RUN] Run ID  : " + runId);
        printLine("[RUN] Set     : FCFS, SJF, SRTF, Priority (Non-Preemptive), Priority (Preemptive), Round Robin\n");
    }

    cachedMetrics_.clear();

    scheduler::FcfsScheduler fcfs;
    scheduler::SjfScheduler sjf;
    scheduler::SrtfScheduler srtf;
    scheduler::PriorityScheduler priNonPreempt(false);
    scheduler::PriorityScheduler priPreempt(true);
    scheduler::RoundRobinScheduler rr(session_.getSettings().defaultTimeQuantum);

    hardware::setDashboardActive(true);
    const int processCount = static_cast<int>(session_.getProcesses().size());
    const auto runScheduler = [this, processCount](scheduler::Scheduler& schedulerInstance,
                                                   std::vector<scheduler::AggregateMetrics>& results) {
        ConsoleUi::throwIfEmergencyStopRequested();
        auto metrics = schedulerInstance.schedule(session_.getProcesses(), session_.getSettings());
        hardware::publishSchedulerMetrics(metrics, processCount);
        hardware::publishGanttChart(metrics);
        results.push_back(std::move(metrics));
    };

    runScheduler(fcfs, cachedMetrics_);
    runScheduler(sjf, cachedMetrics_);
    runScheduler(srtf, cachedMetrics_);
    runScheduler(priNonPreempt, cachedMetrics_);
    runScheduler(priPreempt, cachedMetrics_);
    runScheduler(rr, cachedMetrics_);

    // Persist the supported scheduling result records.
    for (const auto& agg : cachedMetrics_) {
        for (const auto& pr : agg.processResults) {
            database::SchedulingResultRecord rec;
            rec.runId = runId;
            rec.pid = pr.pid;
            rec.arrivalTime = pr.arrivalTime;
            rec.cpuBurst = pr.cpuBurst;
            rec.priority = pr.priority;
            rec.completionTime = pr.completionTime;
            rec.turnaroundTime = pr.turnaroundTime;
            rec.waitingTime = pr.waitingTime;
            rec.responseTime = pr.responseTime;
            runRepo_.recordSchedulingResult(rec);
        }
    }

    if (showOutput) {
        std::ostringstream tbl;
        tbl << "+--------------------------------+---------+---------+---------+-------+---------+\n"
            << "| Algorithm                      | Avg WT  | Avg TAT | Avg RT  | CS    | CPU Util|\n"
            << "+--------------------------------+---------+---------+---------+-------+---------+\n";
        for (const auto& m : cachedMetrics_) {
            tbl << "| " << std::left << std::setw(30) << m.algorithmName << std::right
                << " | " << std::setw(7) << std::fixed << std::setprecision(2) << m.avgWaitingTime
                << " | " << std::setw(7) << m.avgTurnaroundTime
                << " | " << std::setw(7) << m.avgResponseTime
                << " | " << std::setw(5) << m.totalContextSwitches
                << " | " << std::setw(6) << m.cpuUtilization << "% |\n";
        }
        tbl << "+--------------------------------+---------+---------+---------+-------+---------+\n";
        printFormatted(tbl.str());
        ConsoleUi::printSuccess("Scheduling results saved to SQLite under Run ID: " + runId);
    }
}

void AnalysisMenu::opt03_displayGanttCharts() {
    if (session_.getProcesses().empty()) {
        ConsoleUi::printWarning("No processes configured in this session.");
        return;
    }
    ensureSchedulersRun();
    printLine("\n============================================================");
    printLine("                   GANTT CHART VISUALIZER");
    printLine("============================================================");
    for (const auto& agg : cachedMetrics_) {
        printFormatted(scheduler::GanttChart::render(agg.algorithmName, agg.ganttSegments));
    }
    const auto ranking = scheduler::RecommendationEngine::evaluateAndRank(cachedMetrics_);
    if (!ranking.empty()) {
        printLine("\nRecommended Gantt chart for TFT: " + ranking.front().algorithmName);
        hardware::publishGanttChart(ranking.front().metrics);
    }
}

void AnalysisMenu::opt04_showProcessLifecycle() {
    if (session_.getProcesses().empty()) {
        ConsoleUi::printWarning("No processes configured in this session.");
        return;
    }
    printLine("\n============================================================");
    printLine("                 PROCESS LIFECYCLE TRACKER");
    printLine("============================================================");
    process::LifecycleSimulator sim(session_.getProcesses());

    std::string runId = latestRunId_.empty() ? core::IdGenerator::generateRunId() : latestRunId_;
    if (latestRunId_.empty()) {
        latestRunId_ = runId;
    }

    core::TimeUnits t = 0;
    for (const auto& p : session_.getProcesses()) {
        sim.transition(p.getPid(), core::ProcessState::READY, p.getArrivalTime(), "Process arrived");
        runRepo_.recordProcessStateTransition({runId, p.getPid(), p.getArrivalTime(), "NEW", "READY", "Process arrived"});

        t = std::max(t, p.getArrivalTime());
        sim.transition(p.getPid(), core::ProcessState::RUNNING, t, "Dispatched to CPU");
        runRepo_.recordProcessStateTransition({runId, p.getPid(), t, "READY", "RUNNING", "Dispatched to CPU"});

        t += p.getCpuBurst();
        sim.transition(p.getPid(), core::ProcessState::TERMINATED, t, "CPU burst completed");
        runRepo_.recordProcessStateTransition({runId, p.getPid(), t, "RUNNING", "TERMINATED", "CPU burst completed"});
    }

    std::ostringstream tbl;
    tbl << "+-----+---------+------------+-------------+----------+--------------------------------------+\n"
        << "| PID | Arrival | First Start| Completion  | Total WT | Final State / Reason                 |\n"
        << "+-----+---------+------------+-------------+----------+--------------------------------------+\n";
    for (const auto& pair : sim.getAllMetrics()) {
        const auto& m = pair.second;
        tbl << "| " << std::setw(3) << m.pid
            << " | " << std::setw(7) << m.arrivalTime
            << " | " << std::setw(10) << m.firstCpuStartTime
            << " | " << std::setw(11) << m.completionTime
            << " | " << std::setw(8) << m.readyQueueWait
            << " | " << std::left << std::setw(36) << m.terminationReason << std::right << " |\n";
    }
    tbl << "+-----+---------+------------+-------------+----------+--------------------------------------+\n";
    printFormatted(tbl.str());
    ConsoleUi::printSuccess("Process lifecycle transitions recorded under Run ID: " + runId);
}

void AnalysisMenu::opt05_analyzeContextSwitching() {
    if (session_.getProcesses().empty()) {
        ConsoleUi::printWarning("No processes configured in this session.");
        return;
    }
    ensureSchedulersRun();
    std::ostringstream tbl;
    tbl << "\nContext Switching Dynamics Analysis:\n"
        << "+--------------------------------+-----------------+-----------------------+\n"
        << "| Algorithm                      | Initial Starts  | Voluntary / Preempt CS|\n"
        << "+--------------------------------+-----------------+-----------------------+\n";
    for (const auto& m : cachedMetrics_) {
        tbl << "| " << std::left << std::setw(30) << m.algorithmName << std::right
            << " | " << std::setw(15) << session_.getProcesses().size()
            << " | " << std::setw(21) << m.totalContextSwitches << " |\n";
    }
    tbl << "+--------------------------------+-----------------+-----------------------+\n";
    printFormatted(tbl.str());
}

void AnalysisMenu::opt06_detectStarvation() {
    if (session_.getProcesses().empty()) {
        ConsoleUi::printWarning("No processes configured in this session.");
        return;
    }
    ensureSchedulersRun();
    printLine("\nStarvation Risk Detector (Threshold = 10 Time Units):");
    bool anyStarvation = false;
    for (const auto& m : cachedMetrics_) {
        if (m.starvationBreaches > 0) {
            printLine("  - [" + m.algorithmName + "]: " + std::to_string(m.starvationBreaches)
                      + " processes exceeded threshold (Longest Wait: " + std::to_string(m.maxWaitingTime) + " units).");
            anyStarvation = true;
        }
    }
    if (!anyStarvation) {
        printLine("  [OK] No starvation threshold breaches detected for this workload across algorithms.");
    }
}

void AnalysisMenu::opt07_applyAgingAndCompare() {
    if (session_.getProcesses().empty()) {
        ConsoleUi::printWarning("No processes configured in this session.");
        return;
    }
    printLine("\nExecuting Priority Scheduling Aging Experiment (Interval=5, Increment=1)...");
    auto comp = scheduler::AgingAnalyzer::compare(session_.getProcesses(), session_.getSettings(), false, 5, 1, 10);
    printFormatted(scheduler::AgingAnalyzer::formatComparisonReport(comp));
}

void AnalysisMenu::opt08_runDeadlockAnalysis() {
    if (session_.getProcesses().empty()) {
        ConsoleUi::printWarning("No processes configured in this session.");
        return;
    }
    printLine("\nExecuting Resource Allocation & Deadlock Detection Analysis...");
    deadlock::ResourceManager rm;
    rm.loadFromWorkload(session_.getProcesses());

    printLine(rm.renderResourceAllocationGraph());

    std::vector<core::ProcessId> pids;
    for (const auto& p : session_.getProcesses()) pids.push_back(p.getPid());

    auto bankers = deadlock::BankersAlgorithm::evaluateSafeState(rm, pids);
    printLine(deadlock::BankersAlgorithm::formatSafeStateReport(bankers));

    auto det = deadlock::DeadlockDetector::detect(rm, pids);
    printLine(det.report);
    hardware::publishDeadlockResults(det, bankers);

    std::string runId = latestRunId_.empty() ? core::IdGenerator::generateRunId() : latestRunId_;
    if (latestRunId_.empty()) {
        latestRunId_ = runId;
    }

    std::string safeSeqStr;
    for (size_t i = 0; i < bankers.safeSequence.size(); ++i) {
        if (i > 0) safeSeqStr += " -> ";
        safeSeqStr += "P" + std::to_string(bankers.safeSequence[i]);
    }
    std::string dlProcsStr;
    for (size_t i = 0; i < det.deadlockedProcesses.size(); ++i) {
        if (i > 0) dlProcsStr += ", ";
        dlProcsStr += "P" + std::to_string(det.deadlockedProcesses[i]);
    }

    runRepo_.recordDeadlockResult(runId, det.isDeadlocked, bankers.isSafe, safeSeqStr, dlProcsStr, 0, 0, 0.0);
    ConsoleUi::printSuccess("Deadlock evaluation results persisted under Run ID: " + runId);
}

void AnalysisMenu::opt09_estimateDeadlockFrequency() {
    if (session_.getProcesses().empty()) {
        ConsoleUi::printWarning("No processes configured in this session.");
        return;
    }
    printLine("\nRunning Monte Carlo Deadlock Risk Simulation (1,000 Stochastic Trials)...");
    deadlock::ResourceManager rm;
    rm.loadFromWorkload(session_.getProcesses());

    std::vector<core::ProcessId> pids;
    for (const auto& p : session_.getProcesses()) pids.push_back(p.getPid());

    auto freq = deadlock::DeadlockFrequencyEstimator::estimate(rm, pids, 1000, 42);
    printLine(freq.report);

    std::string runId = latestRunId_.empty() ? core::IdGenerator::generateRunId() : latestRunId_;
    if (latestRunId_.empty()) {
        latestRunId_ = runId;
    }
    runRepo_.recordDeadlockResult(runId, freq.deadlockedTrials > 0, freq.deadlockedTrials == 0, "", "", freq.totalTrials, freq.deadlockedTrials, freq.empiricalFrequency);
}

void AnalysisMenu::opt10_virtualMemoryAnalysis() {
    if (session_.getProcesses().empty()) {
        ConsoleUi::printWarning("No processes configured in this session.");
        return;
    }
    printLine("\nExecuting Virtual Memory & Page Replacement Analysis (Frames = 3)...");
    memory::MemoryManager memMgr(16, 4);
    auto results = memMgr.runComparativeAnalysis(session_.getProcesses(), 3);
    for (const auto& res : results) {
        printLine(memory::PageReplacementSimulator::formatResultTable(res));
    }

    ConsoleUi::printSuccess("Memory paging analysis completed.");
}

void AnalysisMenu::opt11_diskUsageAndIoAnalysis() {
    printLine("\nExecuting Disk Head Motion & I/O Scheduling Analysis...");
    std::vector<int32_t> requests = {98, 183, 37, 122, 14, 124, 65, 67};
    int32_t head = 53;

    auto fcfs = io::DiskScheduler::scheduleFcfs(head, requests);
    auto sstf = io::DiskScheduler::scheduleSstf(head, requests);
    auto scan = io::DiskScheduler::scheduleScan(head, requests, 199, "UP");
    auto cscan = io::DiskScheduler::scheduleCscan(head, requests, 199);

    printFormatted(io::DiskScheduler::formatResultReport(fcfs));
    printFormatted(io::DiskScheduler::formatResultReport(sstf));
    printFormatted(io::DiskScheduler::formatResultReport(scan));
    printFormatted(io::DiskScheduler::formatResultReport(cscan));

    ConsoleUi::printSuccess("Disk I/O scheduling analysis completed.");
}

void AnalysisMenu::opt12_processSynchronizationAndIpc() {
    printLine("\nRunning Simulated Process Synchronization & IPC Demonstration...");
    synchronization::SyncManager sync;
    sync.runProducerConsumerSimulation(5, 6);
    printLine(sync.formatEventLog());
}

void AnalysisMenu::opt13_permissionsAndProtection() {
    if (session_.getProcesses().empty()) {
        ConsoleUi::printWarning("No processes configured in this session.");
        return;
    }
    printLine("\nRunning Access Protection & Memory Isolation Simulation...");
    security::AccessController sec;

    core::TimeUnits t = 1;
    size_t processIndex = 0;
    for (auto& p : session_.getProcesses()) {
        auto& access = p.getAccessConfig();
        const std::string role = access.isConfigured
            ? access.userOrRole
            : (processIndex == 0 ? "root" : "user");
        sec.assignRole(p.getPid(), role);

        const auto readDecision = sec.checkAccess(p.getPid(), "user_file", "READ", t++);
        runRepo_.saveAccessPolicy(
            session_.getSessionId(), role, "user_file", "READ",
            readDecision.granted ? "ALLOW" : "DENY"
        );
        const auto writeDecision = sec.checkAccess(p.getPid(), "system_config", "WRITE", t++);
        runRepo_.saveAccessPolicy(
            session_.getSessionId(), role, "system_config", "WRITE",
            writeDecision.granted ? "ALLOW" : "DENY"
        );
        if (access.isConfigured) {
            access.requestedOperation =
                security::normalizeAccessOperation(access.requestedOperation);
            if (!security::isSupportedAccessOperation(access.requestedOperation)) {
                ConsoleUi::printError(
                    "Unsupported access operation for process " + std::to_string(p.getPid())
                    + "; expected READ, WRITE, or EXECUTE."
                );
            } else {
                const auto configuredDecision = sec.checkAccess(
                    p.getPid(), access.resourceName, access.requestedOperation, t++
                );
                runRepo_.saveAccessPolicy(
                    session_.getSessionId(),
                    access.userOrRole,
                    access.resourceName,
                    access.requestedOperation,
                    configuredDecision.granted ? "ALLOW" : "DENY"
                );
            }
        }
        // Memory boundary partition test: base=1000, limit=500
        sec.checkMemoryAccess(p.getPid(), 1200, 1000, 500, t++); // Valid
        sec.checkMemoryAccess(p.getPid(), 1600, 1000, 500, t++); // Segfault
        ++processIndex;
    }

    printLine(sec.formatAuditTable());
    ConsoleUi::printSuccess("Security protection analysis completed.");
}

void AnalysisMenu::opt14_runCompleteKernelAnalysis() {
    if (session_.getProcesses().empty()) {
        ConsoleUi::printWarning("No processes configured in this session. Please add processes first.");
        return;
    }
    printLine("\n============================================================");
    printLine("           RUNNING COMPLETE KERNEL ANALYSIS SUITE");
    printLine("============================================================");
    opt02_runAllSchedulers();
    if (!cachedMetrics_.empty()) {
        const auto ranking = scheduler::RecommendationEngine::evaluateAndRank(cachedMetrics_);
        if (!ranking.empty()) {
            printLine("\nRecommended Scheduler: " + ranking.front().algorithmName);
            hardware::publishGanttChart(ranking.front().metrics);
        }
    }
    opt08_runDeadlockAnalysis();
    opt10_virtualMemoryAnalysis();
    opt11_diskUsageAndIoAnalysis();
    opt13_permissionsAndProtection();

    if (!cachedMetrics_.empty()) {
        printFormatted(reporting::JarvisExplainer::explainSummary(session_.getSessionId(), latestRunId_, cachedMetrics_[0]));
    }
}

void AnalysisMenu::opt15_comparePreviousRuns() {
    printLine("\n[RUNS] Run-history metadata is not retained in the 8-table database schema.");
    if (!latestRunId_.empty()) {
        const auto results = runRepo_.getSchedulingResultsForRun(latestRunId_);
        printLine("Latest scheduling result set (" + latestRunId_ + "): "
                  + std::to_string(results.size()) + " process record(s).");
    }
}

void AnalysisMenu::opt16_viewSessionLogs() {
    printLine("\nFetching session audit event logs from database...");
    auto events = runRepo_.getEventsForRun(latestRunId_);
    if (!events.empty()) {
        std::ostringstream tbl;
        tbl << "+------+-----+--------------------+------------------------------------------+\n"
            << "| Time | PID | Event Type         | Details                                  |\n"
            << "+------+-----+--------------------+------------------------------------------+\n";
        for (const auto& e : events) {
            tbl << "| " << std::setw(4) << e.timestamp
                << " | " << std::setw(3) << e.entityId
                << " | " << std::left << std::setw(18) << e.eventType << std::right
                << " | " << std::left << std::setw(40) << e.details.substr(0, 40) << std::right << " |\n";
        }
        tbl << "+------+-----+--------------------+------------------------------------------+\n";
        printFormatted(tbl.str());
    } else {
        printLine("No event records found for this session or latest run.");
    }
}

void AnalysisMenu::opt17_generateFinalReport() {
    if (session_.getProcesses().empty()) {
        ConsoleUi::printWarning("No processes configured in this session to generate report.");
        return;
    }

    bool exportMarkdown = false;
    bool exportCsv = false;
    while (true) {
        std::ostringstream prompt;
        prompt << "\nGenerate Final Report\n"
               << "Choose output format:\n"
               << "  1 - Markdown\n"
               << "  2 - CSV\n"
               << "  3 - Both (default)\n"
               << "Selection [1-3, default 3]: ";
        printFormatted(prompt.str());

        std::string choice;
        if (!ConsoleUi::readLine(std::cin, choice)) {
            ConsoleUi::printError("Could not read the report format selection.");
            return;
        }
        std::cout << "\r" << std::flush;

        const size_t first = choice.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            exportMarkdown = true;
            exportCsv = true;
            break;
        }
        const size_t last = choice.find_last_not_of(" \t\r\n");
        choice = choice.substr(first, last - first + 1);
        if (choice == "1") {
            exportMarkdown = true;
            break;
        }
        if (choice == "2") {
            exportCsv = true;
            break;
        }
        if (choice == "3") {
            exportMarkdown = true;
            exportCsv = true;
            break;
        }
        ConsoleUi::printWarning("Invalid selection. Enter 1 for Markdown, 2 for CSV, or 3 for both.");
    }

    if (cachedMetrics_.empty()) {
        opt02_runAllSchedulers(false);
    }
    memory::MemoryManager memMgr(16, 4);
    deadlock::ResourceManager rm;
    rm.loadFromWorkload(session_.getProcesses());
    std::vector<core::ProcessId> pids;
    for (const auto& p : session_.getProcesses()) pids.push_back(p.getPid());

    reporting::CompleteReportData reportData;
    reportData.sessionId = session_.getSessionId();
    reportData.sessionDescription = session_.getDescription();
    reportData.runId = latestRunId_.empty() ? core::IdGenerator::generateRunId() : latestRunId_;
    reportData.generatedAt = core::IdGenerator::getCurrentTimestampIso();
    reportData.settings = session_.getSettings();
    reportData.processes = session_.getProcesses();
    reportData.schedulingMetrics = cachedMetrics_;
    reportData.memoryResults = memMgr.runComparativeAnalysis(session_.getProcesses(), 3);
    reportData.bankersResult = deadlock::BankersAlgorithm::evaluateSafeState(rm, pids);
    reportData.deadlockResult = deadlock::DeadlockDetector::detect(rm, pids);
    reportData.diskResult = io::DiskScheduler::scheduleSstf(53, {98, 183, 37, 122, 14, 124, 65, 67});

    std::string mdContent = reporting::ReportGenerator::generateMarkdownReport(reportData);
    std::string csvContent = reporting::ReportGenerator::generateCsvReport(reportData);

    std::string mdPath = "reports/report_" + reportData.sessionId + ".md";
    std::string csvPath = "reports/report_" + reportData.sessionId + ".csv";

    bool markdownExported = false;
    bool csvExported = false;
    if (exportMarkdown) {
        markdownExported = reporting::ReportGenerator::exportToFile(mdPath, mdContent);
        if (markdownExported) {
            runRepo_.saveReportRecord(
                "RPT-" + reportData.sessionId, reportData.sessionId, reportData.runId, "MARKDOWN", mdPath
            );
        } else {
            ConsoleUi::printError("Could not write Markdown report: " + mdPath);
        }
    }
    if (exportCsv) {
        csvExported = reporting::ReportGenerator::exportToFile(csvPath, csvContent);
        if (csvExported) {
            runRepo_.saveReportRecord(
                "RPT-CSV-" + reportData.sessionId, reportData.sessionId, reportData.runId, "CSV", csvPath
            );
        } else {
            ConsoleUi::printError("Could not write CSV report: " + csvPath);
        }
    }

    const int requestedExports = static_cast<int>(exportMarkdown) + static_cast<int>(exportCsv);
    const int successfulExports = static_cast<int>(markdownExported) + static_cast<int>(csvExported);
    const char* exportStatus = successfulExports == requestedExports
        ? "COMPLETE"
        : (successfulExports > 0 ? "PARTIAL" : "FAILED");

    std::ostringstream summary;
    summary << "\nFinal Report\n"
            << "------------\n"
            << "  Session ID : " << reportData.sessionId << '\n'
            << "  Run ID     : " << reportData.runId << '\n'
            << "  Status     : " << exportStatus << '\n';
    if (exportMarkdown) {
        summary << "  Markdown   : " << (markdownExported ? "SAVED" : "FAILED") << " (" << mdPath << ")\n";
    }
    if (exportCsv) {
        summary << "  CSV        : " << (csvExported ? "SAVED" : "FAILED") << " (" << csvPath << ")\n";
    }
    printFormatted(summary.str());
}

} // namespace cli
} // namespace watchos