# MCPサーバ

t-hidのデバイスはMCP(Model Context Protocol)サーバを内蔵しています。ClaudeなどのAIアシスタントからネットワーク越しにデバイスのツールを呼び出して、キー入力の代行、GPIOやADCの操作、RTOSのタスクの確認、WebAssemblyモジュールの実行などができます。

MCPのメッセージ(JSON-RPC 2.0)はMQTTで運びます。デバイスとPCは同じネットワークにある必要がなく、NATの内側にあるデバイスにもブローカー経由で届きます。

## 仕組み

```text
Claude Code / Claude Desktop (MCPクライアント)
    │ 標準入出力(JSON-RPC 2.0)
tools/mcp_bridge.py (PC)
    │ MQTT  要求: mcp/<device_id>/request
    │       応答: mcp/<device_id>/response
MQTTブローカー
    │ MQTT
デバイスのMCPサーバ(μT-Kernel 3.0のタスク)
```

- デバイスは起動するとMQTTブローカーにつなぎ、自分宛ての要求トピック`mcp/<device_id>/request`を購読します。要求を1件ずつ処理し、結果を応答トピック`mcp/<device_id>/response`に出します。
- PC側の`tools/mcp_bridge.py`は、Claudeが標準入出力で送るJSON-RPCをそのまま要求トピックへ流し、応答トピックに来たものを標準出力へ返します。
- ブローカーとの接続が切れると、デバイスは5秒ごとに再接続を試みます。

## 対応構成

| 構成 | ネットワーク | ビルド |
|---|---|---|
| W5100S-EVB-Pico | 有線(W5100S) | `make TARGET=_PICO_RP2040_ MCP=1 ...` |
| Pico W | WiFi(CYW43439) | `make TARGET=_PICO_W_ WIFI=1 ...` |
| NUCLEO-H533RE + WIZ550io | 有線(W5500) | `build_bsp2`で`make`(既定で有効) |

WebAssemblyのツールは、Pico WとNUCLEO-H533REでは常に入り、W5100S-EVB-Picoでは`WASM=1`を付けたときに入ります。配線やネットワークの準備は[network.md](network.md)を見てください。W5100S-EVB-Picoはキーマトリクスとピンが重なるため、キーボード基板と一緒には使えません。

## セットアップ

### APIキーを作る

既定のブローカー`test.mosquitto.org`は誰でも読み書きできる公開ブローカーです。APIキーを設定しないと、デバイスIDを知った人は誰でもデバイスを操作できます。試すだけの場合でもキーを設定してください。

```bash
python -c "import secrets; print(secrets.token_hex(32))" > .mcp_api_key
```

リポジトリ直下に`.mcp_api_key`(16進64桁、英字は小文字)を置いてビルドすると、そのキーがファームウェアに埋め込まれます。`.mcp_api_key`とビルド時に生成される`mcp_apikey_embedded.h`は`.gitignore`で除外しています。キーはファームウェアの中にそのまま入るため、ビルドした`.uf2`や`.elf`をほかの人に渡すとキーも渡ることになります。

キーを埋め込むヘッダは、ヘッダがないときにだけ作られます。キーを後から置いたときや作り直したときは、古いヘッダを消してからビルドしてください。Picoの場合は`setup.sh`からやり直し、`mtk3_bsp/build_make/mtkernel_3/device/wiznet/mcp_apikey_embedded.h`を消します(`make clean`では消えません)。NUCLEO-H533REは`make clean`で消えます。

### ビルドして書き込む

```bash
bash setup.sh
cd mtk3_bsp/build_make

# W5100S-EVB-Pico (wasmのツールも使うならWASM=1を足す)
make TARGET=_PICO_RP2040_ clean
make TARGET=_PICO_RP2040_ MCP=1 KEYMAP=default all

# Pico W (接続先のWiFiはnetwork.mdを参照)
make TARGET=_PICO_W_ clean
make TARGET=_PICO_W_ WIFI=1 all
```

```bash
# NUCLEO-H533RE
cd build_bsp2
make clean && make
```

ツールチェーンの準備と書き込みの方法は[build_and_deploy.md](../build_and_deploy.md)にまとめています。

### デバイスIDを確かめる

