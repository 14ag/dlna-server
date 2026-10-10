from pathlib import Path


def _load():
    values = {}
    text = (Path(__file__).resolve().parent.parent / "identity.env").read_text(encoding="utf-8-sig")
    for line in text.splitlines():
        key, sep, value = line.strip().partition("=")
        if sep:
            values[key] = value
    return values


# values come from identity env only
_IDENTITY = _load()
APP_ID = _IDENTITY["DLNA_APP_ID"]
PRODUCT_NAME = _IDENTITY["DLNA_PRODUCT_NAME"]
GTK_APP_ID = _IDENTITY["DLNA_REVERSE_DNS_PREFIX"] + "." + APP_ID
MAIN_WINDOW_CLASS = APP_ID + ".Main"
