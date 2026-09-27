# TCP/IPネットワーク

t-hidは、μT-Kernel 3.0の上でTCP/IPの通信ができます。有線Ethernet(WIZnet W5100S / W5500)と、Raspberry Pi Pico WのWiFiに対応していて、アプリからはどちらも同じソケットAPIで扱えます。この上に、DHCP、DNS、SNTP、MQTT、HTTPサーバの各サービスと、MCPサーバ([mcp.md](mcp.md))が載ります。

## 対応ハードウェア

| 方式 | チップ | ボード | ソケット数 | TCP/IPの処理 |
| --- | --- | --- | --- | --- |
| 有線Ethernet | W5100S | W5100S-EVB-Pico(RP2040) | 4 | チップ内蔵のハードウェアTCP/IP |
| 有線Ethernet | W5500 | NUCLEO-H533RE+WIZ550io | 8 | チップ内蔵のハードウェアTCP/IP |
| WiFi | CYW43439 | Raspberry Pi Pico W | 8 | lwIP(ソフトウェア) |

W5100SとW5500はTCP/IPをチップの中で処理するので、マイコン側の負担が小さく、RAMの少ないRP2040でも複数のサービスを同時に動かせます。

### ピン割当

W5100S-EVB-Pico(RP2040)は次のピンを使います。SPIクロックは6.25MHzです。

| 信号 | ピン |
| --- | --- |
| SPI0 MISO / CS / SCK / MOSI | GP16 / GP17 / GP18 / GP19 |
| RST | GP20 |
| INT | GP21 |

GP16〜21はTL Splitのキーマトリクスと6本重なり、GP21はW5100Sの割り込み入力として登録されるので、その列が反応しなくなります。キーボード基板では`NET=1`にしないでください。ネットワークの機能はW5100S-EVB-Pico単体で使う構成向けです。

NUCLEO-H533RE+WIZ550io(W5500)は次のようにつなぎます。SPIクロックは7.8MHzです。

| 信号 | NUCLEOのピン | WIZ550ioのピン |
| --- | --- | --- |
| SCK | D13(PA5) | 6番 |
| MISO | D12(PA6) | 4番 |
| MOSI | D11(PA7) | 3番 |
| CS | D10(PB6) | 5番 |
| INT | D9(PC7) | 7番 |
| RST | D8(PA9) | 8番 |
| 3.3V / GND | 3V3 / GND | 10番 / 1・2番 |

NUCLEOのユーザーLED(PA5)はSCKと共用になるため使えません。

Pico WのCYW43439はボード上で配線済みなので、追加の配線はいりません。

## 構成

```text
┌─────────────────────────────────────────────────────────────┐
│ アプリ / MCPサーバ / wasm                                   │
├─────────────────────────────────────────────────────────────┤
│ プロトコルサービス                                          │
│   DHCP  DNS  SNTP  MQTT  HTTPサーバ  リンク監視  SHA-256    │
├─────────────────────────────────────────────────────────────┤
│ ソケットAPI (tk_sock_*)                                     │
├──────────────────────────────┬──────────────────────────────┤
│ WIZnetドライバ               │ lwIP版ソケットAPI (Pico W)   │
│   W5100S / W5500の共通層     │   tk_socket_lwip.c           │
│   SPIと割り込み(チップ別)    │   CYW43 WiFiドライバ         │
└──────────────────────────────┴──────────────────────────────┘
```

| ディレクトリ | 内容 |
| --- | --- |
| `device/wiznet/` | ソケットAPI、プロトコルサービス、MCPサーバ、W5100S / W5500の共通層(`wiznet_drv.h`) |
| `device/wiznet/sysdepend/w5100s/` | W5100SのSPIフレームとRP2040のSPI制御 |
| `device/wiznet/sysdepend/w5500/` | W5500のSPIフレームとSTM32H5のSPI制御 |
| `device/wifi/` | Pico WのWiFi接続の管理、lwIP版のソケットAPI、接続情報のFlash保存 |

ファイル単位の説明は[device/wiznet/README.md](../../device/wiznet/README.md)にあります。

