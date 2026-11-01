#include "sim/simulation.hpp"

#include <iomanip>
#include <optional>
#include <ostream>

namespace sim {

    namespace {

        constexpr size_t kStepCount = 8;

        std::string Pad(const std::string& text, size_t width)
        {
            if (text.size() >= width) {
                return text.substr(0, width - 1) + " ";
            }
            return text + std::string(width - text.size(), ' ');
        }

        void Rule(std::ostream& out, char c = '-')
        {
            out << std::string(78, c) << '\n';
        }

        void StepHeader(std::ostream& out, size_t index, const char* title)
        {
            out << "[" << index << "/" << kStepCount << "] " << title << '\n';
        }

    }  // namespace

    Simulation::Simulation(Scenario scenario)
        : m_scenario(std::move(scenario))
    {
    }

    DetectionResult Simulation::Run(std::ostream& out)
    {
        Rule(out, '=');
        out << " Anti-Cheat Detection Simulation: " << m_scenario.name << '\n';
        if (!m_scenario.description.empty()) {
            out << " " << m_scenario.description << '\n';
        }
        Rule(out, '=');

        InitializeEnvironment(out);
        CreateTargetProcess(out);
        RegisterModules(out);
        GenerateInjectionEvents(out);
        CheckIntegrity(out);
        DetectionResult result = RunDetection(out);
        ApplyResponse(result, out);
        PrintReport(result, out);
        return result;
    }

    void Simulation::InitializeEnvironment(std::ostream& out)
    {
        StepHeader(out, 1, "Initialise virtual environment");
        out << "      event log and virtual address spaces created (no real process is touched)\n";
        out << "      seed=" << m_scenario.seed << '\n';
    }

    void Simulation::CreateTargetProcess(std::ostream& out)
    {
        StepHeader(out, 2, "Create virtual game process");
        m_process = &m_env.CreateProcess(m_scenario.pid, m_scenario.processName);
        out << "      " << m_process->Name() << " pid=" << m_process->Pid()
            << " state=" << ToString(m_process->State()) << '\n';
    }

    void Simulation::RegisterModules(std::ostream& out)
    {
        StepHeader(out, 3, "Register and load simulated modules");
        for (const ModuleSpec& spec : m_scenario.modules) {
            // Image bytes are deterministic filler derived from the scenario seed and the module name.
            const std::vector<uint8_t> nameBytes(spec.name.begin(), spec.name.end());
            const uint32_t mix = static_cast<uint32_t>(m_scenario.seed) ^
                static_cast<uint32_t>(Fnv1a64(nameBytes));
            DeterministicRandom rng(mix);
            std::vector<uint8_t> image(static_cast<size_t>(spec.size));
            for (auto& byte : image) {
                byte = rng.NextByte();
            }

            auto base = m_process->LoadModule(spec.name, "virtual://modules/" + spec.name, image, spec.trusted);
            out << "      load " << Pad(spec.name, 24) << (base ? "base=" + Hex(*base) : "FAILED")
                << (spec.trusted ? "  trusted" : "  untrusted") << '\n';
        }

        for (const TamperSpec& tamper : m_scenario.tampers) {
            const VirtualModule* module = m_process->FindModule(tamper.module);
            if (module == nullptr || tamper.offset >= module->size) {
                out << "      tamper " << tamper.module << " skipped (no such module or offset out of range)\n";
                continue;
            }
            const AccessResult result = m_process->Memory().Patch(module->base + tamper.offset, { tamper.value });
            out << "      tamper " << tamper.module << " +" << Hex(tamper.offset) << " = "
                << Hex(tamper.value) << " -> " << ToString(result) << '\n';
        }
    }

