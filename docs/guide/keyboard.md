# キーボード

t-hidのキーボード機能は、μT-Kernel 3.0のタスクと周期ハンドラの上に作った、QMKに似た構成の分割キーボードフレームワークです。左右2枚の基板をI2Cでつなぎ、片方(マスター)がUSB HIDキーボードとしてPCに接続します。キーマップはQMKと同じく`keymaps/<名前>/keymap.c`に書き、ビルド時に選びます。

ビルドと書き込みの手順、ターゲットの一覧は[build_and_deploy.md](../build_and_deploy.md)にまとめています。ここではキーボードとしての仕様、設定、仕組みを説明します。

## 対象のキーボード

標準のピン割当とキーマップは、TL Split(mintlsplit互換、72キー)向けです。

| 項目 | 内容 |
| --- | --- |
| マトリクス | 片手10行×4列。左右で20行×4列、そのうち72キーを使います |
| ダイオード方向 | COL2ROW(行をLowにして列を読みます) |
| 分割のI2C | GP8(SDA)/GP9(SCL)、400kHz。右手のスレーブアドレスは`0x32` |
| USB VID/PID | `0xCAFE`/`0x4004` |
| USBの製品名 | `TL Split using uT-Kernel`(メーカー名`satromi works`) |

VID `0xCAFE`は開発用の仮の値で、どの団体にも割り当てられていません。ファームウェアを配布する場合は、取得したVID/PIDに置き換えてください。VID/PID、製品名、メーカー名は`app_program/keyboard/kb_config.h`の`KB_VENDOR_ID`、`KB_PRODUCT_ID`、`KB_PRODUCT`、`KB_MANUFACTURER`で変えられます。同じファイルで、マトリクスの大きさ、ダイオード方向(`DIODE_DIRECTION`)、デバウンス時間、分割のI2Cアドレスも設定します。

## ピン割当

GPIOの割当は`app_program/keyboard/sysdepend/rp2040/kb_config_hw.h`で定義しています。右手の基板は左手と180度回転して実装するため、左右で割当が異なります。

| | 行(ROW) 0〜9 | 列(COL) 0〜3 |
| --- | --- | --- |
| 左手 | 22, 19, 17, 16, 27, 13, 12, 11, 28, 10 | 18, 20, 21, 26 |
| 右手 | 17, 2, 16, 13, 14, 4, 15, 11, 10, 6 | 12, 7, 5, 3 |

そのほかのピンは次のとおりです。

| GPIO | 用途 |
| --- | --- |
| GP0/GP1 | UART0(ログ出力、115200bps) |
| GP8/GP9 | 分割のI2C(SDA/SCL) |
| GP25 | オンボードLED(動作中に点滅) |

行ピンは、基板の配線順ではなく、`LAYOUT`マクロのマトリクス行の順に並べます。TL Splitの配線は左右とも「主要キー列を外側から内側へ7本、カーソル/矢印クラスタ、親指クラスタ、内側の2キー列」の順ですが、カーソル/矢印クラスタはマトリクス上では左手が行4、右手が行15に入ります。配線順のまま並べると、そこから先が1つずれて、キーが別の行の文字を出すようになります。また、右手はマトリクス行を内側から外側へ振っているので、主要列は配線順と逆に並びます。

別の基板に移植するときは、`wiring_test`ビルドで実際のセル座標を確かめてから表を書き換えるのが確実です(後述の「調査用のビルド」)。

キーボード基板では`NET=1`にしないでください。W5100S-EVB-PicoはSPI、CS、RST、INTにGP16〜21を使い、TL Splitのマトリクスと6本重なります。とくにGP21はW5100Sの割り込み入力として登録されるため、その列(左手のA S D F Gの段)がまったく反応しなくなります。ネットワーク機能はW5100S-EVB-Pico単体で使う構成向けです。

## 構成

| 構成 | ボード | PCとの接続 | 左右の接続 | ビルド |
| --- | --- | --- | --- | --- |
| 有線分割(標準) | Raspberry Pi Pico×2 | USB | I2C | `split` |
| 片手のみ | Pico×1 | USB | なし | `half` |
| 無線分割 | Pico W×2 | BLE | BLE | `split_ble` |
| ハイブリッド | Pico W(マスター)+Pico(スレーブ) | USB/BLE | I2C、なければBLE | `split_auto` |
| キーボードブレイン | NUCLEO-H533RE+Pico×2 | USB | I2C | `build_bsp2` |

