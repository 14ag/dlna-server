---
name: tv-browse
description: Simulate a real DLNA/UPnP control point (as VLC for Android would behave) against a running dlna-server instance, end to end from discovery through content browsing to media fetch, using an Android phone connected over USB via adb as the actual network vantage point. Use when asked to simulate a DLNA/UPnP client, reproduce "the TV/phone can't see the server" without the app UI, or verify discovery/browsing/streaming from the device's own network position.
---

# tv-browse

Simulates the network conversation a real DLNA control point (VLC for Android, a smart TV's
built-in DLNA browser, BubbleUPnP, Kodi) has with a UPnP MediaServer, stage by stage, issuing the
actual bytes on the wire from the Android device itself — not from the developer machine — because
the device is the real vantage point a phone/TV client would use (its own subnet, its own AP
association, its own multicast/IGMP behavior).

This skill never touches the target app's source or the `dlna-server` binary. It is a black-box
network probe. Findings should be cross-referenced against
`dlna-server-full-review-and-fix-workflow-20-09-26.md` Phase 1 when a discovery-stage failure is
found — that document contains the code-level fix for known discovery defects; this skill only
proves which stage is broken and captures the evidence.

## Why the device, not the host machine

A control point's real failure modes are frequently invisible from the developer's PC because the
PC and the phone are not guaranteed to be on the same effective broadcast/multicast domain even
when both show the same Wi-Fi SSID: AP client isolation, per-client VLANs, and IGMP snooping
without an active querier all silently drop multicast between wireless clients while unicast still
works. Running the probe from the phone's own shell over `adb`, with the phone attached to the
target network exactly as it would be during real use, reproduces the client's actual vantage
point. USB carries only the `adb` control channel; all protocol traffic under test still goes out
the phone's own Wi-Fi/Ethernet interface.

## How a DLNA/UPnP control point actually works (read this before running anything)

A control point session is five sequential HTTP/UDP exchanges. Each stage in this skill maps to
exactly one of them, in order, because a failure at stage N makes every later stage moot — do not
skip ahead.

1. **Discovery (SSDP, UDP 1900).** The control point sends a multicast `M-SEARCH` to
   `239.255.255.250:1900` (IPv4) or `[ff02::c]:1900` (IPv6) with a search target (`ST`) and an
   `MX` value (max wait, seconds). Every matching device on the network replies **unicast**, back
   to the sender's IP and the same UDP source port, with `HTTP/1.1 200 OK` and a `LOCATION` header.
   A control point also passively listens for unsolicited `NOTIFY * HTTP/1.1` with
   `NTS: ssdp:alive`, which a well-behaved server also emits periodically without being asked.
   Real client behavior (this is what VLC's libupnp-based control point and every other
   libupnp/gupnp derivative does): it treats the search as failed once `MX` seconds elapse with no
   reply, and it discards a late reply outright.
2. **Description retrieval (HTTP GET of `LOCATION`).** A plain unicast `GET` on the `LOCATION`
   URL. The response must be `200 OK`, `Content-Type` containing `text/xml` or `application/xml`,
   a correct `Content-Length` (or a connection the server closes cleanly), and a body whose
   `<device><deviceType>` is exactly `urn:schemas-upnp-org:device:MediaServer:1`, with a non-empty
   `<friendlyName>`, a `<UDN>`, and a `<service>` entry whose `serviceType` is
   `urn:schemas-upnp-org:service:ContentDirectory:1`, carrying `<SCPDURL>` and `<controlURL>`.
3. **Service description (HTTP GET of `SCPDURL`).** Confirms which actions
   (`Browse`, `Search`, `GetSystemUpdateID`, ...) the service actually implements before the
   control point calls any of them.
4. **Content browsing (HTTP POST of `controlURL`, SOAP).** The control point sends a SOAP 1.1
   envelope as an HTTP POST with header `SOAPACTION:
   "urn:schemas-upnp-org:service:ContentDirectory:1#Browse"`, body containing `ObjectID`,
   `BrowseFlag` (`BrowseDirectChildren` to list a folder, `BrowseMetadata` to inspect one object),
   `Filter`, `StartingIndex`, `RequestedCount`, `SortCriteria`. The response body's `<Result>`
   element is itself XML-escaped DIDL-Lite: a nested `<DIDL-Lite>` document listing `<container>`
   (folders) and `<item>` (playable objects, each carrying one or more `<res>` resource URLs).
