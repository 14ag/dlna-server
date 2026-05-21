#!/usr/bin/env python3
"""Small dependency-light GUI for the POSIX DLNA server."""

from __future__ import annotations

import os
import queue
import signal
import socket
import subprocess
import sys
import threading
import uuid
from dataclasses import dataclass, field
from pathlib import Path


def default_server_name() -> str:
    return socket.gethostname() or os.environ.get("HOSTNAME") or "dlna-server"


def _split_sources(value: str) -> list[str]:
    return [item for item in value.split("|") if item]


def _as_bool(value: str, fallback: bool = False) -> bool:
    if value == "":
        return fallback
    return value.strip().lower() in {"1", "true", "yes", "on"}


@dataclass
class ServerConfig:
    server_name: str = field(default_factory=default_server_name)
    port: int = 8200
    file_server_port: int = 8201
    flat_folder_style: bool = False
    show_file_names: bool = False
    proxy_streams: bool = False
    sort_by_title: bool = False
    hide_all_media_folders: bool = False
    add_artist_album_folders: bool = False
    debug_log: bool = False
    ip_whitelist: str = ""
    device_uuid: str = field(default_factory=lambda: str(uuid.uuid4()))
    run_on_boot: bool = False
    media_sources: list[str] = field(default_factory=list)


class ConfigStore:
    def __init__(self, path: Path) -> None:
        self.path = path

    def load(self) -> ServerConfig:
        values: dict[str, str] = {}
        if self.path.exists():
            text = self.path.read_text(encoding="utf-8-sig")
            section = ""
            for raw_line in text.splitlines():
                line = raw_line.strip()
                if not line or line[0] in "#;":
                    continue
                if line.startswith("[") and line.endswith("]"):
                    section = line[1:-1]
                    continue
                if section != "Settings" or "=" not in line:
                    continue
                key, value = line.split("=", 1)
                values[key.strip()] = value

        config = ServerConfig()
        config.server_name = values.get("ServerName", config.server_name) or config.server_name
        config.port = self._parse_int(values.get("Port"), config.port)
        config.file_server_port = self._parse_int(values.get("FileServerPort"), config.file_server_port)
        config.flat_folder_style = _as_bool(values.get("FlatFolderStyle", ""), config.flat_folder_style)
        config.show_file_names = _as_bool(values.get("ShowFileNamesInsteadOfTitles", ""), config.show_file_names)
        config.proxy_streams = _as_bool(values.get("ProxyStreams", ""), config.proxy_streams)
        config.sort_by_title = _as_bool(values.get("SortByTitle", ""), config.sort_by_title)
        config.hide_all_media_folders = _as_bool(values.get("DoNotShowAllMediaFolders", ""), config.hide_all_media_folders)
        config.add_artist_album_folders = _as_bool(values.get("AddArtistAlbumFolders", ""), config.add_artist_album_folders)
        config.debug_log = _as_bool(values.get("DebugLog", ""), config.debug_log)
        config.run_on_boot = _as_bool(values.get("RunOnBoot", ""), config.run_on_boot)
        config.ip_whitelist = values.get("IPWhiteList", config.ip_whitelist)
        config.device_uuid = values.get("DeviceUUID", config.device_uuid) or config.device_uuid
        config.media_sources = _split_sources(values.get("MediaSources", ""))
        return config

    def save(self, config: ServerConfig) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        lines = [
            "[Settings]",
            f"ServerName={config.server_name}",
            f"Port={config.port}",
            f"FileServerPort={config.file_server_port}",
            f"FlatFolderStyle={int(config.flat_folder_style)}",
            f"ShowFileNamesInsteadOfTitles={int(config.show_file_names)}",
            f"ProxyStreams={int(config.proxy_streams)}",
            f"SortByTitle={int(config.sort_by_title)}",
            f"DoNotShowAllMediaFolders={int(config.hide_all_media_folders)}",
            f"AddArtistAlbumFolders={int(config.add_artist_album_folders)}",
            f"DebugLog={int(config.debug_log)}",
            f"RunOnBoot={int(config.run_on_boot)}",
            f"IPWhiteList={config.ip_whitelist}",
            f"DeviceUUID={config.device_uuid}",
            f"MediaSources={'|'.join(config.media_sources)}",
            "",
        ]
        self.path.write_text("\n".join(lines), encoding="utf-8")

    @staticmethod
    def _parse_int(value: str | None, fallback: int) -> int:
        if not value:
            return fallback
        try:
            return int(value)
        except ValueError:
            return fallback