Pico Wを使う構成(BLE HIDキーボード、無線分割、ハイブリッド)の詳細は[ble.md](ble.md)を参照してください。キーボードブレインはこの資料の後半で説明します。

有線分割の標準的なビルドは次のとおりです。

```bash
bash setup.sh
cd mtk3_bsp/build_make
make TARGET=_PICO_RP2040_ KEYMAP=default split
#   → mtkernel_3_master_default.uf2  (左手、USB側)
#   → mtkernel_3_slave_default.uf2   (右手)
```

## 分割の仕組み

左手がマスター、右手がスレーブです。スレーブはI2Cスレーブとして64バイトのレジスタ領域を持ち、その先頭に次の共有メモリを置きます。

| オフセット | 内容 |
| --- | --- |
| 0 | マトリクスのCRC8(多項式0x31、初期値0xFF) |
| 1〜10 | 右手のマトリクス(1行1バイト、ビットが列) |
| 11 | LEDの状態(予約。今は使っていません) |
| 12 | 更新のたびに増えるカウンタ |

スレーブは自分のマトリクスをスキャンしてデバウンスし、その結果とCRC8を共有メモリに書きます。スキャンはマスターのI2Cのアクセスが終わるたびに行い、アクセスがなくても10msごとに行います。

マスターは自分のスキャンのたびに、I2Cでスレーブの共有メモリからCRC8と10行分を読み、CRCを計算し直して照合します。読めなかった、またはCRCが合わなかったスキャンでは、右手のキーをすべて離した状態として扱います。マスター側でもデバウンスを通すので、一瞬の失敗でキーが離れることはありません。

失敗が10回続くと切断とみなし、ログに`Split: disconnected`を出します。切断中は500msごとに読み直しを試み、読めたら`Split: reconnected`を出して自動で復帰します。切断中も左手のスキャンは2ms周期のまま続くので、左手だけで入力できます。

## スキャンとデバウンス

キースキャンは、μT-Kernelの周期ハンドラで2msごとにスキャン用のタスクを起こして行います。タスクはイベントフラグで待つので、待ち時間はCPUを他のタスクに譲ります。ネットワークなど他の処理と同居しても、スキャンの周期が乱れにくい作りです。

行を1本ずつLowにし、約800ns待ってから列を読みます。列は内蔵プルアップ付きの入力です。

デバウンスはキーごとのカウンタ方式です。読んだ状態が確定済みの状態と違うスキャンが続けて`DEBOUNCE_MS`÷スキャン周期(切り上げ)回に達すると、状態を確定します。既定の`DEBOUNCE_MS`は5なので、3スキャン(約6ms)続いたら押した、離したと判定します。途中で元の状態に戻ると数え直します。

## キーマップ

キーマップは`app_program/keyboard/keymaps/<名前>/keymap.c`に置き、ビルド時に`KEYMAP=<名前>`で選びます。今あるキーマップは次のとおりです。

| 名前 | 内容 |
| --- | --- |
| `default` | QWERTY(JIS配列のPC向け)。FNキーを押している間はF1〜F10、FN+QでBLEのペアリング(Pico W) |
| `dvorak` | Dvorak。FNキーは押すたびに切り替わる(`KC_TG(1)`) |
| `gamepad` | `default`にゲームパッドのレイヤーを足したもの。FN+Gで切り替え([gamepad.md](gamepad.md)) |
| `matrixtest` | 調査用。全セルに別々の1文字を割り当てる |

新しく作るときは`keymaps/default`をコピーして編集し、`KEYMAP=<名前>`でビルドします。

```c
#include "kb_keymap.h"

#define ___ KC_TRNS

const keycode_t keymaps[NUM_LAYERS][MATRIX_ROWS][MATRIX_COLS] = {
    [LAYER_BASE] = LAYOUT(
        /* 物理配置どおりにキーを並べる */
        KC_RBRC, KC_1, KC_2, KC_END, ...
    ),
    [LAYER_FN]  = LAYOUT( ... ),
    [LAYER_NUM] = LAYOUT( ... ),
};
```

`LAYOUT`マクロ(`app_program/keyboard/kb_keymap.h`)は、72キーの物理配置の順に並べたキーを20行×4列のマトリクスへ並べ替えます。物理配置とマトリクスの対応は`kb_keymap.h`のコメントにあります。キーがないセルには`KC_NO`を置きます。