5. **Media fetch (HTTP GET/HEAD of a `<res>` URL).** The control point issues a `HEAD` first to
   read `Content-Length`/`Accept-Ranges`/`Content-Type`, then a `GET` with a `Range:` header to
   start playback partway through a byte range, and expects `206 Partial Content` with a matching
   `Content-Range` when it asked for one.

Any stage that returns the wrong content type, a malformed envelope, a missing `Content-Length`
with no connection-close, or arrives outside a real client's timeout, presents to the user as
"nothing happens" or "device disappeared" with no further diagnostic on the client side. This
skill's job is to name the exact stage and show the exact bytes.

## Prerequisites

- `adb` on PATH on the machine running this skill, one Android device attached over USB with
  `adb devices` showing it as `device` (not `unauthorized`).
- The device's screen unlocked and USB debugging authorized (one-time prompt on the phone).
- The device joined to the **same network** the dlna-server instance is advertising on. Confirm
  with `adb shell ip route` — the phone's default route interface must be Wi-Fi/Ethernet, not
  anything related to the USB `adb` link itself (`adb` here is control-only).
- The dlna-server instance running and reachable in principle from that network. If its LAN
  address is not already known, Stage 0/1 discover it manually; Stage 2 discovers it protocol-side.

## Stage 0 — Tool inventory on the device (run first, every time)

Stock Android's shell is **toybox**, not GNU coreutils or busybox. It ships `ping`, `ip`, `netstat`
(a reduced implementation), and a minimal `wget` and `nc`. It does **not** ship `curl` or `telnet`.
Do not assume any tool exists — probe first, and only then choose the command form for later
stages from Table 1.

```bash
adb devices -l                                   # confirm exactly one authorized device
for tool in ping wget nc netstat curl telnet ip getprop; do
  echo -n "$tool: "; adb shell "command -v $tool || echo MISSING"
done
adb shell getprop ro.product.cpu.abi              # e.g. arm64-v8a — needed if a static binary is pushed later
adb shell ip -4 addr show wlan0                   # confirm the phone's actual LAN IP/subnet
```

**Table 1 — tool substitution matrix.** Use this table for every later stage; do not hard-code a
tool name into a command until you have confirmed it exists on this device.

| Needed capability | If present on device | If missing |
|---|---|---|
| ICMP reachability | `adb shell ping -c 4 -W 2 <ip>` | Skip; note as untestable, rely on TCP connect result instead |
| TCP port open check | `adb shell nc -vz -w 3 <ip> <port>` | `adb shell "exec 3<>/dev/tcp/<ip>/<port> && echo open || echo closed"` (works under toybox `sh`, which supports `/dev/tcp`) |
| Plain HTTP GET | `adb shell wget -T 5 -O - http://<ip>:<port>/path` | Same `/dev/tcp` trick, manually writing the request line and headers (Stage 3b shows the exact form) |
| HTTP POST with custom headers/body (SOAP) | `adb shell wget --header=... --post-data=... ...` only if this device's toybox wget build supports `--header`/`--post-data` (many do not) | Push a static BusyBox binary (Stage 3a) and use `busybox wget`/`busybox nc`, or hand-roll the request over `/dev/tcp` (Stage 4) |
| Raw socket / telnet-style banner grab | `adb shell nc <ip> <port>` | `/dev/tcp` trick |

Record the actual availability table in the run's findings file (Stage 6) — it varies by Android
build and OEM.

## Stage 1 — Reachability (the layer under every DLNA problem report)

Before touching UPnP at all, confirm plain L3/L4 reachability from the device to the server host.
Most "can't see server" reports are actually this stage failing, disguised as a UPnP complaint.

```bash
SERVER_IP=<server-lan-ip>          # from manual entry, or filled in after Stage 2's discovery result
HTTP_PORT=<server-http-port>       # e.g. 8200 — from config.ini's Port key

# 1. ICMP
adb shell ping -c 4 -W 2 $SERVER_IP

# 2. Is the HTTP port actually listening and reachable
adb shell nc -vz -w 3 $SERVER_IP $HTTP_PORT

# 3. Local routing sanity on the device itself
adb shell ip route get $SERVER_IP

# 4. Confirm the device is not itself blocked by AP client isolation: from the SAME device,
#    ping the AP's own gateway to prove basic LAN connectivity works at all, then compare.
GATEWAY=$(adb shell ip route show default | awk '{print $3}' | tr -d '\r')
adb shell ping -c 2 -W 2 $GATEWAY
```

