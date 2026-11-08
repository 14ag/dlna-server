from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def read(rel):
    return (ROOT / rel).read_text(encoding="utf-8")


def test_dockerfile_builds_headless_server_as_non_root():
    text = read("Dockerfile")
    assert text.count("FROM ") >= 2
    assert "AS build" in text
    assert "-DDLNA_ENABLE_GTK4_GUI=OFF" in text
    assert "cmake --build" in text
    assert "USER dlna" in text
    assert "libcurl4" in text
    assert "EXPOSE 8200/tcp 1900/udp" in text
    assert 'VOLUME ["/config", "/media"]' in text
    assert "HEALTHCHECK" in text
    assert "/description.xml" in text


def test_dockerfile_runs_server_in_foreground():
    line = next(
        item for item in read("Dockerfile").splitlines()
        if item.startswith("ENTRYPOINT")
    )
    assert "tini" in line
    assert "dlna-server" in line
    assert "--foreground" in line
    assert "--debug" not in line


def test_dockerignore_keeps_context_small_and_private():
    entries = set(read(".dockerignore").splitlines())
    for entry in (".git", ".github", ".opencode", "config", "media", "output", "tests", "tmp"):
        assert entry in entries


def test_compose_uses_host_networking_and_writable_config_volume():
    text = read("compose.yaml")
    assert "network_mode: host" in text
    assert "read_only: true" in text
    assert "no-new-privileges:true" in text
    assert "XDG_CONFIG_HOME: /config" in text
    assert "./media:/media:ro" in text
    assert "dlna-config:/config" in text
    assert '"--source", "/media"' in text
    assert "tmpfs:" in text


def test_compose_builds_local_context_and_names_image():
    text = read("compose.yaml")
    assert "build:" in text
    assert "context: ." in text
    assert "ghcr.io/14ag/dlna-server" in text


def test_docker_workflow_builds_multi_arch_and_publishes():
    text = read(".github/workflows/docker.yml")
    assert "docker/setup-buildx-action" in text
    assert "docker/build-push-action" in text
    assert "linux/amd64" in text
    assert "linux/arm64" in text
    assert "ghcr.io" in text
    assert "packages: write" in text
    assert "description.xml" in text
    assert "--network host" in text


def test_docker_docs_cover_platform_and_discovery_limits():
    text = read("docs/Docker.md")
    for token in (
        "--foreground",
        "--network host",
        "Docker Desktop",
        "1900",
        "8200",
        "/config",
        "/media",
        "macOS",
        "Windows",
    ):
        assert token in text, token


def test_docker_docs_cover_linux_install_and_common_failures():
    text = read("docs/Docker.md")
    for token in (
        "docker.io docker-compose-v2",
        "newgrp docker",
        "docker compose version",
        "./media",
        "NetworkInterfaceAllowList",
        "BackgroundScanEnabled",
        "Promiscuous Mode",
        "docker.sock",
        "t3.small",
    ):
        assert token in text, token


def test_docs_link_to_docker_page():
    assert "docs/Docker.md" in read("README.md")
    assert "Docker.md" in read("docs/Home.md")


def test_configuration_documents_foreground_flag():
    text = read("docs/CONFIGURATION.md")
    assert "`--foreground`" in text


def test_cli_flag_table_documents_foreground_for_posix():
    text = read("src/cli_flags.h")
    assert "--foreground" in text
    assert "Run in foreground" in text or "do not detach" in text
    assert "ShouldDetachToBackground" in read("src/posix_daemonize.h")
    assert "--foreground" in read("src/posix_main.cpp")
