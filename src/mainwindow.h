#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <windows.h>
#include "close_pending_state.h"
#include "copydata_validation.h"
#include "access_keys.h"
#include "source_drop_target.h"
#include "source_list_focus.h"
#include "hover_focus_state.h"
#include "config.h"
#include "server_ui_state.h"
#include <string>
#include <thread>
#include <atomic>
#include <unordered_map>

class MainWindow {
public:
    MainWindow();
    ~MainWindow();

    bool Create(HINSTANCE hInstance, int nCmdShow, bool startHeadless = false);
    void SetStatus(ServerUiState state, const std::wstring& endpoint = L"");
    HWND GetHwnd() const { return m_hwnd; }

    static constexpr UINT_PTR kInitialScanPollTimerId = 1;
    // Sent by a second process instance (see main.cpp's single-instance
    // detection) to ask the already-running instance to restore/focus its
    // window through the same code path the tray icon uses, so
    // WS_EX_TOOLWINDOW (see RestoreAndFocusMainWindow) is always cleared
    // consistently regardless of which code path revealed the window.
    static constexpr UINT WM_SHOW_EXISTING_INSTANCE = WM_APP + 20;
    // asks this instance to stop the server and close entirely
    // sent by a separate short lived process launched with kill server or k
    static constexpr UINT WM_REQUEST_SHUTDOWN = WM_APP + 21;
    // sent by --kill-server: stops the server synchronously, then
    // destroys the window. unlike WM_REQUEST_SHUTDOWN this always
    // performs a full graceful teardown that emits ssdp:byebye
    // regardless of current server state.
    static constexpr UINT WM_KILL_SERVER = WM_APP + 22;
    // dwData discriminator used on the wm copydata messages this app sends
    // to itself from a second process see main cpp for the sender side
    static constexpr ULONG_PTR kCopyDataSourceReplace = 1;
    // query discriminator: the second process sends a temp-file path in
    // lpData and the running instance writes its effective media sources
    // (one per line, UTF-8) into that file before returning.
    static constexpr ULONG_PTR kCopyDataQueryEffectiveSources = 2;

    bool TryHandleAccessKeyChar(wchar_t ch);
    bool TryHandleFunctionKey(WPARAM vkCode);
    void RefreshToolbarMnemonics();

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK ListBoxProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK ToolbarButtonProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    void AddTrayIcon();
    void RemoveTrayIcon();
    void ShowTrayMenu();
    void ShowSourceListContextMenu(HWND sourceHwnd, int screenX, int screenY);
    void RestoreAndFocusMainWindow();
    // adds one path as a media source if it is not already present
    // returns true if it was actually added false if it was a duplicate
    // does not check file type validity that is the caller's job see
    // source drop target cpp for the file extension gate used by drag drop
    bool AddMediaSourceIfNew(const std::wstring& path);
    void HandleDroppedPaths(const std::vector<std::wstring>& paths);
    void OpenFolderPicker();
    void RemoveSelectedSource();
    void BeginRescan();
    void RefreshSourceList();
    void UpdateDeleteButton();
    void ArmMouseTracking(HWND hwnd);
    void UpdateControlHover(HWND hwnd, int controlId, bool entered);
    void UpdateControlFocus(int controlId, bool gained);
    void RepaintHighlightTransition(int before, int after);
    void UpdateListLayout(int width, int height);
    int SelectedSourceIndex() const;
    HFONT CreateUiFont(int pixelSize, int weight, const wchar_t* faceName);
    void DrawToolbarButton(const DRAWITEMSTRUCT* drawItem);
    void BeginStartServer();
    void BeginStopServer();
    void BeginRestartServer();
    // Interrupts a running session to serve a newly-arrived --source
    // override: stops the server (which also clears any prior override,
    // see Server::Stop()), installs the new override, then starts again.
    // No-op if the server is not currently running -- callers should use
    // AppConfig.SetRuntimeSourceOverride() + RefreshSourceList() directly
    // for the not-running case, since there is nothing to interrupt.
    void BeginSourceOverrideRestart(std::vector<MediaSource> overrideSources);
    void CompleteServerOperation(ServerUiState finalState, const std::wstring& endpoint, bool success, const std::wstring& message);
    bool IsShowingOverrideSources() const;
    bool IsBusy() const;
    bool IsRunning() const;
    void UpdateWakeLock();
    void SetControlsForState();

    HWND m_hwnd;
    HINSTANCE m_hInstance;
    HBRUSH m_hBgBrush;
    HBRUSH m_hDarkBrush;
    HBRUSH m_hToolbarBrush;
    HFONT m_hTitleFont;
    HFONT m_hBodyFont;
    HFONT m_hButtonFont;

    HWND m_hBtnAdd;
    HWND m_hBtnDelete;
    HWND m_hBtnStartStop;
    HWND m_hBtnSettings;
    HWND m_hListSources;
    std::vector<wchar_t> m_lastMnemonics;
    WNDPROC m_listOldProc;
    WNDPROC m_toolbarOldProc;

    KeyboardCueState m_cueState;
    ServerUiState m_state;
    std::wstring m_statusEndpoint;
    ClosePendingState m_closePending;
    std::thread m_worker;

    bool m_startedHeadless;
    bool m_lastPolledScanInProgress = false;
    RECT m_statusRect = {0, 0, 0, 0};
    std::atomic<bool> m_scanInProgress;
    std::atomic<bool> m_scanningStatusActive;
    int m_consecutiveUnhealthyPolls = 0;
    SourceListFocusState m_focusState;
    HoverFocusState m_hoverFocusState;
    RECT m_listRingRect = {0, 0, 0, 0};
    std::unordered_map<HWND, bool> m_mouseTracking;
    SourceListDropTarget* m_sourceDropTarget = nullptr;
    HPOWERNOTIFY m_hSuspendResumeNotify = NULL;
};

#endif // MAINWINDOW_H
