from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_server_restart_does_not_reload_config_or_fail_silently_on_endpoint_refresh():
    server = read("src/server.cpp")
    start_body = server[server.index("bool Server::Start()"):server.index("bool Server::Rescan()")]
    refresh_body = server[server.index("void Server::RefreshEndpoints"):server.index("bool Server::Start()")]

    assert "AppConfig.Load();" not in start_body
    assert 'LogPrint(L"Network endpoint enumeration failed.' in refresh_body
    assert "m_endpoints.clear();" in refresh_body


def test_windows_ssdp_start_requires_multicast_join_and_logs_hard_failures():
    ssdp = read("src/ssdp.cpp")
    start_body = ssdp[ssdp.index("bool SSDP::Start"):ssdp.index("void SSDP::Stop()")]

    assert "ipv4JoinCount" in start_body
    assert "ipv6JoinCount" in start_body
    assert "ipv4EndpointCount" in start_body
    assert "ipv6EndpointCount" in start_body
    assert "if (ipv4JoinCount == 0 && ipv6JoinCount == 0)" in start_body
    assert 'LogPrint(L"SSDP IPv4 bind failed' in start_body
    assert 'LogPrint(L"SSDP IPv4 multicast join failed' in start_body
    assert 'LogPrint(L"SSDP IPv6 multicast join failed' in start_body


def test_windows_ssdp_stop_marks_not_running_before_byebye_and_wakes_workers():
    ssdp = read("src/ssdp.cpp")
    stop_body = ssdp[ssdp.index("void SSDP::Stop()"):ssdp.index("void SSDP::CloseSockets()")]

    assert stop_body.index("m_running.store(false") < stop_body.index('SendNotifyBurst("ssdp:byebye"')
    assert "m_responseCondition.notify_all();" in stop_body


def test_windows_endpoint_enumeration_skips_non_multicast_adapters():
    netutils = read("src/netutils.cpp")

    assert "IP_ADAPTER_NO_MULTICAST" in netutils
    assert "adapter->Flags & IP_ADAPTER_NO_MULTICAST" in netutils


def test_windows_log_print_uses_one_debug_snapshot_and_one_log_lock():
    log = read("src/log.cpp")
    body = log[log.index("void LogPrint"):log.index("std::wstring GetSystemLog()")]

    assert "const bool writeDebugLog = AppConfig.IsDebugLogEnabled();" in body
    assert body.count("std::lock_guard<std::mutex> lock(g_logMutex);") == 1
    assert "if (writeDebugLog)" in body


def test_posix_log_uses_shared_utf8_converter_and_no_deprecated_codecvts():
    log = read("src/posix_log.cpp")

    assert '#include "netutils.h"' in log
    assert "WideToUtf8(AppConfig.GetConfigPath())" in log
    assert "WideToUtf8(line)" in log
    assert "codecvt" not in log
    assert "wstring_convert" not in log


def test_windows_log_dialog_refreshes_after_opening():
    logdlg = read("src/logdlg.cpp")
    resources = read("resources/app.rc")

    assert "SetTimer(hwndDlg, kLogRefreshTimerId" in logdlg
    assert "KillTimer(hwndDlg, kLogRefreshTimerId)" in logdlg
    assert "case WM_TIMER:" in logdlg
    assert "RefreshLogText(hwndDlg)" in logdlg
    assert "PUSHBUTTON      \"Refresh\",IDC_BTN_REFRESH_LOG" in resources