起動ログ(シリアル115200bps、つなぎ方は[build_and_deploy.md](../build_and_deploy.md))に、購読した要求トピックが出ます。

```text
MCP: sub mcp/kb-1a2b3c/request
```

この`kb-1a2b3c`がデバイスIDで、MACアドレスの下位3バイトから作ります。Pico Wは無線チップ固有のMACアドレスを使うので台ごとに違います。有線の2構成(W5100S、W5500)はドライバが全ボード共通のMACアドレス`00:08:DC:12:34:56`を使うため、デバイスIDはいつも`kb-123456`になります。複数台を使う場合や公開ブローカーにつなぐ場合は、`device/wiznet/wiznet_dev.c`のMACアドレスを台ごとに変えてください。

### ブローカーを選ぶ

| ブローカー | 用途 |
|---|---|
| `test.mosquitto.org:1883`(既定) | 試用。通信内容は誰でも読める |
| 自分で立てたMosquittoなど | 実際に使うとき |

デバイスはユーザー名とパスワードなし(匿名)でブローカーにつなぎます。自前のブローカーは匿名の接続を受け付けるように設定してください。Dockerで立てる場合の例です。

```text
# mosquitto.conf
listener 1883
allow_anonymous true
```

```bash
docker run -d --name mosquitto -p 1883:1883 \
  -v "$PWD/mosquitto.conf:/mosquitto/config/mosquitto.conf" eclipse-mosquitto
```

接続先の変え方は構成によって違います。

- Pico W:ビルド時に`MQTT_BROKER_HOST`(IPアドレスかホスト名)と`MQTT_BROKER_PORT`を指定します。

```bash
make TARGET=_PICO_W_ WIFI=1 \
  EXTRA_DEFS='-DMQTT_BROKER_HOST=\"192.168.0.10\" -DMQTT_BROKER_PORT=1883' all
```

- W5100S-EVB-Pico:`app_program/app_main.c`の`"test.mosquitto.org"`(DNSで引くホスト名)と`mcp_conf.broker_port`を書き換え、`setup.sh`からやり直します。
- NUCLEO-H533RE:`app_h533/usermain.c`の同じ箇所を書き換えます。

## Claude CodeとClaude Desktopから使う

PCにPython 3.10以上とpaho-mqttを入れておきます。

```bash
pip install paho-mqtt
```

### Claude Code

```bash
claude mcp add --transport stdio \
  --env MQTT_BROKER=test.mosquitto.org \
  --env MQTT_PORT=1883 \
  --env DEVICE_ID=kb-1a2b3c \
  --env MCP_API_KEY_FILE=/path/to/t-hid/.mcp_api_key \
  utk3-keyboard -- python /path/to/t-hid/tools/mcp_bridge.py
```

プロジェクトの`.mcp.json`に書く場合は次のようにします。パスは自分の環境の絶対パスに置き換えてください。`.mcp.json`は`.gitignore`で除外しているので、コミットされません。

```json
{
  "mcpServers": {
    "utk3-keyboard": {
      "type": "stdio",
      "command": "python",
      "args": ["/path/to/t-hid/tools/mcp_bridge.py"],
      "env": {
        "MQTT_BROKER": "test.mosquitto.org",
        "MQTT_PORT": "1883",
        "DEVICE_ID": "kb-1a2b3c",
        "MCP_API_KEY_FILE": "/path/to/t-hid/.mcp_api_key"
      }
    }
  }
}
```

### Claude Desktop

`claude_desktop_config.json`に追加します。パスは絶対パスで書きます。

```json
{
  "mcpServers": {
    "utk3-keyboard": {
      "command": "python",
      "args": ["C:/path/to/t-hid/tools/mcp_bridge.py"],
      "env": {
        "MQTT_BROKER": "test.mosquitto.org",
        "MQTT_PORT": "1883",
        "DEVICE_ID": "kb-1a2b3c",
        "MCP_API_KEY_FILE": "C:/path/to/t-hid/.mcp_api_key"
      }
    }
  }
}
```

### ブリッジの環境変数

