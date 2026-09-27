# t-hid:μT-Kernel 3.0で動くキーボードファームウェア

t-hidは、組み込み向けリアルタイムOSのμT-Kernel 3.0で動くキーボードファームウェアです。

Raspberry Pi Pico(RP2040)で作る分割キーボードを中心に、Pico WのBLE、有線LANとWiFiのネットワーク、AIから操作するためのMCPサーバ、WebAssemblyによる機能の追加、NUCLEO-H533REを使ったセキュリティキー(FIDO U2F)までを1つのリポジトリで扱います。

- 対象:Raspberry Pi Pico / Pico W、W5100S-EVB-Pico、Arducam Pico4ML、NUCLEO-H533RE
- OS:μT-Kernel 3.0(RP2040はμT-Kernel 3.0 BSP、STM32H533はBSP 2.0)
- 開発環境:GNU Arm Embedded Toolchain、make、bash、Python 3

## できること

- キーボード:左右2枚の基板をI2Cでつなぐ分割キーボードとして、USB HIDで動きます。キーマップはビルド時に選び、押している間だけ切り替わるレイヤー(`MO`)と押すたびに切り替わるレイヤー(`TG`)が使えます。片手だけで動かすビルドや、押したキーの座標を入力する配線確認用のビルドもあります。[keyboard.md](docs/guide/keyboard.md)
- BLE:Pico WでPCとBLE HIDでつなぐほか、左右をBLEでつなぐ無線分割や、起動時にI2Cの相手が見つからなければBLEに切り替えるハイブリッド構成にできます。[ble.md](docs/guide/ble.md)
- ネットワーク:WIZnet W5100S / W5500の有線LANと、Pico WのWiFiに対応します。DHCP、DNS、SNTP、MQTT、HTTPサーバが使えます。[network.md](docs/guide/network.md)
- MCPサーバ:ClaudeなどのAIが、MQTT経由でキー入力の代行、GPIO/ADC/PWMの操作、RTOSのタスクの確認、Flashの読み書きなどのツールを呼び出せます。[mcp.md](docs/guide/mcp.md)
- WebAssembly:PCでCからコンパイルした`.wasm`をMCP経由で送り込み、ファームウェアを書き換えずに機能を追加します。NUCLEO-H533RE構成では、キー処理そのものをwasmで差し替えられます。[wasm.md](docs/guide/wasm.md)
- セキュリティキー(FIDO U2F):NUCLEO-H533REを、Webサイトの2段階認証に使えるUSBのセキュリティキーにします。キーボードと同じUSBに同居でき、B1ボタンで本人の操作を確認します。TrustZoneで鍵と暗号処理をセキュア側に分け、TZ-Closedにしてデバッガからも鍵を読めないようにできます。[u2f.md](docs/guide/u2f.md)
- ゲームパッド:キーボードをUSB HIDのゲームコントローラとしても使えるようにします。キーマップに置いたキーで、ボタン、十字キー、スティックを操作します。[gamepad.md](docs/guide/gamepad.md)
- 離席検出:Pico4MLのカメラとTensorFlow Lite Microで人の有無を判定し、席を離れたらPCの画面をロックします。[presence.md](docs/guide/presence.md)

## ドキュメント

| ファイル | 内容 |
| --- | --- |
| [docs/build_and_deploy.md](docs/build_and_deploy.md) | 必要なツール、セットアップ、全ビルドターゲットとオプション、書き込み、ログの確認 |
| [docs/guide/keyboard.md](docs/guide/keyboard.md) | キーボード。分割、キーマップ、レイヤー、ピン割当、USB HID、NUCLEO-H533REのキーボードブレイン |
| [docs/guide/ble.md](docs/guide/ble.md) | Pico WのBLE。BLE HIDキーボード、無線分割、ハイブリッド |
| [docs/guide/network.md](docs/guide/network.md) | TCP/IP。W5100S / W5500 / Pico WのWiFi、ソケットAPI、DHCP、DNS、SNTP、MQTT、HTTP |
| [docs/guide/mcp.md](docs/guide/mcp.md) | MCPサーバ |
| [docs/guide/wasm.md](docs/guide/wasm.md) | WebAssembly。NUCLEO-H533REのキー処理wasmを含む |
| [docs/guide/u2f.md](docs/guide/u2f.md) | セキュリティキー(FIDO U2F)、TrustZone、TZ-Closed |
| [docs/guide/gamepad.md](docs/guide/gamepad.md) | ゲームパッド |
| [docs/guide/presence.md](docs/guide/presence.md) | Pico4MLの離席検出 |

## 構成とハードウェア

