#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <windows.h>
#include "access_keys.h"
#include "source_list_focus.h"
#include "hover_focus_state.h"
#include <string>
#include <thread>
#include <atomic>
#include <unordered_map>

enum class ServerUiState {
    Stopped,
    Starting,
    Running,
    Stopping
};

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

    bool TryHandleAccessKeyChar(wchar_t ch);
    void RefreshToolbarMnemonics();

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK ListBoxProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK ToolbarButtonProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    void AddTrayIcon();
    void RemoveTrayIcon();
    void ShowTrayMenu();
    void RestoreAndFocusMainWindow();
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
    void CompleteServerOperation(ServerUiState finalState, const std::wstring& endpoint, bool success, const std::wstring& message);
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
    std::thread m_worker;

    bool m_startedHeadless;
    bool m_lastPolledScanInProgress = false;
    RECT m_statusRect = {0, 0, 0, 0};
    std::atomic<bool> m_scanInProgress;
    std::atomic<bool> m_scanningStatusActive;
    SourceListFocusState m_focusState;
    HoverFocusState m_hoverFocusState;
    RECT m_listRingRect = {0, 0, 0, 0};
    std::unordered_map<HWND, bool> m_mouseTracking;
};

#endif // MAINWINDOW_H
