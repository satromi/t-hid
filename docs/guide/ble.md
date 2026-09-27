# Pico WのBLE

Raspberry Pi Pico W(RP2040+CYW43439)では、Bluetooth Low Energy(BLE)を使って次のことができます。

- PCとBLE HIDキーボード(HID over GATT)としてつなぐ
- 左右の基板をBLEでつなぐ無線分割
- 起動時にI2Cの相手が見つからなければBLEで探すハイブリッド分割
- WiFi(MCPサーバ、WebAssembly)とBLEキーボードの併用

BLEのプロトコル処理にはBTstack(`lib/btstack`)を、CYW43439の制御にはcyw43-driver(`lib/cyw43-driver`)を使います。どちらも利用条件に制限があるので、先に「制限」の節を読んでください。

## 構成

| 構成 | 左手(マスター) | 右手(スレーブ) | PCとの接続 | ビルド |
| --- | --- | --- | --- | --- |
| BLEキーボード | Pico W | Pico(I2C) | USB / BLE | `all` |
| 無線分割 | Pico W | Pico W(BLE) | USB / BLE | `split_ble` |
| ハイブリッド | Pico W | PicoならI2C、Pico WならBLE | USB / BLE | `split_auto`、`split_auto_slave_ble` |
| WiFi + BLEキーボード | Pico W | Pico(I2C) | USB / BLE | `WIFI=1` |

どの構成でも、PCとの接続は左手のPico Wが受け持ちます。アプリは、WiFiを使わない構成が`app_kb_program/`、WiFiを使う構成が`app_wifi_program/`です。キーのスキャン、レイヤー、キーマップは有線のキーボードと共通です([keyboard.md](keyboard.md))。

## USBとBLEの切り替え

Pico Wのビルドは、USB HIDとBLE HIDの両方を持っていて、起動時にどちらで出力するかを決めます(`app_program/keyboard/kb_output.c`)。

- 起動から約1秒の間にUSBのホストを検出したら、USB HIDで出力します。BLEの初期化も裏で行うので、USBで使っている間もペアリングの操作は効きます。
- USBのホストが見つからないとき(電池やUSB充電器から給電しているとき)は、BLE HIDで出力します。

判定は起動時の1回だけです。動作中にUSBケーブルを抜き差ししても出力先は変わらないので、切り替えるときは電源を入れ直してください。USBでの送信エラーが続くとUSBを切断とみなしますが、そのときもBLEには切り替わりません。

BLEで送るのはUSBと同じ8バイトのキーボードレポート(Boot Protocol、6キー同時押し)です。

## PCとペアリングする

1. Pico Wに書き込んで起動します。ボンディング情報(ペアリングで交換した鍵)がなければ、そのままペアリングを待つ状態になります。
2. PCのBluetoothの設定で、デバイスを追加します。キーボードは`app_program/keyboard/kb_config.h`の`KB_PRODUCT`の先頭18文字をアドバタイズします(既定では`TL Split using uT-`)。
3. パスキーの入力はありません。LE Secure ConnectionsのJust Worksでペアリングし、キーボード側は自動で承認します。

接続後にPCが読み出すGAPのデバイス名は`TK Keyboard`です(`device/ble_hid/ble_hid_keyboard.gatt`)。つながったかどうかは、シリアルのログ(115200bps、UART0)で`BLE: pairing OK`、`BLE: HID connected`、`BLE: HID report enabled`が出ることで確かめられます。

## ボンディング情報と再接続

ペアリングで得た鍵は、Flashの末尾8KB(0x1FE000〜0x1FFFFF、4KBのバンク2つ)に保存します(`device/ble_hid/btstack_tlv_flash.c`)。電源を切っても残るので、次に起動したときはペアリングし直さずにPCとつながります。保存できる相手は最大4台です(`device/ble_hid/btstack_config.h`)。

PCとの接続が切れると、アドバタイズを30ms間隔で再開します。30秒たってもつながらなければ1.5秒間隔に落とすので、それ以降はPCが見つけるまでに少し時間がかかります。

## ペアリングし直す

次のどちらかを3秒押し続けると、ペアリングモードに入ります。

- キーマップの`XMK_BLE_PAIR`のキー。`default`と`gamepad`のキーマップでは、FNレイヤーのQの位置にあります(FNを押しながらQ)
- Pico WのBOOTSELボタン(PCとのBLE接続を受け持つ左手のみ)

ペアリングモードでは次の順に動きます。

1. 保存しているボンディング情報をすべて消す
2. PCとの接続を切る
3. BLEを5秒止める。この間にPC側で古いキーボードの登録を削除する
4. BLEを起動し直し、新しいペアリングを待つ

PC側に古い登録が残っていると、PCが古い鍵で再接続しようとして、つながらないことがあります。別のPCに切り替えるときや、うまくつながらなくなったときに使ってください。

## 無線分割(split_ble)

左右ともPico Wにします。左手がPCとUSBまたはBLEでつながり、右手のキーの状態をBLEで受け取ります。