Pico Wでは、DHCPとDNSの処理をlwIPが受け持ちます。`device/wiznet/`からはMQTT、MCPサーバ、SHA-256、wasmだけを使い、DHCP、SNTP、HTTPサーバ、リンク監視は入りません。

RTOSとの関係は次のとおりです。

- 割り込みハンドラはイベントフラグをセットするだけで、チップのレジスタは専用のタスクが操作します。
- ソケットAPIの待ちはすべて`tk_wai_flg`による待機で、ビジーウェイトはしません。通信を待つ間も、キーボードのスキャンなど他のタスクはそのまま動きます。
- チップへのアクセスはFastLockで排他しているので、別々のソケットであれば複数のタスクから同時に使えます。1つのソケットを複数のタスクで共有することはできません。

## ビルドと起動

ビルドの全体の手順は[build_and_deploy.md](../build_and_deploy.md)にまとめています。ここではネットワークに関係するところだけを書きます。

### W5100S-EVB-Pico

```bash
cd mtk3_bsp/build_make
make TARGET=_PICO_RP2040_ clean
make TARGET=_PICO_RP2040_ NET=1 KEYMAP=default all
```

`NET=1`でネットワークとサービス一式が入ります。`MCP=1`でMCPサーバが、`WASM=1`でさらにwasmが加わります。`MCP=1`は`NET=1`を、`WASM=1`は`MCP=1`を含みます。

### NUCLEO-H533RE

`build_bsp2`の既定(`make`)でネットワークが有効です。`make NET=0`で外せます。`NET=0`にするとMCPサーバとwasmも無効になります。

### Pico W(WiFi)

接続先のSSIDとパスワードは、リポジトリ直下の`.wifi_config`に書きます。このファイルは`.gitignore`済みで、コミットされません。

```bash
cp .wifi_config.example .wifi_config
```

```text
ssid=MyNetwork
psk=my-password
# open / wpa2 / wpa3 (省略時はwpa2、pskが空ならopen)
auth=wpa2
```

`#`で始まる行はコメントですが、行末にコメントは書けません(`#`以降も値として扱われます)。SSIDは1〜32バイト、パスワードは8〜63文字です。

```bash
bash setup.sh
cd mtk3_bsp/build_make
make TARGET=_PICO_W_ clean
make TARGET=_PICO_W_ WIFI=1 all
```

ビルドのときに`.wifi_config`からヘッダ(`wifi_config_embedded.h`)を作り、ファームウェアに埋め込みます。`.wifi_config`を書き換えたら、`setup.sh`からやり直してください。`WIFI=1`のビルドには、WiFi、MCPサーバ、wasmに加えてBLEキーボードも入ります。BLEとの併用や`BLE=0`は[ble.md](ble.md)を参照してください。

起動時は、次の順に接続情報を探します。

1. Flashに保存された値。MCPの`wifi_set_credentials`で保存でき、次に起動したときから使われます
2. `.wifi_config`から埋め込んだ値
3. どちらもなければ、シリアルに`WiFi: no credentials`と出してWiFiを使いません

認証はWPA2-PSK、WPA3-PSK(WPA2との混在モード)、オープンに対応しています。接続が切れると、5秒から始めて最大60秒の間隔で自動的につなぎ直します。

### 起動時の動き

W5100S / W5500の構成では、ドライバの初期化(`dev_init_wiznet(0)`)でDHCPが自動的に始まります(最後のソケットを使います)。アドレスが取れるまでは、固定の既定値(192.168.0.100/24、ゲートウェイ192.168.0.1)で動きます。

W5100S-EVB-Picoのサンプル(`app_program/app_main.c`)では、シリアルコンソールに次のようなログが出ます。

```text
IP=192.168.1.23 GW=192.168.1.1 DNS=192.168.1.1
DNS: www.google.com = 142.250.x.x
SNTP: 2026-09-23 12:34:56 (JST)
NETMON: link=UP
HTTPD: port 80
```

## ソケットAPI

