#!/usr/bin/env python3
"""wasm_upload.py — .wasm モジュールを MCP 経由で Pico W に注入・実行・永続化。

使い方:
    # 投入 + 実行
    python tools/wasm_upload.py blink.wasm --slot 0 --run

    # 投入 + Flash 保存 (auto_boot=false)
    python tools/wasm_upload.py blink.wasm --slot 0 --store 0 --name blink --run

    # 投入 + Flash 保存 + autoboot
    python tools/wasm_upload.py blink.wasm --slot 0 --store 0 --name blink --auto-boot --run

    # Flash → RAM 復元 + 実行 (upload スキップ)
    python tools/wasm_upload.py --load 0 --slot 0 --run

    # Flash slot 一覧
    python tools/wasm_upload.py --list-flash

    # Flash slot 消去
    python tools/wasm_upload.py --erase 0

NUCLEO-H533RE キーボードブレイン (docs/guide/wasm.md):
    # キー処理として取り付け + Flash 保存 (次回起動時も自動で使う)
    python tools/wasm_upload.py kb.wasm --kb-attach --kb-save \\
        --broker test.mosquitto.org --port 1883 --device kb-XXXXXX

    # 状態確認 / 組み込みキーマップに戻す (--kb-erase で保存分も消す)
    python tools/wasm_upload.py --kb-status ...
    python tools/wasm_upload.py --kb-detach [--kb-erase] ...

環境変数 (mcp_bridge.py と共通):
    MQTT_BROKER   default: localhost
    MQTT_PORT     default: 1884
    DEVICE_ID     default: kb-000001
"""
from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
import random
import sys
import time
import warnings

warnings.filterwarnings("ignore")
import paho.mqtt.client as mqtt


# デバイスが受け取れる MCP 要求の最大長 (tk_mcp_int.h MCP_BUF_SZ 512 - 終端)。
# これを超える要求は捨てられ、応答も返らない。
MCP_MAX_REQUEST = 511

# 1 チャンクの生バイト数。base64 化と JSON・API キーを含めて 482 バイトに収まる
DEFAULT_CHUNK_BYTES = 192


def call(tool: str, args: dict, broker: str, port: int, device_id: str,
         api_key: str | None = None, timeout: float = 10.0) -> dict:
    """MCP tool を呼出して結果の JSON を返す。"""
    req_id = random.randint(1, 99_999_999)
    got: list[str] = []
    cl = mqtt.Client(mqtt.CallbackAPIVersion.VERSION1, client_id=f"wu-{req_id}")
    cl.on_message = lambda c, u, m: got.append(m.payload.decode())
    cl.connect(broker, port)
    cl.subscribe(f"mcp/{device_id}/response")
    cl.loop_start()
    time.sleep(0.3)
    req = {"jsonrpc": "2.0", "id": req_id,
           "method": "tools/call",
           "params": {"name": tool, "arguments": args}}
    if api_key:
        req["api_key"] = api_key
    payload = json.dumps(req)
    if len(payload) > MCP_MAX_REQUEST:
        cl.loop_stop()
        raise ValueError(f"{tool}: request is {len(payload)} bytes; the device "
                         f"accepts at most {MCP_MAX_REQUEST} (use a smaller --chunk-bytes)")
    cl.publish(f"mcp/{device_id}/request", payload)

    deadline = time.time() + timeout
    resp = None
    while time.time() < deadline:
        time.sleep(0.05)
        for g in got:
            try:
                r = json.loads(g)
                if r.get("id") == req_id:
                    resp = r
                    break
            except Exception:
                pass
        if resp:
            break
    cl.loop_stop()
    if resp is None:
        raise TimeoutError(f"No response from device for {tool}")
    if "error" in resp:
        raise RuntimeError(f"{tool}: {resp['error']}")
    res = resp.get("result", {})
    content = res.get("content")
    if isinstance(content, list) and content and "text" in content[0]:
        try:
            return json.loads(content[0]["text"])
        except json.JSONDecodeError:
            return {"text": content[0]["text"]}
    return res


def do_upload(args, call_kwargs) -> int:
    with open(args.wasm_file, "rb") as f:
        wasm = f.read()
    size = len(wasm)
    sha = hashlib.sha256(wasm).hexdigest()
    print(f"file={args.wasm_file} size={size} sha256={sha[:16]}...")

    try:
        call("wasm_abort", {"slot": args.slot}, **call_kwargs, timeout=3.0)
    except Exception:
        pass

    print(f"wasm_begin slot={args.slot} size={size}")
    r = call("wasm_begin", {"slot": args.slot, "size": size,
                            "sha256": sha, "memory_kb": args.memory_kb},
             **call_kwargs)
    if r.get("status") != "ok":
        print(f"  begin failed: {r}")
        return 1

    cs = args.chunk_bytes
    sent = 0
    off = 0
    while off < size:
        chunk = wasm[off:off + cs]
        b64 = base64.b64encode(chunk).decode("ascii")
        r = call("wasm_chunk", {"slot": args.slot, "offset": off, "data": b64},
                 **call_kwargs)
        sent += len(chunk)
        pct = sent * 100 // size
        print(f"\r  {sent}/{size} ({pct}%)", end="", flush=True)
        if r.get("status") != "ok":
            print(f"\n  chunk failed at offset {off}: {r}")
            return 1
        off += cs
    print()
    return 0


def do_store(args, call_kwargs) -> int:
    print(f"wasm_store ram_slot={args.slot} flash_slot={args.store} "
          f"name={args.name!r} auto_boot={args.auto_boot}")
    r = call("wasm_store",
             {"slot": args.slot, "flash_slot": args.store,
              "name": args.name or "", "auto_boot": args.auto_boot},
             **call_kwargs, timeout=30.0)
    print(f"  {json.dumps(r, ensure_ascii=False, indent=2)}")
    return 0 if r.get("status") == "ok" else 1


