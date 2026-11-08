# Docker

The image is a Linux container of the headless `dlna-server` command. It has
no window and no tray icon. Use it on a Linux host, or through Docker Desktop
on Windows or macOS.

Docker is optional. The normal Windows and Linux packages contain the same
server code.

## Why host networking is required

A DLNA server must do two things that Docker bridge networking breaks:

- Send and receive SSDP discovery traffic on UDP port `1900` multicast.
- Advertise an HTTP address that players on the same LAN can reach.

The container therefore runs with `network_mode: host`. Do not map ports with
`-p` and do not use the default bridge network. In host mode, port mappings
are ignored. The server listens on TCP `8200` and UDP `1900` directly on the
host.

## Requirements

- Docker Engine on Linux, or Docker Desktop on Windows or macOS.
- Docker Compose v2 (`docker compose`, not the old `docker-compose`).
- Internet access for the first build, so Debian packages and source
  dependencies can be downloaded.
- At least 2 GB of RAM for the image build. A small cloud instance with 1 GB
  may fail while compiling C++.
- A media folder on the host.

Check the tools:

```bash
docker version
docker compose version
```

## Install Docker Engine on Linux

These steps work on Debian, Ubuntu and Kali. Run them as your normal user.

```bash
sudo apt update
sudo apt install -y docker.io docker-compose-v2
sudo systemctl enable --now docker
sudo usermod -aG docker "$USER"
newgrp docker
```

`newgrp docker` applies the new group to the current terminal. Log out and
back in if you open a different terminal. Verify:

```bash
id -nG | tr ' ' '\n' | grep -x docker
docker version
docker compose version
```

If your release does not provide `docker-compose-v2`, install the Compose
plugin from Docker's official repository as `docker-compose-plugin`, or use a
newer distribution package that provides Compose v2.

## Install Docker on Amazon Linux 2023

```bash
sudo dnf install -y docker
sudo systemctl enable --now docker
sudo usermod -aG docker "$USER"
newgrp docker
docker version
docker compose version
```

If `docker compose version` fails, install the Compose v2 plugin from Docker's
official repository before continuing.

## Install Docker Desktop on Windows or macOS

Install Docker Desktop and start it. Use Linux containers, not Windows
containers.

Enable host networking in Docker Desktop settings if your version offers it.
Host networking is required because SSDP multicast cannot work through the
default bridge.

Docker Desktop always runs the Linux image inside a Linux VM, including on
macOS. Native macOS containers do not exist.

## Get the project and prepare media

```bash
git clone https://github.com/14ag/dlna-server.git
cd dlna-server
mkdir -p media
cp /path/to/video.mp4 media/
```

The compose file mounts `./media`, relative to `compose.yaml`. A folder such
as `~/media` is a different folder and is not mounted. Put files in
`./media` unless you change `compose.yaml`.

## Start the server

```bash
docker compose up -d --build
```

Check that the container is running and healthy:

```bash
docker compose ps
docker inspect --format '{{.State.Health.Status}}' dlna-server
curl -fsS http://127.0.0.1:8200/description.xml
```

Follow the log:

```bash
docker compose logs -f
```

Stop the server:

```bash
docker compose down
```

`docker compose down -v` also deletes the settings volume.

## Configure the server

The container stores settings in `/config/dlna-server/config.ini`. Copy the
file out, edit it, and copy it back:

```bash
docker compose cp dlna-server:/config/dlna-server/config.ini ./config.ini
# edit ./config.ini
docker compose cp ./config.ini dlna-server:/config/dlna-server/config.ini
docker compose restart
```

A useful first configuration:

```ini
[Settings]
ServerName=KALI-DLNA
Port=8200
NetworkInterfaceAllowList=eth3
BackgroundScanEnabled=1
DebugLog=1
```

- `ServerName` is the name players show.
- `Port` changes the HTTP port. If you change it, also change the health
  check in `Dockerfile` or `compose.yaml`.
- `NetworkInterfaceAllowList` pins the server to one network adapter. Use the
  exact name from `ip -br addr` or `ip -br link`.
- `BackgroundScanEnabled=1` watches local folders so new files appear without
  restarting the container.
- `DebugLog=1` writes `/config/dlna-server/debug.log` for troubleshooting.

The compose command uses `--source /media`. That replaces the sources in
`config.ini` for this run and is not saved. To share different folders, edit
the volume mount and the `command` in `compose.yaml`. To use sources saved in
`config.ini`, remove the `command` line.

## Multiple network adapters

A machine with NAT, host-only, internal, VPN or Tailscale adapters can make
the server advertise the wrong address. Players may then find the server but
fail to fetch media because the resource URL points at an unreachable IP.

Find the adapter that has the LAN address:

```bash
ip -br addr
```

Set `NetworkInterfaceAllowList` to that adapter, restart, and check again. The
URL in VLC must use an address that the player can reach. For example:

```text
http://192.168.1.50:8200/media/1000001.mp4
```

If the URL contains `10.0.2.15`, `172.17.0.1`, a Tailscale `100.x` address, or
another adapter that the player cannot route to, the allow list is wrong or
was not applied. Restart the container after changing `config.ini`.

`NetworkInterfaceAllowList` is a comma-separated list and matches any part of
the adapter name. An empty value uses the automatic selection.

## File permissions

The container runs as user `dlna`, UID and GID `1000`. A file that the server
cannot read appears in the catalog but fails when played. Check:

```bash
ls -ln media
```

Make the files readable:

```bash
sudo chown -R 1000:1000 media
sudo chmod -R a+rX media
```

If you replace the named settings volume with a bind mount, the folder must
also be writable by UID `1000`.

## Verify from another device

On the host:

```bash
curl -fsS http://127.0.0.1:8200/description.xml
```