| 変数 | 既定 | 説明 |
|---|---|---|
| `MQTT_BROKER` | `localhost` | ブローカーのホスト名かIPアドレス |
| `MQTT_PORT` | `1883` | ブローカーのポート |
| `MQTT_USER` / `MQTT_PASS` | なし | ブリッジがブローカーにつなぐときの認証 |
| `MQTT_KEEPALIVE` | `60` | MQTTのキープアライブ(秒) |
| `DEVICE_ID` | `kb-000000` | 操作するデバイス |
| `MCP_API_KEY` | なし | APIキー(16進64桁)。64桁でなければ無視する |
| `MCP_API_KEY_FILE` | なし | APIキーを書いたファイル。`MCP_API_KEY`があればそちらを使う |
| `BRIDGE_DEBUG` | なし | `1`にすると送受信の内容を標準エラーに出す |
| `BRIDGE_LOG` | なし | ログを追記するファイル。標準エラーが見えないクライアントで使う |

ブリッジは、APIキーがあればすべての要求に`api_key`を付けて送ります。デバイスが受け取れる大きさ(511バイト)を超えた要求は送らずに、エラー`-32600`を返します。

### 使ってみる

登録すると、Claudeからデバイスのツールが見えるようになります。たとえば次のように頼めます。

- 「デバイスの稼働時間とタスクの一覧を見せて」(`get_uptime`、`get_tasks`)
- 「メモ帳にHello Worldと入力して」(`send_keys`)
- 「Ctrl+Sを押して」(`press_combo`)
- 「GP15をHighにして」(`gpio_control`)
- 「内蔵温度センサの値を読んで」(`read_adc`)
- 「このCのコードをwasmにして、デバイスで動かして」(`wasm_*`、[wasm.md](wasm.md))

## ツール一覧

鍵の列が○のツールは、APIキーを設定したデバイスではキーかHMAC署名が必要です。キーを設定していなければ、どのツールも誰でも呼べます。ツールの中で起きたエラーは、結果のJSONの`error`に入ります。

### キーボード

| ツール | 鍵 | 引数 | 内容 |
|---|---|---|---|
| `send_keys` | ○ | `text` | 文字列をUSBキーボードの打鍵として送る。空白から`z`までのASCIIと改行(Enter)を送れる |
| `press_combo` | ○ | `modifier`, `key`, `hold_ms` | 修飾キーとキーを同時に押して離す。`modifier`はビット(1=Ctrl、2=Shift、4=Alt、8=GUI)か`"ctrl+shift"`のような名前(`ctrl`/`shift`/`alt`/`gui`/`win`/`cmd`)。`key`はHIDキーコードかASCII 1文字。`hold_ms`は既定10、最大5000 |
| `set_layer` | ○ | `layer`, `action` | TG()レイヤー(0〜7)を`on`/`off`/`toggle`(既定)で切り替える。`clear_all`ですべて解除 |
| `get_keyboard_status` | | なし | 現在のレイヤー、有効なレイヤーのビット、HIDの出力先(USB/BLE) |

### システム

| ツール | 鍵 | 引数 | 内容 |
|---|---|---|---|
| `get_uptime` | | なし | 起動からの経過時間 |
| `get_datetime` | | なし | 現在の日時とUNIX時刻。SNTPで時刻を合わせていなければ`not synced` |
| `get_tasks` | | なし | RTOSのタスク一覧(ID、優先度、状態) |
| `task_create` | ○ | `action`, `pin`, `interval_ms`, `name` | 決まった動作をくり返すタスクを作る。`action`は`blink`(ピンの点滅)、`adc_log`(温度センサの読み取り)、`heartbeat`(短い点灯)。`pin`の既定は25、`interval_ms`の既定は1000。同時に4個まで |
| `task_stop` | ○ | `slot`か`task_id` | `task_create`で作ったタスクをスロット番号(0〜3)で止める。またはタスクIDで強制終了する。ID 1〜8のタスクは止められない |
| `agent_start` | ○ | `action`, `interval_ms` | 監視タスクを`start`/`stop`する(`status`で状態)。一定間隔(既定5000ms)でリンク、MQTT接続、休止状態のタスク、ヒープの残りを調べ、異常をシリアルに出す |
| `shell` | ○ | `cmd` | 簡易コマンド。`mem`、`reg <16進アドレス>`、`tasks`、`net`、`time`、`echo <文字列>`、`help` |
| `watchdog` | ○ | `action`, `timeout_ms` | ハードウェアウォッチドッグを`status`/`enable`/`feed`/`disable`する。`timeout_ms`は既定5000で、上限はRP2040が8388、H533が8000。H533は一度有効にすると止められない |
| `reboot` | ○ | なし | 再起動 |

