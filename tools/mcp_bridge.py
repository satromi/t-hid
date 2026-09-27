#!/usr/bin/env python3
"""
MCP Bridge: MQTT <-> stdio

μT-Kernel 3.0 MCP サーバ (MQTT) と Claude Desktop / Claude Code などの
MCP クライアント (stdio) を接続するブリッジ。

通信フロー:
  stdin  → JSON-RPC リクエスト → MQTT publish  mcp/<device_id>/request
  MQTT subscribe  mcp/<device_id>/response → JSON-RPC レスポンス → stdout

環境変数:
  MQTT_BROKER   MQTT ブローカ host/ip  (default: localhost)
  MQTT_PORT     MQTT ブローカ port     (default: 1883)
  MQTT_USER     MQTT 認証 username     (optional)
  MQTT_PASS     MQTT 認証 password     (optional)
  DEVICE_ID     対象 Pico の device_id (default: kb-000000)
                Pico のシリアルコンソールで "MCP: sub mcp/kb-XXXXXX/request" ログで確認。
  MQTT_KEEPALIVE MQTT keepalive 秒     (default: 60)
  MCP_API_KEY      デバイスの API キー (64 桁 hex)。各要求に付けて送る
  MCP_API_KEY_FILE API キーを書いたファイル (.mcp_api_key)。MCP_API_KEY が優先
  BRIDGE_DEBUG   "1" で stderr にトレースログ

Claude Desktop 設定例 (claude_desktop_config.json):
  {
    "mcpServers": {
      "utk3-keyboard": {
        "command": "python",
        "args": ["C:/path/to/t-hid/tools/mcp_bridge.py"],
        "env": {
          "MQTT_BROKER": "test.mosquitto.org",
          "MQTT_PORT": "1883",
          "DEVICE_ID": "kb-000001"
        }
      }
    }
  }

Claude Code 設定例 (.mcp.json をプロジェクトに置く):
  {
    "mcpServers": {
      "utk3-keyboard": {
        "command": "python",
        "args": ["${CLAUDE_PROJECT_DIR}/tools/mcp_bridge.py"],
        "env": {
          "MQTT_BROKER": "localhost",
          "DEVICE_ID": "kb-000001"
        }
      }
    }
  }

ローカル broker (docker) の例:
  docker run -d --name mosq -p 1883:1883 \
    -v /etc/mosquitto/mosquitto.conf:/mosquitto/config/mosquitto.conf \
    eclipse-mosquitto

依存:
  pip install paho-mqtt    (>=1.6 動作、>=2.0 で CallbackAPIVersion 経路を使用)
"""

import sys
import os
import json
import warnings

# paho-mqtt 2.x は VERSION1 callback を deprecated 警告する。
# v2 新 API は callback signature が変わるため、互換性維持のため VERSION1 を使い、
# 警告だけ抑制する (スクリプト寿命の間に v2 移行する価値はない薄いブリッジ)。
warnings.filterwarnings("ignore", category=DeprecationWarning,
                        message=r".*Callback API version 1.*")

import paho.mqtt.client as mqtt


def _log(msg: str) -> None:
    """診断ログは stderr (stdout は JSON-RPC 専用).

    BRIDGE_LOG が指定されていれば追記ファイルにも同時出力
    (Claude Code など stderr が見えない環境向け)。
    """
    sys.stderr.write(msg + "\n")
    sys.stderr.flush()
    log_path = os.environ.get("BRIDGE_LOG")
    if log_path:
        try:
            with open(log_path, "a", encoding="utf-8") as f:
                import datetime
                ts = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
                f.write(f"{ts} {msg}\n")
        except Exception:
            pass


def _dbg(msg: str) -> None:
    if os.environ.get("BRIDGE_DEBUG") == "1":
        _log("[bridge] " + msg)


def _make_client(client_id: str):
    """paho-mqtt 1.x / 2.x 両対応の Client 生成."""
    cav = getattr(mqtt, "CallbackAPIVersion", None)
    if cav is not None:
        # paho-mqtt 2.x: CallbackAPIVersion.VERSION1 が 1.x 互換シグネチャ
        return mqtt.Client(
            callback_api_version=cav.VERSION1,
            client_id=client_id,
            protocol=mqtt.MQTTv311,
        )
    # paho-mqtt 1.x fallback
    return mqtt.Client(client_id=client_id, protocol=mqtt.MQTTv311)


