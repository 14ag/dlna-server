import os
import shutil
import subprocess
import time
from pathlib import Path

import pytest


@pytest.fixture
def binary_dir(dlna_binary):
    return Path(dlna_binary).parent.resolve()


@pytest.fixture
def config_backup(binary_dir):
    config_path = binary_dir / "config.ini"
    backup = None
    if config_path.exists():
        backup = config_path.read_bytes()
    yield config_path, backup
    if backup is not None:
        config_path.write_bytes(backup)
    elif config_path.exists():
        config_path.unlink(missing_ok=True)


def run_print(binary, binary_dir, *args):
    result = subprocess.run(
        [str(binary), *args],
        cwd=str(binary_dir),
        capture_output=True,
        text=True,
        timeout=15,
    )
    return result.stdout.strip().splitlines()


def write_config(config_path, media_source_path, port):
    config_path.write_text(
        "[Settings]\n"
        f"Port={port}\n"
        f"MediaSources=\"{media_source_path}\"\n"
    )


# --- Phase 1: effectiveMediaSources plumbing -------------------------------

def test_effective_media_sources_falls_back_to_config_when_no_override(
    dlna_binary, binary_dir, tmp_path, config_backup
):
    src = tmp_path / "configured"
    src.mkdir()
    config_path, _ = config_backup
    write_config(config_path, str(src), 18201)

    out = run_print(dlna_binary, binary_dir, "--print-effective-media-sources")
    assert out == [str(src)]


def test_effective_media_sources_reflects_override_not_config(
    dlna_binary, binary_dir, tmp_path, config_backup
):
    configured = tmp_path / "configured"
    configured.mkdir()
    config_path, _ = config_backup
    write_config(config_path, str(configured), 18202)

    override = tmp_path / "override"
    override.mkdir()

    # --source must precede --print-effective-media-sources: CLI args are
    # processed in order, and the override must be installed before the
    # print hook reads the snapshot.
    out = run_print(
        dlna_binary,
        binary_dir,
        "--source", f'"{override}"',
        "--print-effective-media-sources",
    )
    assert out == [str(override)]

    # the raw, persisted list must be unaffected by the override
    out_raw = run_print(dlna_binary, binary_dir, "--print-media-sources")
    assert out_raw == [str(configured)]


def test_override_with_multiple_quoted_comma_paths(
    dlna_binary, binary_dir, tmp_path, config_backup
):
    config_path, _ = config_backup
    write_config(config_path, str(tmp_path / "unused"), 18203)
    a = tmp_path / "a"; a.mkdir()
    b = tmp_path / "b"; b.mkdir()

    out = run_print(
        dlna_binary,
        binary_dir,
        "--source", f'"{a}","{b}"',
        "--print-effective-media-sources",
    )
    assert out == [str(a), str(b)]


# --- Phase 2: clearing on stop (isolated, no live server) ------------------

def test_clear_override_reverts_to_config_sources(
    dlna_binary, binary_dir, tmp_path, config_backup
):
    configured = tmp_path / "configured"
    configured.mkdir()
    config_path, _ = config_backup
    write_config(config_path, str(configured), 18204)
    override = tmp_path / "override"
    override.mkdir()

    out = run_print(
        dlna_binary,
        binary_dir,
        "--source", f'"{override}"',
        "--print-clear-override-then-effective",
    )
    assert out == [str(configured)]


# --- Phase 2 integration: real Start()/Stop() cycle (POSIX only) -----------

@pytest.mark.windows_only
def test_stop_clears_override_across_a_real_start_stop_cycle(
    dlna_binary, binary_dir, tmp_path, config_backup
):
    configured = tmp_path / "configured"
    configured.mkdir()
    config_path, _ = config_backup
    write_config(config_path, str(configured), 18205)
    override = tmp_path / "override"
    override.mkdir()

    out = run_print(
        dlna_binary,
        binary_dir,
        "--print-source-override-lifecycle", str(override),
    )
    assert "--override-active--" in out
    assert "--after-stop--" in out
    active_idx = out.index("--override-active--")
    after_idx = out.index("--after-stop--")
    assert out[active_idx + 1: after_idx] == [str(override)]
    assert out[after_idx + 1:] == [str(configured)]


# --- Phase 1: Server::Start() no longer requires cfg.mediaSources ----------

@pytest.mark.posix_only
def test_start_succeeds_with_only_an_override_and_empty_config_sources(
    dlna_binary, binary_dir, tmp_path
):
    config_path = binary_dir / "config.ini"
    old = None
    if config_path.exists():
        old = config_path.read_bytes()
    try:
        config_path.write_text("[Settings]\nPort=18206\n")
        override = tmp_path / "override"
        override.mkdir()

        proc = subprocess.Popen(
            [str(dlna_binary), "--source", f'"{override}"'],
            cwd=str(binary_dir),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        try:
            # posix_main.cpp blocks on a signal after a successful Start(); a
            # short liveness wait plus "process has not exited" is a sufficient
            # black-box signal that Start() did not hit the "No media sources
            # configured" early-return (which would have exited immediately
            # with a nonzero code).
            time.sleep(1.0)
            assert proc.poll() is None, "process exited early: Start() likely rejected the override"
        finally:
            proc.terminate()
            proc.wait(timeout=5)
    finally:
        if old is not None:
            config_path.write_bytes(old)
        elif config_path.exists():
            config_path.unlink(missing_ok=True)
