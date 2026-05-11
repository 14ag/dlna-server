import tempfile
import unittest
from pathlib import Path

import os
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "src"))

from posix_gui import ConfigStore, ServerConfig, find_server_binary


class ConfigStoreTests(unittest.TestCase):
    def test_round_trips_server_settings(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            path = Path(temp_dir) / "config.ini"
            store = ConfigStore(path)
            config = ServerConfig(
                server_name="Living Room",
                port=8300,
                debug_log=True,
                device_uuid="11111111-2222-3333-4444-555555555555",
                media_sources=["/media/movies", "/media/music"],
            )

            store.save(config)
            loaded = store.load()

            self.assertEqual(loaded.server_name, "Living Room")
            self.assertEqual(loaded.port, 8300)
            self.assertTrue(loaded.debug_log)
            self.assertEqual(loaded.device_uuid, "11111111-2222-3333-4444-555555555555")
            self.assertEqual(loaded.media_sources, ["/media/movies", "/media/music"])

    def test_reads_utf8_bom_config(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            path = Path(temp_dir) / "config.ini"
            path.write_text("\ufeff[Settings]\nServerName=BOM OK\nPort=8205\nMediaSources=/srv/media\n", encoding="utf-8")

            loaded = ConfigStore(path).load()

            self.assertEqual(loaded.server_name, "BOM OK")
            self.assertEqual(loaded.port, 8205)
            self.assertEqual(loaded.media_sources, ["/srv/media"])

    def test_env_server_binary_wins(self):
        old_value = os.environ.get("DLNA_SERVER_BIN")
        try:
            os.environ["DLNA_SERVER_BIN"] = str(Path(tempfile.gettempdir()) / "dlna-server")
            self.assertEqual(find_server_binary(), (Path(tempfile.gettempdir()) / "dlna-server").resolve())
        finally:
            if old_value is None:
                os.environ.pop("DLNA_SERVER_BIN", None)
            else:
                os.environ["DLNA_SERVER_BIN"] = old_value


if __name__ == "__main__":
    unittest.main()