def find_server_binary() -> Path:
    env_path = os.environ.get("DLNA_SERVER_BIN")
    if env_path:
        return Path(env_path).expanduser().resolve()

    script_dir = Path(__file__).resolve().parent
    candidates = [
        script_dir / "dlna-server",
        script_dir.parent / "dlna-server",
        script_dir.parent.parent / "bin" / "dlna-server",
        Path.cwd() / "dlna-server",
        Path.cwd() / "build" / "dlna-server",
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate.resolve()
    return candidates[0].resolve()


class DlnaGui:
    def __init__(self, root: "tk.Tk", server_bin: Path, config_store: ConfigStore) -> None:
        self.root = root
        self.server_bin = server_bin
        self.config_store = config_store
        self.config = config_store.load()
        self.process: subprocess.Popen[str] | None = None
        self.log_queue: queue.Queue[str] = queue.Queue()

        self._build_ui()
        self._load_config_into_widgets()
        self._poll_log_queue()
        self.root.protocol("WM_DELETE_WINDOW", self.close)

    def _build_ui(self) -> None:
        import tkinter as tk
        from tkinter import ttk

        self.root.title("DLNA Server")
        self.root.minsize(640, 460)

        main = ttk.Frame(self.root, padding=12)
        main.grid(row=0, column=0, sticky="nsew")
        self.root.columnconfigure(0, weight=1)
        self.root.rowconfigure(0, weight=1)
        main.columnconfigure(1, weight=1)
        main.rowconfigure(4, weight=1)

        ttk.Label(main, text="Name").grid(row=0, column=0, sticky="w", pady=(0, 6))
        self.name_var = tk.StringVar()
        ttk.Entry(main, textvariable=self.name_var).grid(row=0, column=1, sticky="ew", pady=(0, 6))

        ttk.Label(main, text="Port").grid(row=1, column=0, sticky="w", pady=(0, 6))
        self.port_var = tk.StringVar()
        ttk.Spinbox(main, from_=1024, to=65535, textvariable=self.port_var, width=8).grid(row=1, column=1, sticky="w", pady=(0, 6))

        self.debug_var = tk.BooleanVar()
        ttk.Checkbutton(main, text="Debug log", variable=self.debug_var).grid(row=2, column=1, sticky="w", pady=(0, 10))

        ttk.Label(main, text="Media folders").grid(row=3, column=0, sticky="nw")
        source_frame = ttk.Frame(main)
        source_frame.grid(row=3, column=1, rowspan=2, sticky="nsew")
        source_frame.columnconfigure(0, weight=1)
        source_frame.rowconfigure(0, weight=1)
        self.sources = tk.Listbox(source_frame, height=7)
        self.sources.grid(row=0, column=0, columnspan=3, sticky="nsew")
        ttk.Button(source_frame, text="Add", command=self.add_source).grid(row=1, column=0, sticky="ew", pady=(6, 0), padx=(0, 4))
        ttk.Button(source_frame, text="Remove", command=self.remove_source).grid(row=1, column=1, sticky="ew", pady=(6, 0), padx=4)
        ttk.Button(source_frame, text="Save", command=self.save).grid(row=1, column=2, sticky="ew", pady=(6, 0), padx=(4, 0))

        controls = ttk.Frame(main)
        controls.grid(row=5, column=0, columnspan=2, sticky="ew", pady=(12, 8))
        controls.columnconfigure(2, weight=1)
        self.start_button = ttk.Button(controls, text="Start", command=self.start)
        self.start_button.grid(row=0, column=0, padx=(0, 6))
        self.stop_button = ttk.Button(controls, text="Stop", command=self.stop, state="disabled")
        self.stop_button.grid(row=0, column=1, padx=(0, 12))
        self.status_var = tk.StringVar(value="Stopped")
        ttk.Label(controls, textvariable=self.status_var).grid(row=0, column=2, sticky="w")

        self.log = tk.Text(main, height=9, wrap="word", state="disabled")
        self.log.grid(row=6, column=0, columnspan=2, sticky="nsew")
        main.rowconfigure(6, weight=1)

    def _load_config_into_widgets(self) -> None:
        self.name_var.set(self.config.server_name)
        self.port_var.set(str(self.config.port))
        self.debug_var.set(self.config.debug_log)
        for source in self.config.media_sources:
            self.sources.insert("end", source)

    def _read_widgets(self) -> ServerConfig:
        config = self.config
        config.server_name = self.name_var.get().strip() or default_server_name()
        config.port = ConfigStore._parse_int(self.port_var.get(), 8200)
        config.debug_log = bool(self.debug_var.get())
        config.media_sources = [self.sources.get(i) for i in range(self.sources.size())]
        return config

    def add_source(self) -> None:
        from tkinter import filedialog

        path = filedialog.askdirectory(title="Choose media folder")
        if path and path not in [self.sources.get(i) for i in range(self.sources.size())]:
            self.sources.insert("end", path)

    def remove_source(self) -> None:
        selected = list(self.sources.curselection())
        selected.reverse()
        for index in selected:
            self.sources.delete(index)

    def save(self) -> None:
        self.config = self._read_widgets()
        self.config_store.save(self.config)
        self._append_log(f"Saved {self.config_store.path}")

    def start(self) -> None:
        from tkinter import messagebox

        if self.process and self.process.poll() is None:
            return
        if not self.server_bin.exists():
            messagebox.showerror("DLNA Server", f"Server binary not found:\n{self.server_bin}")
            return
        self.config = self._read_widgets()
        if not self.config.media_sources:
            messagebox.showerror("DLNA Server", "Add at least one media folder.")
            return
        self.config_store.save(self.config)
        try:
            self.process = subprocess.Popen(
                [str(self.server_bin)],
                cwd=str(self.server_bin.parent),
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                bufsize=1,
            )
        except OSError as exc:
            messagebox.showerror("DLNA Server", str(exc))
            return

        self.status_var.set("Starting")
        self.start_button.configure(state="disabled")
        self.stop_button.configure(state="normal")
        threading.Thread(target=self._collect_logs, daemon=True).start()

    def stop(self) -> None:
        if not self.process:
            return
        if self.process.poll() is None:
            self.process.send_signal(signal.SIGTERM)
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=5)
        self.status_var.set("Stopped")
        self.start_button.configure(state="normal")
        self.stop_button.configure(state="disabled")

    def close(self) -> None:
        self.stop()
        self.root.destroy()

    def _collect_logs(self) -> None:
        assert self.process is not None
        assert self.process.stdout is not None
        for line in self.process.stdout:
            self.log_queue.put(line.rstrip())
        code = self.process.wait()
        self.log_queue.put(f"Server exited with code {code}")

    def _poll_log_queue(self) -> None:
        while True:
            try:
                line = self.log_queue.get_nowait()
            except queue.Empty:
                break
            self._append_log(line)
            if "DLNA server running on" in line:
                self.status_var.set(line)
            elif line.startswith("Server exited"):
                self.status_var.set("Stopped")
                self.start_button.configure(state="normal")
                self.stop_button.configure(state="disabled")
        self.root.after(200, self._poll_log_queue)

    def _append_log(self, line: str) -> None:
        self.log.configure(state="normal")
        self.log.insert("end", line + "\n")
        self.log.see("end")
        self.log.configure(state="disabled")


def main() -> int:
    try:
        import tkinter as tk
        from tkinter import messagebox
    except ImportError as exc:
        print(f"Tkinter GUI support is not installed: {exc}", file=sys.stderr)
        return 1

    server_bin = find_server_binary()
    config_store = ConfigStore(server_bin.parent / "config.ini")
    root = tk.Tk()
    try:
        DlnaGui(root, server_bin, config_store)
    except Exception as exc:
        messagebox.showerror("DLNA Server", str(exc))
        return 1
    root.mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
