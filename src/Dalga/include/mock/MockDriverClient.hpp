#pragma once
#include "interface/IDriverClient.hpp"
#include <unordered_set>

namespace Xign
{
    class MockDriverClient : public IDriverClient
    {
    public:
        MockDriverClient() = default;
        ~MockDriverClient() override { Close(); }

        bool Open(const std::vector<uint8_t>& authBlob, const std::wstring& deviceName) override
        {
            (void)deviceName;
            // Simulated authentication check: fails if authBlob is empty
            if (authBlob.empty() && m_requireAuthBlob)
            {
                m_ready = false;
                return false;
            }
            m_ready = true;
            return true;
        }

        void Close() override
        {
            m_ready = false;
            m_registeredPids.clear();
        }

        bool IsReady() const override
        {
            return m_ready;
        }

        bool SendCommand(uint32_t opcode,
                         const std::vector<uint8_t>& inPayload,
                         std::vector<uint8_t>& outResponse,
                         int32_t& outStatus,
                         uint64_t& outValue) override
        {
            if (!m_ready) return false;
            (void)opcode;
            (void)inPayload;
            outResponse.resize(128, 0xAA);
            outStatus = 0;
            outValue = 0x1337;
            return true;
        }

        bool RegisterProcess(uint32_t pid) override
        {
            if (!m_ready || pid == 0) return false;
            m_registeredPids.insert(pid);
            return true;
        }

        bool OpenProcessHandle(uint32_t pid, uint64_t& outHandle) override
        {
            if (!m_ready || m_registeredPids.find(pid) == m_registeredPids.end())
            {
                outHandle = 0;
                return false;
            }
            outHandle = static_cast<uint64_t>(pid) | 0x8000000000000000ULL;
            return true;
        }

        // Test configuration helpers
        void SetRequireAuthBlob(bool req) { m_requireAuthBlob = req; }
        bool IsPidRegistered(uint32_t pid) const { return m_registeredPids.count(pid) > 0; }

    private:
        bool m_ready{ false };
        bool m_requireAuthBlob{ true };
        std::unordered_set<uint32_t> m_registeredPids;
    };
}
