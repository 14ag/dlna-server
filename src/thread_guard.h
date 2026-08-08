#ifndef THREAD_GUARD_H
#define THREAD_GUARD_H

#include "log.h"
#include <exception>

// Runs fn() and converts any exception that escapes it into a logged
// error instead of an unhandled exception. This matters specifically
// because fn() is always the top-level entry point of a std::thread:
// per [thread.thread.constr] in the C++ standard, if an exception
// propagates out of the function passed to std::thread, the library
// calls std::terminate() -- there is no stack unwinding into the
// thread that joins it, no destructor runs first, and nothing is
// printed anywhere by default. The process ends immediately with no
// diagnostic. See F-CRASH-01/F-CRASH-02 in
// dlna-server-stability-and-audit-workflow-31-7-26.md.
//
// Wrap every std::thread/detached-thread entry point that can reach
// allocation, container mutation, or third-party library calls
// (libcurl, FLTK, file I/O) with this. threadName is a short constant
// identifying the call site in the log; it is not an OS thread name.
template <typename F>
void RunGuarded(const wchar_t* threadName, F&& fn) {
    try {
        fn();
    } catch (const std::exception& ex) {
        LogPrint(L"[fatal-guard] Unhandled exception on thread '%ls': %hs", threadName, ex.what());
    } catch (...) {
        LogPrint(L"[fatal-guard] Unknown unhandled exception on thread '%ls'.", threadName);
    }
}

#endif // THREAD_GUARD_H
