#!/usr/bin/env python3
# Generate wifi_config_embedded.h from a .wifi_config file.
#
# .wifi_config is a plain text file kept out of version control:
#
#     ssid=MyNetwork
#     psk=my-password
#     auth=wpa2          # open / wpa2 / wpa3 (optional, default wpa2)
#
# The generated header defines WIFI_FALLBACK_SSID / _PSK / _AUTH, which the
# firmware uses when no credentials are stored in Flash. Without the file the
# header defines an empty SSID and the device waits for credentials to be
# written to Flash.
#
# Usage:
#     python gen_wifi_header.py <.wifi_config> <output_header>

import sys
from pathlib import Path

AUTH_MACROS = {
    "open": "TK_WIFI_AUTH_OPEN",
    "wpa2": "TK_WIFI_AUTH_WPA2_PSK",
    "wpa3": "TK_WIFI_AUTH_WPA3_PSK",
}


def c_string(s):
    out = []
    for ch in s.encode("utf-8"):
        c = chr(ch)
        if c in ('"', chr(92)):
            out.append(chr(92) + c)
        elif 0x20 <= ch < 0x7F:
            out.append(c)
        else:
            out.append(chr(92) + "{:03o}".format(ch))
    return '"' + "".join(out) + '"'


def parse(path):
    conf = {}
    for n, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            sys.exit(str(path) + ":" + str(n) + ": expected key=value")
        key, value = line.split("=", 1)
        conf[key.strip().lower()] = value.strip()
    return conf


def main():
    if len(sys.argv) != 3:
        print("Usage: gen_wifi_header.py <.wifi_config> <output.h>", file=sys.stderr)
        sys.exit(1)

    src = Path(sys.argv[1])
    dst = Path(sys.argv[2])

    ssid, psk, auth = "", "", "wpa2"
    if src.exists():
        conf = parse(src)
        ssid = conf.get("ssid", "")
        psk = conf.get("psk", "")
        auth = conf.get("auth", "open" if not psk else "wpa2").lower()
        if not 1 <= len(ssid.encode("utf-8")) <= 32:
            sys.exit("Error: " + str(src) + ": ssid must be 1..32 bytes")
        if auth not in AUTH_MACROS:
            sys.exit("Error: " + str(src) + ": auth must be open, wpa2 or wpa3")
        if auth != "open" and not 8 <= len(psk) <= 63:
            sys.exit("Error: " + str(src) + ": psk must be 8..63 characters")
        note = "Auto-generated from .wifi_config: contains a secret, never commit"
    else:
        print("Note: " + str(src) + " not found; firmware will use Flash-stored "
              "WiFi credentials only", file=sys.stderr)
        note = "Auto-generated: no .wifi_config file found"

    dst.write_text(
        "/* " + note + " */\n"
        "#ifndef WIFI_CONFIG_EMBEDDED_H\n"
        "#define WIFI_CONFIG_EMBEDDED_H\n"
        "\n"
        "#define WIFI_FALLBACK_SSID  " + c_string(ssid) + "\n"
        "#define WIFI_FALLBACK_PSK   " + c_string(psk) + "\n"
        "#define WIFI_FALLBACK_AUTH  " + AUTH_MACROS[auth] + "\n"
        "\n"
        "#endif\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