**Interpretation.**
- Ping fails, port check fails, gateway ping fails too → phone has no LAN connectivity; not a
  server defect.
- Ping fails, port check fails, gateway ping succeeds → phone can reach the router but not the
  server host. Classic **AP client isolation**: many consumer/guest Wi-Fi networks silently drop
  client-to-client traffic including the server's own subnet peers. This alone fully explains "the
  TV can't see the server" independent of any UPnP defect and is not fixable in `dlna-server`.
- Ping fails but port check succeeds → ICMP is filtered (common on some routers); not a discovery
  blocker, continue.
- Both succeed → proceed to Stage 2. A unicast-reachable-but-not-discovered server is now isolated
  to a genuine SSDP-layer problem, which is exactly what Phase 1 of the fix workflow addresses.

## Stage 2 — Discovery: send the real M-SEARCH VLC for Android sends

VLC for Android's control point is built on libupnp. Reproduce its actual request, not a
simplified one: `ST` targeting the MediaServer device type (not `ssdp:all`, which some servers
handle on a different code path than a targeted search), and `MX: 5` matching Task 1.1/1.2 of the
fix workflow.

```bash
adb shell <<'EOF'
MSEARCH=$(printf 'M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: "ssdp:discover"\r\nMX: 5\r\nST: urn:schemas-upnp-org:device:MediaServer:1\r\nUSER-AGENT: Android/13 UPnP/1.0 org.videolan.vlc/3.5\r\n\r\n')
printf '%s' "$MSEARCH" | nc -u -w6 239.255.255.250 1900
EOF
```

If `nc -u` on this device's toybox build does not support this send/receive pattern (some builds
exit immediately after writing stdin instead of waiting for a reply), fall back to Stage 3a's
pushed BusyBox binary and repeat with `busybox nc -u -w6 239.255.255.250 1900`.

Also attempt to capture the passive `ssdp:alive` stream, to confirm the server's own advertisement
schedule independent of any search — this normally fails on stock, unrooted ROMs because binding a
multicast-membership UDP socket on port 1900 from userspace is commonly restricted; treat a failure
here as expected and rely on the M-SEARCH round trip above as the primary signal:

```bash
adb shell timeout 30 nc -u -l 1900 2>/dev/null || echo "device cannot bind the multicast port from userspace — expected on most stock ROMs, not a finding"
```

**What to record, verbatim, per attempt:**
- Whether any bytes came back at all within the 5–6 s window.
- Elapsed wall-clock time from send to first byte received (compare against the `MX` value; a
  reply arriving after `MX` seconds is a server-side timing defect even if it eventually arrives —
  see Task 1.2 of the fix workflow).
- The full reply text: it must parse as `HTTP/1.1 200 OK` with `LOCATION:`, `ST:`, `USN:`,
  `SERVER:` containing an `UPnP/1.0` token, and `CACHE-CONTROL: max-age=`.
- How many distinct `LOCATION` values appeared across repeated attempts. More than one distinct
  `LOCATION` for the same `USN` reproduces Task 1.7's multi-address advertisement defect — a real
  control point keeps only the last one it saw, and if that happens to be an unreachable
  secondary/VM/VPN address the device will silently fail to load starting at Stage 3.
- Run the whole M-SEARCH round trip 5 times with a few seconds between attempts. A reply that
  succeeds sometimes and not others is evidence of the single-shot-UDP-drop defect (Task 1.3);
  count the failure rate.

**Interpretation.**
- No reply, ever, in 5 attempts, but Stage 1 reachability succeeded → the datagram is not making it
  to the server or the reply is not making it back; this is either firewall/multicast filtering on
  the network, or the server's SSDP listener is not bound/joined on this interface.
- Reply arrives but after the `MX` window → server-side scheduling defect (Task 1.2).
- Reply arrives intermittently across the 5 attempts with Stage 1 solid → single-shot transmit
  defect (Task 1.3).
- Reply's `LOCATION` varies across attempts → multi-address advertisement defect (Task 1.7).

## Stage 3 — Description and service description retrieval

### 3a. Preferred method — push a static BusyBox for a faithful client-side HTTP stack