### I/Oとメモリ

| ツール | 鍵 | 引数 | 内容 |
|---|---|---|---|
| `gpio_control` | ○ | `pin`, `mode` | GPIOを`read`(入力)、`output_high`、`output_low`にする。`pin`は0〜29 |
| `read_adc` | | `ch` | ADCを読み、生の値と電圧(mV)を返す。`ch`は0〜4で、RP2040では4が内蔵温度センサ(`temp_c_x10`に摂氏の10倍を返す) |
| `pwm_control` | ○ | `pin`, `duty`, `freq` | PWMを出す。`duty`は0〜100(%)、`freq`は既定1000Hz |
| `set_led` | ○ | `state`か`heartbeat` | ボードのLED(RP2040はGP25、H533はPB0)を点灯・消灯する。`heartbeat: true`で点滅に戻す |
| `peek` | ○ | `addr` | メモリやレジスタを32ビット読む。アドレスは4の倍数 |
| `poke` | ○ | `addr`, `value` | メモリやレジスタに32ビット書き、読み戻した値を返す |
| `flash_write` | ○ | `action`, `offset`, `data`, `length` | Flash末尾の保存用セクタを`read`/`write`/`erase`する。`read`は最大128バイト(`length`の既定は16)。`write`の`data`は16進文字列で、1回にRP2040は256バイト、H533は16バイトまで |

H533の`gpio_control`、`pwm_control`、`task_create`のピン番号は、0〜15がPA0〜PA15、16〜29がPB0〜PB13です。H533で`pwm_control`に使えるのは0〜3(PA0〜PA3)と16、17(PB0、PB1)です。

`flash_write`が扱うセクタには、`mcp_auth`で保存したAPIキー(オフセット0x100から)、`wifi_set_credentials`で保存したWiFiの設定(0x200から)、wasmの自動実行の設定(0x2C0から)も入っています。`erase`や、この範囲への`write`でこれらも消えます。

### ネットワーク

| ツール | 鍵 | 引数 | 内容 |
|---|---|---|---|
| `get_network` | | なし | IPアドレス、MACアドレス、リンク状態、ホスト名 |
| `tcp_connect` | ○ | `host`, `port`, `send`, `timeout` | TCPで接続し、`send`を送ってから最大255バイトを受け取って切断する。`host`はIPアドレス。`timeout`は既定5000ms(100〜30000) |
| `net_scan` | ○ | `host`, `ports`, `timeout` | TCPポートが開いているかを調べる。`ports`は`"80,443"`か`"1-100"`の形で、最大32ポート。`timeout`は1ポートごとで既定1000ms |
| `relay` | ○ | `device_id`, `method`, `params` | 別のデバイスの要求トピックへJSON-RPCの要求を送る。`params`はJSONを文字列にしたもの。応答は相手の応答トピックに出て、このツールの結果には入らない |

`relay`で送る要求には`api_key`が付かないため、相手がAPIキーを設定していると、呼べるのは鍵のいらないツールだけです。

### 認証とWiFi

| ツール | 鍵 | 引数 | 内容 |
|---|---|---|---|
| `mcp_auth` | △ | `action` | APIキーを`generate`(生成)、`revoke`(削除)する。`status`で状態を返す。`status`は鍵不要で、`generate`と`revoke`はキーが有効なときだけ鍵が必要 |
| `wifi_set_credentials` | ○ | `ssid`, `psk`, `auth` | WiFiのSSIDとパスワードをFlashに保存し、次の起動から使う。`auth`は`open`/`wpa2`(既定)/`wpa3`。Pico Wのみ |
| `wifi_get_credentials` | | なし | 保存済みのSSID、認証方式、パスワードの長さ。パスワードそのものは返さない。Pico Wのみ |

### WebAssembly

使い方と引数は[wasm.md](wasm.md)にまとめています。

