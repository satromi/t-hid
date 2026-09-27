# ライセンスと第三者コード

t-hidのうち、独自の著作権表示を持たないファイルは[MIT License](LICENSE)で配布します。次に挙げるファイルとサブモジュールは、それぞれの著作権者のライセンスに従います。

## このリポジトリに含まれる第三者由来のコード

### μT-Kernel 3.0(T-License)

Copyright (C) Ken Sakamura. TRON Forum(<https://www.tron.org/>)が公開しています。

μT-Kernel 3.0 BSPのファイルを変更して上書きするための次のファイルは、各ファイルのヘッダに記載されたT-Licenseに従います。

- `bsp_overlay/`のKen Sakamuraの著作権表示があるファイル(サブモジュール`mtk3_bsp`の`pico_rp2040`ブランチに上書きする、RP2040とNUCLEO-H723向けの変更)
- `bsp2_overlay/`のすべてのファイル
- `tz_h533/nonsecure/kernel/`のうち`task_sysdep.c`以外のファイル(TrustZone構成で差し替えるμT-Kernelのファイル)
- `device/common/drvif/msdrvif.c`, `device/common/drvif/msdrvif.h`

T-License 2.2の本文は、サブモジュールの中の`mtk3_bsp2/mtkernel/docs/TEF000-219-200401.pdf`にあります(TRON ForumのWebサイトでも公開されています)。

### Raspberry Pi pico-sdk(BSD-3-Clause)

Pico WのCYW43439(WiFi / Bluetoothチップ)を動かすための次のファイルは、pico-sdkのコードをμT-Kernel向けに書き換えたもので、BSD-3-Clauseに従います。

- `device/ble_hid/cybt_logging.h`
- `device/ble_hid/cybt_shared_bus.c`
- `device/ble_hid/cybt_shared_bus_driver.c`, `device/ble_hid/cybt_shared_bus_driver.h`
- `device/ble_hid/cyw43_configport.h`
- `device/ble_hid/hci_transport_cyw43.c`, `device/ble_hid/hci_transport_cyw43.h`
- `device/ble_hid/sysdepend/rp2040/cyw43_arch_tkernel.c`
- `device/ble_hid/sysdepend/rp2040/cyw43_spi_pio.c`, `device/ble_hid/sysdepend/rp2040/cyw43_spi_pio.h`

```text
Copyright (c) 2020-2023 Raspberry Pi (Trading) Ltd.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors
   may be used to endorse or promote products derived from this software without
   specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

### HM01B0カメラの設定(BSD-2-Clause)

`app_presence/hm01b0.c`のセンサのレジスタ設定とフレームの長さの計算は、PicoHM01B0(<https://github.com/pmarques-dev/PicoHM01B0>)を元にしており、BSD-2-Clauseに従います。

```text
BSD 2-Clause License

Copyright (c) 2024, pmarques-dev

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

### BTstackへの変更

`patches/btstack_hci_run_after_acl.patch`はBTstack(`lib/btstack`)への変更で、BTstackのライセンスに従います。

## サブモジュール

サブモジュールはこのリポジトリには含まれず、各プロジェクトから取得します。ライセンス本文は、各サブモジュールのLICENSEファイルを参照してください。

| サブモジュール | ライセンス | 注意 |
| --- | --- | --- |
| `mtk3_bsp`, `mtk3_bsp2`(μT-Kernel 3.0 BSP) | T-License 2.1 / 2.2(ファイルごとに記載) | |
| `lib/wasm3` | MIT | |
| `lib/cjson` | MIT | |
| `lib/micro-ecc` | BSD-2-Clause | |
| `lib/ioLibrary`(WIZnet) | MIT | |
| `lib/btstack`(BlueKitchen) | 独自ライセンス | 非商用の個人利用のみ。商用利用にはBlueKitchenの商用ライセンスが必要 |
| `lib/cyw43-driver`(George Robotics) | 独自ライセンス | 非商用のみ、またはRaspberry Pi製チップ(RP2040など)での利用に限る |
| `lib/pico-tflmicro`(TensorFlow Lite MicroのRP2040版。CMSIS、CMSIS-NN、FlatBuffers、gemmlowp、ruyとperson detectionのモデルを含む) | Apache-2.0(ファイルごとに記載) | |

## ファームウェアを配布する場合

ビルドしたファームウェア(`.uf2` / `.bin`)を配布する場合は、組み込まれたコードのライセンス条件に従ってください。

- BSD-3-Clause(pico-sdk由来のコード)、BSD-2-Clause(micro-ecc)、MIT(wasm3、cJSONなど)は、ドキュメントなどに著作権表示とライセンス文を含める必要があります。
- Pico WのBLE機能を含むファームウェアにはBTstackが入ります。BTstackは非商用の個人利用に限られるため、商用製品には使えません。
- Pico Wのファームウェアにはcyw43-driverとCYW43439のファームウェアが入ります。Raspberry Pi製チップの上での利用に限って再配布できます。
- Pico WのWiFi構成にはlwIP(`lib/btstack/3rd-party/lwip`、BSD-3-Clause)が入ります。
- Pico4MLの離席検出(`presence`)にはTensorFlow Lite Microとperson detectionのモデル(Apache-2.0)が入ります。配布物にライセンス文を含めてください。

## 商標

- TRON、μT-KernelはTRON Forumの登録商標または商標です。
- FIDOはFIDO Allianceの商標です。本リポジトリのU2F実装は、FIDOの認定を受けていない実験的な実装です。
- Raspberry Pi、PicoはRaspberry Pi Ltdの商標です。
- STM32、NUCLEOはSTMicroelectronicsの商標です。
