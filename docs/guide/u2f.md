# 物理セキュリティキー(FIDO U2F)

NUCLEO-H533RE(STM32H533)を、Webサイトの2段階認証に使えるUSBセキュリティキーにします。キーボードと同じUSBケーブルでPCにつながり、PCからは「キーボード+セキュリティキー」の1台の複合デバイスに見えます。ログインのたびにNUCLEOのB1(USER)ボタンを押して、本人の操作であることを示します。

FIDO Allianceの認定を受けていない実験的な実装です。重要なアカウントの唯一の2段階認証手段にはしないでください。

## できること

| 項目 | 内容 |
|---|---|
| 規格 | FIDO U2F(CTAP1)。CTAPHIDの上でREGISTER/AUTHENTICATE/VERSIONを処理し、ブラウザのWebAuthnから「セキュリティキー」として使えます |
| 登録できるサイト数 | 無制限(サイトごとの秘密鍵は保存せず、毎回計算し直します) |
| 本人の操作の確認 | B1ボタン。押し始めてから3秒以内の要求1回に有効です(押し続けても1回分) |
| 署名 | ECDSA P-256。公開鍵暗号アクセラレータ(PKA)で計算します |
| ハッシュ | SHA-256/HMAC-SHA256。ハッシュアクセラレータ(HASH)で計算します |
| 乱数 | 真性乱数生成器(TRNG) |
| 鍵とカウンタ | 内蔵Flashに保存し、電源を切っても残ります。鍵はチップ固有の鍵(DHUK)で暗号化します |
| 証明書 | 初回起動時に作る自己署名のX.509証明書(CN=uT-Kernel U2F) |

FIDO2(CTAP2)、パスキー(resident key)、PIN、生体認証には対応していません。パスキーを必須にしているサイトでは使えません。

## 必要なもの

| もの | 用途 |
|---|---|
| NUCLEO-H533RE | セキュリティキー本体 |
| USB Type-Cケーブル2本 | ST-LINK側(CN1、書き込みとコンソール)とUSER USB(CN3、PCとの接続) |
| GNU Arm Embedded Toolchain | ビルド |
| STM32CubeProgrammer | `make flash`での書き込み、オプションバイトの設定 |
| clang、openssl | PCだけでの検証 |
| Python 3、`fido2`パッケージ | 実機の検証 |
| STM32TrustedPackageCreator、Pythonの`cryptography`パッケージ | TZ-Closedにする場合のみ |

左右のPicoをつながなくても、セキュリティキー単体として動きます。キーボードとしての使い方と、USB複合デバイスの構成は[keyboard.md](keyboard.md)を参照してください。

## ビルドと書き込み

ツールチェーンの準備などビルド全般は[../build_and_deploy.md](../build_and_deploy.md)を参照してください。

```bash
bash setup.sh
cd build_bsp2
make clean && make NET=0        # キーボード + セキュリティキー (ネットワークなし)
```

成果物は`build/mtk3_h533.bin`です。既定の`make`(ネットワーク、MCP、wasmを含む)でもセキュリティキーは入ります。外す場合は`make U2F=0`を指定します。

書き込みは次のどちらかで行います。

- STM32CubeProgrammerがある場合は、ST-LINK側(CN1)をPCにつないで`make flash`を実行します。`STM32_Programmer_CLI`がPATHにない場合は、`make flash STM32_PROGRAMMER="$LOCALAPPDATA/stm32cube/bundles/programmer/<版>/bin/STM32_Programmer_CLI.exe"`のようにパスを指定します(VS CodeのSTM32Cube拡張に同梱されたものの場合)
- ない場合は、ST-LINK側をつなぐとUSBドライブとして見えるので、`build/mtk3_h533.bin`をそのドライブにコピーします

書き込んだら、USER USB(CN3)もPCにつなぎます。セキュリティキーとキーボードはこちらのUSBから見えます。

### USER USBだけで動かす(JP5)

出荷時の設定では、ST-LINK側(CN1)をPCにつながないとボードに電源が入りません。USER USB(CN3)だけをつないでも動かないのはこのためです。ボードの5VをどこからとるかはジャンパJP5(2列×5ピン)で選びます。