ヘッダは`device/wiznet/tk_socket.h`です。BSDソケットと違い、ソケットは番号(0〜N-1)で固定的に指定します。番号の範囲は、W5100Sが0〜3、W5500とPico Wが0〜7です。

### 基本

| 関数 | 説明 |
| --- | --- |
| `ER tk_sock_open(UB sn, UB protocol, UH port)` | ソケットを開く。`protocol`は`TK_PROTO_TCP` / `TK_PROTO_UDP`。`port`が0なら自動 |
| `ER tk_sock_close(UB sn)` | 閉じる |
| `ER tk_sock_connect(UB sn, UB *ip, UH port, TMO tmout)` | TCPで接続する(クライアント) |
| `ER tk_sock_listen(UB sn)` | TCPの待ち受けを始める |
| `ER tk_sock_accept(UB sn, TMO tmout)` | 接続が来るまで待つ。待ち受けたソケットがそのまま接続になる |
| `ER tk_sock_disconnect(UB sn, TMO tmout)` | TCPを正常に切断する |
| `W tk_sock_send(UB sn, const UB *buf, UH len, TMO tmout)` | 送信。戻り値は送ったバイト数 |
| `W tk_sock_recv(UB sn, UB *buf, UH len, TMO tmout)` | 受信。戻り値は受け取ったバイト数で、0は相手が切断したことを表す |
| `W tk_sock_sendto(UB sn, const UB *buf, UH len, UB *addr, UH port, TMO tmout)` | UDPで送信する |
| `W tk_sock_recvfrom(UB sn, UB *buf, UH len, UB *addr, UH *port, TMO tmout)` | UDPで受信する |

`TMO`はミリ秒です。`TMO_FEVR`で無期限に待ち、`0`なら待たずに戻ります。失敗したときは負のエラーコードを返します(`E_TMOUT`はタイムアウト、`E_IO`は予期しない切断など)。

### 補助

| 関数 | 説明 |
| --- | --- |
| `W tk_sock_available(UB sn)` | 受信済みで読めるバイト数 |
| `ER tk_sock_select(UB sn_mask, UINT evt_mask, UINT *result, TMO tmout)` | 複数のソケットのイベントを待つ |
| `ER tk_sock_set_keepalive(UB sn, UH sec)` | TCPのキープアライブ(0で無効)。W5100S / W5500では5秒単位に丸める |
| `ER tk_sock_getpeer(UB sn, UB *ip, UH *port)` / `tk_sock_getlocal` | 相手 / 自分のアドレス |
| `ER tk_sock_break(UB sn)` | 他のタスクがそのソケットで待っているのを中断させる(`E_ABORT`で戻る) |
| `ER tk_sock_getopt` / `tk_sock_setopt` | 受信・送信のタイムアウトなどのオプション |
| `ER tk_net_sethostname` / `tk_net_gethostname` | ホスト名 |

### TCPクライアントの例

```c
UB server[4] = {192, 168, 1, 10};
UB buf[256];

tk_sock_open(1, TK_PROTO_TCP, 0);
if (tk_sock_connect(1, server, 8080, 5000) == E_OK) {
    tk_sock_send(1, (const UB *)"hello\n", 6, 1000);
    W n = tk_sock_recv(1, buf, sizeof(buf), 3000);   /* n > 0なら受信、0なら切断 */
    tk_sock_disconnect(1, 1000);
}
tk_sock_close(1);
```

### TCPサーバの例(1接続ずつ)

```c
tk_sock_open(2, TK_PROTO_TCP, 5000);
tk_sock_listen(2);
if (tk_sock_accept(2, TMO_FEVR) == E_OK) {
    /* ソケット2がそのまま接続になる */
    ...
    tk_sock_disconnect(2, 1000);
}
tk_sock_close(2);
```

## プロトコルサービス

どれも`device/wiznet/`にあります。使うソケットの番号は呼び出し側が割り当てます。DHCP、DNS(`tk_dns_resolve`)、SNTP、HTTPサーバ、リンク監視はW5100S / W5500の構成で使えます。

### DHCP(`tk_dhcp.h`)

