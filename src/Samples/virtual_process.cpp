
#include "sim/virtual_process.hpp"

namespace sim {

    namespace {
        constexpr uint64_t kFirstRegionBase = 0x10000;
        constexpr uint64_t kPageSize = 0x1000;
    }  // namespace

    const char* ToString(ProcessState state)
    {
        switch (state) {
        case ProcessState::Running:    return "Running";
        case ProcessState::Suspended:  return "Suspended";
        case ProcessState::Terminated: return "Terminated";
        }
        return "Unknown";
    }

    const char* ToString(OperationType type)
    {
        switch (type) {
        case OperationType::Allocate:     return "ALLOC";
        case OperationType::Write:        return "WRITE";
        case OperationType::Protect:      return "PROTECT";
        case OperationType::CreateThread: return "THREAD";
        }
        return "UNKNOWN";
    }

    VirtualProcess::VirtualProcess(uint32_t pid, std::string name, EventLog& log)
        : m_pid(pid)
        , m_name(std::move(name))
        , m_log(log)
        , m_memory(log, kFirstRegionBase, kPageSize)
    {
        m_log.Record(Severity::Info, "process.create", m_name + " pid=" + std::to_string(m_pid));
    }

    std::optional<uint64_t> VirtualProcess::LoadModule(const std::string& name, const std::string& path,
        const std::vector<uint8_t>& image, bool trusted)
    {
        if (FindModule(name) != nullptr || image.empty()) {
            return std::nullopt;
        }
        auto base = m_memory.Allocate(image.size(), Protection{ true, true, false },
            RegionKind::Image, name, path);
        if (!base) {
            return std::nullopt;
        }
        if (m_memory.Write(*base, image) != AccessResult::Ok) {
            return std::nullopt;
        }
        m_memory.Protect(*base, Protection{ true, false, true });
        m_memory.SetBaseline(*base);

        m_modules.push_back(VirtualModule{ name, path, *base, image.size(), trusted });
        m_log.Record(Severity::Info, "module.load",
            name + " base=" + Hex(*base) + (trusted ? " trusted" : " untrusted"));
        return base;
    }

    const VirtualModule* VirtualProcess::FindModule(const std::string& name) const
    {
        for (const auto& module : m_modules) {
            if (module.name == name) {
                return &module;
            }
        }
        return nullptr;
    }

    uint32_t VirtualProcess::CreateThread(uint64_t startAddress)
    {
        const uint32_t tid = m_nextTid++;
        m_threads.push_back(VirtualThread{ tid, startAddress });
        m_log.Record(Severity::Info, "thread.create",
            "tid=" + std::to_string(tid) + " start=" + Hex(startAddress));
        return tid;
    }

    std::optional<uint64_t> VirtualProcess::RequestAllocation(uint32_t sourcePid, uint64_t size, Protection protection)
    {
        auto base = m_memory.Allocate(size, protection, RegionKind::Private, "", "");
        RecordOperation(OperationType::Allocate, sourcePid, base.value_or(0), size, protection, base.has_value());
        return base;
    }

    AccessResult VirtualProcess::RequestWrite(uint32_t sourcePid, uint64_t address, const std::vector<uint8_t>& bytes)
    {
        const AccessResult result = m_memory.Write(address, bytes);
        RecordOperation(OperationType::Write, sourcePid, address, bytes.size(), Protection{},
            result == AccessResult::Ok);
        return result;
    }

    bool VirtualProcess::RequestProtection(uint32_t sourcePid, uint64_t address, Protection protection)
    {
        const bool ok = m_memory.Protect(address, protection);
        RecordOperation(OperationType::Protect, sourcePid, address, 0, protection, ok);
        return ok;
    }

    std::optional<uint32_t> VirtualProcess::RequestThread(uint32_t sourcePid, uint64_t startAddress)
    {
        const uint32_t tid = CreateThread(startAddress);
        RecordOperation(OperationType::CreateThread, sourcePid, startAddress, 0, Protection{}, true);
        return tid;
    }

    void VirtualProcess::RecordOperation(OperationType type, uint32_t sourcePid, uint64_t address, uint64_t size,
        Protection protection, bool succeeded)
    {
        Operation op;
        op.sequence = m_operations.size();
        op.type = type;
        op.sourcePid = sourcePid;
        op.address = address;
        op.size = size;
        op.protection = protection;
        op.succeeded = succeeded;
        m_operations.push_back(op);

        m_log.Record(Severity::Info, "operation",
            std::string(ToString(type)) + " src=" + std::to_string(sourcePid) +
            " addr=" + Hex(address) + (succeeded ? " ok" : " failed"));
    }

    VirtualEnvironment::VirtualEnvironment()
    {
        m_log.Record(Severity::Info, "environment.init", "virtual environment initialised");
    }

    VirtualProcess& VirtualEnvironment::CreateProcess(uint32_t pid, const std::string& name)
    {
        auto& slot = m_processes[pid];
        slot = std::make_unique<VirtualProcess>(pid, name, m_log);
        return *slot;
    }

    VirtualProcess* VirtualEnvironment::FindProcess(uint32_t pid)
    {
        auto it = m_processes.find(pid);
        return it == m_processes.end() ? nullptr : it->second.get();
    }

}  // namespace sim