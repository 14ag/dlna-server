#include "scan_cancellation.h"

ScanCancellation& ScanCancellation::Get() {
    static ScanCancellation instance;
    return instance;
}

void ScanCancellation::BeginScan() {
    m_cancelled.store(false, std::memory_order_release);
}

void ScanCancellation::RequestCancel() {
    m_cancelled.store(true, std::memory_order_release);
}

bool ScanCancellation::IsCancelled() const {
    return m_cancelled.load(std::memory_order_acquire);
}
