# device/wiznet

WIZnetのEthernetチップ(W5100S、W5500)のドライバと、その上に載るソケットAPI、プロトコルサービス、MCPサーバ、WebAssemblyのラッパです。チップに依存するのは`sysdepend/`の下だけで、ソケットAPIから上はW5100SとW5500で共通です。

使い方は[docs/guide/network.md](../../docs/guide/network.md)、MCPサーバは[docs/guide/mcp.md](../../docs/guide/mcp.md)、wasmは[docs/guide/wasm.md](../../docs/guide/wasm.md)を参照してください。

## ファイル構成

| ファイル | 内容 |
| --- | --- |
| `wiznet_drv.h` | チップの選択と、チップに依存しないレジスタ操作の宣言 |
| `wiznet_sock.h` | ソケットのイベントフラグの定義 |
| `wiznet_dev.c` | デバイスドライバ`"neta"`の登録、チップの初期化、DHCPの自動開始 |
| `tk_socket.c` / `.h` | ソケットAPI(`tk_sock_*`) |
| `tk_dhcp.c` / `.h` | DHCPクライアント |
| `tk_dns.c` / `.h` | DNSリゾルバ(Aレコード) |
| `tk_sntp.c` / `.h` | SNTPクライアント |
| `tk_mqtt.c` / `.h` | MQTT 3.1.1クライアント |
| `tk_httpd.c` / `.h` | HTTPサーバ(HTTP/1.0のGET) |
| `tk_netmon.c` / `.h` | リンク監視 |
| `tk_sha256.c` / `.h` | SHA-256とHMAC-SHA256 |
| `tk_mcp_core.c`、`tk_mcp_tools.c`、`tk_mcp_vm.c`、`tk_mcp.h`、`tk_mcp_int.h`、`tk_mcp_hal.h` | MCPサーバ(MQTT上のJSON-RPC)、ツール、GPIOなどのプラットフォーム別の操作 |
| `tk_wasm.c` / `.h`、`tk_wasm_host.c`、`tk_wasm_mcp.c` | wasm3のラッパ、wasmから呼べる関数、wasm用のMCPツール |
| `sysdepend/w5100s/` | W5100Sのレジスタ定義(`w5100s_reg.h`)、SPIフレームとバッファ操作(`w5100s_io.c`)、初期化(`w5100s_chip.c`)、RP2040のSPIと割り込み(`w5100s_spi.c` / `.h`) |
| `sysdepend/w5500/` | W5500のレジスタ定義(`w5500_reg.h`)、SPIフレームとバッファ操作(`w5500_io.c`)、初期化とPHY(`w5500_chip.c`)、STM32H5のSPIと割り込み(`w5500_spi_stm32.c`)、RP2040のSPIと割り込み(`w5500_spi_rp2040.c` / `.h`) |

WIZnetのioLibraryは使っていません。必要なレジスタ定義と操作は`sysdepend/`の中に持っています。

## チップの選択

ビルド時に`WIZCHIP_W5100S`か`WIZCHIP_W5500`のどちらかを定義します。`wiznet_drv.h`がそれを見て、レジスタ定義とソケット数(W5100Sは4、W5500は8)を切り替えます。

| ビルド | チップ | 定義している場所 | SPIの制御 |
| --- | --- | --- | --- |
| RP2040(`build_make`、`NET=1`) | W5100S | `build_make/mtkernel_3/device/wiznet/subdir.mk` | `sysdepend/w5100s/w5100s_spi.c` |
| NUCLEO-H533RE(`build_bsp2`、`NET=1`) | W5500 | `build_bsp2/Makefile` | `sysdepend/w5500/w5500_spi_stm32.c` |

`sysdepend/w5500/w5500_spi_rp2040.c`はRP2040とW5500(WIZ550ioなど)の組み合わせ用ですが、これを使うビルドのターゲットはまだありません。

どのチップでも、割り込みハンドラはイベントフラグをセットするだけにして、チップのレジスタは専用のタスクが操作します。SPIのアクセスはFastLockで排他します。

## Pico WのWiFiで使う場合

Pico W(`TARGET=_PICO_W_ WIFI=1`)では、このディレクトリのうち`tk_mqtt.c`、`tk_mcp_*.c`、`tk_sha256.c`、`tk_wasm*.c`だけをビルドします(`build_make/mtkernel_3/device/wiznet/subdir_wifi.mk`)。ソケットAPIは`device/wifi/tk_socket_lwip.c`がlwIPの上に同じ関数名で用意し、DHCPとDNSはlwIPが受け持ちます。

## device/w5100s/ について

`device/w5100s/`は、W5100Sだけに対応していた以前のドライバです。現在のビルドはどれもこのディレクトリ(`device/wiznet/`)を使い、`device/w5100s/`はビルドに含まれません。