    void Simulation::GenerateInjectionEvents(std::ostream& out)
    {
        StepHeader(out, 4, "Generate simulated injection telemetry");
        if (m_scenario.injection.empty()) {
            out << "      no injection events scripted\n";
            return;
        }

        const uint32_t source = m_scenario.telemetrySourcePid;
        if (m_env.FindProcess(source) == nullptr) {
            m_env.CreateProcess(source, "VirtualLauncher.exe");
        }

        DeterministicRandom rng(static_cast<uint32_t>(m_scenario.seed) ^ 0xA5A5A5A5u);
        std::optional<uint64_t> lastBase;
        size_t index = 0;

        for (const InjectionStep& step : m_scenario.injection) {
            ++index;
            std::string description;
            switch (step.op) {
            case InjectionOp::Allocate:
                lastBase = m_process->RequestAllocation(source, step.arg1, step.protection);
                description = "alloc " + Hex(step.arg1) + " " + step.protection.ToString() + " -> " +
                    (lastBase ? Hex(*lastBase) : std::string("failed"));
                break;
            case InjectionOp::Write: {
                if (!lastBase) { description = "write skipped (no allocation)"; break; }
                std::vector<uint8_t> bytes(static_cast<size_t>(step.arg2));
                for (auto& byte : bytes) byte = rng.NextByte();
                const AccessResult result = m_process->RequestWrite(source, *lastBase + step.arg1, bytes);
                description = "write offset " + Hex(step.arg1) + " len " + Hex(step.arg2) + " -> " + ToString(result);
                break;
            }
            case InjectionOp::Protect: {
                if (!lastBase) { description = "protect skipped (no allocation)"; break; }
                const bool ok = m_process->RequestProtection(source, *lastBase, step.protection);
                description = "protect " + step.protection.ToString() + (ok ? " ok" : " failed");
                break;
            }
            case InjectionOp::Thread: {
                if (!lastBase) { description = "thread skipped (no allocation)"; break; }
                const auto tid = m_process->RequestThread(source, *lastBase + step.arg1);
                description = "thread at offset " + Hex(step.arg1) + " -> " +
                    (tid ? "tid " + std::to_string(*tid) : std::string("failed"));
                break;
            }
            }
            out << "      op " << index << ": " << description << '\n';
        }
    }

    void Simulation::CheckIntegrity(std::ostream& out)
    {
        StepHeader(out, 5, "Module and memory integrity checks");
        const VirtualMemory& memory = m_process->Memory();

        m_integrity.clear();
        for (const VirtualModule& module : m_process->Modules()) {
            ModuleIntegrity row;
            row.name = module.name;
            row.base = module.base;
            row.size = module.size;
            row.trusted = module.trusted;
            const MemoryRegion* region = memory.FindRegion(module.base);
            row.regionPresent = region != nullptr && region->base == module.base;
            row.baselineIntact = row.regionPresent && memory.VerifyIntegrity(module.base);
            row.protection = row.regionPresent ? region->protection.ToString() : "----";
            m_integrity.push_back(row);

            out << "      " << Pad(module.name, 24) << "region=" << (row.regionPresent ? "yes" : "no ")
                << " integrity=" << (row.baselineIntact ? "intact  " : "MISMATCH") << " prot=" << row.protection << '\n';
        }

        // Address validation: a probe inside a guard page should be unmapped.
        if (!m_process->Modules().empty()) {
            const VirtualModule& last = m_process->Modules().back();
            const uint64_t probe = last.base + last.size + 0x10;
            out << "      address probe " << Hex(probe) << " -> "
                << ToString(memory.ValidateAddress(probe, 1)) << '\n';
        }
    }

    DetectionResult Simulation::RunDetection(std::ostream& out)
    {
        StepHeader(out, 6, "Heartbeat monitoring and anti-cheat detection");

        m_heartbeat = EvaluateHeartbeats(m_scenario.heartbeat,
            GenerateHeartbeats(m_scenario.heartbeat, m_scenario.heartbeatOverrides));
        out << "      heartbeat interval=" << m_scenario.heartbeat.interval
            << " timeout=" << m_scenario.heartbeat.timeout
            << " tolerance=" << m_scenario.heartbeat.tolerance << '\n';
        for (const HeartbeatArrival& beat : m_heartbeat.arrivals) {
            out << "        beat seq=" << std::setw(3) << beat.sequence << " nominal=" << std::setw(4) << beat.nominalTick
                << " arrived=" << std::setw(4) << beat.tick << (beat.valid ? "" : "  INVALID") << '\n';
        }
        for (const HeartbeatTransition& change : m_heartbeat.transitions) {
            out << "        state @" << change.tick << " -> " << ToString(change.state) << " (" << change.reason << ")\n";
        }
        out << "      heartbeat final state: " << ToString(m_heartbeat.finalState) << '\n';

        DetectionEngine engine(m_scenario.policy);
        DetectionResult result = engine.Analyze(*m_process, m_heartbeat.findings);
        out << "      findings=" << result.findings.size()
            << " confidence=" << std::fixed << std::setprecision(3) << result.confidence
            << " risk=" << ToString(result.risk) << '\n';
        return result;
    }