カーソル/矢印クラスタの4キーは、ほかの列と列線の順が違います。`default`の`LAYOUT`でも、左手はEND、PGDN、HOME、PGUP、右手は左、下、右、上の順に書いています。

レイヤーは`LAYER_BASE`(0)、`LAYER_FN`(1)、`LAYER_NUM`(2)の3枚です。押されたキーは、有効なレイヤーのうち番号の大きいものから順に探し、`KC_TRNS`でない最初のキーを使います。ベースのレイヤーは常に有効です。

| 書き方 | 意味 |
| --- | --- |
| `KC_A`など | 通常のキー。HIDのキーコードで、一覧は`app_program/keyboard/kb_hid_keycodes.h` |
| `KC_LCTL`〜`KC_RGUI` | 修飾キー |
| `KC_NO` | 何もしない |
| `KC_TRNS`(`___`) | 透過。下のレイヤーのキーを使う |
| `KC_MO(n)` | 押している間だけレイヤーnを有効にする |
| `KC_TG(n)` | 押すたびにレイヤーnの有効と無効を切り替える |
| `XMK_BLE_PAIR` | 3秒押し続けるとBLEのペアリングを始める(Pico W) |
| `JS_A`など | ゲームパッドのボタン、十字キー、スティック([gamepad.md](gamepad.md)) |

`kb_hid_keycodes.h`には、英数字、記号、F1〜F12、カーソル移動、テンキーのほか、日本語キーボード用の`KC_JYEN`(¥)、`KC_RO`(ろ)、`KC_KANA`(カタカナ/ひらがな)、`KC_HENK`(変換)、`KC_MHEN`(無変換)、`KC_ZKHK`(半角/全角)があります。`KC_ENTER`や`KC_LSHIFT`など、QMKと同じ別名も一部使えます。

同時に送れる通常のキーは6個までです。7個目以降は送りません。

## USB HID

マスターは、USBのBoot Protocolに対応したHIDキーボードとしてPCにつながります。

| 項目 | 内容 |
| --- | --- |
| レポート | 8バイト(修飾キー1バイト、予約1バイト、キーコード6個)。6キーロールオーバー |
| エンドポイント | EP1 IN、インタラプト転送、10ms間隔 |
| LED | ホストから届くNum Lock、Caps LockなどのLEDの状態(SET_REPORT)を受け付けます。今は表示には使っていません |
| リモートウェイクアップ | NUCLEO-H533REでは、PCがサスペンド中にキーを押すとPCを起こします |

レポートはキーの状態が変わったときだけ送ります。

USBデバイスドライバは`device/usb_hid/`にあり、RP2040、STM32H5(NUCLEO-H533RE)、STM32H7向けの実装があります。割り込みハンドラではハードウェアの状態のクリアとUSB用メモリからのデータのコピーだけを行い、SETUPパケットの解析やディスクリプタの応答などのプロトコル処理はUSB処理用のタスク(優先度5、キースキャンのタスクより高い)で行います。

アプリからは、μT-Kernelのデバイス`usbk`として使います。

```c
#include "dev_usb_hid.h"

ID dd = tk_opn_dev((UB *)USB_HID_DEVNM, TD_UPDATE);   /* "usbk" */

T_USB_HID_KBD_REPORT r;
SZ asize;
memset(&r, 0, sizeof(r));
r.keycode[0] = 0x04;                                    /* A */
tk_swri_dev(dd, USB_HID_DN_KBD, &r, sizeof(r), &asize); /* データ番号 0 = キーボード */
```

| API | 内容 |
| --- | --- |
| `tk_swri_dev(dd, USB_HID_DN_KBD, ...)` | キーボードのレポートを送ります。前のレポートがまだホストに読まれていなければ`E_BUSY`、ホストに認識される前なら`E_IO`を返します |
| `tk_swri_dev(dd, USB_HID_DN_PAD, ...)` | ゲームパッドのレポートを送ります(`GAMEPAD=1`のとき、[gamepad.md](gamepad.md)) |
| `usb_hid_get_cfg_flgid()` | ホストがデバイスを認識した(SET_CONFIGURATIONを受けた)ときに`USB_HID_EVT_CONFIGURED`が立つイベントフラグのIDを返します |

