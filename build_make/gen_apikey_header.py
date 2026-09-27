#!/usr/bin/env python3
# Generate mcp_apikey_embedded.h from .mcp_api_key file.
#
# The .mcp_api_key file contains a 64-char hex string (32 bytes).
# The generated header provides a compile-time default api_key that
# apikey_load() falls back to when Flash has no stored key.
#
# Usage:
#     python gen_apikey_header.py <input_file> <output_header>

import sys
from pathlib import Path


def main():
    if len(sys.argv) != 3:
        print("Usage: gen_apikey_header.py <.mcp_api_key> <output.h>", file=sys.stderr)
        sys.exit(1)

    src = Path(sys.argv[1])
    dst = Path(sys.argv[2])

    if not src.exists():
        # No api_key file: emit header with MCP_APIKEY_EMBEDDED undefined
        dst.write_text(
            "/* Auto-generated: no .mcp_api_key file found */\n"
            "#ifndef MCP_APIKEY_EMBEDDED_H\n"
            "#define MCP_APIKEY_EMBEDDED_H\n"
            "/* MCP_APIKEY_EMBEDDED undefined: apikey_load uses Flash only */\n"
            "#endif\n",
            encoding="utf-8",
        )
        return

    hex_str = src.read_text(encoding="utf-8").strip()
    if len(hex_str) != 64:
        print("Error: " + str(src) + " must contain 64 hex chars (got " + str(len(hex_str)) + ")", file=sys.stderr)
        sys.exit(1)

    try:
        key_bytes = bytes.fromhex(hex_str)
    except ValueError as e:
        print("Error: invalid hex in " + str(src) + ": " + str(e), file=sys.stderr)
        sys.exit(1)

    byte_literals = ", ".join("0x{:02x}".format(b) for b in key_bytes)

    lines = [
        "/* Auto-generated from .mcp_api_key: do not edit */",
        "#ifndef MCP_APIKEY_EMBEDDED_H",
        "#define MCP_APIKEY_EMBEDDED_H",
        "",
        "#define MCP_APIKEY_EMBEDDED 1",
        "",
        "static const unsigned char mcp_apikey_embedded[32] = {",
        "    " + byte_literals,
        "};",
        "",
        "#endif",
        "",
    ]
    dst.write_text("\n".join(lines), encoding="utf-8")


if __name__ == "__main__":
    main()