| ツール | 鍵 | 内容 |
|---|---|---|
| `wasm_begin` / `wasm_chunk` | ○ | モジュールを受け取る |
| `wasm_run` / `wasm_stop` / `wasm_abort` | ○ | 実行、停止、受け取りの取り消し |
| `wasm_info` | | RAMスロットの状態 |
| `wasm_store` / `wasm_load` / `wasm_erase` | ○ | Flashへの保存、RAMへの読み戻し、Flashの消去(RP2040のみ) |
| `wasm_flash_list` | | Flashに保存したモジュールと自動実行の対象(RP2040のみ) |

### NUCLEO-H533REのみ

| ツール | 鍵 | 引数 | 内容 |
|---|---|---|---|
| `kb_wasm_attach` | ○ | `slot`, `save` | 受け取り済みのwasmモジュールをキー処理にする。`save: true`でFlashにも保存し、次の起動でも使う([wasm.md](wasm.md)) |
| `kb_wasm_detach` | ○ | `erase` | 組み込みのキーマップに戻す。`erase: true`で保存済みのモジュールも消す |
| `kb_wasm_status` | | なし | キー処理の状態(wasmか組み込みか、スキャン回数、`kb_scan`の最長時間、左右の接続) |
| `u2f_status` | | なし | U2F認証器の状態(暗号処理にハードウェアを使っているか、署名カウンタ、起動後の登録と認証の回数)。秘密の値は返さない([u2f.md](u2f.md)) |

### 非推奨

`vm_run`、`vm_stop`、`vm_store`は独自バイトコードVM用の非推奨ツールです。代わりにWebAssemblyのツールを使ってください。

## セキュリティ

### APIキー

- キーは32バイト(16進64桁)です。ビルド時に`.mcp_api_key`から埋め込むか、`mcp_auth`の`generate`で作ってFlashに保存します。起動時はFlashに保存したキーを先に使い、なければ埋め込んだキーを使います。
- 鍵の列が空欄のツール(読み取りだけのもの)と、`initialize`、`ping`、`tools/list`はキーなしで呼べます。
- キーを設定していないデバイスでは、誰でも`mcp_auth`の`generate`を呼べます。キーなしで公開ブローカーにつなぐと、第三者にキーを設定されて自分が操作できなくなることもあります。
- `generate`で作ったキーは、その応答で一度だけ返ります。応答はブローカーを通るので、公開ブローカーでは`generate`を使わず、`.mcp_api_key`で埋め込んでください。
- `generate`はキーが有効なときは失敗します。先に`revoke`してください。`revoke`はFlashのキーを消しますが、埋め込んだキーは次の起動で有効に戻ります。
- `generate`の乱数は、RP2040ではリングオシレータの乱数ビットを使います。H533では起動からの時間を種にした簡易な擬似乱数で、推測されやすいため、`.mcp_api_key`で埋め込む方法を使ってください。

### 要求にキーを付ける

要求のJSONの最上位に、次のどちらかを付けます。キーと署名の16進は小文字で書きます。

```json
{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{...},
 "api_key":"<16進64桁のAPIキー>"}
```

```json
{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{...},
 "timestamp":1727000000,
 "signature":"<HMAC-SHA256の16進>"}
```

HMACの鍵はAPIキーを16進から戻した32バイトで、署名する文字列は`<method>|<timestamp>`(timestampは10進の整数)です。Pythonで作る例です。

```python
import hashlib, hmac, time
key = bytes.fromhex(open(".mcp_api_key").read().strip())
ts = int(time.time())
sig = hmac.new(key, f"tools/call|{ts}".encode(), hashlib.sha256).hexdigest()
```

`mcp_bridge.py`は`api_key`の方式で付けます。

### その他の保護

- レート制限:デバイス全体で1秒あたり10要求までです。超えるとエラー`-32003`を返します。
- `poke`はRAM先頭のベクタテーブル(0x20000000〜0x200000BF)と、RP2040のFlash(0x10000000〜0x101FFFFF)には書けません。
- H533のU2F構成では、鍵などの秘密を置いたアドレスを`peek`、`poke`、`shell`の`reg`で読み書きできません。
- `task_stop`はID 1〜8のタスクを止められません。

