# 離席の検出(Pico4ML)

Arducam Pico4MLのカメラで人が写っているかを判定し、人がいなくなって30秒たつと、PCにWin+L(画面のロック)を送ります。カメラの読み込み、推論(TensorFlow Lite Micro)、USBキーボードとしての送信を、すべてPico4MLの中のμT-Kernel 3.0の上で行います。

Pico4MLの実機で、カメラの映像の表示、人の有無の判定、離席後の画面のロックまでを確認しています。

## 必要なもの

- Arducam Pico4ML TinyML Dev Kit(RP2040、HM01B0カメラ、0.96インチ液晶)
- PCにつなぐUSBケーブル

## ビルドと書き込み

TensorFlow Lite Microはサブモジュール`lib/pico-tflmicro`を使います。`git clone --recursive`で取得していれば、そのままビルドできます。

```bash
bash setup.sh
cd mtk3_bsp/build_make
make TARGET=_PICO_RP2040_ presence
#   → mtkernel_3_presence.uf2
```

Pico4MLのBOOTボタンを押しながらUSBにつなぐと`RPI-RP2`ドライブが現れるので、`mtkernel_3_presence.uf2`をコピーします。

## 動き

1. 起動すると、USBキーボードとしてPCに認識されます(送るキーはWin+Lだけです)。
2. カメラに人が写ると判定が始まります。起動したときに人がいなくても、ロックはしません。
3. 人が写らない状態が30秒続くと、Win+Lを1回送ります。
4. また人が写ると(人らしい判定が3回続くと)、次の離席の判定に戻ります。背景を一瞬だけ人と見間違えても、数え直しにはなりません。

推論は1秒に1回ほどです(RP2040の1コアで約0.8秒かかります)。

### 液晶の表示

| 位置 | 内容 |
| --- | --- |
| 上(80×80) | カメラの画像(推論に使う範囲) |
| 色の帯 | 灰:判定前、緑:人がいる、黄:人がいない(数えている)、赤:ロックした |
| 黄色の数字 | ロックまでの残り秒数 |
| 下の数字 | 人らしさ(%)。緑なら人がいると判定しています |

画面が赤くなって数字が出たときはエラーです。1はカメラが応答しない、2はモデルの読み込みの失敗、3は画像の取り込みの失敗です。

## 設定

`app_presence/presence_main.c`の先頭の定義で変えられます。

| 定義 | 既定値 | 内容 |
| --- | --- | --- |
| `PRESENCE_AWAY_SEC` | 30 | 人が写らなくなってからロックするまでの秒数 |
| `PRESENCE_THRESHOLD` | 40 | 在席中に、推論の点数(-128〜127)がこれ以下になったら、すぐに離席の数えを始めます(約66%) |
| `PRESENCE_RETURN_THRESHOLD` | 40 | 人がいない状態から在席に戻すときの点数(約66%) |
| `PRESENCE_RETURN_FRAMES` | 3 | 在席に戻すには、上の点数を超える判定がこの回数続く必要があります(見間違いで数え直しにならないようにするため) |

## 仕組み

| 部分 | ファイル | 内容 |
| --- | --- | --- |
| カメラ | `app_presence/hm01b0.c` | PIOでカメラのクロック(MCLK)を作り、別のPIOとDMAで1ビットのシリアル出力を164×162の画像として取り込みます |
| 推論 | `app_presence/presence_ml.cpp` | TensorFlow Lite Microのperson detection(96×96のグレースケール、int8)。画像の中央を96×96に縮小して入力します |
| 液晶 | `app_presence/st7735.c` | SPI1でST7735(80×160)に描きます |
| 本体 | `app_presence/presence_main.c` | 判定のタスク、離席の判定、USBキーボードとしての送信 |

TensorFlow Lite Microは、`lib/pico-tflmicro`(Raspberry PiのRP2040版)を静的ライブラリにしてリンクします。pico-sdkに依存する時間とログの部分は、`app_presence/tflm_port.cpp`でμT-Kernelの機能に置き換えています。2コアで分担する処理は無効にしています。

## キーボードと同居させる

TL Splitのマスターのファームウェアに離席の判定を加えるビルドもあります。この場合、Win+Lはキーボードのスキャンのタスクが、今押しているキーに重ねて送ります。判定のタスクはスキャンより低い優先度で動くので、推論中もキーのスキャンは遅れません。分割の通信は行わず、単体のキーボードとして動きます。

```bash
make TARGET=_PICO_RP2040_ KEYMAP=default presence_kb
#   → mtkernel_3_presence_kb.uf2
```

液晶には、スキャンの間隔の最大値(ms)も水色の数字で表示します。

カメラと液晶のピンはTL Splitのマトリクスと重なります。Pico4MLだけで試すときは、マトリクスを読まないように`PRESENCE_KB_DEFS=-DKB_MATRIX_NONE`を付けてビルドしてください。

## PCだけで検証する

推論の部分はPC上で確かめられます。TensorFlow Lite Microに付属する「人あり」「人なし」の画像を、正しく判定できるかを見ます。clang、clang++、llvm-arが必要です。

```bash
cd tools/presence_test
make run
```

## 制限

- モノクロで96×96の小さなモデルなので、暗い場所、逆光、カメラから遠い場合は人を見落とすことがあります。見落とすと、ロックまでの秒数が進みます。
- 人を検出しても、画面のロックは解除しません。解除はいつもどおりPCで行います。
- USBの製品名は、このリポジトリのキーボードと同じ名前で表示されます。