| 関数 | 説明 |
| --- | --- |
| `ER tk_dhcp_start(UB sn, const T_DHCP_CB *cb, T_DHCP_INFO *info)` | 開始する。すぐに戻り、裏でタスクが動く |
| `INT tk_dhcp_get_state(void)` | 状態。`TK_DHCP_ST_LEASED`ならアドレスを取得済み |
| `void tk_dhcp_get_info(T_DHCP_INFO *info)` | IPアドレス、サブネットマスク、ゲートウェイ、DNSサーバ、リース時間 |
| `void tk_dhcp_stop(void)` | 止める |

リースの更新も自動で行います。W5100S / W5500の構成ではドライバの初期化で自動的に始まるので、アプリは状態が`TK_DHCP_ST_LEASED`になるのを待つだけで済みます。

### DNS(`tk_dns.h`)

```c
ER tk_dns_resolve(UB sn, const UB *dns_ip, const char *hostname, UB *result, TMO tmout);
```

Aレコード(IPv4)を引きます。DNSサーバには、ふつうDHCPで得たアドレスを使います。Pico Wでは`tk_wifi_dns_resolve(hostname, result, tmout)`(`device/wifi/tk_wifi.h`)を使います。

### SNTP(`tk_sntp.h`)

```c
ER tk_sntp_sync(UB sn, const UB *ntp_ip, TMO tmout);   /* 時刻を取得してシステム時刻に設定する */
UW tk_sntp_get_unixtime(void);
```

日時の表示には`kernel/extension/datetime`(`dt_settimezone`、`dt_gettime`、`dt_format`)を使います。

### MQTTクライアント(`tk_mqtt.h`)

| 関数 | 説明 |
| --- | --- |
| `ER tk_mqtt_start(const T_MQTT_CONF *conf, FP_MQTT_RECV on_recv)` | 接続を始める(非同期) |
| `ER tk_mqtt_publish(const char *topic, const UB *payload, UH len)` | 送信(QoS 0) |
| `ER tk_mqtt_subscribe(const char *topic, UB qos)` | 購読 |
| `BOOL tk_mqtt_is_connected(void)` | 接続中かどうか |
| `void tk_mqtt_stop(void)` | 切断する |

MQTT 3.1.1で、ユーザー名とパスワードによる認証、キープアライブに対応しています。受け取ったメッセージは`on_recv`に届きます(トピックは63文字、ペイロードは512バイトまで)。送信はQoS 0だけです。自動の再接続はしないので、必要ならアプリ側でつなぎ直します(MCPサーバは自分で再接続します)。

### HTTPサーバ(`tk_httpd.h`)

```c
LOCAL ER http_handler(T_HTTP_REQ *req)
{
    if (strcmp(req->path, "/status") == 0) {
        SYSTIM t;
        tk_get_otm(&t);                         /* 起動からの経過ms */
        req->resp_len = snprintf((char *)req->resp_buf, req->resp_max,
                                 "{\"uptime_ms\":%lu}", (unsigned long)t.lo);
        req->content_type = "application/json";
        return E_OK;
    }
    return E_NOEXS;             /* 404 */
}

tk_httpd_start(2, 80, http_handler);
```

HTTP/1.0のGETだけを、1接続ずつ処理します。応答の本体は512バイトまでです。

### リンク監視(`tk_netmon.h`)

```c
ER   tk_netmon_start(UB dhcp_sn, FP_LINK_CHANGE on_change);   /* 2秒ごとに監視する */
BOOL tk_netmon_is_link_up(void);
```

ケーブルが抜けて戻ったときに、DHCPをやり直します。

### SHA-256(`tk_sha256.h`)

`sha256_init` / `sha256_update` / `sha256_final`と`hmac_sha256`があります。MCPの認証やwasmモジュールの検証に使っています。Pico Wでも使えます。

## デバイスドライバとして使う(neta)

W5100S / W5500は、μT-Kernelのデバイス`"neta"`としても登録されます(`device/include/dev_wiznet.h`)。`tk_opn_dev` / `tk_rea_dev` / `tk_wri_dev`で扱えます。

