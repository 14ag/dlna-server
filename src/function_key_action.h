#ifndef FUNCTION_KEY_ACTION_H
#define FUNCTION_KEY_ACTION_H

// pure decision for what a function key press should do
// kept free of hwnd and windows types so it can be exercised by a print flag test
// see the workflow document task a1 for the full rationale

enum class FunctionKeyAction {
    None,
    ShowHelp,
    Rescan,
    RefreshSourceList,
    ShowSourceListContextMenu
};

// vkCode is the raw virtual key code for example seventy for f1 seventy four for f5
// isRunning mirrors MainWindow IsRunning
// isBusy mirrors MainWindow IsBusy
// isScanning mirrors the m_scanInProgress flag already tracked by MainWindow
inline FunctionKeyAction DecideFunctionKeyAction(int vkCode, bool isRunning, bool isBusy, bool isScanning) {
    constexpr int kVkF1 = 0x70;
    constexpr int kVkF5 = 0x74;
    if (vkCode == kVkF1) {
        return FunctionKeyAction::ShowHelp;
    }
    if (vkCode == kVkF5) {
        if (isBusy || isScanning) return FunctionKeyAction::None;
        return isRunning ? FunctionKeyAction::Rescan : FunctionKeyAction::RefreshSourceList;
    }
    return FunctionKeyAction::None;
}

#endif // FUNCTION_KEY_ACTION_H