### 公開ブローカーを使うときの注意

- 公開ブローカーでは、要求と応答(ツールの引数と結果)を誰でも読めます。TLSも使っていません。
- `api_key`の方式では、要求を読まれるとキーも漏れます。
- HMACの方式でも、署名する文字列はメソッド名と時刻だけで、ツール名や引数は含みません。時刻の新しさも検査しないため、一度読まれた署名は別の要求にそのまま流用できます。キーそのものが通信に出ない点を除けば、`api_key`の方式と同じ強さと考えてください。
- `send_keys`や`press_combo`は、デバイスをつないだPCを実際に操作します。

このため、公開ブローカーは試用にとどめ、実際に使うときはLAN内などに自分で立てたブローカーを使ってください。信頼できない人やAIにキーを渡さないでください。

## プロトコル

| 項目 | 内容 |
|---|---|
| 転送 | MQTT 3.1.1、QoS 0、キープアライブ60秒 |
| トピック | 要求`mcp/<device_id>/request`、応答`mcp/<device_id>/response` |
| MQTTのクライアントID | デバイスは`mcp-<device_id>`、ブリッジは`mcp-bridge-<device_id>` |
| 形式 | JSON-RPC 2.0 |
| メソッド | `initialize`(protocolVersion `2024-11-05`)、`ping`、`tools/list`、`tools/call`、`notifications/initialized`(応答なし) |
| サーバ名 | `utk3-keyboard` |
| ツールの結果 | `content: [{"type":"text","text":"<結果のJSON文字列>"}]`。存在しないツールは`isError: true` |
| 要求の最大長 | 511バイト。超えた要求は捨てられ、応答も返らない |

エラーコードは次のとおりです。

| コード | 意味 |
|---|---|
| -32700 | JSONの解析に失敗した |
| -32600 | 要求の形式が正しくない |
| -32601 | 未知のメソッド |
| -32001 | APIキーが必要、または一致しない |
| -32003 | レート制限を超えた |

ブリッジを使わずに、mosquittoのクライアントから直接呼ぶこともできます。

```bash
mosquitto_sub -h test.mosquitto.org -t 'mcp/kb-1a2b3c/response' &
mosquitto_pub -h test.mosquitto.org -t 'mcp/kb-1a2b3c/request' \
  -m '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"get_uptime","arguments":{}}}'
```

## 制限

- 要求は511バイトまでです。`api_key`を付けると約80バイト使うので、`send_keys`で長い文字列を送るときは分けてください。
- デバイスは要求を1件ずつ処理します。ツールの実行中に届いた要求は捨てられることがあるので、応答を受け取ってから次の要求を送ってください。`net_scan`や`hold_ms`の長い`press_combo`は、終わるまでほかの要求を受け付けません。
- MQTTはQoS 0なので、要求や応答が失われることがあります。応答がなければ送り直してください。
- 同じ`DEVICE_ID`のブリッジを2つ同時に動かすと、MQTTのクライアントIDが重なって互いに切断し合います。
- デバイスはブローカーに匿名でつなぎます。ユーザー名とパスワードが必要なブローカーを使うには、アプリの`mcp_conf.mqtt_user`と`mcp_conf.mqtt_pass`を設定するように書き換えます。
- `send_keys`の文字の変換はUSキーボード配列を前提にしています。PC側の配列がJISだと、一部の記号が別の文字になります。`{`、`|`、`}`、`~`、タブ、ASCII以外の文字は送れません。
- `tcp_connect`と`net_scan`はホスト名を引きません。IPアドレスで指定します。
- `peek`の値など、大きな数値は16進の文字列で返ります。
- Pico WのGP23〜25とGP29は無線チップの信号線です。`gpio_control`や`task_create`でこれらのピンを指定するとWiFiが止まります。`set_led`ではPico WのLED(無線チップ側につながっている)は操作できません。
- Pico Wの構成はSNTPで時刻を合わせないため、`get_datetime`は`not synced`を返します。
- NUCLEO-H533REでは、`read_adc`で内蔵温度センサ(ADCのチャネル18)は読めません。
- NUCLEO-H533RE(W5500)でのMCPは、ビルドまでで実機での動作は確認していません。
