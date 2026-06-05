import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class ReleaseOutputLayoutTests(unittest.TestCase):
    def read(self, path):
        return (ROOT / path).read_text(encoding="utf-8")

    def test_windows_release_script_uses_platform_dirs_and_flags(self):
        script = self.read("scripts/build-release-assets.ps1")
        batch = self.read("build-assets.bat")

        self.assertIn('[string]$Platform = "winx64,winx86,linux,macos-x64,macos-arm64"', script)
        self.assertIn('[Alias("no-clean")]', script)
        self.assertIn('$allPlatforms = @("winx64", "winx86", "linux", "macos-x64", "macos-arm64")', script)
        self.assertNotIn("function Get-AvailablePlatforms", script)
        self.assertNotIn('"all"', script)
        self.assertNotIn('"all-known"', script)
        self.assertNotIn('"macos-x86_64"', script)
        self.assertIn('"winx64"', script)
        self.assertIn('"winx86"', script)
        self.assertIn('Join-Path $output "winx64"', script)
        self.assertIn('Join-Path $output "winx86"', script)
        self.assertIn('Join-Path $output "macos-x64"', script)
        self.assertIn('dlna-server-$Version-windows-x86_64.zip', script)
        self.assertIn('dlna-server-$Version-windows-x86.zip', script)
        self.assertNotIn('Remove-RootNonReleaseFiles', script)
        self.assertNotIn('Remove-RootPlatformArtifacts', script)
        self.assertNotIn('Remove-LegacyOutputFolders', script)
        self.assertNotIn('Get-ChildItem -LiteralPath $output -File', script)
        self.assertNotIn('SHA256SUMS.txt', script)
        self.assertNotIn('Join-Path $output "windows/DLNA Server.exe"', script)
        self.assertNotIn('Join-Path $output "windows-x86/DLNA Server.exe"', script)
        self.assertIn("--platform", batch)
        self.assertIn("--no-clean", batch)
        self.assertNotIn("wsl.exe not found. Linux assets require WSL.", batch)
        self.assertNotIn("all-known", batch)

    def test_linux_release_script_keeps_artifacts_under_linux_folder(self):
        script = self.read("scripts/build-linux-desktop-assets.sh")

        self.assertIn('platform_dir=${DLNA_LINUX_PLATFORM_DIR:-"$repo_root/output/linux"}', script)
        self.assertIn('install_dir=${DLNA_LINUX_INSTALL_DIR:-"$platform_dir/install"}', script)
        self.assertIn('output_dir=${DLNA_OUTPUT_DIR:-"$platform_dir"}', script)
        self.assertIn('tools_dir=${DLNA_RELEASE_TOOLS_DIR:-"$repo_root/build-release-tools/linux"}', script)
        self.assertIn('cpack --config "$build_dir/CPackConfig.cmake" -B "$output_dir"', script)
        self.assertIn('appdir="$output_dir/dlna-server.AppDir"', script)
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
        smoke = self.read("tests/verify-smoke.ps1")
        android = self.read("tests/verify-android-smoke.ps1")

        self.assertIn('Join-Path $repo "output\\winx64"', smoke)
        self.assertIn('Join-Path $repo "output\\winx64"', android)
        self.assertIn('if (-not (Test-Path -LiteralPath $exePath))', smoke)
        self.assertIn('Test-Path -LiteralPath $exePath', android)

    def test_readme_documents_platform_output_contract(self):
        readme = self.read("README.md")

        self.assertIn("Platform-specific release files are written under `output/<platform>/`", readme)
        self.assertIn("build-assets.bat --platform winx64,linux --no-clean", readme)
        self.assertIn("No build script cleans the `output/` root", readme)
        self.assertIn("Supported platform names are exactly `winx64`, `winx86`, `linux`, `macos-x64`, and `macos-arm64`", readme)
        self.assertNotIn("all-known", readme)
        self.assertNotIn("SHA256SUMS.txt", readme)
        self.assertIn("`output/winx64/dlna-server-<version>-windows-x86_64.zip`", readme)
        self.assertIn("`output/linux/dlna-server_<version>_amd64.deb`", readme)
        self.assertIn("`output/macos-x64/DLNA_Server-<version>-macos-x64.dmg`", readme)


if __name__ == "__main__":
    unittest.main()
