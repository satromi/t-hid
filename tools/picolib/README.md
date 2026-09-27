# picolib

t-hidのwasm3で動かすWebAssemblyモジュールを、Cで書くためのヘッダとサンプルです。使い方、ホスト関数、制限は[docs/guide/wasm.md](../../docs/guide/wasm.md)にまとめています。

## 必要環境

wasm32ターゲット付きのclang(16以降)とwasm-ld、make、`wc`、`awk`が必要です。WindowsではGit Bashなどから実行してください。

| OS | インストール |
|---|---|
| Windows | `winget install LLVM.LLVM` |
| Ubuntu / Debian | `sudo apt install clang lld` |
| macOS | `brew install llvm`(Apple製のclangはwasm32に対応していません) |

## ビルド

```bash
cd tools/picolib
make verify     # clangのバージョンとwasm32のターゲット名を表示
make            # examples/*.c と kb/*.c をすべて .wasm にする
make kb         # kb/*.c だけ
make clean
```

別のclangを使うときは`make CLANG=/path/to/clang`と指定します。`examples/`や`kb/`に`.c`を置けば、同じオプションでビルドされます。

デバイスへの送信には`../wasm_upload.py`(Python 3、paho-mqtt 2.0以降)を使います。

```bash
python ../wasm_upload.py examples/blink.wasm --slot 0 --run
```

## ファイル

| ファイル | 内容 |
|---|---|
| `picolib.h` | 汎用モジュール用のホスト関数の宣言と`LOG()`マクロ |
| `kb/kb_brain_api.h` | NUCLEO-H533REのキー処理モジュール用のAPIとキーコード |
| `Makefile` | wasm32向けのビルドルール |

## サンプル

`examples/`は汎用モジュールで、`wasm_run`で実行します。

| ファイル | 内容 |
|---|---|
| `blink.c` | GP22を200ms間隔で10回点滅 |
| `morse.c` | GP22でSOSをモールス信号で点滅し、MQTTにも通知 |
| `collatz_bench.c` | コラッツ予想を1〜1000で計算し、結果と所要時間をMQTTに送る |
| `sensor_dashboard.c` | ADCの全チャネルと温度センサを500msごとに10回ログに出す |
| `temp_alert.c` | 内蔵温度センサを1秒ごとに30回読み、30.0℃を超えたらMQTTに通知 |

GP22はPico Wの左手のキーボードではキーマトリクスの行です。キーボードとして使っている基板では空いているピンに書き換えてください。MQTTの通知先は`mcp/kb-000001/...`になっているので、自分のデバイスIDに書き換えてからビルドします。

`kb/`はNUCLEO-H533REのキー処理モジュールで、`kb_wasm_attach`で取り付けます。

| ファイル | 内容 |
|---|---|
| `kb_minimal.c` | ベースレイヤーをそのまま送る。自作するときの出発点 |
| `kb_default.c` | 組み込みと同じMO/TGレイヤー、Ctrl+Shiftキー、JとKの同時押しでEsc |
| `kb_macro.c` | Insertキーで「hello」とEnterを入力 |
| `kb_taphold.c` | Spaceを短く押すとSpace、200ms以上押し続けるとShift |

キー処理モジュールは、送る前に`../kb_wasm_test`で`make check MOD=...`を実行して確かめられます。