Real Android DLNA clients use a proper HTTP client library, which correctly handles headers,
chunked/`Content-Length` framing, and connection reuse — none of which toybox `wget` reliably
reproduces. The closest faithful reproduction from an unrooted device's own shell is a statically
linked BusyBox binary run from `/data/local/tmp`, which does not require root and is the standard
technique for getting a real `wget`/`nc` onto a stock Android shell.

```bash
ABI=$(adb shell getprop ro.product.cpu.abi | tr -d '\r')     # e.g. arm64-v8a, armeabi-v7a, x86_64
# Obtain a statically linked busybox binary matching $ABI from a trusted source and stage it
# locally as ./busybox-android before running the next two commands. Do not run this stage if
# no such binary is available in the working environment — fall back to 3b instead.
adb push ./busybox-android /data/local/tmp/busybox
adb shell chmod 755 /data/local/tmp/busybox
adb shell /data/local/tmp/busybox wget -q -O - --header="User-Agent: Android/13 UPnP/1.0 org.videolan.vlc/3.5" \
  "http://$SERVER_IP:$HTTP_PORT/description.xml"
```

Then fetch the `SCPDURL` advertised inside that response (the value differs by server but
`dlna-server` fixes it at `/ContentDirectory.xml`):

```bash
adb shell /data/local/tmp/busybox wget -q -O - "http://$SERVER_IP:$HTTP_PORT/ContentDirectory.xml"
```

### 3b. Fallback method — hand-rolled HTTP/1.1 over `/dev/tcp` (no extra binary needed)

Works under toybox `sh` on essentially every Android version without pushing anything.

```bash
adb shell <<EOF
exec 3<>/dev/tcp/$SERVER_IP/$HTTP_PORT
printf 'GET /description.xml HTTP/1.1\r\nHost: $SERVER_IP:$HTTP_PORT\r\nUser-Agent: Android/13 UPnP/1.0 org.videolan.vlc/3.5\r\nConnection: close\r\n\r\n' >&3
cat <&3
EOF
```

Repeat with `GET /ContentDirectory.xml` for the service description.

**What to record:**
- HTTP status line and `Content-Type` for each fetch.
- `deviceType`, `friendlyName`, `UDN`, and the `ContentDirectory` service's `controlURL` and
  `SCPDURL` extracted from the description body.
- Whether `ContentDirectory.xml` actually lists a `Browse` action (it must, for Stage 4 to be
  meaningful).

**Interpretation.** A non-`200` status, a `Content-Type` that is not XML, or a body that fails to
parse (unbalanced tags, wrong `deviceType`) is the class of failure a client that got past Stage 2
would still silently fail to add to its browsable device list, with no diagnostic surfaced to the
user.

## Stage 4 — Content browsing: the actual Browse SOAP call a client sends

Build the exact envelope a real control point sends for "list the root folder", using whichever
transport Stage 3 established as working (BusyBox `wget --post-data`/`--header`, or the `/dev/tcp`
hand-roll below, which is guaranteed to work everywhere since it is raw bytes on a socket).

```bash
adb shell <<EOF
BODY='<?xml version="1.0" encoding="utf-8"?><s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/"><s:Body><u:Browse xmlns:u="urn:schemas-upnp-org:service:ContentDirectory:1"><ObjectID>0</ObjectID><BrowseFlag>BrowseDirectChildren</BrowseFlag><Filter>*</Filter><StartingIndex>0</StartingIndex><RequestedCount>50</RequestedCount><SortCriteria></SortCriteria></u:Browse></s:Body></s:Envelope>'
LEN=\$(printf '%s' "\$BODY" | wc -c)
exec 3<>/dev/tcp/$SERVER_IP/$HTTP_PORT
{
  printf 'POST /upnp/control/content_directory HTTP/1.1\r\n'
  printf 'Host: $SERVER_IP:$HTTP_PORT\r\n'
  printf 'User-Agent: Android/13 UPnP/1.0 org.videolan.vlc/3.5\r\n'
  printf 'Content-Type: text/xml; charset="utf-8"\r\n'
  printf 'SOAPACTION: "urn:schemas-upnp-org:service:ContentDirectory:1#Browse"\r\n'
  printf 'Content-Length: %s\r\n\r\n' "\$LEN"
  printf '%s' "\$BODY"
} >&3
cat <&3
EOF
```

Use the `controlURL` recorded in Stage 3 verbatim if it differs from `/upnp/control/content_directory`.

