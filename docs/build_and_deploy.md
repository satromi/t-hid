# ビルドと書き込み

t-hidのファームウェアは、Raspberry Pi Pico / Pico W(RP2040)向けとNUCLEO-H533RE(STM32H533)向けの2系統があります。RP2040向けはサブモジュール`mtk3_bsp/`の中の`build_make/`で、NUCLEO-H533RE向けはリポジトリ直下の`build_bsp2/`でビルドします。

## 必要なもの

| 用途 | ツール |
| --- | --- |
| ビルド | GNU Arm Embedded Toolchain(`arm-none-eabi-gcc`)、make、bash、Python 3 |
| RP2040への書き込み | 追加のツールは不要です。UF2ファイルをコピーするだけで書き込めます |
| NUCLEO-H533REへの書き込み | STM32CubeProgrammer(`STM32_Programmer_CLI`)。無くてもST-LINKのドライブへのコピーで書き込めます |
| wasmを使う場合 | wasm32ターゲット付きのclang 16以降と`wasm-ld`、`pip install paho-mqtt` |

ビルドの途中でPythonのスクリプトを`python`という名前で呼び出すため、Python 3が`python`で起動できるようにしておいてください。Linuxで`python3`しか無い場合は、`python-is-python3`などを入れます。

開発はWindows上のGit Bashで、xPack版のarm-none-eabi-gcc 15.2とmake(windows-build-tools)を使って行っています。ツールチェーンは次のようにPATHに加えておきます。

```bash
export PATH="/path/to/arm-none-eabi-gcc/bin:/path/to/make/bin:$PATH"
```

## 取得とセットアップ

```bash
git clone --recursive https://github.com/satromi/t-hid.git
cd t-hid
bash setup.sh
```

`--recursive`を付け忘れた場合も、`setup.sh`がサブモジュールを取得します。

`setup.sh`は次のことを行って、ビルドできるツリーを組み立てます。

- `bsp_overlay/`をサブモジュール`mtk3_bsp/`(μT-Kernel 3.0 BSP)に上書きする
- `app_program/`、`app_kb_program/`、`app_wifi_program/`、`app_presence/`、`device/`、`build_make/`、`kernel/extension/`、ライブラリのサブモジュールを`mtk3_bsp/`の中へコピーする
- リポジトリ直下の`.wifi_config`と`.mcp_api_key`を`mtk3_bsp/`へコピーする
- `patches/`のパッチをBTstack(`lib/btstack`)に当てる
- `bsp2_overlay/`をサブモジュール`mtk3_bsp2/`(μT-Kernel 3.0 BSP 2.0)に上書きする

RP2040向けのビルドは、このコピーされた側のソースを使います。ソースを編集したら、ビルドの前に毎回`setup.sh`を実行してください。`mtk3_bsp/`の中のファイルを直接編集しても、次の`setup.sh`で上書きされます。NUCLEO-H533RE向けのビルドはリポジトリのソースを直接読みますが、`mtk3_bsp2/`には`bsp2_overlay/`が当たっている必要があるので、同じく`setup.sh`を実行してからビルドします。

`setup.sh`を実行すると、サブモジュール`mtk3_bsp/`、`mtk3_bsp2/`、`lib/btstack`に変更やファイルの追加が出ますが、そのままで問題ありません。また、ビルドの途中で`setup.sh`を実行すると、使用中のファイルを消してしまいビルドが失敗します。ビルドが終わってから実行してください。

## 秘密情報のファイル

次のファイルをリポジトリ直下に置くと、ビルド時にファームウェアへ埋め込まれます。どちらも`.gitignore`に入っているので、コミットされません。無くてもビルドはできます。

| ファイル | 内容 | 使う構成 |
| --- | --- | --- |
| `.wifi_config` | Pico WのWiFiのSSIDとパスワード。`.wifi_config.example`をコピーして書き換えます | Pico W(`WIFI=1`)。詳しくは[network.md](guide/network.md) |
| `.mcp_api_key` | MCPのAPIキー(64桁の16進) | MCPを使う構成。詳しくは[mcp.md](guide/mcp.md) |

`.wifi_config`の書式は次のとおりです。

```text
ssid=MyNetwork
psk=my-password
auth=wpa2
```

`auth`には`open`、`wpa2`、`wpa3`を指定できます。省略するとwpa2です。

APIキーは次のようにして作れます。

```bash
python -c "import secrets; print(secrets.token_hex(32))" > .mcp_api_key
```

## RP2040 / Pico Wのビルド

`setup.sh`の実行後、`mtk3_bsp/build_make`でmakeします。生成ファイルもこのディレクトリにできます。

