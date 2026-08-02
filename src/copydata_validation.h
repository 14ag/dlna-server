#ifndef COPYDATA_VALIDATION_H
#define COPYDATA_VALIDATION_H

// True iff cbData describes a byte buffer whose length is a whole,
// positive number of wchar_t units -- the minimum shape a WM_COPYDATA
// payload must have before it is safe to reinterpret lpData as a
// wchar_t array and construct a std::wstring from it using an
// EXPLICIT length (cbData / sizeof(wchar_t)) rather than scanning for
// a null terminator. See mainwindow.cpp's WM_COPYDATA handler and
// F-CMR-01 for the full rationale: COPYDATASTRUCT::cbData is the only
// size Microsoft's own documentation guarantees the receiver, and a
// WM_COPYDATA sender is not a trusted channel (any process in the
// same desktop session that can FindWindowW this window and knows
// dwData can send one).
//
// unsigned long, not DWORD: this header has no Windows-type
// dependency by design (matches startup_mode.h / input_gate.h /
// network_interface_policy.h in this codebase) so it compiles and is
// testable via a CLI hook on both platforms even though only the
// Win32 build ever receives a real WM_COPYDATA message.
inline bool IsPlausibleWideStringCopyDataSize(unsigned long cbData) {
    return cbData > 0 && (cbData % static_cast<unsigned long>(sizeof(wchar_t))) == 0;
}

#endif // COPYDATA_VALIDATION_H