用途に応じて、次のどれかの構成で使います。ビルドの欄はmakeのターゲットかオプションです。

| 構成 | ボード | PCとの接続 | 左右の接続 | ビルド |
| --- | --- | --- | --- | --- |
| 有線分割(標準) | Raspberry Pi Pico × 2 | USB | I2C | `split` |
| 片手のみ | Pico × 1 | USB | なし | `half` |
| 無線分割 | Pico W × 2 | BLE | BLE | `split_ble` |
| ハイブリッド | Pico W(左手) + PicoまたはPico W(右手) | USB / BLE | I2C、無ければBLE | `split_auto` |
| Pico W + WiFi | Pico W | USB / BLE | なし | `TARGET=_PICO_W_ WIFI=1` |
| 有線LAN | W5100S-EVB-Pico | USB | なし | `NET=1` / `MCP=1` / `WASM=1` |
| 離席検出 | Arducam Pico4ML | USB | なし | `presence` |
| キーボードブレイン | NUCLEO-H533RE + Pico × 2(+ WIZ550io) | USB | I2C | `build_bsp2`の`make`と`brain_slaves` |
| セキュリティキー | NUCLEO-H533RE | USB | なし | `build_bsp2`の`make` |

標準のピン割当とキーマップは、TL Split(mintlsplit互換、72キー)向けです。片手10行×4列のマトリクスで、ダイオードの向きはCOL2ROW、左右はPicoのGP8(SDA)とGP9(SCL)のI2Cでつなぎます。ピン割当は[keyboard.md](docs/guide/keyboard.md)にあります。

## 全体の仕組み

```text
┌──────────────────────────────────────────────────────────────────┐
│ アプリケーション                                                 │
│   キーボード(app_program/keyboard)   ネットワーク/MCP/wasm (任意)│
├──────────────────────────────────────────────────────────────────┤
│ キーボードフレームワーク                                         │
│   スキャン(2ms周期)、デバウンス、分割の受信、レイヤー解決、HID   │
├──────────────────────────────────────────────────────────────────┤
│ デバイスドライバ(μT-Kernelのデバイス管理 / mSDI)                 │
│   USB HID  I2Cマスタ/スレーブ  BLE HID  W5100S/W5500  WiFi       │
├──────────────────────────────────────────────────────────────────┤
│ μT-Kernel 3.0 (タスク、イベントフラグ、周期ハンドラ ...)         │
├──────────────────────────────────────────────────────────────────┤
│ BSP: RP2040 (mtk3_bsp) / STM32H533 (mtk3_bsp2)                   │
└──────────────────────────────────────────────────────────────────┘
```

キースキャンは、RTOSの周期ハンドラで2msごとに起動します。待ち時間はCPUをほかのタスクに譲るので、ネットワークの処理と同居してもスキャンの周期が乱れにくくなっています。

割り込みハンドラはフラグを立てるだけにとどめ、実際の処理はタスクで行うのが基本です(ネットワーク、I2C、BLEなど)。

分割の左右は、スレーブ側が用意したマトリクスの状態(10行分とCRC8)をマスター側が読み取ります。通信エラーが続くと、その手のキーをすべて離した扱いにしてから再接続を待ちます。

## はじめかた

有線分割キーボード(Pico × 2)を作る最小の手順です。ツールの準備、ほかの構成のビルド、NUCLEO-H533REの書き込みは[build_and_deploy.md](docs/build_and_deploy.md)を参照してください。

```bash
git clone --recursive https://github.com/satromi/t-hid.git
cd t-hid
bash setup.sh
cd mtk3_bsp/build_make
make TARGET=_PICO_RP2040_ KEYMAP=default split
```

ビルドが終わると`mtkernel_3_master_default.uf2`(左手)と`mtkernel_3_slave_default.uf2`(右手)ができます。PicoのBOOTSELボタンを押しながらUSBでつなぐと`RPI-RP2`ドライブが現れるので、それぞれの`.uf2`をコピーします。

`setup.sh`は、リポジトリのソースをサブモジュール`mtk3_bsp/`にコピーしてビルドできるツリーを組み立てます。ソースを編集したら、ビルドの前に毎回`setup.sh`を実行してください。

## ディレクトリ構成

