import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class ReleaseOutputLayoutTests(unittest.TestCase):
    def read(self, path):
        return (ROOT / path).read_text(encoding="utf-8")

    def test_linux_release_script_keeps_artifacts_under_linux_folder(self):
        script = self.read("scripts/build-linux-assets.sh")

        self.assertIn('platform_dir=${DLNA_LINUX_PLATFORM_DIR:-"$repo_root/output/linux"}', script)
        self.assertIn('release_stage_dir=${DLNA_LINUX_STAGE_DIR:-"$repo_root/build-release-linux-stage"}', script)
        self.assertIn('install_dir=${DLNA_LINUX_INSTALL_DIR:-"$release_stage_dir/install"}', script)
        self.assertIn('output_dir=${DLNA_OUTPUT_DIR:-"$platform_dir"}', script)
        self.assertIn('tools_dir=${DLNA_RELEASE_TOOLS_DIR:-"$repo_root/build-release-tools/linux"}', script)
        self.assertIn('cpack --config "$build_dir/CPackConfig.cmake" -B "$output_dir"', script)
        self.assertIn('appdir="$release_stage_dir/dlna-server.AppDir"', script)
        self.assertIn('flatpak_bundle="$output_dir/dlna-server-${version}-linux-x86_64.flatpak"', script)
        self.assertNotIn('tools_dir="$output_dir/tools"', script)

    def test_macos_release_script_writes_dmg_to_platform_folder(self):
        script = self.read("scripts/build-macos-dmg.sh")

        self.assertIn('platform_name=macos-x64', script)
        self.assertIn('platform_dir=${DLNA_MACOS_PLATFORM_DIR:-"$repo_root/output/$platform_name"}', script)
        self.assertIn('install_dir=${DLNA_MACOS_INSTALL_DIR:-"$platform_dir/install"}', script)
        self.assertIn('artifact_arch=x64', script)
        self.assertIn('dmg_path="$platform_dir/DLNA_Server-${version}-macos-${artifact_arch}.dmg"', script)
        self.assertNotIn('dmg_path="$output_dir/DLNA_Server-${version}-macos-${arch}.dmg"', script)

    def test_release_workflow_uploads_from_platform_dirs(self):
        workflow = self.read(".github/workflows/release-assets.yml")

        self.assertIn("scripts\\build-release-assets.ps1 --platform winx64,winx86", workflow)
        self.assertIn("output/winx64/*.zip", workflow)
        self.assertIn("output/winx86/*.zip", workflow)
        self.assertIn("DLNA_OUTPUT_DIR: output/linux", workflow)
        self.assertIn("output/linux/*.deb", workflow)
        self.assertIn("output/linux/*.AppImage", workflow)
        self.assertIn("output/linux/*.flatpak", workflow)
        self.assertIn("platform: macos-x64", workflow)
        self.assertIn("platform: macos-arm64", workflow)
        self.assertIn("DLNA_MACOS_PLATFORM_DIR: output/${{ matrix.platform }}", workflow)
        self.assertIn("output/${{ matrix.platform }}/*.dmg", workflow)
        self.assertNotIn("output/SHA256SUMS.txt", workflow)

    def test_smoke_scripts_prefer_winx64_output(self):
        conftest = self.read("tests/conftest.py")
        self.assertIn('repo_root / "output" / "winx64" / "DLNA Server.exe"', conftest)

    def test_pytest_entrypoints_set_platform_binaries(self):
        conftest = self.read("tests/conftest.py")

        self.assertIn('os.environ.setdefault("DLNA_SERVER", server_path)', conftest)
        self.assertIn('os.environ.setdefault("DLNA_CLI_BINARY", server_path)', conftest)
        self.assertIn('os.environ.setdefault("DLNA_GUI_BINARY", server_path)', conftest)
        self.assertIn('build-release-linux-stage" / "install" / "bin" / "dlna-server"', conftest)
        self.assertIn('build-release-linux-stage" / "install" / "bin" / "dlna-server-gui-bin"', conftest)

if __name__ == "__main__":
    unittest.main()