```bash
cd mtk3_bsp/build_make
make TARGET=_PICO_RP2040_ KEYMAP=default split
#   → mtkernel_3_master_default.uf2 (左手、USBをつなぐ側)
#   → mtkernel_3_slave_default.uf2  (右手)
```

ELFからUF2への変換には、既定で`build_make`に入っているPythonスクリプト(`python elf2uf2.py`)を使います。pico-sdkの`elf2uf2`など別のツールを使う場合は`ELF2UF2`で指定します。

```bash
make TARGET=_PICO_RP2040_ ELF2UF2=/path/to/elf2uf2 KEYMAP=default split
```

キーマップは`KEYMAP=<名前>`で選びます。名前は`app_program/keyboard/keymaps/`の下のディレクトリ名で、`default`(QWERTY)、`dvorak`、`matrixtest`(配線調査用)、`gamepad`(ゲームパッド用のレイヤー入り)があります。省略すると`default`です。キーマップの書き方は[keyboard.md](guide/keyboard.md)を参照してください。

### ビルドターゲット

`<keymap>`は`KEYMAP`に指定した名前、`<hand>`は`HAND`に指定した`L`か`R`です。

| ターゲット | ボード | 生成ファイル | 内容 |
| --- | --- | --- | --- |
| `split` | Pico × 2 | `mtkernel_3_master_<keymap>.uf2`、`mtkernel_3_slave_<keymap>.uf2` | 有線分割(I2C)の左右 |
| `split_master` | Pico | `mtkernel_3_master_<keymap>.uf2` | 有線分割の左手だけ |
| `split_slave` | Pico | `mtkernel_3_slave_<keymap>.uf2` | 有線分割の右手だけ |
| `split_ble` | Pico W × 2 | `mtkernel_3_master_ble_<keymap>.uf2`、`mtkernel_3_slave_ble_<keymap>.uf2` | 左右をBLEでつなぐ無線分割 |
| `split_auto` | Pico W + Pico | `mtkernel_3_master_auto_<keymap>.uf2`、`mtkernel_3_slave_<keymap>.uf2` | ハイブリッド。左手(Pico W)は起動時にI2Cの相手を探し、見つからなければBLEで探す。右手は`split`のスレーブと同じもの |
| `split_auto_slave_ble` | Pico W | `mtkernel_3_slave_ble_<keymap>.uf2` | ハイブリッドの右手にPico Wを使う場合 |
| `half` | Pico | `mtkernel_3_half_<hand>_<keymap>.uf2` | 分割の片側だけをUSBにつなぎ、その手のキーマップどおりに入力する。相方の基板は要りません |
| `wiring_test` | Pico | `mtkernel_3_wiring_test_<hand>.uf2` | 押したセルの座標を`R03C2`の形式で文字入力する。配線の確認用 |
| `brain_slaves` | Pico × 2 | `mtkernel_3_brain_slave_L.uf2`(I2C 0x31)、`mtkernel_3_brain_slave_R.uf2`(I2C 0x32) | NUCLEO-H533REをキーボードブレインにする構成の左右 |
| `presence` | Pico4ML | `mtkernel_3_presence.uf2` | 離席検出([presence.md](guide/presence.md)) |
| `presence_kb` | Pico | `mtkernel_3_presence_kb.uf2` | キーボード(分割なし)に離席検出を同居させる |
| `all` | `TARGET`による | `mtkernel_3.uf2` | 単体のビルド。ネットワーク付きの構成などに使う |

`split_ble_master`、`split_ble_slave`、`split_auto_master`、`brain_slave_left`、`brain_slave_right`のように、片側だけを作るターゲットもあります。

`presence_kb`をPico4ML単体で試すときは、マトリクスのピンがカメラと重なるので`PRESENCE_KB_DEFS=-DKB_MATRIX_NONE`を付けます。

コマンドの例です。`all`を使うときは、先に前のビルドを`clean`しておきます(後述)。