def do_load(args, call_kwargs) -> int:
    print(f"wasm_load flash_slot={args.load} ram_slot={args.slot} run={args.run}")
    r = call("wasm_load",
             {"flash_slot": args.load, "slot": args.slot,
              "run": args.run, "entry": args.entry},
             **call_kwargs, timeout=15.0)
    print(f"  {json.dumps(r, ensure_ascii=False, indent=2)}")
    return 0 if r.get("status") in ("loaded", "ok") else 1


def do_erase(args, call_kwargs) -> int:
    print(f"wasm_erase flash_slot={args.erase}")
    r = call("wasm_erase", {"flash_slot": args.erase},
             **call_kwargs, timeout=15.0)
    print(f"  {json.dumps(r, ensure_ascii=False)}")
    return 0 if r.get("status") == "erased" else 1


def do_list(call_kwargs) -> int:
    r = call("wasm_flash_list", {}, **call_kwargs)
    print(f"autoboot_slot={r.get('autoboot_slot')}")
    for s in r.get("slots", []):
        if s.get("valid"):
            print(f"  flash[{s['flash_slot']}] {s['size']:>5}B "
                  f"mem={s['memory_kb']}KB "
                  f"auto={s.get('auto_boot')} "
                  f"name={s.get('name','')!r} "
                  f"sha={s.get('sha256','')[:16]}...")
        else:
            print(f"  flash[{s['flash_slot']}] <empty>")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("wasm_file", nargs="?", help="path to .wasm (upload mode)")
    ap.add_argument("--slot", type=int, default=0)
    ap.add_argument("--memory-kb", type=int, default=16)
    ap.add_argument("--chunk-bytes", type=int, default=DEFAULT_CHUNK_BYTES,
                    help=f"raw bytes per wasm_chunk (default {DEFAULT_CHUNK_BYTES}; "
                         f"each request must fit in {MCP_MAX_REQUEST} bytes)")
    ap.add_argument("--run", action="store_true")
    ap.add_argument("--entry", default="run")

    ap.add_argument("--store", type=int, metavar="FLASH_SLOT",
                    help="After upload, persist to this Flash slot (0..3)")
    ap.add_argument("--name", default="",
                    help="Module name for --store (max 31 chars)")
    ap.add_argument("--auto-boot", action="store_true",
                    help="Mark stored slot for autoboot at power-on")
    ap.add_argument("--load", type=int, metavar="FLASH_SLOT",
                    help="Load Flash slot into RAM slot (skip upload)")
    ap.add_argument("--erase", type=int, metavar="FLASH_SLOT",
                    help="Erase Flash slot")
    ap.add_argument("--list-flash", action="store_true",
                    help="Show Flash slot list")
    ap.add_argument("--kb-attach", action="store_true",
                    help="After upload, use the module as the keyboard key "
                         "processor (NUCLEO-H533RE keyboard brain)")
    ap.add_argument("--kb-save", action="store_true",
                    help="With --kb-attach, also save it to Flash for boot")
    ap.add_argument("--kb-status", action="store_true",
                    help="Show keyboard key processor status")
    ap.add_argument("--kb-detach", action="store_true",
                    help="Detach the key processor (back to built-in keymap)")
    ap.add_argument("--kb-erase", action="store_true",
                    help="With --kb-detach, also erase the module saved in Flash")

    ap.add_argument("--broker", default=os.environ.get("MQTT_BROKER", "localhost"))
    ap.add_argument("--port", type=int, default=int(os.environ.get("MQTT_PORT", "1884")))
    ap.add_argument("--device", default=os.environ.get("DEVICE_ID", "kb-000001"))
    ap.add_argument("--api-key-file",
                    default=os.environ.get("MCP_API_KEY_FILE",
                                           os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                        "..", ".mcp_api_key")))
    args = ap.parse_args()

    api_key = None
    try:
        with open(args.api_key_file, "r") as f:
            api_key = f.read().strip()
    except Exception:
        pass

    call_kwargs = dict(broker=args.broker, port=args.port,
                       device_id=args.device, api_key=api_key)

    if args.kb_status:
        r = call("kb_wasm_status", {}, **call_kwargs)
        print(json.dumps(r, ensure_ascii=False, indent=2))
        return 0
    if args.kb_detach:
        r = call("kb_wasm_detach", {"erase": args.kb_erase}, **call_kwargs, timeout=15.0)
        print(json.dumps(r, ensure_ascii=False))
        return 0 if r.get("ok") else 1
    if args.list_flash:
        return do_list(call_kwargs)
    if args.erase is not None:
        return do_erase(args, call_kwargs)
    if args.load is not None:
        return do_load(args, call_kwargs)

    if not args.wasm_file:
        ap.error("wasm_file is required for upload mode "
                 "(or use --list-flash / --load / --erase)")

    rc = do_upload(args, call_kwargs)
    if rc != 0:
        return rc

    if args.store is not None:
        rc = do_store(args, call_kwargs)
        if rc != 0:
            return rc

    if args.kb_attach:
        print(f"kb_wasm_attach slot={args.slot} save={args.kb_save}")
        r = call("kb_wasm_attach", {"slot": args.slot, "save": args.kb_save},
                 **call_kwargs, timeout=15.0)
        print(f"  {json.dumps(r, ensure_ascii=False)}")
        return 0 if r.get("ok") else 1

    if args.run:
        print(f"wasm_run slot={args.slot} entry={args.entry}")
        r = call("wasm_run", {"slot": args.slot, "entry": args.entry},
                 **call_kwargs, timeout=15.0)
        print(f"  {json.dumps(r, ensure_ascii=False)}")

    print("done.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
