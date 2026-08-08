#ifndef SOURCE_DROP_POLICY_H
#define SOURCE_DROP_POLICY_H

// pure decision extracted so it can be exercised by a print flag test
// on both platforms
// no windows types and no ole types so it compiles on posix too
// mirrors the same extraction pattern already used by
// source_list_focus h hover_focus_state h function_key_action h
// true means a drop is currently allowed
// false means show the no drop cursor
inline bool ShouldAllowSourceDrop(bool serverBusyOrRunning) {
    return !serverBusyOrRunning;
}

#endif // SOURCE_DROP_POLICY_H
