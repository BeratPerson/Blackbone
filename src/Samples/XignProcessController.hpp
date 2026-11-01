#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include "interface/IDriverClient.hpp"

namespace XignSamples
{
    enum class InjectionStep
    {
        Idle,
        ValidatingInputs,
        AttachingProcess,
        WaitingMainModule,
        WaitingAntiCheatModule,
        SuspendingProcess,
        UnlinkingPeb,
        ResumingProcess,
        DriverRegistration,
        MappingImage,
        PostMappingCleanup,
        DetachingProcess,
        Completed,
        Failed
    };

    struct InjectionResult
    {
        bool Success{ false };
        InjectionStep FailedStep{ InjectionStep::Idle };
        uint32_t LastWin32Error{ 0 };
        int32_t LastNtStatus{ 0 };
        uintptr_t MappedBase{ 0 };
        uint32_t MappedSize{ 0 };
        std::wstring StatusMessage{};
    };

    using LogCallback = std::function<void(const wchar_t* message)>;

    class XignProcessController
    {
    public:
        explicit XignProcessController(std::shared_ptr<Xign::IDriverClient> driverClient = nullptr);
        ~XignProcessController();

        void SetLogCallback(LogCallback callback);
        void SetDriverClient(std::shared_ptr<Xign::IDriverClient> driverClient);
        std::shared_ptr<Xign::IDriverClient> GetDriverClient() const;

        bool InitializeDriver(const std::vector<uint8_t>& wbmfBytes, const std::wstring& deviceName = L"");
        void ShutdownDriver();
        bool IsDriverReady() const;

        InjectionResult ExecuteInjection(uint32_t pid, const std::wstring& dllPath);

    private:
        void Log(const wchar_t* fmt, ...);

        std::shared_ptr<Xign::IDriverClient> m_driverClient;
        LogCallback m_logCallback;
    };
}
