#include "watchos/cli/console_ui.hpp"
#include "watchos/cli/ansi_terminal.hpp"
#include <iostream>
#include <iomanip>
#include <atomic>

#ifdef ARDUINO
#include <Arduino.h>
#include "watchos/hardware/tft_dashboard.hpp"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <streambuf>
#endif

namespace watchos {
namespace cli {

namespace {
ConsoleUi::MainMenuCallback mainMenuCallback = nullptr;
std::atomic<bool> emergencyStopRequested{false};

#ifdef ARDUINO
class TerminalMirrorBuffer : public std::streambuf {
public:
    explicit TerminalMirrorBuffer(std::streambuf* output) : output_(output) {}

protected:
    int_type overflow(int_type character) override {
        if (traits_type::eq_int_type(character, traits_type::eof())) {
            return traits_type::not_eof(character);
        }
        const char byte = traits_type::to_char_type(character);
        if (!writeOutputCharacter(byte)) {
            return traits_type::eof();
        }
        hardware::appendTerminalText(std::string(1, byte));
        return character;
    }

    std::streamsize xsputn(const char* text, std::streamsize count) override {
        std::streamsize written = 0;
        while (written < count && writeOutputCharacter(text[written])) {
            ++written;
        }
        if (written > 0) {
            hardware::appendTerminalText(std::string(text, static_cast<size_t>(written)));
        }
        return written;
    }

    int sync() override {
        return output_->pubsync();
    }

private:
    bool writeOutputCharacter(char character) {
        if (character == '\n' && !lastOutputWasCarriageReturn_
            && traits_type::eq_int_type(output_->sputc('\r'), traits_type::eof())) {
            return false;
        }
        if (traits_type::eq_int_type(output_->sputc(character), traits_type::eof())) {
            return false;
        }
        lastOutputWasCarriageReturn_ = character == '\r';
        return true;
    }