    void Simulation::ApplyResponse(const DetectionResult& result, std::ostream& out)
    {
        StepHeader(out, 7, "Security response evaluation");
        m_responseLog.clear();

        for (ResponseAction action : result.actions) {
            std::string note;
            switch (action) {
            case ResponseAction::None:
                note = "no action required";
                break;
            case ResponseAction::Warning:
                note = "warning raised";
                break;
            case ResponseAction::SuspiciousActivity:
                note = "process marked suspicious";
                break;
            case ResponseAction::AdditionalValidation:
                note = "additional validation requested";
                break;
            case ResponseAction::SessionRestriction:
                m_process->SetState(ProcessState::Suspended);
                note = "session restricted (process suspended in simulation)";
                break;
            case ResponseAction::SimulatedTermination: {
                m_process->SetState(ProcessState::Terminated);
                // Revoke access to every region so nothing in the simulated process can keep running.
                std::vector<uint64_t> bases;
                for (const auto& entry : m_process->Memory().Regions()) {
                    bases.push_back(entry.first);
                }
                for (uint64_t base : bases) {
                    m_process->Memory().Protect(base, Protection{});
                }
                note = "simulated termination: process terminated, all regions set to NONE";
                break;
            }
            case ResponseAction::IncidentReport:
                note = "incident report generated (see section 8)";
                break;
            }
            m_responseLog.push_back(std::string(ToString(action)) + ": " + note);
            out << "      " << Pad(ToString(action), 24) << note << '\n';
        }
    }

    void Simulation::PrintReport(const DetectionResult& result, std::ostream& out) const
    {
        StepHeader(out, 8, "Simulation report");
        Rule(out);

        out << "Process        : " << m_process->Name() << " (pid " << m_process->Pid() << ")\n";
        out << "Final state    : " << ToString(m_process->State()) << '\n';
        out << "Heartbeat      : " << ToString(m_heartbeat.finalState) << " ("
            << m_heartbeat.arrivals.size() << " arrivals)\n";
        out << "Confidence     : " << std::fixed << std::setprecision(3) << result.confidence << '\n';
        out << "Risk           : " << ToString(result.risk) << '\n';
        out << "Policy         : " << result.policyReason << '\n';
        Rule(out);

        out << "Modules\n";
        for (const ModuleIntegrity& row : m_integrity) {
            out << "  " << Pad(row.name, 24) << Pad(Hex(row.base), 14) << Pad(Hex(row.size), 10)
                << Pad(row.trusted ? "trusted" : "untrusted", 11)
                << (row.baselineIntact ? "intact" : "MISMATCH") << '\n';
        }

        out << "Memory regions\n";
        for (const auto& entry : m_process->Memory().Regions()) {
            const MemoryRegion& region = entry.second;
            out << "  " << Pad(Hex(region.base), 14) << Pad(Hex(region.size), 10) << Pad(ToString(region.kind), 9)
                << Pad(region.protection.ToString(), 6)
                << (region.owner.empty() ? "private" : region.owner) << '\n';
        }

        out << "Cross-process operations\n";
        for (const Operation& op : m_process->Operations()) {
            out << "  #" << op.sequence << " " << Pad(ToString(op.type), 9) << "src=" << Pad(std::to_string(op.sourcePid), 7)
                << Pad(Hex(op.address), 14) << (op.succeeded ? "ok" : "failed") << '\n';
        }

        out << "Findings\n";
        if (result.findings.empty()) {
            out << "  none\n";
        }
        for (const Finding& finding : result.findings) {
            out << "  " << Pad(finding.id, 8) << Pad(ToString(finding.severity), 10)
                << Pad(std::to_string(finding.weight).substr(0, 4), 7) << finding.description << '\n';
        }

        out << "Responses\n";
        for (const std::string& entry : m_responseLog) {
            out << "  " << entry << '\n';
        }

        out << "Event log\n";
        const EventLog& log = m_env.Log();
        for (const char* category : { "environment.init", "process.create", "module.load", "memory.alloc",
                                      "memory.patch", "operation", "memory.denied", "thread.create" }) {
            out << "  " << Pad(category, 22) << log.CountCategory(category) << '\n';
        }
        out << "  total events: " << log.Entries().size() << '\n';
        Rule(out);
        // Strongest response is the last non-incident action in the ladder.
        ResponseAction strongest = ResponseAction::None;
        for (ResponseAction action : result.actions) {
            if (action != ResponseAction::IncidentReport) {
                strongest = action;
            }
        }
        out << "Verdict: " << ToString(result.risk) << " risk, strongest response "
            << ToString(strongest) << '\n';
        Rule(out, '=');
    }

}  // namespace sim