import datetime
import importlib.util
import os
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
WORKFLOWS = ROOT / ".github" / "workflows"
NOW = datetime.datetime(2026, 10, 13, 14, 37)


def load_version_module():
    spec = importlib.util.spec_from_file_location("version_script", ROOT / "scripts" / "version.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


version = load_version_module()


def test_default_tag_has_no_build_suffix(tmp_path):
    assert version.select_tag(None, NOW, tmp_path) == "v2.26.7640"


def test_existing_version_file_appends_build2(tmp_path):
    (tmp_path / "dlna-server-2.26.7640-linux-x86_64.flatpak").touch()
    assert version.select_tag(None, NOW, tmp_path) == "v2.26.7640-build2"


def test_existing_build_version_increments_build_suffix(tmp_path):
    (tmp_path / "dlna-server-2.26.7640-linux-x86_64.flatpak").touch()
    (tmp_path / "dlna-server-2.26.7640-build2-linux-x86_64.flatpak").touch()
    assert version.select_tag(None, NOW, tmp_path) == "v2.26.7640-build3"


def test_version_detection_ignores_directories(tmp_path):
    (tmp_path / "dlna-server-2.26.7640").mkdir()
    assert version.select_tag(None, NOW, tmp_path) == "v2.26.7640"


def test_numeric_of_ignores_build_suffix():
    out = subprocess.run(
        [sys.executable, str(ROOT / "scripts" / "version.py"), "--numeric-of", "v2.26.7640-build6"],
        capture_output=True, text=True, check=True,
    )
    assert out.stdout.strip() == "2.26.7640"


def load_workflow(name):
    yaml = pytest.importorskip("yaml")
    doc = yaml.safe_load((WORKFLOWS / name).read_text(encoding="utf-8"))
    triggers = doc.get("on", doc.get(True))
    return doc, triggers


@pytest.mark.parametrize("name", ["build-windows.yml", "build-linux.yml", "release-notes.yml"])
def test_reusable_workflows_declare_workflow_call(name):
    _, triggers = load_workflow(name)
    assert "workflow_call" in triggers


def test_release_workflow_requires_no_input():
    _, triggers = load_workflow("release-all.yml")
    assert not (triggers["workflow_dispatch"] or {}).get("inputs")
    assert "DLNA_BUILD" not in (WORKFLOWS / "release-all.yml").read_text(encoding="utf-8")


def test_release_workflow_calls_both_builds_then_notes():
    doc, _ = load_workflow("release-all.yml")
    jobs = doc["jobs"]
    assert jobs["windows"]["uses"].endswith("build-windows.yml")
    assert jobs["linux"]["uses"].endswith("build-linux.yml")
    assert jobs["notes"]["uses"].endswith("release-notes.yml")
    assert set(jobs["publish"]["needs"]) == {"version", "windows", "linux"}


def test_no_workflow_calls_gemini_outside_notes():
    for path in WORKFLOWS.glob("*.yml"):
        if path.name != "release-notes.yml":
            assert "gemini" not in path.read_text(encoding="utf-8").lower()


@pytest.mark.posix_only
@pytest.mark.parametrize("script", ["build.sh", "scripts/release-notes.sh"])
def test_shell_scripts_parse(script):
    subprocess.run(["bash", "-n", str(ROOT / script)], check=True)


@pytest.mark.posix_only
def test_build_option_without_value_fails_before_building():
    result = subprocess.run(["bash", str(ROOT / "build.sh"), "--build"], capture_output=True, text=True)
    assert result.returncode != 0
    assert "--build needs a number" in result.stderr


@pytest.mark.posix_only
def test_release_notes_fall_back_to_commit_list_without_api_key(tmp_path):
    scripts = tmp_path / "scripts"
    scripts.mkdir()
    for name in ("release-notes.sh", "release-diff.sh"):
        shutil.copy(ROOT / "scripts" / name, scripts / name)

    def git(*args):
        subprocess.run(["git", "-c", "user.name=t", "-c", "user.email=t@t", *args],
                       cwd=tmp_path, check=True, capture_output=True)

    git("init", "-q")
    (tmp_path / "a.txt").write_text("1")
    git("add", ".")
    git("commit", "-qm", "first change")
    git("tag", "v1")
    (tmp_path / "a.txt").write_text("2")
    git("commit", "-qam", "second change")
    git("tag", "v2")

    env = {k: v for k, v in os.environ.items() if k != "GEMINI_API_KEY"}
    out = subprocess.run(["bash", "scripts/release-notes.sh", "v2"], cwd=tmp_path, env=env,
                         check=True, capture_output=True, text=True).stdout
    assert "## Changes" in out
    assert "second change" in out
    assert "first change" not in out


@pytest.mark.posix_only
@pytest.mark.parametrize(
    ("response", "expected", "success"),
    [
        ('{"candidates":[{"content":{"parts":[{"text":"## Features\\n\\n- Added feature"}]}}]}',
         "## Features", True),
        ("", "Gemini returned an invalid response", False),
        ("not json", "Gemini returned an invalid response", False),
    ],
)
def test_release_notes_handle_gemini_response_without_jq(tmp_path, response, expected, success):
    scripts = tmp_path / "scripts"
    scripts.mkdir()
    for name in ("release-notes.sh", "release-diff.sh"):
        shutil.copy(ROOT / "scripts" / name, scripts / name)

    def git(*args):
        subprocess.run(["git", "-c", "user.name=t", "-c", "user.email=t@t", *args],
                       cwd=tmp_path, check=True, capture_output=True)

    git("init", "-q")
    (tmp_path / "a.txt").write_text("1")
    git("add", ".")
    git("commit", "-qm", "first change")
    git("tag", "v1")
    (tmp_path / "a.txt").write_text("2")
    git("commit", "-qam", "second change")
    git("tag", "v2")

    fake_bin = tmp_path / "bin"
    fake_bin.mkdir()
    jq = fake_bin / "jq"
    jq.write_text("#!/bin/sh\nexit 127\n")
    jq.chmod(0o755)
    curl = fake_bin / "curl"
    curl.write_text(
        "#!/bin/sh\n"
        "while [ \"$#\" -gt 0 ]; do\n"
        "  case \"$1\" in\n"
        "    --output) output=\"$2\"; shift 2 ;;\n"
        "    --write-out) shift 2 ;;\n"
        "    *) shift ;;\n"
        "  esac\n"
        "done\n"
        "printf '%s' \"$GEMINI_RESPONSE\" > \"$output\"\n"
        "printf 200\n"
    )
    curl.chmod(0o755)

    env = os.environ.copy()
    env["GEMINI_API_KEY"] = "test-key"
    env["GEMINI_RESPONSE"] = response
    env["PATH"] = str(fake_bin) + os.pathsep + env["PATH"]
    result = subprocess.run(
        ["bash", "scripts/release-notes.sh", "v2"], cwd=tmp_path, env=env,
        capture_output=True, text=True,
    )
    assert (result.returncode == 0) is success
    if success:
        assert expected in result.stdout
    else:
        assert expected in result.stderr