| 操作 | 内容 |
| --- | --- |
| `tk_rea_dev(dd, sn, buf, size, ...)` | ソケット`sn`から受信する |
| `tk_wri_dev(dd, sn, buf, size, ...)` | ソケット`sn`へ送信する |
| 属性`TDN_NET_IP` / `MASK` / `GW` / `MAC` | アドレスの読み書き |
| 属性`TDN_NET_STATUS` | リンクの状態(読み出し) |
| 属性`TDN_SOC_CONNECT + sn` | TCPで接続する(`T_NET_ADDR`を書く) |
| 属性`TDN_SOC_LISTEN + sn` | 待ち受けて、接続を受け入れる |
| 属性`TDN_SOC_UDP + sn` / `TDN_SOC_CLOSE + sn` | UDPを開く / 閉じる |
| 属性`TDN_SOC_STATUS + sn` | ソケットの状態 |

## アプリの書き方

`app_program/app_main.c`(W5100S-EVB-Pico用)が、ひととおりのサービスを使う例です。要点は次のとおりです。

```c
EXPORT INT usermain(void)
{
    T_DHCP_INFO dhcp;

    kb_start();                       /* キーボードを先に起動する (DHCP待ちで止めない) */

    /* DHCPの取得を最大30秒待つ */
    for (INT i = 0; i < 30 && tk_dhcp_get_state() != TK_DHCP_ST_LEASED; i++) {
        tk_dly_tsk(1000);
    }
    tk_dhcp_get_info(&dhcp);

    /* 時刻合わせ */
    UB ntp[4];
    if (tk_dns_resolve(1, dhcp.dns, "ntp.nict.jp", ntp, 5000) == E_OK) {
        dt_settimezone(9 * 60);
        tk_sntp_sync(1, ntp, 5000);
    }

    tk_netmon_start(3, on_link_change);        /* ソケット3 = DHCP */
    tk_httpd_start(2, 80, http_handler);       /* ソケット2 = HTTP */
    ...
}
```

サンプルでのソケットの割り当ては次のとおりです。

| ソケット | W5100S-EVB-Pico(`app_program`) | NUCLEO-H533RE(`app_h533`) | Pico W(`app_wifi_program`) |
| --- | --- | --- | --- |
| 0 | MCP(MQTT) | MCP(MQTT) | MCP(MQTT) |
| 1 | DNS / SNTP(一時的に使う) | DNS / SNTP(一時的に使う) | - |
| 2 | HTTPサーバ | - | - |
| 3 | DHCP(自動) | - | - |
| 7 | - | DHCP(自動) | - |

MCPサーバが使うMQTTブローカーは、どのサンプルも`test.mosquitto.org`の1883番です。Pico Wでは、`EXTRA_DEFS`で`MQTT_BROKER_HOST`と`MQTT_BROKER_PORT`を指定すると変えられます([mcp.md](mcp.md))。

ネットワークを使うタスクのスタックは、4KB程度を確保してください。

## 制限

- ソケットは番号で固定です。BSDの`socket()`のような動的な割り当てはないので、アプリの中で番号の使い道を決めておきます。
- `tk_sock_accept`は、待ち受けたソケット自身が接続になります。バックログはないので、同時に複数の接続を受けるにはソケットを複数使います。
- DNSはAレコードだけです。逆引きとAAAAはなく、IPv6には対応していません。
- MQTTの送信はQoS 0だけで、TLSもありません。到達の保証が必要な用途には向きません。
- HTTPサーバはGETだけで、1接続ずつ、応答は512バイトまでです。
- W5100S / W5500のドライバは、全ボード共通の固定のMACアドレス(`00:08:DC:12:34:56`)を設定します。同じネットワークに複数台つなぐときは、`device/wiznet/wiznet_dev.c`の値を台ごとに変えてください。
- Pico Wでは`tk_sock_select` / `tk_sock_getopt` / `tk_sock_setopt`に対応していません(`E_NOSPT`を返します)。
- W5500(NUCLEO-H533RE+WIZ550io)は、実機での動作をまだ確かめていません。