    std::streambuf* output_;
    bool lastOutputWasCarriageReturn_ = false;
};
#endif
}

void ConsoleUi::setMainMenuCallback(MainMenuCallback callback) {
    mainMenuCallback = callback;
}

void ConsoleUi::installOutputMirror() {
#ifdef ARDUINO
    static TerminalMirrorBuffer mirror(std::cout.rdbuf());
    static bool installed = false;
    if (!installed) {
        std::cout.rdbuf(&mirror);
        installed = true;
    }
#endif
}

bool ConsoleUi::readLine(std::istream& input, std::string& line) {
#ifdef ARDUINO
    (void)input;
    static bool skipLineFeed = false;
    line.clear();
    std::cout.flush();

    while (true) {
        throwIfEmergencyStopRequested();
        if (Serial.available() > 0) {
            const char character = static_cast<char>(Serial.read());
            if (skipLineFeed) {
                skipLineFeed = false;
                if (character == '\n') {
                    continue;
                }
            }

            if (character == '\r' || character == '\n') {
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                Serial.print("\r\n");
                hardware::appendTerminalText("\n");
                skipLineFeed = character == '\r';
                return true;
            }
            Serial.print(character);
            line += character;
            hardware::appendTerminalText(std::string(1, character));
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
#else
    throwIfEmergencyStopRequested();
    return static_cast<bool>(std::getline(input, line));
#endif
}

void ConsoleUi::requestEmergencyStop() {
    emergencyStopRequested.store(true, std::memory_order_release);
}

void ConsoleUi::clearEmergencyStop() {
    emergencyStopRequested.store(false, std::memory_order_release);
}

void ConsoleUi::throwIfEmergencyStopRequested() {
    if (emergencyStopRequested.load(std::memory_order_acquire)) {
        throw EmergencyStopException();
    }
}

void ConsoleUi::printStartupBanner() {
    std::cout << AnsiTerminal::cyan() << AnsiTerminal::bold()
              << "============================================================\n"
              << "                         WATCH OS\n"
              << "           Kernel Simulation & Analysis Engine\n"
              << "============================================================\n"
              << AnsiTerminal::reset();
}

void ConsoleUi::printBootSequence() {
    std::cout << AnsiTerminal::dim() << "[BOOT] Initializing WATCH OS...\n"
              << "[CORE] Loading simulation configuration...\n"
              << "[DB]   Connecting to session database...\n"
              << "[CORE] Validating stored sessions...\n"
              << AnsiTerminal::reset()
              << AnsiTerminal::green() << "\nSystem ready.\n" << AnsiTerminal::reset();
}

void ConsoleUi::printMainMenu() {
    if (mainMenuCallback != nullptr) {
        mainMenuCallback();
    }

    std::cout << "\n"
              << AnsiTerminal::bold() << "1." << AnsiTerminal::reset() << " Create New Session\n"
              << AnsiTerminal::bold() << "2." << AnsiTerminal::reset() << " Resume Existing Session\n"
              << AnsiTerminal::bold() << "3." << AnsiTerminal::reset() << " View Session History\n"
              << AnsiTerminal::bold() << "4." << AnsiTerminal::reset() << " Compare Previous Sessions\n"
              << AnsiTerminal::bold() << "5." << AnsiTerminal::reset() << " Settings & Configuration\n"
              << AnsiTerminal::bold() << "6." << AnsiTerminal::reset() << " Exit\n\n"
              << "Select an option: ";
}

void ConsoleUi::printSessionTable(const std::vector<session::SessionMetadata>& sessions) {
    if (sessions.empty()) {
        std::cout << "\n[HISTORY] No saved sessions found in database.\n";
        return;
    }

    std::cout << "\n" << AnsiTerminal::cyan() << AnsiTerminal::bold()
              << "+-----+---------------------+-----------------------+-----------+----------+------+\n"
              << "| No  | Session ID          | Created At (UTC)      | Status    | Procs    | Runs |\n"
              << "+-----+---------------------+-----------------------+-----------+----------+------+\n"
              << AnsiTerminal::reset();

    int idx = 1;
    for (const auto& s : sessions) {
        std::string statusCol = core::sessionStatusToString(s.status);
        std::cout << "| " << std::setw(3) << idx++
                  << " | " << std::setw(19) << s.sessionId
                  << " | " << std::setw(21) << s.createdAt
                  << " | " << std::setw(9) << statusCol
                  << " | " << std::setw(8) << s.processCount
                  << " | " << std::setw(4) << s.runCount << " |\n";
    }

    std::cout << "+-----+---------------------+-----------------------+-----------+----------+------+\n";
}

void ConsoleUi::printWorkloadTable(const std::vector<process::Process>& processes) {
    std::cout << "+-----+---------+-----------+----------+----------+-----------------------------+\n";
    std::cout << "| PID | Arrival | CPU Burst | Priority | Memory   | Subsystems Configured       |\n";
    std::cout << "+-----+---------+-----------+----------+----------+-----------------------------+\n";

    if (processes.empty()) {
        std::cout << "|                    No processes configured.                               |\n";
    } else {
        for (const auto& p : processes) {
            std::string memStr = p.getMemoryConfig().isConfigured ? (std::to_string(p.getMemoryConfig().demandUnits) + " u") : "None";
            std::string subStr = "";
            if (!p.getIoRequests().empty()) subStr += "I/O(" + std::to_string(p.getIoRequests().size()) + ") ";
            if (!p.getResourceClaims().empty()) subStr += "Res(" + std::to_string(p.getResourceClaims().size()) + ") ";
            if (!p.getDiskRequests().empty()) subStr += "Disk(" + std::to_string(p.getDiskRequests().size()) + ") ";
            if (!p.getSyncRequirements().empty()) subStr += "Sync(" + std::to_string(p.getSyncRequirements().size()) + ") ";
            if (p.getAccessConfig().isConfigured) subStr += "Access ";
            if (subStr.empty()) subStr = "CPU-only";

            std::cout << "| " << std::setw(3) << p.getPid()
                      << " | " << std::setw(7) << p.getArrivalTime()
                      << " | " << std::setw(9) << p.getCpuBurst()
                      << " | " << std::setw(8) << p.getPriority()
                      << " | " << std::setw(8) << memStr
                      << " | " << std::left << std::setw(27) << subStr << std::right << " |\n";
        }
    }
    std::cout << "+-----+---------+-----------+----------+----------+-----------------------------+\n";
}

void ConsoleUi::printConfirmation(const std::string& sessionId) {
    std::cout << "\n" << AnsiTerminal::green()
              << "[SESSION] Configuration saved successfully.\n"
              << "[SESSION] Session ID: " << sessionId << "\n"
              << "[DB] All configured inputs persisted.\n\n"
              << "Configuration complete.\n"
              << "Okay, noted. You may proceed with your next query\n"
              << "or request an analysis.\n"
              << AnsiTerminal::reset();
}

void ConsoleUi::printInfo(const std::string& msg) {
    std::cout << AnsiTerminal::cyan() << "[INFO] " << msg << AnsiTerminal::reset() << "\n";
}

void ConsoleUi::printWarning(const std::string& msg) {
    std::cout << AnsiTerminal::yellow() << "[WARNING] " << msg << AnsiTerminal::reset() << "\n";
}

void ConsoleUi::printError(const std::string& msg) {
    std::cout << AnsiTerminal::red() << "[ERROR] " << msg << AnsiTerminal::reset() << "\n";
}

void ConsoleUi::printSuccess(const std::string& msg) {
    std::cout << AnsiTerminal::green() << "[SUCCESS] " << msg << AnsiTerminal::reset() << "\n";
}

} // namespace cli
} // namespace watchos
