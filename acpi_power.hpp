#pragma once

#include <vector>

class AcpiPowerKeyWatcher {
public:
    AcpiPowerKeyWatcher() = default;
    AcpiPowerKeyWatcher(const AcpiPowerKeyWatcher&) = delete;
    AcpiPowerKeyWatcher& operator=(const AcpiPowerKeyWatcher&) = delete;
    ~AcpiPowerKeyWatcher();

    bool Init();
    bool WaitForPowerKey(int timeout_ms) const;
    bool IsAvailable() const;

private:
    std::vector<int> fds_;
};