```bash
# 有線分割
make TARGET=_PICO_RP2040_ KEYMAP=default split

# 無線分割 (Pico W × 2)
make TARGET=_PICO_W_ KEYMAP=default split_ble

# ハイブリッド (右手がPicoならsplit_auto、Pico Wならsplit_auto_slave_bleも作る)
make TARGET=_PICO_W_ KEYMAP=default split_auto
make TARGET=_PICO_W_ KEYMAP=default split_auto_slave_ble

# 片手だけで動かす / 配線の確認
make TARGET=_PICO_RP2040_ HAND=R KEYMAP=default half
make TARGET=_PICO_RP2040_ HAND=R wiring_test

# キーボードブレイン構成の左右 (NUCLEO-H533RE側は後述)
make TARGET=_PICO_RP2040_ KEYMAP=default brain_slaves

# ゲームパッドを加える
make TARGET=_PICO_RP2040_ KEYMAP=gamepad GAMEPAD=1 split

# Pico W: WiFi + MCP + wasm + BLEキーボード (先に.wifi_configを用意する)
make TARGET=_PICO_W_ WIFI=1 all

# Pico W: BLEキーボード (ネットワークなし)
make TARGET=_PICO_W_ KEYMAP=default all

# W5100S-EVB-Pico: 有線LAN + MCP + wasm
make TARGET=_PICO_RP2040_ WASM=1 all

# 離席検出 (Pico4ML)
make TARGET=_PICO_RP2040_ presence
```

BLEやハイブリッド構成の使い方は[ble.md](guide/ble.md)、ネットワーク構成は[network.md](guide/network.md)、MCPは[mcp.md](guide/mcp.md)、wasmは[wasm.md](guide/wasm.md)を参照してください。

### ビルドオプション

| 変数 | 値 | 内容 |
| --- | --- | --- |
| `TARGET` | `_PICO_RP2040_` / `_PICO_W_` | 対象のボード。省略すると`_PICO_RP2040_` |
| `KEYMAP` | `default`など | キーマップ。省略すると`default` |
| `HAND` | `L` / `R` | `half`と`wiring_test`で使う手。省略すると`L` |
| `GAMEPAD` | `0` / `1` | USBをキーボードとゲームパッドの複合デバイスにする(`TARGET=_PICO_RP2040_`のみ)。[gamepad.md](guide/gamepad.md) |
| `NET` | `0` / `1` | W5100Sの有線LANとネットワークスタックを入れる(`TARGET=_PICO_RP2040_`)。[network.md](guide/network.md) |
| `MCP` | `0` / `1` | MCPサーバを入れる。`NET=1`を含みます。[mcp.md](guide/mcp.md) |
| `WASM` | `0` / `1` | WebAssemblyのランタイムを入れる。`MCP=1`を含みます。[wasm.md](guide/wasm.md) |
| `WIFI` | `0` / `1` | Pico WでWiFi、MCP、wasmを使う(`TARGET=_PICO_W_`)。`0`ならBLEキーボードだけのビルド |
| `BLE` | `1` / `0` | `WIFI=1`のときにBLEキーボードも入れるか。省略すると`1` |
| `ELF2UF2` | コマンド | ELFからUF2への変換ツール |
| `EXTRA_DEFS` | `-D...` | 追加のコンパイル定義。Pico WのMQTTブローカーの指定などに使う |

`NET`、`MCP`、`WASM`の既定はすべて`0`で、キーボードだけの最小のイメージになります。W5100S-EVB-PicoはW5100SとのやりとりにGP16〜21を使い、TL Splitのマトリクスのピンと6本重なるため、キーボード基板では`NET=1`にしないでください。ネットワーク機能はW5100S-EVB-Pico単体で使う構成向けです。

RP2040のFlash(2MB)の使用量の目安は次のとおりです。

| 構成 | Flash使用量 |
| --- | --- |
| キーボードのみ | 約77KB |
| `NET=1` | 約229KB |
| `MCP=1` | 約269KB |
| `WASM=1` | 約369KB |

## RP2040 / Pico Wへの書き込み

1. BOOTSELボタンを押しながらPicoをUSBでPCにつなぎます(Pico4MLではBOOTボタン)。
2. `RPI-RP2`という名前のドライブが現れます。
3. `.uf2`ファイルをそのドライブにコピーすると、自動で再起動して新しいファームウェアが動きます。

```bash
cp mtkernel_3_master_default.uf2 /d/                        # Windows (ドライブ名は環境による)
cp mtkernel_3_master_default.uf2 /Volumes/RPI-RP2/          # macOS
cp mtkernel_3_master_default.uf2 /media/$USER/RPI-RP2/      # Linux
```

分割キーボードでは、左右に次のファイルを書き込みます。

| 構成 | 左手 | 右手 |
| --- | --- | --- |
| 有線分割(`split`) | `mtkernel_3_master_<keymap>.uf2` | `mtkernel_3_slave_<keymap>.uf2` |
| 無線分割(`split_ble`) | `mtkernel_3_master_ble_<keymap>.uf2` | `mtkernel_3_slave_ble_<keymap>.uf2` |
| ハイブリッド(`split_auto`) | `mtkernel_3_master_auto_<keymap>.uf2` | Picoなら`mtkernel_3_slave_<keymap>.uf2`、Pico Wなら`mtkernel_3_slave_ble_<keymap>.uf2` |
| キーボードブレイン(`brain_slaves`) | `mtkernel_3_brain_slave_L.uf2` | `mtkernel_3_brain_slave_R.uf2` |

