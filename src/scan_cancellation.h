#ifndef SCAN_CANCELLATION_H
#define SCAN_CANCELLATION_H

#include <atomic>

class ScanCancellation {
public:
    static ScanCancellation& Get();

    void BeginScan();

    void RequestCancel();

    bool IsCancelled() const;

private:
    ScanCancellation() = default;
    std::atomic<bool> m_cancelled{false};
};

#define AppScanCancel ScanCancellation::Get()

#endif // SCAN_CANCELLATION_H