| JP5の位置 | シルク | 5Vのとり方 |
|---|---|---|
| 1-2(出荷時) | `5V_STLK` | ST-LINK側(CN1)をPCにつなぐ。ST-LINKがPCに認識されてから、ボードの電源スイッチ(U4)が入る |
| 3-4 | `5V_VIN` | VIN(CN6 pin 8/CN7 pin 24)に7〜12V |
| 5-6 | `E5V` | E5V(CN7 pin 6)に5V(4.75〜5.25V) |
| 7-8 | `VBUS_STLK` | ST-LINK側(CN1)にUSB充電器をつなぐ(PCへの認識なしで給電) |
| 9-10 | `VBUSC` | USER USB(CN3)から |

出荷時の[1-2]では、5VはCN1側の電源スイッチを通ってしか来ません。そのためUSER USBだけをつなぐと、CN3に5Vが来ていることを示す緑のLD6は点くのに、ボードの5Vを示す緑のLD3は消えたままで、何も動きません。

USER USBだけで動かす手順は次のとおりです。

1. USBケーブルをすべて抜きます。
2. JP5のジャンパを[1-2]`5V_STLK`から[9-10]`VBUSC`に差し替えます。ピンの位置はボードのシルク印刷で確かめてください。
3. USER USB(CN3)をPCにつなぎ、緑のLD3(5V)が点くことを確かめます。

電源をCN3からとっていても、ST-LINKで書き込みや仮想COMポートを使えます。その場合は先にUSER USB(CN3)をつないでLD3が点くのを確かめ、そのあとでST-LINK側(CN1)をPCにつなぎます。順番が逆になると、ST-LINK側の5Vが先にボードに回り込み、PCのUSBポートの電流制限(500mA)を超えたり、認識に失敗して5Vが入らなかったりすることがあります。書き込みを何度も繰り返す間は、出荷時の[1-2]に戻しておくほうが手軽です。

そのほかのジャンパについて、次の点に注意してください。

- 隣のJP2(シルク`IDD`)は電流測定用で、電源の選択ではありません。JP2を外すと、電流計をつながない限りマイコンに電源が入らないので、付けたままにします。
- JP1(ST-LINKのリセット)は外したまま(OFF)にします。付けるとST-LINKが止まり、内蔵のST-LINKで書き込めなくなります。
- どのとり方でも、ボードとシールドを合わせて500mAまでです。
- JP5を[9-10]にしたときに電源を入れ直す場合は、CN1ではなくUSER USB(CN3)を抜き挿しします。

ジャンパと電源の詳細は、STのUM3121(STM32H5 Nucleo-64 board(MB1814))の「Power supply and power selection」の章にあります。

### 起動の確認

ST-LINKの仮想COMポートを115200bpsで開くと、起動ログが見えます。

```text
U2F: P-256 by PKA
U2F: SHA-256 by HASH
U2F: key storage encrypted by SAES (DHUK)
U2F: no keys, generating device keys       ← 初回起動時のみ
U2F: ready (counter=0, cert 290 bytes)
U2F: key sector hidden by HDP (HDPL 2)      ← HDPを設定した場合
```

| ログ | 意味 |
|---|---|
| `U2F: P-256 by PKA` | 起動時の自己診断でPKAが正しく計算できた |
| `U2F: P-256 by software (PKA self-test failed)` | PKAの結果が既知の値と合わなかったため、ソフトウェア(micro-ecc)で計算する。動作はするが遅い |
| `U2F: SHA-256 by HASH` | 起動時の自己診断でHASHが正しく計算できた |
| `U2F: SHA-256 by software (HASH self-test failed)` | HASHの結果が合わなかったため、SHA-256/HMACをソフトウェアで計算する |
| `U2F: key storage encrypted by SAES (DHUK)` | 保存する鍵をチップ固有の鍵で暗号化する |
| `U2F: key storage in plaintext (SAES self-test failed)` | SAESの自己診断に失敗したため、鍵を暗号化せずに保存する |
| `U2F: stored keys re-saved encrypted (ok)` | 平文で保存されていた鍵を暗号化して保存し直した。1回だけ出て、登録済みのサイトはそのまま使える |
| `U2F: stored keys corrupted, generating device keys` | 保存された鍵が壊れていたため、作り直した。登録済みのサイトは使えなくなる |
| `U2F: ready` | 使える状態 |
| `U2F: press B1 (USER button) to confirm` | PCから確認の要る要求が来ている。B1を押す |
| `U2F: key sector hidden by HDP (HDPL 2)` | 鍵のセクタを隠した(HDPの設定を参照) |
| `U2F: key sector not in HDP area (...)` | HDP領域が未設定。鍵は暗号化されているが、セクタ自体は読める |