キーボードブレイン以外の構成では、左手がマスター(PCとつながる側)です。左右とも、同じキーマップ、同じオプションでビルドしたものを書き込んでください。片方だけ古いファームウェアのままだと、キーの配置がずれることがあります。書き込んでから、キーボードとして認識されるまで数秒かかります。

## NUCLEO-H533REのビルドと書き込み

`setup.sh`の実行後、`build_bsp2`でmakeします。

```bash
cd build_bsp2
make clean && make          # キーボード + セキュリティキー + WIZ550io + MCP + wasm
make clean && make NET=0    # キーボード + セキュリティキー (ネットワークなし)
#   → build/mtk3_h533.elf、build/mtk3_h533.bin
```

| 変数 | 既定 | 内容 |
| --- | --- | --- |
| `NET` | `1` | W5500(WIZ550io)の有線LAN。`0`にするとMCPとwasmも外れます |
| `MCP` | `1` | MCPサーバ。`0`にするとwasmも外れます |
| `WASM` | `1` | WebAssembly(キー処理をwasmで差し替える) |
| `U2F` | `1` | セキュリティキー(FIDO U2F) |
| `TZ` | `0` | `1`でTrustZoneを使い、U2Fの鍵と暗号処理をセキュア側に分ける。`U2F=1`を含みます |
| `TZTEST` | `0` | `TZ=1`のとき、TrustZoneの自己テストを一緒に入れる |
| `KEYMAP` | `default` | キーマップ。RP2040と同じ`app_program/keyboard/keymaps/`から選ぶ |
| `GAMEPAD` | `0` | `1`でUSBにゲームパッドを加える |
| `STM32_PROGRAMMER` | `STM32_Programmer_CLI` | `make flash`で使うSTM32CubeProgrammerのCLI |

書き込みは次のどちらかで行います。どちらもNUCLEOのST-LINK側のUSBをPCにつなぎます。

- STM32CubeProgrammerがある場合は`make flash`を実行します。`STM32_Programmer_CLI`がPATHに無いときは、`make flash STM32_PROGRAMMER=/path/to/STM32_Programmer_CLI`のように指定します。
- 無い場合は、ST-LINK側をつなぐと現れるUSBドライブに`build/mtk3_h533.bin`をコピーします。

書き込んだら、USER USB側のケーブルもPCにつなぎます。キーボードとセキュリティキーは、こちらのUSBからPCに見えます。

`TZ=1`のビルドは、先にオプションバイトの設定が必要で、書き込むイメージも2つになります。手順は[u2f.md](guide/u2f.md)を参照してください。キーボードブレインとしての配線は[keyboard.md](guide/keyboard.md)、セキュリティキーとしての使い方は[u2f.md](guide/u2f.md)にあります。

## ログの確認

どのボードも、起動時のログや動作のログをシリアルに出します。通信速度は115200bpsです。

| ボード | シリアル |
| --- | --- |
| Pico / Pico W | UART0(GP0=TX、GP1=RX) |
| NUCLEO-H533RE | ST-LINKの仮想COMポート(USART2) |

PicoのUART0は3.3VのUSBシリアル変換ケーブルなどでPCにつなぎます。NUCLEO-H533REはST-LINK側のUSBをつなぐとCOMポートとして見えます。

## cleanについて

`split`や`half`など、名前付きのターゲットは内部で`clean`してからビルドするので、そのまま続けて実行できます。

`all`で単体のビルドをするときは、`TARGET`、オプション、キーマップを変えるたびに`clean`してください。オプションやキーマップを変えても、古いオブジェクトは作り直されずに残ります。`clean`は、指定した変数で選ばれる構成のオブジェクトだけを消すので、前のビルドと同じ`TARGET`とオプションを付けて実行します。

```bash
make TARGET=_PICO_W_ WIFI=1 clean
make TARGET=_PICO_RP2040_ MCP=1 clean
```

`.mcp_api_key`を作り直したときは、`setup.sh`を実行したうえで、`mtk3_bsp/build_make/mtkernel_3/device/wiznet/mcp_apikey_embedded.h`を消してからビルドします。このファイルは`clean`では消えません。

NUCLEO-H533REの`make clean`は`build/`をまるごと消します。こちらはヘッダの変更を追跡しないので、ヘッダを編集したときやオプションを変えたときは、`make clean`してからビルドしてください。