公開APIの定義は`device/include/dev_usb_hid.h`です。デバイスの初期化はBSPのデバイス初期化処理から`dev_init_usb_hid()`で行うので、アプリで呼ぶ必要はありません。

## キーボードブレイン(NUCLEO-H533RE)

TL Splitの左右のPicoをマトリクスの読み取り専用のI2Cスレーブにし、キー処理とUSB HIDの出力をNUCLEO-H533RE(STM32H533)が受け持つ構成です。キー処理は組み込みのキーマップで動くほか、MCP経由でWebAssemblyのモジュールを取り付けて、ファームウェアを書き換えずに差し替えることもできます。同じUSBに物理セキュリティキー(FIDO U2F、[u2f.md](u2f.md))を同居させられます。

```
[左手Pico]──┐ I2C 0x31
             ├── I2C1 (400kHz) ──[NUCLEO-H533RE]── USER USB ──[PC]
[右手Pico]──┘ I2C 0x32                │
                                      ├── WIZ550io (SPI1) ── LAN (任意)
                                      └── ST-LINK仮想COM ── コンソール
```

| 基板 | ファームウェア | 役割 |
| --- | --- | --- |
| 左手Pico | `mtkernel_3_brain_slave_L.uf2` | マトリクスのスキャンとデバウンス。I2Cスレーブ`0x31` |
| 右手Pico | `mtkernel_3_brain_slave_R.uf2` | 同上。I2Cスレーブ`0x32` |
| NUCLEO-H533RE | `build_bsp2/build/mtk3_h533.bin` | 2msごとに左右を読み、キー処理をしてUSB HIDで送る |

H533は、各Picoの共有メモリ(CRC8と10行分)を1回のI2C転送で読みます。デバウンスは各Picoで済ませているので、H533では行いません。CRCが合わない、または応答がないときは前回の状態をそのまま使い、10回続けて失敗したらその手のキーをすべて離します。

### 配線

| 信号 | NUCLEO-H533RE | 左手Pico | 右手Pico |
| --- | --- | --- | --- |
| SCL | D15(PB8) | GP9 | GP9 |
| SDA | D14(PB9) | GP8 | GP8 |
| GND | GND | GND | GND |
| 電源 | 5V | VSYS | VSYS |

- SCLとSDAは、それぞれ4.7kΩで3.3Vにプルアップします。H533とPicoの内蔵プルアップは弱く、3台をつないでも400kHzには足りません。
- 左右をつなぐI2Cのケーブルに、H533を分岐で加える形になります。
- PCとはNUCLEOのUSER USB(Type-C)でつなぎます。ST-LINK側(CN1)は書き込みとコンソール用です。
- 出荷時の設定では、ボードの5VはST-LINK側から取るので、USER USBだけでは電源が入りません。USER USBだけで動かす方法(JP5の切り替え)は[u2f.md](u2f.md)を参照してください。
- NUCLEOのユーザーLED(LD2、PA5)はW5500のSPIと共用なので、キーボードの動作表示には使いません。
- WIZ550ioの配線は[network.md](network.md)を参照してください。ネットワークを使わないなら不要です(`make NET=0`)。

### ビルド

```bash
bash setup.sh

# 左右のPico
cd mtk3_bsp/build_make
make TARGET=_PICO_RP2040_ brain_slaves
#   → mtkernel_3_brain_slave_L.uf2 (左手, 0x31)
#   → mtkernel_3_brain_slave_R.uf2 (右手, 0x32)

# NUCLEO-H533RE
cd ../../build_bsp2
make clean && make NET=0 U2F=0     # キーボードだけ
make clean && make NET=0           # キーボード + セキュリティキー
make clean && make KEYMAP=dvorak   # 組み込みのキーマップを選ぶ
make flash                         # ST-LINKで書き込む
```

キーマップはRP2040と同じ`app_program/keyboard/keymaps/`のものを使います。`GAMEPAD=1`、`TZ=1`などほかのオプションと書き込みの詳細は[build_and_deploy.md](../build_and_deploy.md)を参照してください。

### キー処理をwasmで差し替える

MCP経由でWebAssemblyのモジュールを取り付けると、キー処理がそれに置き換わります。モジュールは2msごとに呼ばれ、マトリクスを受け取ってHIDのレポートを返します。Flashに保存すれば次の起動でも使われ、モジュールがtrapしたときは組み込みのキーマップに戻ります。wasmでキー処理を使っている間は、ゲームパッドのレポートは出しません。書き方とAPI、サンプルは[wasm.md](wasm.md)を参照してください。