- 右手は`TK Split`という名前と専用のサービスUUIDでアドバタイズし、左手がそれを探して接続します。左右の間でペアリングの操作はいりません。
- 右手はスキャンのたびに、自分の側のマトリクス(10行分)とCRC8をNotificationで左手に送ります。
- 右手との接続が切れると、左手は右手のキーをすべて離した扱いにして、右手を探し直します。つながれば元どおり使えます。

左手はPCとの接続(ペリフェラル)と右手との接続(セントラル)を同時に持ちます。

## ハイブリッド(split_auto)

左手はPico W、右手はPicoでもPico Wでもかまいません。左手は起動時にI2Cで右手を1回読んでみて、応答があればI2Cで、なければBLEで右手とつなぎます。

| 右手 | ビルド | 右手のファイル |
| --- | --- | --- |
| Pico(ケーブル、I2C) | `split_auto` | `mtkernel_3_slave_<keymap>.uf2` |
| Pico W(無線、BLE) | `split_auto_slave_ble` | `mtkernel_3_slave_ble_<keymap>.uf2` |

左手のファイルは、どちらの場合も`split_auto`で作る`mtkernel_3_master_auto_<keymap>.uf2`です。`split_auto_slave_ble`は右手だけを作ります。

判定は起動時の1回だけです。I2Cで使うときはケーブルをつないでから電源を入れてください。起動した後でケーブルをつないでも、BLEのままです。

## WiFiと併用する(WIFI=1)

`WIFI=1`でビルドすると、WiFi、MCPサーバ、WebAssemblyが入ります。BLEキーボードを一緒に入れるかどうかは`BLE`で選びます。

| 指定 | 内容 |
| --- | --- |
| `BLE=1`(省略時) | WiFi、MCP、wasmに加えて、BLEキーボード(USB/BLE出力、右手はI2C)が入る |
| `BLE=0` | WiFi、MCP、wasmだけ。キーボードの機能も入らない |

WiFiとBLEは同じCYW43439を使います。`BLE=1`では、BLEの初期化が終わり、さらに1.5秒待ってからWiFiの接続を始めます。BLEの初期化に失敗したときは、WiFiも動きません。

WiFiの接続先の設定(`.wifi_config`)は[network.md](network.md)、MCPサーバは[mcp.md](mcp.md)、wasmは[wasm.md](wasm.md)を参照してください。

## ビルドと書き込み

`setup.sh`でビルドツリーを組み立ててから、`mtk3_bsp/build_make`でビルドします。

```bash
bash setup.sh
cd mtk3_bsp/build_make

# BLEキーボード (mtkernel_3.uf2)。単体のallは構成を変えるたびにcleanする
make TARGET=_PICO_W_ clean
make TARGET=_PICO_W_ KEYMAP=default all

# その右手 (Pico、I2C)
make TARGET=_PICO_RP2040_ KEYMAP=default split_slave

# 無線分割 (mtkernel_3_master_ble_default.uf2 / mtkernel_3_slave_ble_default.uf2)
make TARGET=_PICO_W_ KEYMAP=default split_ble

# ハイブリッド (mtkernel_3_master_auto_default.uf2 / mtkernel_3_slave_default.uf2)
make TARGET=_PICO_W_ KEYMAP=default split_auto
make TARGET=_PICO_W_ KEYMAP=default split_auto_slave_ble   # 右手をPico Wにする場合

# WiFi + BLEキーボード / WiFiのみ (mtkernel_3.uf2)
make TARGET=_PICO_W_ clean
make TARGET=_PICO_W_ WIFI=1 all
make TARGET=_PICO_W_ WIFI=1 BLE=0 all
```

書き込みは、BOOTSELボタンを押しながらUSBにつなぎ、現れた`RPI-RP2`ドライブに`.uf2`をコピーします。左右とも同じキーマップでビルドしたものを使ってください。ツールチェーンの準備や書き込みの詳しい手順は[build_and_deploy.md](../build_and_deploy.md)にまとめています。

## 制限

- BTstackのライセンスは非商用の個人利用に限られます。BLEの機能を含むファームウェア(`WIFI=1 BLE=0`以外のPico Wのビルド)は、商用製品には使えません。
- cyw43-driverは、非商用か、Raspberry Pi製のチップ(Pico WのRP2040)上での利用に限られます。Pico Wのビルドはすべてこれを含みます。ライセンスの一覧は[NOTICE.md](../../NOTICE.md)にあります。
- ペアリングはJust Worksだけで、パスキーによる確認(MITM対策)はしません。
- 出力先(USBかBLEか)と、ハイブリッドのI2C/BLEは、起動時に決めたまま変わりません。
- 無線分割の左手は、`TK Split`をアドバタイズしている相手のうち最初に見つけたものにつなぎます。近くに同じ構成の右手が複数あると、別の右手につながることがあります。
- Pico Wのビルドでは`GAMEPAD=1`は効きません。BLEの出力もゲームパッドには対応していません。
- `WIFI=1`(`BLE=1`)の構成では、ボンディング情報を置くFlash末尾8KBが、MCPのAPIキーとWiFiの接続情報を置く末尾4KB(0x1FF000〜)と、wasmモジュールの保存スロット3(0x1F7000〜0x1FEFFF)に重なっています。MCPからこれらを書き込むと、ボンディング情報や書き込んだ値が壊れることがあります。