**What to record:**
- HTTP status (must be `200 OK`; a UPnP fault still returns `200` with a `<s:Fault>` body — a
  non-`200` HTTP status here is itself already wrong per the ContentDirectory:1 spec).
- `<NumberReturned>`, `<TotalMatches>`, and the raw (still-escaped) `<Result>` payload.
- Un-escape the `<Result>` payload by hand (replace `&lt;`/`&gt;`/`&amp;`/`&quot;`) and confirm it
  parses as a `<DIDL-Lite>` document with at least one `<container>` or `<item>`.
- For at least one returned `<item>`, extract its `<res>` URL for Stage 5.
- Repeat the same call with `<ObjectID>` set to a container ID discovered above, to walk one level
  deeper — a real client does this recursively as the user taps into folders.

**Interpretation.** An HTTP-layer success with an empty or malformed `Result` presents to a real
client as "empty folder" or a parse crash; a `<s:Fault>` with `errorCode 710` means the server has
not finished its initial scan yet — retry after a short delay before treating it as a defect.

## Stage 5 — Media fetch: HEAD, then ranged GET, exactly like playback start

```bash
RES_PATH=<path-extracted-from-Stage-4-res-url>     # e.g. /media/1000005.mp4

adb shell <<EOF
exec 3<>/dev/tcp/$SERVER_IP/$HTTP_PORT
printf 'HEAD $RES_PATH HTTP/1.1\r\nHost: $SERVER_IP:$HTTP_PORT\r\nUser-Agent: Android/13 UPnP/1.0 org.videolan.vlc/3.5\r\nConnection: close\r\n\r\n' >&3
cat <&3
EOF

adb shell <<EOF
exec 3<>/dev/tcp/$SERVER_IP/$HTTP_PORT
printf 'GET $RES_PATH HTTP/1.1\r\nHost: $SERVER_IP:$HTTP_PORT\r\nUser-Agent: Android/13 UPnP/1.0 org.videolan.vlc/3.5\r\nRange: bytes=0-65535\r\nConnection: close\r\n\r\n' >&3
head -c 1024 <&3 | xxd | head -20
EOF
```

**What to record:**
- `HEAD` response's `Content-Type`, `Content-Length`, `Accept-Ranges`.
- Ranged `GET` response status (`206 Partial Content` expected) and its `Content-Range` header.
- First bytes of the body are a plausible container magic number for the file's extension — a
  transcoding/serving-path defect can otherwise present to a real client as "plays but is garbage".

## Stage 6 — Findings file

Produce `tv-browse-findings-<dd-mm-yy>.md` with one row per stage:

```markdown
| Stage | Command run | Result | Pass/Fail | Notes |
|---|---|---|---|---|
| 0 Tool inventory | ... | ... | | which tools were present on THIS device |
| 1 Reachability | ... | ... | | |
| 2 Discovery | ... | ... | | reply latency vs MX, LOCATION consistency, retry success rate |
| 3 Description | ... | ... | | |
| 4 Browse | ... | ... | | |
| 5 Media fetch | ... | ... | | |
```

Every `Fail` row must name the stage number and, where it maps to a known code defect, the Phase 1
task ID from `dlna-server-full-review-and-fix-workflow-20-09-26.md` (Task 1.2, 1.3, or 1.7) so the
two documents stay linked. A failure with no matching task ID is new evidence and should be written
up as a new finding rather than forced into an existing task.

## Cleanup

```bash
adb shell rm -f /data/local/tmp/busybox   # if Stage 3a was used
```

Leaving BusyBox on the device is otherwise harmless but should not be left behind on a shared or
loaner test device.

## Common pitfalls

- Running any of these commands from the PC's own shell instead of `adb shell` defeats the purpose
  of this skill — it tests the PC's network position, not the phone's.
- `adb shell` runs each invocation as a separate non-interactive shell; a `cd` or `exec 3<>...` in
  one `adb shell` call does not persist to the next. Keep everything that must share a file
  descriptor or working directory inside one heredoc, as shown above.
- Toybox `wget`/`nc` option support varies by Android/OEM version — always confirm with Stage 0's
  inventory instead of assuming a flag exists.
- A single failed attempt at Stage 2 is not evidence of a bug; SSDP over UDP is unreliable by
  design. Always repeat 5 times before concluding anything, per Stage 2's instructions.
- `MX` in the M-SEARCH request must be honored as the client's own read deadline when timing the
  reply — do not read indefinitely; a real client would have already given up.
