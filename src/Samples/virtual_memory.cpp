#include "sim/virtual_memory.hpp"

#include <algorithm>

namespace sim {

    const char* ToString(RegionKind kind)
    {
        return kind == RegionKind::Image ? "Image" : "Private";
    }

    const char* ToString(AccessResult result)
    {
        switch (result) {
        case AccessResult::Ok:               return "OK";
        case AccessResult::Unmapped:         return "UNMAPPED";
        case AccessResult::OutOfBounds:      return "OUT_OF_BOUNDS";
        case AccessResult::ProtectionDenied: return "PROTECTION_DENIED";
        }
        return "UNKNOWN";
    }

    namespace {

        // Finds the region that fully contains [address, address + size).
        // Returns end() and sets `result` when no such region exists.
        template <typename MapT>
        auto FindRangeIn(MapT& regions, uint64_t address, size_t size, AccessResult& result)
            -> decltype(regions.begin())
        {
            if (size == 0) {
                result = AccessResult::OutOfBounds;
                return regions.end();
            }
            auto it = regions.upper_bound(address);
            if (it == regions.begin()) {
                result = AccessResult::Unmapped;
                return regions.end();
            }
            --it;
            const uint64_t offset = address - it->first;
            if (offset >= it->second.size) {
                result = AccessResult::Unmapped;
                return regions.end();
            }
            if (offset + size > it->second.size) {
                result = AccessResult::OutOfBounds;
                return regions.end();
            }
            result = AccessResult::Ok;
            return it;
        }

    }  // namespace

    VirtualMemory::VirtualMemory(EventLog& log, uint64_t firstBase, uint64_t pageSize)
        : m_log(log)
        , m_nextBase(firstBase)
        , m_pageSize(pageSize)
    {
    }

    std::optional<uint64_t> VirtualMemory::Allocate(uint64_t size, Protection protection, RegionKind kind,
        const std::string& owner, const std::string& backingPath)
    {
        if (size == 0) {
            return std::nullopt;
        }
        const uint64_t rounded = (size + m_pageSize - 1) / m_pageSize * m_pageSize;

        MemoryRegion region;
        region.base = m_nextBase;
        region.size = rounded;
        region.kind = kind;
        region.protection = protection;
        region.owner = owner;
        region.backingPath = backingPath;
        region.data.assign(static_cast<size_t>(rounded), 0);

        m_regions.emplace(region.base, region);
        m_nextBase += rounded + m_pageSize;  // one unmapped guard page after every region

        m_log.Record(Severity::Info, "memory.alloc",
            "base=" + Hex(region.base) + " size=" + Hex(rounded) +
            " kind=" + ToString(kind) + " prot=" + protection.ToString());
        return region.base;
    }

    bool VirtualMemory::Free(uint64_t base)
    {
        auto it = m_regions.find(base);
        if (it == m_regions.end()) {
            m_log.Record(Severity::Low, "memory.free", "no region at " + Hex(base));
            return false;
        }
        m_log.Record(Severity::Info, "memory.free",
            "base=" + Hex(base) + " size=" + Hex(it->second.size) + " owner=" +
            (it->second.owner.empty() ? std::string("private") : it->second.owner));
        m_regions.erase(it);
        return true;
    }

    bool VirtualMemory::Protect(uint64_t base, Protection protection)
    {
        auto it = m_regions.find(base);
        if (it == m_regions.end()) {
            m_log.Record(Severity::Low, "memory.protect", "no region at " + Hex(base));
            return false;
        }
        const std::string previous = it->second.protection.ToString();
        it->second.protection = protection;
        m_log.Record(Severity::Info, "memory.protect",
            "base=" + Hex(base) + " " + previous + " -> " + protection.ToString());
        return true;
    }

    AccessResult VirtualMemory::Read(uint64_t address, size_t size, std::vector<uint8_t>& out) const
    {
        AccessResult result = AccessResult::Ok;
        auto it = FindRangeIn(m_regions, address, size, result);
        if (result != AccessResult::Ok) {
            m_log.Record(Severity::Low, "memory.access", "read " + Hex(address) + " failed: " + ToString(result));
            return result;
        }
        if (!it->second.protection.read) {
            m_log.Record(Severity::Medium, "memory.denied", "read " + Hex(address) + " denied by protection");
            return AccessResult::ProtectionDenied;
        }
        const size_t offset = static_cast<size_t>(address - it->first);
        out.assign(it->second.data.begin() + offset, it->second.data.begin() + offset + size);
        return AccessResult::Ok;
    }

    AccessResult VirtualMemory::Write(uint64_t address, const std::vector<uint8_t>& bytes)
    {
        AccessResult result = AccessResult::Ok;
        auto it = FindRangeIn(m_regions, address, bytes.size(), result);
        if (result != AccessResult::Ok) {
            m_log.Record(Severity::Low, "memory.access", "write " + Hex(address) + " failed: " + ToString(result));
            return result;
        }
        if (!it->second.protection.write) {
            m_log.Record(Severity::Medium, "memory.denied", "write " + Hex(address) + " denied by protection");
            return AccessResult::ProtectionDenied;
        }
        const size_t offset = static_cast<size_t>(address - it->first);
        std::copy(bytes.begin(), bytes.end(), it->second.data.begin() + offset);
        m_log.Record(Severity::Info, "memory.write", Hex(address) + " len=" + Hex(bytes.size()));
        return AccessResult::Ok;
    }

    AccessResult VirtualMemory::Patch(uint64_t address, const std::vector<uint8_t>& bytes)
    {
        AccessResult result = AccessResult::Ok;
        auto it = FindRangeIn(m_regions, address, bytes.size(), result);
        if (result != AccessResult::Ok) {
            return result;
        }
        const Protection original = it->second.protection;
        it->second.protection.write = true;
        const size_t offset = static_cast<size_t>(address - it->first);
        std::copy(bytes.begin(), bytes.end(), it->second.data.begin() + offset);
        it->second.protection = original;
        m_log.Record(Severity::Medium, "memory.patch",
            "out-of-band patch at " + Hex(address) + " len=" + Hex(bytes.size()));
        return AccessResult::Ok;
    }

    AccessResult VirtualMemory::ValidateAddress(uint64_t address, size_t size) const
    {
        AccessResult result = AccessResult::Ok;
        FindRangeIn(m_regions, address, size, result);
        return result;
    }

    const MemoryRegion* VirtualMemory::FindRegion(uint64_t address) const
    {
        AccessResult result = AccessResult::Ok;
        auto it = FindRangeIn(m_regions, address, 1, result);
        return result == AccessResult::Ok ? &it->second : nullptr;
    }

    void VirtualMemory::SetBaseline(uint64_t base)
    {
        auto it = m_regions.find(base);
        if (it == m_regions.end()) {
            return;
        }
        it->second.hasBaseline = true;
        it->second.baselineHash = Fnv1a64(it->second.data);
    }

    bool VirtualMemory::VerifyIntegrity(uint64_t base) const
    {
        auto it = m_regions.find(base);
        if (it == m_regions.end() || !it->second.hasBaseline) {
            return false;
        }
        return Fnv1a64(it->second.data) == it->second.baselineHash;
    }

}  // namespace sim