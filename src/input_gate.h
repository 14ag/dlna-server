#ifndef INPUT_GATE_H
#define INPUT_GATE_H

#include <initializer_list>
#include <vector>

// True when at least one of the given field lengths is non-zero. Used to
// gate a submit/Add button that should stay disabled until the user has
// typed something into at least one of a fixed set of input fields.
inline bool AnyFieldHasContent(std::initializer_list<int> fieldLengths) {
    for (int len : fieldLengths) {
        if (len > 0) return true;
    }
    return false;
}

// Vector overload for callers that assemble field lengths dynamically
// (the --print-any-field-has-content CLI test hook parses a
// comma-separated list of unknown size from argv and cannot safely
// construct a std::initializer_list from a runtime-sized buffer).
inline bool AnyFieldHasContent(const std::vector<int>& fieldLengths) {
    for (int len : fieldLengths) {
        if (len > 0) return true;
    }
    return false;
}

#endif // INPUT_GATE_H