On another computer on the same LAN:

```bash
curl -fsS http://<server-lan-ip>:8200/description.xml
```

Open VLC and choose **View > Playlist > Local Network > Universal
Plug'n'Play**. Select the server, then the folder.

To test one file directly, use a range request:

```bash
curl -v -r 0-1023 -o /dev/null http://<server-lan-ip>:8200/media/<id>.mp4
```

- `200` or `206`: the server can serve the file.
- `403`: the IP whitelist or file permissions rejected the request.
- `404`: the file is missing or unreadable.
- Timeout: the client cannot route to the advertised address.

## Adding media later

Download into the mounted folder:

```bash
cd dlna-server
curl -L -o media/sample.mp4 https://example.com/sample.mp4
```

With `BackgroundScanEnabled=1` the server notices the new file. VLC may still
cache the folder listing; reopen the server node or restart VLC. Without the
watcher, restart the container to trigger a new scan:

```bash
docker compose restart
```

## Virtual machines

Use a bridged adapter for the VM. NAT does not pass SSDP multicast, and
Tailscale does not carry DLNA discovery.

- Put the VM on the same subnet as the player.
- In VirtualBox, set the bridged adapter to **Promiscuous Mode: Allow All**.
- Keep the VM and player on the same VLAN and disable Wi-Fi client isolation.
- Pin the bridged interface with `NetworkInterfaceAllowList` when the VM has
  several adapters.
- Test from a separate device such as a phone or TV. Testing only inside the
  VM does not prove that multicast reaches the LAN.

## Run without compose

```bash
docker volume create dlna-config
docker run -d --name dlna-server \
  --network host \
  --restart unless-stopped \
  --read-only \
  --tmpfs /tmp:size=16m,mode=1777 \
  --security-opt no-new-privileges:true \
  -e XDG_CONFIG_HOME=/config \
  -v dlna-config:/config \
  -v "$PWD/media:/media:ro" \
  ghcr.io/14ag/dlna-server:latest --source /media
```

The image entrypoint is:

```text
/usr/bin/tini -- dlna-server --foreground
```

`tini` reaps child processes and forwards `SIGTERM`. `--foreground` stops the
server from detaching, so it stays alive as the container's main process.
Arguments after the image name are appended to those arguments. Do not use
`--debug` in place of `--foreground`.

## Build and upgrade

Build the image locally:

```bash
docker build -t dlna-server .
```

Set the version at build time:

```bash
docker build --build-arg DLNA_VERSION=2.26.7631 -t dlna-server .
```

Build both architectures with BuildKit:

```bash
docker buildx build --platform linux/amd64,linux/arm64 -t dlna-server .
```

To upgrade a compose deployment:

```bash
docker compose pull
docker compose up -d
```

If you build locally instead, run `docker compose build --pull` and start
again. Settings in the named volume are kept.

## Security

- The server runs as a non-root user.
- The compose file uses a read-only root filesystem, a small writable `/tmp`,
  and `no-new-privileges`.
- Media is mounted read-only by default.
- The image does not contain an SSH server or a package manager cache.

## Docker Desktop limits

Host networking is a recent Docker Desktop feature. Enable it in Docker
Desktop settings if it is not already on. The Linux container always runs
inside Docker Desktop's Linux VM, even on Windows and macOS. `127.0.0.1`
inside the container is not the Windows or macOS host; test from a device on
the physical LAN.

SSDP multicast is the fragile part. Some Docker Desktop versions do not pass
UDP `1900` multicast between the VM and the physical LAN. If the HTTP check
works but players do not find the server:

- Confirm the player and the computer are on the same subnet.
- Allow UDP `1900` and TCP `8200` in the host firewall.
- Try another Docker Desktop version, or run the container on a Linux host.
- Use the player's direct URL or HTTP support as a workaround.

On Windows, make sure Docker Desktop uses Linux containers, not Windows
containers.

## Cloud hosts such as EC2

The container starts on a normal Linux cloud instance, but DLNA discovery does
not work across AWS VPC networking. AWS VPCs do not carry SSDP multicast on
UDP `1900`, and the server advertises its private EC2 address.

On EC2:

- Open TCP `8200` in the security group for direct HTTP access.
- Use a `t3.small` or larger instance for a local image build.
- Expect `curl http://<ec2-ip>:8200/description.xml` to work.
- Expect normal TVs and players not to discover the server. Run it on the
  player's LAN, or use a bridged/VPN setup that carries multicast.

## Troubleshooting

Check the process, health and log:

```bash
docker compose ps
docker inspect --format '{{.State.Health.Status}}' dlna-server
docker compose logs --tail=200
```

The health check requests `http://127.0.0.1:8200/description.xml`. If you
change the HTTP port, update the health check too.

| Problem | Cause | Fix |
|---|---|---|
| `permission denied ... docker.sock` | User is not in the `docker` group | `sudo usermod -aG docker "$USER"`, then `newgrp docker` or log in again |
| VLC shows only one file | Files are in `~/media`, not `./media` | Move them into the folder mounted by compose |
| New files need a restart | Folder watching is off | Set `BackgroundScanEnabled=1` and restart once |
| Files appear but do not play | Wrong advertised IP or unreadable files | Pin `NetworkInterfaceAllowList`, check `ls -ln media` |
| Resource URL uses `10.0.2.x` | VM NAT adapter was selected | Pin the bridged interface |
| Players cannot discover the server | Bridge, Wi-Fi isolation or VM NAT blocks multicast | Use a bridged adapter, allow multicast, test from another device |
| Container exits immediately | `--foreground` was removed from the entrypoint | Restore the image entrypoint |

If the server exits immediately, check that the entrypoint still contains
`--foreground`. Without it the POSIX server forks to the background, the main
process exits, and Docker stops the container.