```text
t-hid/
├── app_program/          RP2040用アプリ(キーボード + 任意でネットワーク)
│   └── keyboard/         キーボードフレームワーク、キーマップ
├── app_kb_program/       Pico W: BLEキーボード
├── app_wifi_program/     Pico W: WiFi + BLEキーボード
├── app_presence/         Pico4ML: 離席検出
├── app_h533/             NUCLEO-H533RE: キーボードブレイン、U2F
├── tz_h533/              NUCLEO-H533RE: TrustZone構成のセキュア側と差し替えるカーネル
├── device/               デバイスドライバ
│   ├── usb_hid/          USB HID (RP2040 / STM32H7 / STM32H5)
│   ├── i2c_slave/        I2Cスレーブ(分割の従側)
│   ├── i2c_master/       I2Cマスタ(STM32H5)
│   ├── ble_hid/          BLE HID (CYW43439 + BTstack)
│   ├── wifi/             Pico WのWiFi (CYW43 + lwIP)
│   ├── wiznet/           W5100S / W5500、プロトコル、MCP、wasm
│   └── flash/            内蔵Flash (STM32H5)
├── build_make/           RP2040 / Pico Wのビルド設定
├── build_bsp2/           NUCLEO-H533REのビルド
├── bsp_overlay/          mtk3_bspに上書きするファイル(RP2040、NUCLEO-H723向けの変更)
├── bsp2_overlay/         mtk3_bsp2に上書きするファイル
├── patches/              BTstackへのパッチ
├── kernel/extension/     カーネル拡張(日時)
├── lib/                  ライブラリ(btstack、cyw43-driver、wasm3、cJSON、micro-ecc、pico-tflmicroなどのサブモジュール)
├── tools/                PC側のツール(MCPブリッジ、wasmのアップロード、検証プログラム、デバッグ認証)
├── docs/                 ドキュメント(機能別はdocs/guide/)
├── mtk3_bsp/             サブモジュール: μT-Kernel 3.0 BSP (RP2040)
├── mtk3_bsp2/            サブモジュール: μT-Kernel 3.0 BSP 2.0 (STM32ほか)
└── setup.sh              ビルドツリーの組み立て
```

## 制限と注意

- MCPとwasmは、既定で公開MQTTブローカー(`test.mosquitto.org`)を使います。インターネット上の誰でも同じトピックを読み書きできるので、MCPを使うときは必ずAPIキーを設定してください([mcp.md](docs/guide/mcp.md))。
- USBのVID `0xCAFE`(PID `0x4004`)は開発用の仮の値で、どの団体にも割り当てられていません。ファームウェアを配布する場合は、取得したVID/PIDに置き換えてください。
- FIDO U2Fは、FIDOの認定を受けていない実験的な実装です。重要なアカウントには使わないでください。
- W5100S-EVB-PicoはGP16〜21をW5100Sに使い、TL Splitのマトリクスのピンと重なります。キーボード基板では`NET=1`にしないでください。
- NUCLEO-H533REのキーボードブレインとW5500(WIZ550io)は、ビルドとPC上での検証までで、実機では確認していません。
- W5100S-EVB-Pico上のwasmと、ゲームパッドの実機のUSB接続での動作は、まだ確認していません。

## ライセンス

t-hid本体は[MIT License](LICENSE)です。ただし、μT-Kernel由来のファイル(T-License)とpico-sdk由来のファイル(BSD-3-Clause)は、それぞれのライセンスに従います。対象ファイルの一覧と第三者のライセンスは[NOTICE.md](NOTICE.md)にまとめています。

| 対象 | ライセンス |
| --- | --- |
| t-hid本体 | MIT |
| μT-Kernel 3.0 BSP(`mtk3_bsp`, `mtk3_bsp2`)と、その変更ファイル(`bsp_overlay/`, `bsp2_overlay/`) | T-License(ファイルごとに記載) |
| Pico WのCYW43439制御コード(`device/ble_hid/`の一部) | BSD-3-Clause(Raspberry Pi) |
| wasm3(`lib/wasm3`)、cJSON(`lib/cjson`)、WIZnet ioLibrary(`lib/ioLibrary`) | MIT |
| micro-ecc(`lib/micro-ecc`) | BSD-2-Clause |
| BTstack(`lib/btstack`) | 独自ライセンス。非商用の個人利用のみ |
| cyw43-driver(`lib/cyw43-driver`) | 独自ライセンス。非商用のみ、またはRaspberry Pi製チップでの利用に限る |
| TensorFlow Lite Micro(`lib/pico-tflmicro`) | Apache-2.0 |

Pico WのBLE機能はBTstackを使うため、商用製品には使えません。cyw43-driverは、Raspberry Pi製チップ(Pico W)の上であれば利用できます。RP2040の有線キーボード構成とNUCLEO-H533RE構成は、どちらもこの2つを含みません。Pico WのWiFi構成が使うlwIPは、BTstackに同梱されたもの(`lib/btstack/3rd-party/lwip`、BSD-3-Clause)です。