# デバイスが受け取れる要求の最大長 (tk_mcp_int.h MCP_BUF_SZ 512 - 終端)。
# 超えた要求はデバイスで捨てられ応答が返らないため、ブリッジで弾く。
MCP_MAX_REQUEST = 511


def _load_api_key() -> str | None:
    """MCP_API_KEY (64 桁 hex) または MCP_API_KEY_FILE の内容を返す。"""
    key = os.environ.get("MCP_API_KEY")
    path = os.environ.get("MCP_API_KEY_FILE")
    if not key and path:
        try:
            with open(path, "r", encoding="utf-8") as f:
                key = f.read().strip()
        except OSError as e:
            _log(f"Cannot read MCP_API_KEY_FILE {path}: {e}")
            return None
    if key and len(key) != 64:
        _log("API key must be 64 hex characters; ignored")
        return None
    return key or None


def main() -> int:
    _dbg(f"bridge starting pid={os.getpid()} py={sys.version.split()[0]} cwd={os.getcwd()}")
    broker     = os.environ.get("MQTT_BROKER", "localhost")
    port       = int(os.environ.get("MQTT_PORT", "1883"))
    device_id  = os.environ.get("DEVICE_ID",   "kb-000000")
    mqtt_user  = os.environ.get("MQTT_USER")
    mqtt_pass  = os.environ.get("MQTT_PASS")
    keepalive  = int(os.environ.get("MQTT_KEEPALIVE", "60"))

    topic_req = f"mcp/{device_id}/request"
    topic_res = f"mcp/{device_id}/response"

    client = _make_client(client_id=f"mcp-bridge-{device_id}")
    client.reconnect_delay_set(min_delay=1, max_delay=30)
    if mqtt_user:
        client.username_pw_set(mqtt_user, mqtt_pass or "")

    def on_message(client, userdata, msg):
        try:
            payload = msg.payload.decode("utf-8", errors="replace")
            _dbg(f"<- {msg.topic}: {payload[:120]}")
            sys.stdout.write(payload + "\n")
            sys.stdout.flush()
        except Exception as e:
            _log(f"Error writing response: {e}")

    def on_connect(client, userdata, flags, rc):
        if rc == 0:
            client.subscribe(topic_res, qos=0)
            _log(f"Connected {broker}:{port}, subscribed {topic_res}")
        else:
            _log(f"MQTT connect failed: rc={rc}")

    def on_disconnect(client, userdata, rc):
        if rc != 0:
            _log(f"MQTT disconnected (rc={rc}), reconnecting...")

    client.on_connect = on_connect
    client.on_message = on_message
    client.on_disconnect = on_disconnect

    try:
        client.connect(broker, port, keepalive=keepalive)
    except Exception as e:
        _log(f"Cannot connect to MQTT broker {broker}:{port}: {e}")
        return 1

    client.loop_start()

    api_key = _load_api_key()
    if api_key:
        _log("API key loaded (added to every request)")

    try:
        for line in sys.stdin:
            line = line.strip()
            if not line:
                continue
            try:
                req = json.loads(line)   # 妥当性確認
            except json.JSONDecodeError as e:
                _log(f"Invalid JSON: {e}")
                continue
            if api_key and isinstance(req, dict) and "api_key" not in req:
                req["api_key"] = api_key
                line = json.dumps(req, separators=(",", ":"))
            data = line.encode("utf-8")
            if len(data) > MCP_MAX_REQUEST:
                _log(f"Request too large for device ({len(data)} > {MCP_MAX_REQUEST} bytes)")
                if isinstance(req, dict) and "id" in req:
                    err = {"jsonrpc": "2.0", "id": req["id"],
                           "error": {"code": -32600,
                                     "message": f"request too large for device "
                                                f"({len(data)} > {MCP_MAX_REQUEST} bytes)"}}
                    sys.stdout.write(json.dumps(err) + "\n")
                    sys.stdout.flush()
                continue
            _dbg(f"-> {topic_req}: {line[:120]}")
            client.publish(topic_req, data, qos=0)
    except (KeyboardInterrupt, EOFError):
        pass
    finally:
        client.loop_stop()
        client.disconnect()

    return 0


if __name__ == "__main__":
    sys.exit(main())
