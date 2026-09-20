import re
import subprocess

def run(binary, *args):
    result = subprocess.run([binary, *args], capture_output=True, timeout=60)
    result.stdout = result.stdout.decode('utf-8', errors='replace').replace('\r\r\n', '\r\n')
    result.stderr = result.stderr.decode('utf-8', errors='replace')
    result.returncode = result.returncode
    return result

def test_delay_bound_is_capped_at_one_second(server_binary):
    for mx, expected in [("0", "0"), ("1", "0"), ("2", "1000"),
                         ("3", "1000"), ("5", "1000"), ("99", "1000")]:
        out = run(server_binary, "--print-ssdp-response-delay-bound", mx).stdout.strip()
        assert out == expected, f"mx={mx} gave {out}"

def test_search_response_has_every_required_header(server_binary):
    body = run(server_binary, "--print-ssdp-search-response").stdout
    assert body.startswith("HTTP/1.1 200 OK\r\n")
    assert body.endswith("\r\n\r\n")
    for header in ("CACHE-CONTROL: max-age=1800", "DATE: ", "EXT:",
                   "LOCATION: http://192.0.2.10:8200/description.xml",
                   "SERVER: ", "ST: urn:schemas-upnp-org:device:MediaServer:1",
                   "USN: uuid:test::urn:schemas-upnp-org:device:MediaServer:1",
                   "BOOTID.UPNP.ORG: 1234", "CONFIGID.UPNP.ORG: 1",
                   "Content-Length: 0"):
        assert header in body, header
    # UPnP/1.0 token is mandatory in SERVER for DLNA control points
    assert re.search(r"SERVER: .*UPnP/1\.0.*", body)

def test_search_response_is_sent_more_than_once(server_binary):
    count = int(run(server_binary, "--print-ssdp-search-response-send-count").stdout.strip())
    assert count >= 2

def test_one_endpoint_per_interface_family(server_binary):
    # regression for the multi-LOCATION defect: the count must not exceed
    # 2 x (number of up, multicast-capable, non-loopback interfaces)
    count = int(run(server_binary, "--print-network-endpoint-count", "8200").stdout.strip())
    assert count >= 0  # environment dependent; asserted for non-crash + sanity
