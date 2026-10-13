from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

REMOVED = [
    "dlna-server.appdata.xml",
    "dlna-server-gui",
    "dlna-server-wsl.bat",
    "install_linux_desktop.cmake",
    "CPackConfig.cmake",
    "CPackSourceConfig.cmake",
    "Makefile",
]
IGNORED = ["/generated/", "/Makefile", "/CMakeCache.txt", "/CMakeFiles/",
           "/cmake_install.cmake", "/CPack*.cmake", "/_CPack_Packages/",
           "/packaging/flatpak/*.stamped.yml"]


def test_redundant_root_files_are_gone():
    for name in REMOVED:
        assert not (ROOT / name).exists(), name


def test_no_root_cmake_scripts():
    assert [p.name for p in ROOT.glob("*.cmake")] == []


def test_no_in_source_build_output():
    for name in ["generated", "CMakeFiles", "CMakeCache.txt", "cmake_install.cmake"]:
        assert not (ROOT / name).exists(), name


def test_gitignore_blocks_regeneration():
    lines = (ROOT / ".gitignore").read_text(encoding="utf-8").splitlines()
    for entry in IGNORED:
        assert entry in lines, entry


def test_wired_root_files_still_exist():
    for name in ["CMakeLists.txt", "build.sh", "build.bat", "pytest.ini"]:
        assert (ROOT / name).is_file(), name
