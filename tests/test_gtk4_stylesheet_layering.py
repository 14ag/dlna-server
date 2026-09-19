SOURCE = "src/gtk4_gui_main.cpp"


def test_win32_base_sheets_are_loaded_between_style_and_figma(repo_root):
    code = (repo_root / SOURCE).read_text()
    for sheet in ("gtk/styles2.css", "gtk/styles3.css", "gtk/styles4.css"):
        assert sheet in code
        assert (repo_root / "resources" / sheet).is_file()

    base_at = code.index('"gtk/styles2.css"')
    figma_at = code.index('"gtk/figma.css"')
    style_at = code.index('"gtk/style.css"')
    assert style_at < base_at < figma_at

    assert "GTK_STYLE_PROVIDER_PRIORITY_USER + 1" in code
    assert "GTK_STYLE_PROVIDER_PRIORITY_USER + 2" in code


def test_win32_base_sheets_are_installed(repo_root):
    cmake = (repo_root / "CMakeLists.txt").read_text()
    for sheet in ("styles2.css", "styles3.css", "styles4.css"):
        assert f"resources/gtk/{sheet}" in cmake