## 使い方

### サイトに登録する

1. Webサイトの2段階認証の設定で「セキュリティキー」を選びます。
2. ブラウザが「セキュリティキーをタッチしてください」と表示したら、B1を押します。

試すだけなら[webauthn.io](https://webauthn.io)が便利です。「Advanced Settings」で次のように選んでから、RegisterやAuthenticateを押します。

| 項目 | 値 |
|---|---|
| User Verification | Discouraged |
| Discoverable Credential | Discouraged |
| Attestation | None(Directにすると自己署名の証明書が見える) |
| Registration Hints | Security Keyにチェック |
| Authentication Hints | Security Keyにチェック |

### ログインする

ログイン時にブラウザがキーを求めたら、B1を押します。

webauthn.ioで試す場合は、Authenticateを押す前にUsername欄へ登録時と同じ名前を入れてください。空のままだとブラウザはパスキーでのログインを求めます。このキーはパスキーに対応していないため、Windowsの画面にはカメラや指紋などの選択肢しか出ません。HintsにSecurity Keyを付けておくと、Windowsの画面でセキュリティキーが優先して選ばれます。

### 初期化する

B1を押したままNUCLEOをリセット(または電源投入)し、そのまま5秒押し続けると、鍵とカウンタを消して作り直します。5秒以内に離すと取り消されます。初期化すると登録済みのサイトはすべて使えなくなるので、先に各サイトからキーを削除するか、別の認証手段を用意してください。

### 状態を見る

MCP([mcp.md](mcp.md))の`u2f_status`で、準備状態、署名とハッシュの計算方法(PKA/HASHかソフトウェアか)、鍵の保存形式(SAESか平文か)、カウンタ、起動後の登録と認証の回数がわかります。秘密の値は返しません。MCPを使うにはネットワークが必要なので、`make NET=0`のビルドでは使えません。

MCPの`peek`/`poke`などのメモリを読み書きするツールからは、鍵のセクタ、導出した鍵を持つRAM、PKAのRAM、HASHのレジスタにアクセスできないようにしています。

## 動作の検証

### PCだけで検証する

ファームウェアと同じソース(`app_h533/u2f`)をPC向けにビルドし、ブラウザ側の役をしてCTAPHIDのパケットをやりとりします。P-256はmicro-ecc、FlashはRAM上の配列で代わりをします。clangとopensslが必要です。

```bash
cd tools/u2f_test
make run
```

最後に`ALL PASSED`と出て、opensslによる証明書の検証が通れば成功です。

| テスト | 内容 |
|---|---|
| CTAPHID | INIT、300バイトのPING(複数パケット)、不正なSEQ、別チャネルからのBUSY、タイムアウト、不正なCID、未知のコマンド |
| REGISTER | 確認なしで`6985`になること、応答の形式、公開鍵が曲線上にあること、アテステーション署名の検証 |
| AUTHENTICATE | 確認のみ、他サイト、確認なし、確認あり、確認不要モード、カウンタの増加、署名の検証 |
| 保存 | 再起動後も鍵とカウンタが続くこと、カウンタが2面を切り替えても増え続けること、鍵の破損を検出して作り直すこと |
| 証明書 | `openssl x509`で読めること、`openssl verify -check_ss_sig`で自己署名が検証できること |

PKA、HASH、TRNG、SAESはPCでは動かないため、実機の起動時の自己診断で確かめます。PKAは既知の鍵(RFC 6979 A.2.5)で公開鍵を計算し、署名をmicro-eccで検証します。結果は起動ログでわかります。

### 実機で検証する

USBでつないだキーにPCからU2Fの要求を送り、応答と署名を確かめます。途中でB1を押すよう求められます。

```bash
pip install fido2
python tools/u2f_test/u2f_hw_test.py
```

| 確認項目 | 内容 |
|---|---|
| CTAPHID | 300バイトのPING(複数パケットに分かれる通信) |
| 登録 | ボタンなしでは拒否されること、公開鍵とキーハンドルの形式、アテステーション署名、証明書 |
| 認証 | 確認のみの要求、他サイトや改ざんしたキーハンドルの拒否、署名の検証、カウンタが増えること |

B1を押さずに済む要求(PING、VERSION、ボタンなしの登録、他人のキーハンドルでの認証)を流し続け、応答が止まらないかを確かめる連続テストもあります。

```bash
python tools/u2f_test/u2f_stress.py --minutes 10
```

どちらも既定でVID `0xCAFE`、PID `0x4004`のデバイスを探します。違う値にした場合は`--vid`/`--pid`で指定します。

Windowsでは、FIDOデバイスを直接開くのに管理者権限が必要です。管理者として起動したターミナルから実行してください(ブラウザから使う場合は不要です)。Linuxでは、FIDO用のudevルールでhidrawへのアクセスを許可してください。

## STM32H533を使う理由

セキュリティキーの役目は、秘密鍵を外に出さずに署名することです。STM32H533は、そのための機能をチップに持っています。キーボードで使っているRaspberry Pi Pico(RP2040)と比べると次のようになります。

| 観点 | STM32H533 | RP2040(Pico) |
|---|---|---|
| 鍵を置く場所 | チップ内蔵のFlash。チップ固有の鍵(DHUK)で暗号化して保存し、起動後はFlashの隠蔽領域(HDP)で読めなくできる | 外付けのQSPI Flashに平文。BOOTSELボタンを押してUSBにつなげば、誰でも中身を読み出せる |
| 乱数 | 真性乱数生成器(TRNG)。シード異常やクロック異常を検出する健全性チェック付き | 専用のTRNGはない(リングオシレータの乱数ビットのみ) |
| 署名の計算 | 公開鍵暗号アクセラレータ(PKA)でECDSA P-256を計算 | ハードウェアの支援がなく、Cortex-M0+でのソフトウェア計算 |
| ハッシュの計算 | ハッシュアクセラレータ(HASH)でSHA-256/HMACを計算 | ソフトウェア計算 |
| 処理の分離 | TrustZoneで鍵と暗号処理をセキュア側に置き、キーボードやネットワークのコードから触れないようにできる | なし |
| デバッグポートの封鎖 | product stateをTZ-Closedに進めると、デバッガから鍵を読めなくなる | 仕組みがない |

この実装では、次の機能を使っています。

| 機能 | 使い方 |
|---|---|
| TRNG | master鍵の生成、登録ごとのnonce、署名ごとの乱数k。STの推奨設定値(AN4230)で動かし、シード異常を検出したら再初期化します |
| PKA | ECDSA P-256の署名と公開鍵の計算。起動時に既知の値で自己診断し、合わなければソフトウェア(micro-ecc)に切り替えます。署名後はPKAのRAMから秘密鍵とkを消します |
| HASH | 鍵の導出、キーハンドルのMAC、署名対象のハッシュ(SHA-256/HMAC-SHA256)。起動時に既知の値とソフトウェア実装で自己診断し、合わなければソフトウェアに切り替えます。HMACのあとはHASHのレジスタを空データのハッシュで上書きし、導出した鍵を残しません |
| SAES+DHUK | master鍵とアテステーション秘密鍵を、DHUKを鍵にしたAES-256 CTRで暗号化して保存します。DHUKはソフトウェアからもデバッガからも読めないので、Flashを丸ごと読み出されても鍵は使えず、別のチップに書き写しても復号できません |
| FlashのHDP | 鍵のセクタを隠蔽領域に設定しておくと、起動時に鍵を読み込んだあとで隠蔽レベル(HDPL)を1から2に上げ、再起動するまでCPUからもデバッガからも読めなくします |
| TrustZone | `make TZ=1`のとき、U2Fの処理と鍵、暗号の周辺回路をセキュア側に置きます |
| product state | TZ-Closedに進めて、デバッグポートを閉じます |

## 鍵のセクタを隠す(HDPの設定)

HDP領域はオプションバイトで設定します。ファームウェアは設定の有無にかかわらず動き、起動ログで状態がわかります。

```bash
# 鍵のセクタ (バンク 2 のセクタ 28 = 0x08078000 から 8KB) を HDP 領域にする
STM32_Programmer_CLI -c port=SWD -ob HDP2_STRT=0x1C HDP2_END=0x1C

# 元に戻す (HDP 領域なし)
STM32_Programmer_CLI -c port=SWD -ob HDP2_STRT=0x1 HDP2_END=0x0
```

設定後は、ST-LINKで鍵のセクタを読んでもすべて0になります。隠すのは鍵のセクタだけで、カウンタやwasm、MCPの保存領域は隠しません。B1による初期化と鍵の生成は隠す前(起動直後)に行うので、設定後もそのまま使えます。

## TrustZoneで分離する(make TZ=1)

`make TZ=1`でビルドすると、U2Fの処理と鍵、暗号の周辺回路(AES、HASH、RNG、SAES、PKA)、本人確認のボタンB1をTrustZoneのセキュア側に置き、μT-Kernelとキーボード、USB、ネットワーク、wasmは非セキュア側で動かします。非セキュア側のコードに不具合や乗っ取りがあっても、鍵や暗号の周辺回路には直接触れられません。

| 領域 | セキュア側 | 非セキュア側 |
|---|---|---|
| Flash | 0x0C000000から64KB(最後の4KBが非セキュア側からの呼び出し口)、鍵とカウンタのセクタ28〜30(0x0C078000〜0x0C07DFFF) | 0x08010000から(wasmとMCPの保存領域を含む) |
| SRAM | SRAM3の上位32KB(0x3003C000〜、タスクのセキュアスタック16KBを含む) | 0x20000000から240KB |
| 周辺回路 | AES、HASH、RNG、SAES、PKA、B1のピン(PC13) | それ以外すべて(USB、I2C、SPI、USART、GPIO、タイマなど)と割り込み |

非セキュア側からの呼び出し口は、U2F用(`u2fs_init`、`u2fs_apdu`、`u2fs_button`など7つ)とカーネル用(`tzk_task_create`、`tzk_save_context`など5つ)です。宣言は`tz_h533/include/`にあります。

- μT-Kernel 3.0セキュア機能拡張の標準実行モデルで動かします。セキュア側を呼ぶU2Fタスクはセキュアコール可能属性(`TA_TZCALL`)で作られ、セキュア側に専用のスタック(12KB)を持ちます。署名などセキュア側の処理の途中でも、割り込みや優先度の高いタスク(キースキャンなど)は通常どおり動きます。
- タスクを切り替えるとき、非セキュア側のディスパッチャがセキュア側のスタックも切り替えます。セキュア側の実行中に割り込まれたタスクのレジスタ(浮動小数点レジスタを含む)は、ハードウェアがセキュア側のスタックに退避して消すので、非セキュア側からは見えません。
- BSP2のカーネル(μT-Kernel 3.00.07)には標準実行モデルの仕組みがないため、`make TZ=1`のときだけ、カーネルの一部を`tz_h533/nonsecure/kernel/`の3.00.08相当のファイルに差し替えてビルドします。
- セキュア側を呼べるのは`TA_TZCALL`のタスクだけです。MCPの`u2f_status`には、U2Fタスクが取得した値の写しを返します。
- セキュア側は、非セキュア側から渡されたポインタが非セキュアの領域を指していることを確かめてから使います。
- B1(PC13)はセキュア側専用のピンです。押し始めの検出と承認はセキュア側が行い、非セキュア側は20msごとに読み取りの機会を渡すだけです。非セキュア側が乗っ取られても、ボタンを押さずに登録や署名を通すことはできません。鍵の初期化も、セキュア側がB1が押されていることを確かめたときだけ受け付けます。

### オプションバイトの準備

TrustZoneを有効にし、Flashをセキュアと非セキュアに分けます。TrustZoneを有効にした時点で分割(SECWM)が全領域セキュアに初期化されるので、2回に分けて書き込みます。

```bash
# 1. TrustZone を有効にし、セキュア側の起動番地を設定
STM32_Programmer_CLI -c port=SWD -ob TZEN=0xB4 SECBOOTADD=0x0C0000

# 2. Flash の分割 (バンク 1 の先頭 64KB と、鍵・カウンタのセクタ 28-30 をセキュアに)
STM32_Programmer_CLI -c port=SWD -ob SECWM1_STRT=0x0 SECWM1_END=0x7 SECWM2_STRT=0x1C SECWM2_END=0x1E
```

HDPも使う場合は、前の節の`HDP2_STRT=0x1C HDP2_END=0x1C`も設定します。

### TrustZone構成のビルドと書き込み

```bash
cd build_bsp2
make clean && make TZ=1 NET=0
make TZ=1 NET=0 flash
```

`build/secure/secure.bin`(セキュア側、0x0C000000)と`build/mtk3_h533.bin`(非セキュア側、0x08010000)の2つができ、`make flash`は両方を書き込みます。起動ログは通常の構成と同じです。セキュア側のログは、非セキュア側がまとめてコンソールに出します。

TrustZone構成で動いているボードにSTM32CubeProgrammer(CLIを含む)を接続すると、読み出しだけでもSAU(セキュリティ属性の設定)が書き換えられ、ファームウェアがSecureFaultやハードフォールトで止まります。動作中の様子はシリアルコンソールで見てください。書き込みは、product stateがOpenのあいだはこれまでどおりできます(書き込み後のリセットで設定し直されます)。

### 動作の確認

`TZTEST=1`を付けると、TrustZoneの自己テストが一緒に動き、10秒ごとにコンソールへ結果を出します。

```bash
make clean && make TZ=1 NET=0 TZTEST=1 && make TZ=1 NET=0 TZTEST=1 flash
```

自己テストは次を繰り返します。`TZTEST:`の行の`calc_err`、`reg_err`、`fp_err`、`child ... err`、`cre_err`が0のままなら正常です。

- セキュア側で計算中のタスクを1msごとに横取りし、戻ったときの計算結果とレジスタ(浮動小数点レジスタを含む)を確かめる
- セキュア側を実行中のタスク同士を切り替える
- `TA_TZCALL`のタスクの生成、終了、削除を繰り返し、セキュアスタックが漏れないことを確かめる

USB側は、`u2f_stress.py`でセキュア側の処理を通る要求を流し続けて確かめます。

### 元に戻す

product stateがOpenのあいだは、オプションバイトでTrustZoneを無効にできます。そのあと通常の`make`でビルドした`build/mtk3_h533.bin`を0x08000000に書き込みます。

```bash
STM32_Programmer_CLI -c port=SWD -ob TZEN=0xC3
```

DHUKの値はTrustZoneの有無やセキュア状態によって変わります。そのため構成を切り替えると保存済みの鍵を復号できず、新しい鍵が作られます。登録済みのサイトは使えなくなります。

## デバッグポートを閉じる(TZ-Closed)

product stateがOpenのままだと、ST-LINKからセキュア側のRAMやFlash(鍵を含む)を読めます。普段使いのキーや他人に渡すボードは、TrustZone構成でproduct stateをTZ-Closedに進めて使います。

| 項目 | TZ-Closedでの状態 |
|---|---|
| キーボード、U2F | そのまま動く。鍵とカウンタは閉じる前のものを使い続ける |
| セキュア側(鍵、暗号処理) | デバッガから読めない |
| 非セキュア側(μT-Kernel、キーボード) | デバッガもつながらない |
| ファームウェアの更新 | できない。更新するには全消去してOpenに戻し、準備からやり直す |
| 元に戻す | 自分で作った証明書によるデバッグ認証(DA)で、全消去してOpenに戻せる |

STM32H5では、デバッグは起動の段階を表すHDPLが決まった値に達したときに開きます(TZ-Closedの非セキュア側はHDPL3)。このファームウェアは鍵を隠したあとのHDPL2のまま動くので、TZ-Closedでは非セキュア側のデバッグも開きません。`da.ps1`には証明書でデバッグを開く許可を出す`open-debug`もありますが、同じ理由でこのファームウェアではデバッガはつながりません。

閉じる前に次を確認してください。

- ファームウェアの書き込み、HDPの設定、`u2f_hw_test.py`での確認を済ませておきます。
- `.da/`の秘密鍵と証明書をなくすとOpenに戻せず、ファームウェアを二度と書き換えられません。別の場所に控えを取ってください。`.da/`は`.gitignore`で除外しており、公開してはいけません。
- Openに戻すとFlashがすべて消えます。U2Fの鍵も消えるので、登録済みのサイトは使えなくなります。
- Lockedは二度と戻せないので、使いません。
- TrustZone構成のDAは証明書方式です(パスワード方式はTrustZone無効時のもの)。鍵と証明書は自分で作ります。STのサンプルの鍵は公開されているので使わないでください。

### 鍵、証明書、OBKを作る

1回だけ行います。`make_da_certs.py`はPythonの`cryptography`パッケージを使います。

```bash
python tools/da/make_da_certs.py keys .da     # root / intermediate / leaf の鍵
python tools/da/make_da_certs.py certs .da    # 証明書チェーン (PSA ADAC 形式)
# DA の設定 (root 公開鍵のハッシュと権限) を OBK ファイルにする
STM32TrustedPackageCreator_CLI -obk <DA_Config.xml の絶対パス>
```

`.da/`には鍵(`key_1_root.pem`、`key_2_intermediate.pem`、`key_3_leaf.pem`と各`_pub.pem`)と証明書(`cert_leaf_chain.b64`など)ができます。`keys`は既存の鍵を上書きしません。

`DA_Config.xml`は、STM32CubeH5の`Projects/NUCLEO-H533RE/ROT_Provisioning/DA/Config/DA_Config.xml`を元に、鍵と出力先を`.da/`の絶対パスに書き換えて作ります(相対パスは解決されません)。出力するOBKファイルの名前は`DA_Config.obk`にします。権限の値は`make_da_certs.py`の既定値`0x00005077`と合わせてください。Trusted Package CreatorはSTM32CubeMX(`utilities/STM32TrustedPackageCreator`)にも入っています。

### ボードに適用する

`tools/da/da.ps1`は、リポジトリ直下の`.da/`にある`key_3_leaf.pem`、`cert_leaf_chain.b64`、`DA_Config.obk`を使います。STM32CubeProgrammerは`%LOCALAPPDATA%\stm32cube\bundles\programmer\2.23.0\bin\`にあるものを使うので、別の版や場所の場合は`da.ps1`の`$P`を書き換えてください。PowerShellで、どのフォルダからでも実行できます。

```powershell
$DA = "C:\path\to\t-hid\tools\da\da.ps1"
powershell -ExecutionPolicy Bypass -File $DA provision   # Provisioning にして OBK を書き込む
powershell -ExecutionPolicy Bypass -File $DA discovery   # provisioning integrity status が VALID か
powershell -ExecutionPolicy Bypass -File $DA test        # 証明書で認証できるか (データは消えない)
# discovery と test のあとはボードの電源を入れ直す
powershell -ExecutionPolicy Bypass -File $DA close       # TZ-Closed にする
powershell -ExecutionPolicy Bypass -File $DA discovery   # PSA lifecycle が ST_LIFECYCLE_TZ_CLOSED か
```

`provision`は`PRODUCT_STATE=0x17`(Provisioning)、`close`は`PRODUCT_STATE=0xC6`(TZ-Closed)を書き込みます。どちらも実行前に確認を求めるので、`yes`と入力します。

- `close`の最後に`Unable to reconnect after setting the Option Bytes`と出ますが、デバッグが閉じたためで正常です。`discovery`で`ST_LIFECYCLE_TZ_CLOSED`になったことを確かめます。
- `discovery`と`test`を実行すると、チップは電源を入れ直すまで認証の待ち受け状態に残り、ファームウェアが動かず、デバッガもつながりません。実行後はNUCLEOのUSBケーブルを抜き挿しして電源を入れ直してください(RESETボタンでは戻りません)。抜き挿しするのは電源をとっている側で、JP5が出荷時の[1-2]ならCN1、[9-10]ならCN3です。

### Openに戻す

全消去して、product stateをOpenに戻します。

```powershell
powershell -ExecutionPolicy Bypass -File $DA regression
```

戻したあとは、TrustZoneのオプションバイトとHDPの設定をやり直し、ファームウェアを書き込み直してから、必要ならもう一度閉じます。

## しくみ

### 鍵の導出

```text
master (32バイト、初回起動時にTRNGで生成してFlashに保存)
  ├─ priv_key = HMAC(master, "u2f-private-key")
  └─ mac_key  = HMAC(master, "u2f-key-handle-mac")

登録: nonce = TRNG(32)
      秘密鍵d = HMAC(priv_key, appid || nonce)   (1 <= d < nになるまでnonceを引き直す)
      キーハンドル = nonce || HMAC(mac_key, appid || nonce)   (64バイト)
      公開鍵とキーハンドルをサイトに渡す(秘密鍵は保存しない)
認証: キーハンドルのMACをappidで検証し、dを計算し直して署名
```

HMACはすべてHMAC-SHA256です。サイトごとの秘密鍵は保存しないので、登録数に上限がありません。キーハンドルに秘密鍵は含めません。別のサイト(appid違い)にキーハンドルを渡してもMACが合わず`6A80`になるので、サイト間でキーを追跡できません。

アテステーション秘密鍵も初回起動時にTRNGで作り、自己署名の証明書と一緒に保存します。起動時にmasterとアテステーション秘密鍵をFlashから読み込んで復号し、導出した鍵を作ったあと、RAM上のmasterの写しは消します。

### Flashの配置

内蔵Flash(512KB)の上位64KBをデータ領域にしており、リンカスクリプトでコードを置かないようにしています。配置は`device/flash/stm32h5_flash.h`で決めています。

| アドレス | 大きさ | 内容 |
|---|---|---|
| 0x08070000〜0x08077FFF | 32KB | キー処理のwasmモジュール |
| 0x08078000〜0x08079FFF | 8KB | U2Fの鍵と証明書(バンク2のセクタ28) |
| 0x0807A000〜0x0807DFFF | 16KB | U2Fのカウンタ(8KBの2面を交互に使う) |
| 0x0807E000〜0x0807FFFF | 8KB | MCPの`flash_write`の保存領域 |

TrustZone構成では、鍵とカウンタのセクタはセキュア側のアドレス(0x0C078000〜)から使います。

鍵のセクタは、形式`U2K2`でmasterとアテステーション秘密鍵の64バイトをSAESで暗号化して保存します。鍵はDHUK、方式はAES-256 CTRで、IVは保存ごとに作る8バイトのnonceです。全体にはSHA-256のチェック値を付け、壊れていれば鍵を作り直します。SAESが使えないときは平文の形式`U2K1`で保存し、あとでSAESが使えるようになると起動時に`U2K2`へ保存し直します。DHUKは隠蔽レベル(HDPL)やセキュリティ状態ごとに異なるため、暗号化と復号は必ずHDPL1(起動直後)で行い、そのあとでHDPLを2に上げます。

### カウンタ

カウンタは認証のたびに増えるため、8KBのセクタ2面を追記ログとして使います。1エントリは16バイト(Flashの書き込み単位)で、値とその反転値を持ち、有効な最大値が現在のカウンタです。書き込み中のセクタがいっぱいになったら、もう一方を消去してそこへ書きます。消去中に電源が切れても古い面に最大値が残るので、カウンタの値が戻ることはありません。

## 制限と注意

| 項目 | 内容 |
|---|---|
| 認定 | FIDO Allianceの認定を受けていない実験的な実装です。重要なアカウントの唯一の2段階認証手段にはしないでください |
| 鍵の保護 | 鍵はDHUKで暗号化して保存し、HDPを設定すれば起動後は読めません。ただしproduct stateがOpenのままだと、ST-LINKで起動直後に止めればRAM上の鍵を読めます。普段使いのキーや他人に渡すボードは、TrustZone構成でTZ-Closedにしてください |
| TZ-Closed | 閉じるとファームウェアの更新もデバッグもできません。戻すには全消去が必要で、DAの秘密鍵と証明書をなくすと戻せません |
| 改ざん検知 | TAMP(改ざん検知)は使っていません |
| 鍵の移動 | Flashの内容を別のボードに書き写しても使えません(別のボードでは新しい鍵が作られます) |
| 構成の切り替え | `make TZ=1`と通常の構成を切り替えると、保存済みの鍵は使えなくなり、新しい鍵が作られます |
| 証明書 | 自己署名です。認定済みの認証器を求めるサイト(一部の企業向けサービスなど)では使えません |
| 対応規格 | U2F(CTAP1)のみです。FIDO2、パスキー、PINには対応しません |
| 本人確認 | B1ボタンのみです。キーボードのキーでは確認しません |
| USB ID | VID `0xCAFE`は開発用の仮の値です |
| 動作確認 | NUCLEO-H533REとWindows 11で、`u2f_hw_test.py`の全項目、webauthn.ioでの登録とログイン、B1による初期化と取り消し、再起動後のカウンタの保持、TrustZone構成をTZ-Closedにした状態での`u2f_hw_test.py`の全項目を確かめています |