### PC上での検証

`tools/kb_wasm_test`は、実機と同じ`app_h533/kb_wasm.c`とwasm3、組み込みのキー処理(`kb_process.c`)をPC向けにビルドし、μT-KernelのAPIを最小限の代わりの関数に置き換えて動かします。clang(wasm32ターゲットを含む)が必要です。

```bash
cd tools/kb_wasm_test
make run                       # テスト一式
make check MOD=path/to.wasm    # 自作のモジュールの事前チェック
```

| テスト | 内容 |
| --- | --- |
| 同等性 | 組み込みのキー処理と同じ動きをするモジュールに、同じランダム入力を20万スキャン与え、レポートとレイヤーの状態が毎回一致すること(MO/TGを含む) |
| 追加機能 | Ctrl+Shift+キー、J+Kの同時押し |
| 異常系 | trapしたモジュールを外して組み込みの処理に戻ること、`kb_scan`を持たないモジュールを拒否すること |
| 保存 | Flashへの保存と起動時の読み込み、破損の検出、消去 |
| 未知のimport | ホストにない関数を使うモジュールを取り付け時に拒否すること |

### 動作の確認

コンソール(ST-LINKの仮想COMポート、115200bps)に`Brain: left half connected`や`Brain: right half connected`が出ればI2Cの通信が、`Brain: USB HID output active`が出ればUSBへの送信が動いています。

## 調査用のビルド

分割キーボードの片側だけをUSBで直接PCにつないで動かすビルドです。どちらの手に問題があるかの切り分けや、配線の確認に使います。`HAND=L`か`HAND=R`で、スキャンに使うピン割当と、担当するマトリクス行(左手は0〜9、右手は10〜19)を選びます。どちらもI2Cの分割通信は行わないので、相方の基板はなくてもかまいません。

```bash
# その手のキーマップどおりに入力する
make TARGET=_PICO_RP2040_ HAND=R KEYMAP=default half     # → mtkernel_3_half_R_default.uf2

# 押したセルの座標を入力する
make TARGET=_PICO_RP2040_ HAND=R wiring_test             # → mtkernel_3_wiring_test_R.uf2

# 全セルに別々の1文字を割り当てたキーマップで入力する
make TARGET=_PICO_RP2040_ HAND=R KEYMAP=matrixtest half  # → mtkernel_3_half_R_matrixtest.uf2
```

`half`は、スレーブ側の基板も含めて、その手のキーを普通のキーボードとして入力します。キーが出ない、別の文字が出るといったときに、左右どちらの問題かを確かめられます。

`wiring_test`は、キーマップを使わず、押したセルの座標を`R03C2`のように「手(LまたはR)、手の中の行番号2桁、C、列番号」の形で1行ずつ入力します。テキストエディタを開いてキーを1つずつ押すと、どの物理キーがマトリクスのどのセルにつながっているかがわかります。行番号は`kb_config_hw.h`の`ROW_PINS_L`/`ROW_PINS_R`の添字にそのまま対応するので、`LAYOUT`マクロのマトリクス行と比べてピンの並びを直せます。キーを押し続けても座標は1回だけ入力します。

`matrixtest`キーマップは、手ごとに行0から順に`abcd`、`efgh`、`ijkl`、`mnop`、`qrst`、`uvwx`、`yz01`、`2345`、`6789`、`-=[]`を割り当てています。`LAYOUT`マクロを通さないので、72キーの配列で使っていないセルも見分けられます。左右とも同じ文字になるため、`half`で片手ずつ使います。

## 制限

- 通常のキーの同時押しは6個まで(Boot Protocolの6キーロールオーバー)です。NKROには対応していません。
- ホストから届くLEDの状態は受け付けるだけで、表示には使っていません。
- タップとホールドの使い分け、マクロ、コンボなどは組み込みのキー処理にはありません。キーボードブレインでは、wasmのキー処理で実装できます([wasm.md](wasm.md))。
- キーボードブレインは、ビルドとPC上の検証まで確認しています。USBの認識とHIDの送信、Flash、コンソールはセキュリティキー(U2F)の機能で実機確認していますが、左右のPicoをI2Cでつないだキーボードとしての動作は、実機では確認していません。
