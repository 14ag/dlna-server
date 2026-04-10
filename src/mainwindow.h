#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <windows.h>
#include <string>

class MainWindow {
public:
    MainWindow();
    ~MainWindow();

    bool Create(HINSTANCE hInstance, int nCmdShow);
    void SetStatus(bool running, const std::wstring& endpoint = L"");
    HWND GetHwnd() const { return m_hwnd; }

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    void RenderToolbar(HDC hdc, RECT& rect);
    void AddTrayIcon();
    void RemoveTrayIcon();
    void ShowTrayMenu();
    void OpenFolderPicker();
    void RefreshSourceList();

    HWND m_hwnd;
    HINSTANCE m_hInstance;
    HBRUSH m_hBgBrush;
    HBRUSH m_hDarkBrush;

    HWND m_hBtnAdd;
    HWND m_hBtnStartStop;
    HWND m_hBtnSettings;
    HWND m_hListSources;

    bool m_isRunning;
    std::wstring m_statusEndpoint;
};

#endif // MAINWINDOW_H
