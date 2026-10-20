import gzip
import os
import shutil
import subprocess
import sys

import pytest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GENERATOR = os.path.join(ROOT, "tools", "gen_cli_docs.py")
HEADER = os.path.join(ROOT, "src", "cli_flags.h")
USER_FLAGS = ["--headless", "-h", "--port", "--name", "--uuid", "--source",
              "--kill-server", "-k", "--debug", "--no-debug", "--help"]


@pytest.fixture(scope="module")
def docs(tmp_path_factory):
    out = tmp_path_factory.mktemp("cli-docs")
    subprocess.run([sys.executable, GENERATOR, "--header", HEADER, "--product", "dlna-server",
                    "--version", "1.2.3", "--date", "2026-10-20", "--out", str(out)], check=True)
    return out


def read_man(docs, name):
    with gzip.open(str(docs / "man" / (name + ".1.gz")), "rt", encoding="utf-8") as handle:
        return handle.read()


def test_all_files_exist(docs):
    for prog in ("dlna-server", "dlna-server-gui"):
        assert (docs / "man" / (prog + ".1.gz")).is_file()
        assert (docs / "completions" / "bash" / prog).is_file()
        assert (docs / "completions" / "zsh" / ("_" + prog)).is_file()
        assert (docs / "completions" / "fish" / (prog + ".fish")).is_file()


def test_man_is_gzip_minus_9n(docs):
    data = (docs / "man" / "dlna-server.1.gz").read_bytes()
    assert data[3] == 0
    assert data[4:8] == b"\x00\x00\x00\x00"


def test_man_lists_every_user_flag(docs):
    for prog in ("dlna-server", "dlna-server-gui"):
        text = read_man(docs, prog).replace("\\-", "-")
        for flag in USER_FLAGS:
            assert flag in text, (prog, flag)


def test_completions_list_every_user_flag(docs):
    for flag in USER_FLAGS:
        assert flag in (docs / "completions" / "bash" / "dlna-server").read_text()
        assert flag.lstrip("-") in (docs / "completions" / "fish" / "dlna-server.fish").read_text()
    zsh = (docs / "completions" / "zsh" / "_dlna-server").read_text()
    assert zsh.startswith("#compdef dlna-server")


@pytest.mark.posix_only
@pytest.mark.skipif(shutil.which("bash") is None, reason="bash missing")
def test_bash_completion_behaviour(docs, tmp_path):
    script = str(docs / "completions" / "bash" / "dlna-server")
    (tmp_path / "zzmedia.mp4").write_text("x")

    def complete(words, index):
        program = ('source "%s"; COMP_WORDS=(%s); COMP_CWORD=%d; _dlna_server; '
                   'printf "%%s\\n" "${COMPREPLY[@]}"') % (script, " ".join(words), index)
        result = subprocess.run(["bash", "-c", program], cwd=str(tmp_path),
                                capture_output=True, text=True, check=True)
        return result.stdout.split()

    assert set(complete(["dlna-server", "--he"], 1)) == {"--headless", "--help"}
    assert complete(["dlna-server", "--port", ""], 2) == []
    assert complete(["dlna-server", "--source", "zz"], 2) == ["zzmedia.mp4"]


@pytest.mark.posix_only
def test_syntax_of_generated_scripts(docs):
    if shutil.which("bash"):
        subprocess.run(["bash", "-n", str(docs / "completions" / "bash" / "dlna-server-gui")], check=True)
    if shutil.which("zsh"):
        subprocess.run(["zsh", "-n", str(docs / "completions" / "zsh" / "_dlna-server")], check=True)
    if shutil.which("fish"):
        subprocess.run(["fish", "--no-execute", str(docs / "completions" / "fish" / "dlna-server.fish")], check=True)


@pytest.mark.posix_only
def test_man_page_lint(docs):
    path = str(docs / "man" / "dlna-server.1.gz")
    if shutil.which("mandoc"):
        result = subprocess.run(["mandoc", "-T", "lint", path], capture_output=True, text=True)
        assert result.returncode in (0, 1), result.stdout + result.stderr
        assert "ERROR" not in result.stderr
    elif shutil.which("groff"):
        subprocess.run(["groff", "-man", "-ww", "-z", path], check=True)
    else:
        pytest.skip("no man page linter")
