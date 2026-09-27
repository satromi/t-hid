# WebAssembly(wasm)で機能を追加する

t-hidにはWebAssemblyのインタプリタ[wasm3](https://github.com/wasm3/wasm3)(MITライセンス)を組み込んでいます。PCでCなどから`.wasm`を作り、MQTT経由のMCPでデバイスへ送ると、ファームウェアを書き換えずにプログラムを追加して実行できます。

- LEDの点滅、センサー値のログ出力、MQTTへの通知といった小さな処理を後から足す
- ClaudeなどのAIに書かせたコードを、そのまま実機で動かす([mcp.md](mcp.md))
- NUCLEO-H533REのキーボードブレインで、キー処理そのものを差し替える

wasmモジュールはサンドボックスの中で動きます。読み書きできるのは自分のメモリと、デバイスが用意した「ホスト関数」だけです。

## 対応構成

| 構成 | ビルド | 実行 | Flashへの保存 | 電源投入時の自動実行 |
|---|---|---|---|---|
| Pico W(WiFi) | `make TARGET=_PICO_W_ WIFI=1` | ○ | ○ | ○ |
| W5100S-EVB-Pico(有線) | `make TARGET=_PICO_RP2040_ WASM=1` | ○ | ○ | なし |
| NUCLEO-H533RE + W5500 | `make -C build_bsp2`(既定で`WASM=1`) | ○ | キー処理モジュールのみ | キー処理モジュールのみ |

wasmはMCPのツールとして操作するので、ネットワークとMCPが有効な構成でだけ使えます。MCPの準備(APIキー、ブローカ、デバイスID)は先に[mcp.md](mcp.md)で済ませてください。ビルドと書き込みは[build_and_deploy.md](../build_and_deploy.md)にまとめています。

## しくみ

```text
PC                                  デバイス
hello.c                             MCPサーバ
  | clang                             |
hello.wasm                          RAMスロット0〜3(1つ32KBまで)
  | wasm_upload.py ---- MQTT ---->    | SHA-256を照合してwasm3に読み込む
      wasm_begin                      | 専用のタスクでrun()を呼ぶ
      wasm_chunk × N                  | ホスト関数(gpio, adc, log, mqtt …)
      wasm_run
```

1. `wasm_begin`でRAMスロットを確保し、`wasm_chunk`でbase64にしたバイナリを少しずつ送ります。
2. `wasm_run`でSHA-256を照合してwasm3に読み込み、μT-Kernelのタスクを1つ作って指定の関数(既定は`run`)を呼びます。
3. 関数から戻るとタスクは終わります。途中で止めるときは`wasm_stop`を使います。

RAMスロットは4つあり、別々のモジュールを並行して動かせます。wasmのタスクは優先度13で、キースキャン(8)やMCPサーバ(10)より低いので、キー入力を妨げません。

RP2040では、wasmの実行中にハードフォールトが起きると、そのwasmのタスクだけを終わらせてシステムは動き続けます。スロットは`stopped`になり、エラーに`hard fault recovered`が残ります。

## 準備

### clang

wasm32ターゲット付きのclang(16以降)とwasm-ldが必要です。

| OS | インストール |
|---|---|
| Windows | `winget install LLVM.LLVM` |
| Ubuntu / Debian | `sudo apt install clang lld` |
| macOS | `brew install llvm`(Apple製のclangはwasm32に対応していません) |

```bash
cd tools/picolib
make verify     # clangのバージョンとwasm32のターゲット名を表示
make            # examples/*.c と kb/*.c をすべて .wasm にする
```

`make verify`で`wasm32`で始まるターゲット名が表示されれば使えます。Makefileは`wc`と`awk`を使うため、WindowsではGit Bashなどから実行してください。macOSでHomebrewのclangを使うときは`make CLANG=$(brew --prefix llvm)/bin/clang`のように指定します。

### Python

送信用の`tools/wasm_upload.py`は、Python 3とpaho-mqtt 2.0以降で動きます。

```bash
pip install paho-mqtt
```

### 接続先

ファームウェアは既定で公開ブローカ`test.mosquitto.org:1883`に接続します。`wasm_upload.py`の既定値(`localhost:1884`、デバイスID`kb-000001`)とは違うので、環境変数かオプションで指定します。

```bash
export MQTT_BROKER=test.mosquitto.org
export MQTT_PORT=1883
export DEVICE_ID=kb-1a2b3c    # 起動ログの「MCP: sub mcp/kb-1a2b3c/request」で分かる
```

APIキーは、リポジトリ直下の`.mcp_api_key`を読みます。別の場所に置いたときは`--api-key-file`か環境変数`MCP_API_KEY_FILE`で指定します。ビルド時に同じファイルがファームウェアへ埋め込まれるので、キーを作ってからビルドしてください([mcp.md](mcp.md))。

## はじめてのモジュール

コンソールに文字列を5回出すだけのモジュールです。

```c
#include "picolib.h"

__attribute__((export_name("run")))
void run(void)
{
    for (int i = 0; i < 5; i++) {
        LOG("hello from wasm");
        delay_ms(1000);
    }
}
```

ビルドします。

```bash
clang --target=wasm32 -nostdlib -O2 -fno-builtin \
      -Wl,--no-entry -Wl,--allow-undefined -Wl,--export=run -Wl,--strip-all \
      -Wl,--initial-memory=65536 -Wl,--max-memory=65536 \
      -Wl,-z,stack-size=4096 \
      -I tools/picolib -o hello.wasm hello.c
```

| オプション | 理由 |
|---|---|
| `-nostdlib` / `-fno-builtin` | libcはありません。`memcpy`なども自分で書きます |
| `-Wl,--no-entry` | `_start`は使いません |
| `-Wl,--allow-undefined` | ホスト関数を未定義のimportとして残します |
| `-Wl,--export=run` | デバイスから呼ぶ関数を公開します |
| `--initial-memory` / `--max-memory`を65536 | リニアメモリを1ページ(64KB)に固定します。デバイスでは大きな連続領域を確保できないので必須です |
| `-z,stack-size=4096` | wasmの中で使うスタックを4KBにします |

`tools/picolib/examples/`に置けば、`make`だけで同じオプションでビルドされます。

送って実行します。

```bash
python tools/wasm_upload.py hello.wasm --slot 0 --run
```

シリアルコンソールに次のように出れば成功です。

```text
wasm[0]: calling run...
[wasm] hello from wasm
[wasm] hello from wasm
...
wasm[0]: returned
```

## ホスト関数

wasmからデバイスの機能を使うための関数です。すべてimportモジュール`env`から取り込みます。宣言は[tools/picolib/picolib.h](../../tools/picolib/picolib.h)にあります。

| 関数 | 説明 | RP2040 | H533 |
|---|---|:-:|:-:|
| `int gpio_read(int pin)` | GPIOの入力値(pin 0〜29) | ○ | 常に0 |
| `void gpio_write(int pin, int value)` | GPIOの出力値を設定(pin 0〜29) | ○ | 何もしない |
| `int adc_read(int ch)` | 12bitのADC値。ch 0〜3がGP26〜29、4が内蔵温度センサ | ○ | 常に0 |
| `void delay_ms(uint32_t ms)` | 指定ミリ秒待つ。タスクが待ち状態になるので、ほかの処理は止まりません | ○ | ○ |
| `uint32_t get_ticks_ms(void)` | 起動からの経過ミリ秒 | ○ | ○ |
| `int log_printf(const char *msg, uint32_t len)` | コンソールへ出力。1回240バイトまで、先頭に`[wasm] `が付き、末尾で改行します。書式は展開しません | ○ | ○ |
| `int mqtt_pub(const char *topic, uint32_t tlen, const uint8_t *payload, uint32_t plen)` | MQTTにpublish(QoS 0)。トピックは120バイト、ペイロードは2048バイトまで。成功で0、失敗で負の値(MQTT未接続は-3) | ○ | ○ |

`LOG("文字列")`は、文字列リテラルとその長さを`log_printf`に渡すマクロです。

### GPIOを使うときの注意

`gpio_write`は出力の値を切り替えるだけで、ピンを出力に設定しません。起動時に出力になっていないピンは、先にMCPの`gpio_control`を`mode`を`output_low`にして1回呼び、出力にしておきます。

`gpio_write`が確かめるのはピン番号の範囲だけです。キーボードや無線チップが使っているピンを操作すると、それらが正しく動かなくなります。Pico W(WiFi構成)は左手のキーボードとして動くので、ピンの割り当ては次のとおりです。

| Pico Wのピン | 用途 |
|---|---|
| GP0-1 | UART(コンソール) |
| GP8-9 | I2C(左右の通信) |
| GP10-13, 16-22, 26-28 | キーマトリクス(左手) |
| GP23-25, 29 | CYW43(無線チップ) |
| GP2-7, 14, 15 | 空き |

右手のPicoでは、GP2-7とGP10-17がキーマトリクスです。W5100S-EVB-Picoでは、GP16-21をW5100Sが使います。GP26-28をキーマトリクスに使っている基板では、`adc_read`のch 0〜2は意味のある値になりません。

### 数値の出力

`log_printf`は書式を展開しないので、数値は自分で文字列にします。[sensor_dashboard.c](../../tools/picolib/examples/sensor_dashboard.c)や[collatz_bench.c](../../tools/picolib/examples/collatz_bench.c)にある`append_int`や`append_u32`をそのまま使えます。

### 別の関数を呼ぶ

`run`以外の関数も公開しておけば、`--entry`で呼び分けられます。

```c
__attribute__((export_name("fast"))) void fast(void) { /* ... */ }
```

```bash
python tools/wasm_upload.py hello.wasm --run --entry fast
```

関数は引数なし、戻り値なしで呼びます。ビルドで`-Wl,--export=run`を付けている場合も、`export_name`を付けた関数は公開されます。

## サンプル

`tools/picolib/examples/`にあります。`make`でまとめてビルドできます。

| ファイル | 内容 | 使う機能 |
|---|---|---|
| `blink.c` | GP22を200ms間隔で10回点滅 | GPIO |
| `morse.c` | GP22でSOSをモールス信号で点滅し、MQTTにも通知 | GPIO, MQTT |
| `collatz_bench.c` | コラッツ予想を1〜1000で計算し、結果と所要時間をMQTTに送る | 計算性能 |
| `sensor_dashboard.c` | ADCの全チャネルと温度センサを500msごとに10回ログに出す | ADC, ログ |
| `temp_alert.c` | 内蔵温度センサを1秒ごとに30回読み、30.0℃を超えたらMQTTに通知 | ADC, MQTT |

`blink.c`と`morse.c`はGP22を使いますが、Pico Wの左手のキーボードではGP22はキーマトリクスの行です。上の表の空きピンに書き換え、`gpio_control`で出力にしてから使ってください。

`morse.c`、`collatz_bench.c`、`temp_alert.c`の通知先は`mcp/kb-000001/...`と書いてあるので、自分のデバイスIDに書き換えてからビルドします。デバイスIDは同じ長さなので、トピックの長さの引数はそのままで構いません。通知は`mosquitto_sub`などで確認できます。

```bash
mosquitto_sub -h test.mosquitto.org -t 'mcp/kb-1a2b3c/#' -v
```

## Flashに保存して電源投入時に動かす(RP2040)

RAMスロットのモジュールは再起動で消えます。RP2040ではFlashの保存スロット(32KB×4)に書いておけます。

```bash
# アップロードしてFlashスロット0に保存し、電源投入時に自動実行する
python tools/wasm_upload.py morse.wasm --slot 0 --store 0 --name morse --auto-boot

python tools/wasm_upload.py --list-flash     # 保存内容と自動実行の対象を表示
python tools/wasm_upload.py --load 0 --run   # Flashスロット0をRAMスロットに戻して実行
python tools/wasm_upload.py --erase 0        # 消去(自動実行の設定も外れる)
```

| 項目 | 値 |
|---|---|
| 保存領域 | Flashの0x101DF000から32KB×4スロット |
| 1スロットに保存できる大きさ | 32,672バイト(32KBから96バイトのヘッダを除いたもの) |
| 名前 | 31文字まで |
| 自動実行 | 1スロットだけ指定できます。MCPサーバの起動後にRAMスロット0へ読み込んで`run`を呼ぶので、`mqtt_pub`をすぐ使えます |

- 自動実行は、実行の前にいったん設定を外し、起動できたときだけ設定し直します。自動実行したモジュールがハードフォールトしたときも設定を外します。壊れたモジュールを保存しても、起動できなくなることはありません。
- `--auto-boot`を付けずに、自動実行の対象になっているスロットへ保存し直すと、自動実行は外れます。
- 自動実行はPico W(WiFi)構成だけです。W5100S-EVB-Picoでは保存と読み出しはできますが、電源投入時には実行しません。
- Flashの書き換え回数には限りがあります(1セクタあたり約10万回)。頻繁に書き換える用途には向きません。
- NUCLEO-H533REでは、汎用モジュールのFlash保存に対応していません(`E_NOSPT`が返ります)。キー処理モジュールだけは`--kb-save`で保存できます。

## MCPツールで直接操作する

`wasm_upload.py`は、下のMCPツールを順に呼んでいるだけです。ClaudeなどのMCPクライアントからも同じツールを使えます([mcp.md](mcp.md))。

| ツール | 主な引数 | 説明 |
|---|---|---|
| `wasm_begin` | `slot`, `size`, `sha256`, `memory_kb` | 受信を始めます。`sha256`は64桁の16進(省略すると照合しません) |
| `wasm_chunk` | `slot`, `offset`, `data`(base64) | バイナリの一部を書き込みます |
| `wasm_run` | `slot`, `entry` | 照合して読み込み、実行します(既定の`entry`は`run`) |
| `wasm_stop` | `slot` | 実行中のタスクを止め、モジュールを解放します |
| `wasm_abort` | `slot` | 受信途中のアップロードを取り消します |
| `wasm_info` | なし | RAMスロットの状態(empty / loading / loaded / running / stopped / error)、Flashスロット、自動実行の対象 |
| `wasm_store` | `slot`, `flash_slot`, `name`, `auto_boot` | RAMスロットをFlashに保存します(RP2040) |
| `wasm_load` | `flash_slot`, `slot`, `run`, `entry` | FlashからRAMスロットに戻します(RP2040) |
| `wasm_erase` | `flash_slot` | Flashスロットを消去します(RP2040) |
| `wasm_flash_list` | なし | Flashの保存内容と自動実行の対象(RP2040) |
| `kb_wasm_attach` | `slot`, `save` | キー処理として取り付けます(H533) |
| `kb_wasm_detach` | `erase` | 組み込みのキー処理に戻します(H533) |
| `kb_wasm_status` | なし | キー処理の状態(H533) |

- `wasm_info`、`wasm_flash_list`、`kb_wasm_status`以外はAPIキーが必要です。
- MCPの要求は1件511バイトまでです。`wasm_chunk`1回に載せられるのはbase64にする前で200バイト程度で、`wasm_upload.py`は192バイトずつ送ります(`--chunk-bytes`で変更可)。AIに直接アップロードさせるときも、この大きさに合わせる必要があります。
- MCPの要求は1秒に10件までです。
- `memory_kb`(既定16)は、wasm3が関数呼び出しに使うスタックの大きさ(KB)になります。リニアメモリの大きさはモジュールの宣言で決まります。

`wasm_upload.py`の主なオプションは次のとおりです。

| オプション | 既定値 | 内容 |
|---|---|---|
| `--slot` | 0 | RAMスロット(0〜3) |
| `--memory-kb` | 16 | `wasm_begin`の`memory_kb` |
| `--run` / `--entry` | `run` | アップロード後に実行する / 呼ぶ関数 |
| `--store N` / `--name` / `--auto-boot` | | Flashスロットへの保存 |
| `--load N` / `--erase N` / `--list-flash` | | Flashスロットの操作 |
| `--kb-attach` / `--kb-save` | | キー処理として取り付ける / Flashにも保存する |
| `--kb-status` / `--kb-detach` / `--kb-erase` | | キー処理の状態 / 取り外す / 保存分も消す |
| `--broker` / `--port` / `--device` | 環境変数`MQTT_BROKER` / `MQTT_PORT` / `DEVICE_ID` | 接続先 |
| `--api-key-file` | 環境変数`MCP_API_KEY_FILE`、なければリポジトリ直下の`.mcp_api_key` | APIキー |

## キー処理をwasmで書く(NUCLEO-H533RE)

NUCLEO-H533REをキーボードの頭脳にする構成([keyboard.md](keyboard.md))では、2msごとのキー処理を、Cで書いたwasmモジュールに丸ごと差し替えられます。ファームウェアを書き換えずに、レイヤー、マクロ、コンボ、タップ/ホールドなどを入れ替えられます。

### 流れ

1. Cでキー処理を書き、clangで`.wasm`にします。
2. PC上で事前にチェックします(`tools/kb_wasm_test`)。
3. `wasm_upload.py`でH533に送り、キー処理として取り付けます。
4. 問題がなければFlashに保存します。次の起動からも自動で使われます。

H533のスキャンタスクは、2msごとに左右のPicoからマトリクスを読み、取り付けたモジュールの`kb_scan()`を呼び、レポートが前回と変わっていればUSB HIDで送ります。モジュールがなければ組み込みのキーマップで処理します。

取り付けたモジュールが異常終了(trap)すると、自動で組み込みのキー処理に戻ります。キー入力が止まったままになることはありません。

### 準備

H533を既定の構成(`make`)で書き込んでおきます。キー処理のアップロードにはW5500(WIZ550io)のネットワークとMCPサーバを使うので、`NET=0`や`WASM=0`では使えません。

起動ログ(ST-LINKの仮想COMポート、115200bps)に次の行が出れば準備完了です。

```text
IP=192.168.x.x GW=...
MCP: started (0)
MCP: sub mcp/kb-1a2b3c/request
```

最後の行はブローカに接続した後に出ます。`kb-1a2b3c`の部分がデバイスIDです。接続先とAPIキーは、前の「準備」の節と同じように設定します。公開ブローカでは誰でも同じトピックに書き込めるので、APIキーは必ず設定してください。

### クイックスタート

```bash
# 1. サンプルをビルド
cd tools/picolib
make kb                                   # kb/kb_default.wasm ほか

# 2. PCで事前チェック
cd ../kb_wasm_test
make check MOD=../picolib/kb/kb_default.wasm
#   OK: 50000 scans, report changed ... times, kb_send() 0 times

# 3. アップロードして取り付け(まだ保存しない)
cd ../..
python tools/wasm_upload.py tools/picolib/kb/kb_default.wasm --kb-attach
#   kb_wasm_attach slot=0 save=False
#     {"ok": true, "er": 0, "module_size": 2295}

# 4. 状態を確認
python tools/wasm_upload.py --kb-status

# 5. 良ければFlashに保存(もう一度アップロードし、保存付きで取り付ける)
python tools/wasm_upload.py tools/picolib/kb/kb_default.wasm --kb-attach --kb-save

# 元に戻す: 組み込みのキー処理へ(--kb-eraseで保存分も消す)
python tools/wasm_upload.py --kb-detach --kb-erase
```

保存は取り付けと同時にしかできません(`--kb-save`は`--kb-attach`と一緒に指定します)。取り付けるとRAMスロットは空になるので、保存し直すときはアップロードからやり直します。まず保存せずに試し、問題がなければ保存付きで取り付け直すのが安全です。

`--kb-status`では次の項目が分かります。

| 項目 | 内容 |
|---|---|
| `key_processor` | `wasm`か`builtin` |
| `module_size` / `saved_size` | 取り付け中 / Flashに保存したモジュールの大きさ |
| `scan_count` / `max_scan_us` | `kb_scan()`を呼んだ回数 / 1回の最大実行時間(µs) |
| `layers` | `kb_set_layers()`で設定したレイヤーの状態 |
| `last_error` | 取り付けの失敗やtrapの理由 |
| `left_connected` / `right_connected` | 左右のPicoとの接続 |

### モジュールの骨格

```c
#include "kb_brain_api.h"          /* tools/picolib/kb/kb_brain_api.h */

KB_EXPORT(kb_scan) void kb_scan(void)
{
    /* 1. kb_row()で押されているキーを調べる
     * 2. kb_keymap()などでキーコードを決める
     * 3. kb_report()で送る内容を設定する */
}
```

`kb_scan()`は2msごとに呼ばれます。戻った時点で`kb_report()`の内容が前回送ったものと違えば、ファームウェアがUSBで送ります。同じなら何も送りません。

[kb_minimal.c](../../tools/picolib/kb/kb_minimal.c)が最小の完全な例で、ベースレイヤーをそのまま送るだけのモジュールです。

### マトリクスの見方

- 行0〜9が左手、10〜19が右手です。各行のbit0〜3が列0〜3です。
- 値は左右のPicoでデバウンス済みです。H533ではデバウンスしません。
- 物理キーと行・列の対応は[kb_keymap.h](../../app_program/keyboard/kb_keymap.h)の`LAYOUT`を見てください。

```c
for (int r = 0; r < KB_ROWS; r++) {
    int bits = kb_row(r);
    for (int c = 0; c < KB_COLS; c++) {
        if (bits & (1 << c)) {
            /* (r, c) が押されている */
        }
    }
}
```

### キーコード

`kb_keymap(layer, row, col)`は、ファームウェアに組み込まれたキーマップ(ビルド時の`KEYMAP=`で選んだもの)の値を返します。モジュールの中に自分の表を持っても構いません。

| 値 | 意味 |
|---|---|
| `0x0000`(`KC_NO`) | キーなし |
| `0xFFFF`(`KC_TRNS`) | 透過(下のレイヤーを見る) |
| `0x0004`〜`0x00DF` | HIDキーコード(aが`0x04`。一覧は`kb_hid_keycodes.h`) |
| `0x00E0`〜`0x00E7` | 修飾キー(左Ctrl、Shift、Alt、GUI、右Ctrl、Shift、Alt、GUI) |
| `0x1000 \| n` | `MO(n)`:押している間レイヤーn |
| `0x2000 \| n` | `TG(n)`:押すたびにレイヤーnを切り替え |
| `0x3000 \| n` | 拡張アクション(キーボードブレインでは使いません) |
| `0xF000 \| k` | Ctrl+Shift+k(キーマップの`LSFT_LCTL_KC`) |

レイヤーの解決(MO/TG)はモジュールで行います。組み込みのキー処理と同じ規則の実装が[kb_default.c](../../tools/picolib/kb/kb_default.c)にあります。

### レポートの作り方

HIDキーボードのBootレポート(修飾キー1バイトとキー6個)を作ります。

```c
kb_report(mod, k0, k1, k2, k3, k4, k5);
```

- 修飾キーは`mod`のビットで渡します。`0xE0 + i`のキーなら`mod |= 1 << i`です。`k0`〜`k5`に入れると、PCに正しく伝わりません(事前チェックで警告が出ます)。
- 同時に送れる通常キーは6個までです。使わない枠は0にします。
- `kb_report()`を呼ばなかったスキャンでは、前に設定した内容がそのまま残ります。
- キーが1つも押されていないときも`kb_report(0, 0, 0, 0, 0, 0, 0)`を呼んで、全部離したことを送ります。呼ばないと直前のキーが押されたままになります。

### 1回のスキャンで複数のレポートを送る(マクロ)

`kb_report()`と`kb_send()`を繰り返すと、その場で順に送られます。文字列の入力などに使います([kb_macro.c](../../tools/picolib/kb/kb_macro.c))。

```c
static void tap(int mod, int key)
{
    kb_report(mod, key, 0, 0, 0, 0, 0);  kb_send();   /* 押す */
    kb_report(0, 0, 0, 0, 0, 0, 0);      kb_send();   /* 離す */
}
```

`kb_send()`はUSBの送信が終わるまで待つので、その間はスキャンが止まります。長い文字列では、数百msにわたってキー入力が止まります。送り終わったら、`kb_scan()`の通常のレポートを設定し直してください。

### 状態と時間

- グローバル変数と`static`変数は、取り付けている間ずっとスキャンをまたいで保たれます。
- キーを押した瞬間は、前回の状態を覚えておいて比べれば分かります。
- 時間は`kb_now_ms()`(起動からのミリ秒)で測ります。タップ/ホールドの例は[kb_taphold.c](../../tools/picolib/kb/kb_taphold.c)にあります。
- `kb_init()`を公開しておくと、取り付けた後、最初の`kb_scan()`の直前に1回だけ呼ばれます。変数の初期化やログ出力に使います。

### レイヤー表示とログ

- `kb_set_layers(mask)`で、MCPの`get_keyboard_status`に見せるレイヤーの状態(bit nがレイヤーn)を設定します。呼ばなければ1(ベースのみ)のままです。
- `KB_LOG("文字列")`や`log_printf(buf, len)`で、コンソールに`[kb.wasm] ...`と出ます。毎回のスキャンで呼ぶとコンソールがあふれて遅くなるので、何か起きたときだけにしてください。

### API

ファームウェアが用意する関数(importモジュール`env`)です。キー処理モジュールは、これ以外の関数をimportしていると取り付けられません。

| 関数 | 戻り値 | 説明 |
|---|---|---|
| `int kb_row(int row)` | 行のビット列 | 行0〜19の押下状態。範囲外は0 |
| `int kb_keymap(int layer, int row, int col)` | キーコード | 組み込みのキーマップ。レイヤーは0〜2。範囲外は`KC_NO` |
| `void kb_report(int mod, int k0, ..., int k5)` | なし | 次に送るレポートを設定 |
| `int kb_send(void)` | 0で成功、負でエラー | 設定済みのレポートをすぐ送る。USBが空くまで最大50ms程度待つ |
| `void kb_set_layers(int mask)` | なし | MCPに見せるレイヤーの状態 |
| `int kb_now_ms(void)` | ミリ秒 | 起動からの経過時間 |
| `int log_printf(const char *msg, uint32_t len)` | 出力した長さ | コンソールへ出力(96文字まで) |

モジュールが公開する関数は次のとおりです。

| 関数 | 必須 | 呼ばれるとき |
|---|:-:|---|
| `void kb_scan(void)` | ○ | 2msごと |
| `void kb_init(void)` | | 取り付けた後、最初の`kb_scan()`の直前に1回 |

キー処理には`wasm_run`を使いません。`wasm_begin`と`wasm_chunk`で送ったRAMスロットを、`kb_wasm_attach`で取り付けます。

### サンプル

`tools/picolib/kb/`にあります。`make kb`でまとめてビルドできます。

| ファイル | 内容 | 大きさ |
|---|---|---|
| [kb_minimal.c](../../tools/picolib/kb/kb_minimal.c) | ベースレイヤーをそのまま送る。自作するときの出発点 | 約0.7KB |
| [kb_default.c](../../tools/picolib/kb/kb_default.c) | 組み込みと同じMO/TGレイヤー、Ctrl+Shiftキー、JとKの同時押しでEsc | 約2.3KB |
| [kb_macro.c](../../tools/picolib/kb/kb_macro.c) | Insertキーで「hello」とEnterを入力 | 約1.2KB |
| [kb_taphold.c](../../tools/picolib/kb/kb_taphold.c) | Spaceを短く押すとSpace、200ms以上押し続けるとShift | 約1.0KB |

自作するときは`tools/picolib/kb/`に`.c`を置けば`make kb`の対象になります。自分でビルドするときのオプションは次のとおりです(`--export=run`は要りません)。

```bash
clang --target=wasm32 -nostdlib -O2 -fno-builtin \
      -Wl,--no-entry -Wl,--allow-undefined -Wl,--strip-all \
      -Wl,--initial-memory=65536 -Wl,--max-memory=65536 \
      -Wl,-z,stack-size=4096 -I tools/picolib/kb -o my_kb.wasm my_kb.c
```

### PCでの事前チェック

実機に送る前に、ファームウェアと同じ処理(`app_h533/kb_wasm.c`)をPC上で動かして確かめられます。clang(PC向けのビルドもできるもの)とmakeが必要です。

```bash
cd tools/kb_wasm_test
make check MOD=../picolib/kb/my_kb.wasm
```

| 確かめること | 問題があるときの表示 |
|---|---|
| 取り付けられるか | `kb_scan not exported`、`unknown import: memset` |
| ランダムなキー入力で5万回スキャンしてもtrapしないか | `trap at scan 1234: [trap] out of bounds memory access` |
| 修飾キーを`k0`〜`k5`に入れていないか | `WARNING: modifier keycodes (0xE0-0xE7) found in keycode[] ...` |

キーマップは`tools/kb_wasm_test/test_keymap.c`(既定のキーマップにいくつかキーを足したもの)を使います。実行時間はPCでは測れないので、実機で`--kb-status`の`max_scan_us`を見てください。`make run`では、`kb_default.c`と組み込みのキー処理の出力の突き合わせや、trapからの復帰などのテストをまとめて実行します。

### キー処理モジュールの制限

| 項目 | 制限 |
|---|---|
| 大きさ | 32KBまで(Flashへの保存は32,704バイトまで) |
| リニアメモリ | 64KB(1ページ)に固定してビルドします |
| wasm3のスタック | 4KB固定。深い再帰は避けます |
| 実行時間 | `kb_scan()`は2msの周期の中で終えます。目安は数百µs以内です |
| 標準ライブラリ | ありません(`-nostdlib`)。`memset`、`memcpy`、`printf`なども使えません |
| import | 上のAPIの表にある関数だけ |
| 浮動小数点 | 使えます(H533にはFPUがあります)。ただしキー処理は整数で足ります |
| 同時押し | 6キーまで(Bootプロトコル) |
| ゲームパッド | wasmのキー処理ではゲームパッドのレポートを出しません([gamepad.md](gamepad.md)) |

- 大きな構造体のコピーや配列の一括初期化で、clangが`memset`や`memcpy`の呼び出しを自動で入れることがあります。`-fno-builtin`を付け、それでも`unknown import`が出たらループで書き直すか、モジュールの中に同じ名前の関数を自分で定義します。
- USBがサスペンド中にキーを押すと、PCがリモートウェイクアップを許可していればPCを起こします。

### トラブルシューティング

| 症状、メッセージ | 原因と対処 |
|---|---|
| `No response from device for ...`(wasm_upload.py) | ブローカ、ポート、デバイスIDを確かめます。H533のログに`MCP: started`と`MCP: sub ...`が出ているかも見ます |
| `API key required`(code -32001) | 送ったAPIキーが違うか、ありません。ビルド時の`.mcp_api_key`と同じファイルを`--api-key-file`で指定します |
| `slot is not in loaded state` | アップロードが終わっていないか、そのスロットはすでに取り付けで使いました。アップロードからやり直します |
| `attach failed` | 転送中の破損(SHA-256の不一致)かメモリ不足です。アップロードからやり直します |
| `kb_scan not exported` | `KB_EXPORT(kb_scan)`を付け忘れています |
| `unknown import: xxx` | APIの表にない関数(`memset`など)を使っています。上の制限を見てください |
| 取り付けた後にキーが効かない | `kb_scan()`で毎回`kb_report()`を呼んでいるか、修飾キーを`mod`で渡しているかを確かめます。`--kb-status`の`left_connected` / `right_connected`も見ます |
| キーが押しっぱなしになる | キーを離したスキャンで、全部離したレポートを設定していません |
| コンソールに`kb.wasm: trap: ...` | 配列の範囲外アクセスなどです。組み込みのキー処理に戻っています。`--kb-status`の`last_error`を見て直し、取り付け直します |
| 起動のたびにtrapする | 保存したモジュールに問題があります。`--kb-detach --kb-erase`で保存分を消します |
| 入力がときどき遅れる | `kb_scan()`が重いか、`kb_send()`を多く使っています。`max_scan_us`を確かめます |

## 通常のwasmプログラムとの違い

wasmtimeやブラウザ向けに作られた一般的なwasmプログラムは、そのままでは動きません。

- WASIに対応していません。一般的なwasmプログラムは`wasi_snapshot_preview1`の`fd_write`などをimportしますが、ここでは`env`のホスト関数しか用意していません。
- libcがありません。`printf`、`malloc`、`strlen`なども使えないので、`-nostdlib`でビルドし、必要な処理は自分で書きます。
- ファイルやソケットのAPIはありません。外とのやり取りは`log_printf`と`mqtt_pub`だけです。
- RP2040では浮動小数点の命令を含むモジュールを読み込めません。
- リニアメモリは64KBに固定してビルドする前提です。多くのツールチェーンは既定でこれより大きなメモリを宣言するので、リンカのオプションで指定し直します。

動かしやすいのは次のようなものです。

| 種類 | 可否 |
|---|---|
| `picolib.h`を使ったC | そのまま使えます |
| Rustの`no_std`(`wasm32-unknown-unknown`) | `extern "C"`でホスト関数を宣言すれば使えます |
| Zigの`freestanding` | 同じようにホスト関数だけをimportすれば使えます |
| WASIを前提にしたC / Rust | 動きません |
| Go、C#、Python、RubyなどのGCやランタイムを持つ言語 | 大きさとメモリの面で動きません |

Rustでは次のようにします。リニアメモリとスタックの大きさは、リンカのオプションで指定します。

```bash
rustup target add wasm32-unknown-unknown
cargo new --lib myprog
# Cargo.toml の [lib] に crate-type = ["cdylib"] を追加する
RUSTFLAGS="-C link-arg=--initial-memory=65536 -C link-arg=--max-memory=65536 -C link-arg=-zstack-size=4096" \
  cargo build --target wasm32-unknown-unknown --release
```

## 制限

| 項目 | 内容 |
|---|---|
| wasmの仕様 | WebAssembly 1.0(MVP)の範囲で書きます。WASIやlibcはありません |
| 浮動小数点 | RP2040では使えません(FPUがないため、wasm3を浮動小数点なしでビルドしています)。整数で計算します。H533では使えます |
| モジュール | 1つ32KBまで、RAMスロットは4つ |
| リニアメモリ | wasm3の上限は2ページ(128KB)ですが、デバイスのヒープから連続して確保するので、1ページ(64KB)に固定してビルドします。`memory.grow`では増やせません。RAMの都合で、4つのスロットすべてを同時に動かせるとは限りません |
| wasm3のスタック | `memory_kb`(既定16KB)。関数呼び出しの入れ子が深いと、4KBでは`out of bounds memory access`でtrapします |
| 速度 | インタプリタなので、ネイティブのCよりかなり遅くなります。`collatz_bench`(1〜1000)はRP2040で約1秒かかります。ホスト関数の呼び出しにも手間がかかるので、`gpio_write`を細かく何万回も呼ぶような使い方には向きません。制御やつなぎの処理向けです |
| 入出力 | ホスト関数だけです。I2C、SPI、USB HIDの送信などはwasmから直接使えません(H533のキー処理を除く) |
| 未定義のimport | 汎用モジュールは、用意していない関数を呼んだ時点でtrapします。H533のキー処理モジュールは取り付けの時点で拒否します |
| 既知の不具合 | RP2040で、ホスト関数を50回ほど呼んだ後に`mqtt_pub`を呼ぶとハードフォールトすることがあります。`mqtt_pub`は実行を始めてすぐに使うか、`log_printf`で代用してください。`sensor_dashboard.c`はこのためMQTTを使っていません |
| 実機での確認 | Pico W(WiFi)は実機で確認済みです。W5100S-EVB-Picoは実機での再確認がまだです。NUCLEO-H533REはビルドとPC上の検証までで、実機では確認していません |

## セキュリティ

- wasmの注入は、デバイス上で任意の処理を動かすのと同じです。APIキーを必ず設定し([mcp.md](mcp.md))、公開ブローカを使うときも第三者に実行させないでください。
- wasmにはメモリを任意に読み書きする関数を用意していません。カーネルのメモリには触れませんが、`gpio_write`でキーボードや無線チップの信号線を操作すると、機器は正しく動かなくなります。
- `wasm_begin`にSHA-256を渡すと、転送中に壊れたり差し替えられたりしたバイナリを、実行する前に弾けます。`wasm_upload.py`は常に渡しています。
