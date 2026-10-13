import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def read(rel):
    return (ROOT / rel).read_text(encoding="utf-8")


def test_cmake_project_version_comes_from_variable():
    text = read("CMakeLists.txt")
    assert "project(dlna-server VERSION ${DLNA_VERSION})" in text
    assert not re.search(r"project\(dlna-server VERSION \d", text)


def test_version_template_exposes_resource_macro():
    text = read("src/version.h.in")
    assert "DLNA_SERVER_VERSION_STRING" in text
    assert "DLNA_SERVER_VERSION_RC" in text


def test_resource_file_uses_macros_only():
    text = read("resources/app.rc")
    assert '#include "version.h"' in text
    assert "FILEVERSION DLNA_SERVER_VERSION_RC" in text
    assert "PRODUCTVERSION DLNA_SERVER_VERSION_RC" in text
    assert 'VALUE "FileVersion", DLNA_SERVER_VERSION_STRING' in text
    assert 'VALUE "ProductVersion", DLNA_SERVER_VERSION_STRING' in text


def test_identity_template_is_configured():
    assert "app_identity.h" in read("CMakeLists.txt")
    text = read("src/app_identity.h.in")
    assert "@DLNA_APP_ID@" in text
    assert "DLNA_MAIN_WINDOW_CLASS_W" in text
    assert "DLNA_SINGLE_INSTANCE_MUTEX_W" in text

