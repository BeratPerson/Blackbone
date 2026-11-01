#include "sim/scenario.hpp"

#include <fstream>
#include <stdexcept>

namespace sim {

    namespace {

        uint64_t ParseNumber(const std::string& text)
        {
            size_t consumed = 0;
            const uint64_t value = std::stoull(text, &consumed, 0);
            if (consumed != text.size()) {
                throw std::invalid_argument("not a number: " + text);
            }
            return value;
        }

        bool ParseTrusted(const std::string& text)
        {
            if (text == "trusted") return true;
            if (text == "untrusted") return false;
            throw std::invalid_argument("expected 'trusted' or 'untrusted', got: " + text);
        }

        std::vector<std::string> RequireFields(const std::string& value, size_t expected)
        {
            auto parts = Split(value, ',');
            if (parts.size() != expected) {
                throw std::invalid_argument("expected " + std::to_string(expected) + " comma-separated fields");
            }
            return parts;
        }

        ModuleSpec ParseModule(const std::string& value)
        {
            const auto f = RequireFields(value, 3);
            return ModuleSpec{ f[0], ParseNumber(f[1]), ParseTrusted(f[2]) };
        }

        TamperSpec ParseTamper(const std::string& value)
        {
            const auto f = RequireFields(value, 3);
            return TamperSpec{ f[0], ParseNumber(f[1]), static_cast<uint8_t>(ParseNumber(f[2])) };
        }

        InjectionStep ParseStep(const std::string& value)
        {
            const auto parts = Split(value, ',');
            if (parts.empty()) {
                throw std::invalid_argument("empty injection step");
            }
            InjectionStep step;
            const std::string& kind = parts[0];
            if (kind == "alloc") {
                if (parts.size() != 3) throw std::invalid_argument("alloc expects: alloc, size, protection");
                step.op = InjectionOp::Allocate;
                step.arg1 = ParseNumber(parts[1]);
                step.protection = Protection::FromString(parts[2]);
            }
            else if (kind == "write") {
                if (parts.size() != 3) throw std::invalid_argument("write expects: write, offset, length");
                step.op = InjectionOp::Write;
                step.arg1 = ParseNumber(parts[1]);
                step.arg2 = ParseNumber(parts[2]);
            }
            else if (kind == "protect") {
                if (parts.size() != 2) throw std::invalid_argument("protect expects: protect, protection");
                step.op = InjectionOp::Protect;
                step.protection = Protection::FromString(parts[1]);
            }
            else if (kind == "thread") {
                if (parts.size() != 2) throw std::invalid_argument("thread expects: thread, offset");
                step.op = InjectionOp::Thread;
                step.arg1 = ParseNumber(parts[1]);
            }
            else {
                throw std::invalid_argument("unknown injection step: " + kind);
            }
            return step;
        }

        // Format: event = <nominal tick>, drop | invalid | duplicate | delay, [ticks]
        HeartbeatOverride ParseHeartbeatEvent(const std::string& value)
        {
            const auto parts = Split(value, ',');
            if (parts.size() < 2 || parts.size() > 3) {
                throw std::invalid_argument("heartbeat event expects: tick, kind[, delay]");
            }
            HeartbeatOverride override;
            override.tick = ParseNumber(parts[0]);
            const std::string& kind = parts[1];
            if (kind == "drop") override.kind = HeartbeatOverrideKind::Drop;
            else if (kind == "invalid") override.kind = HeartbeatOverrideKind::Invalid;
            else if (kind == "duplicate") override.kind = HeartbeatOverrideKind::Duplicate;
            else if (kind == "delay") {
                if (parts.size() != 3) throw std::invalid_argument("delay expects a tick count");
                override.kind = HeartbeatOverrideKind::Delay;
                override.amount = ParseNumber(parts[2]);
            }
            else {
                throw std::invalid_argument("unknown heartbeat event kind: " + kind);
            }
            return override;
        }

        // Applies one `key = value` pair. Throws on unknown sections or keys.
        void ApplyEntry(Scenario& s, const std::string& section, const std::string& key, const std::string& value)
        {
            if (section == "scenario") {
                if (key == "name") { s.name = value; return; }
                if (key == "description") { s.description = value; return; }
                if (key == "seed") { s.seed = ParseNumber(value); return; }
            }
            else if (section == "process") {
                if (key == "pid") { s.pid = static_cast<uint32_t>(ParseNumber(value)); return; }
                if (key == "name") { s.processName = value; return; }
                if (key == "module") { s.modules.push_back(ParseModule(value)); return; }
            }
            else if (section == "tamper") {
                if (key == "patch") { s.tampers.push_back(ParseTamper(value)); return; }
            }
            else if (section == "injection") {
                if (key == "source_pid") { s.telemetrySourcePid = static_cast<uint32_t>(ParseNumber(value)); return; }
                if (key == "step") { s.injection.push_back(ParseStep(value)); return; }
            }
            else if (section == "heartbeat") {
                if (key == "interval") { s.heartbeat.interval = ParseNumber(value); return; }
                if (key == "timeout") { s.heartbeat.timeout = ParseNumber(value); return; }
                if (key == "tolerance") { s.heartbeat.tolerance = ParseNumber(value); return; }
                if (key == "duration") { s.heartbeat.duration = ParseNumber(value); return; }
                if (key == "event") { s.heartbeatOverrides.push_back(ParseHeartbeatEvent(value)); return; }
            }
            else if (section == "policy") {
                PolicyConfig& p = s.policy;
                if (key == "confidence_medium") { p.confidenceMedium = std::stod(value); return; }
                if (key == "confidence_high") { p.confidenceHigh = std::stod(value); return; }
                if (key == "confidence_critical") { p.confidenceCritical = std::stod(value); return; }
                if (key == "foreign_operation_threshold") {
                    p.foreignOperationThreshold = static_cast<size_t>(ParseNumber(value));
                    return;
                }
            }
            else {
                throw std::invalid_argument("unknown section [" + section + "]");
            }
            throw std::invalid_argument("unknown key '" + key + "' in [" + section + "]");
        }

    }  // namespace

    Scenario LoadScenario(const std::string& path)
    {
        std::ifstream file(path);
        if (!file) {
            throw std::runtime_error("cannot open scenario file: " + path);
        }

        Scenario scenario;
        std::string section;
        std::string raw;
        size_t lineNo = 0;

        while (std::getline(file, raw)) {
            ++lineNo;
            const auto comment = raw.find_first_of("#;");
            const std::string line = Trim(comment == std::string::npos ? raw : raw.substr(0, comment));
            if (line.empty()) {
                continue;
            }

            try {
                if (line.front() == '[' && line.back() == ']') {
                    section = Trim(line.substr(1, line.size() - 2));
                    continue;
                }
                const auto eq = line.find('=');
                if (eq == std::string::npos) {
                    throw std::invalid_argument("expected 'key = value'");
                }
                if (section.empty()) {
                    throw std::invalid_argument("key outside of any [section]");
                }
                ApplyEntry(scenario, section, Trim(line.substr(0, eq)), Trim(line.substr(eq + 1)));
            }
            catch (const std::exception& e) {
                throw std::runtime_error(path + ":" + std::to_string(lineNo) + ": " + e.what());
            }
        }

        if (scenario.heartbeat.interval == 0) {
            throw std::runtime_error(path + ": [heartbeat] interval must be greater than zero");
        }
        return scenario;
    }

}  // namespace sim