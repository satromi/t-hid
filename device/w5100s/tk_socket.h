/*
 *----------------------------------------------------------------------
 *    W5100S μT-Kernel Native Socket API
 *
 *    ioLibrary socket.c の RTOS ネイティブ置き換え。
 *    全ビジーウェイトを割り込み + tk_wai_flg() に変換。
 *    W5100S のレジスタアクセスには ioLibrary の w5100s.h マクロを使用。
 *----------------------------------------------------------------------
 */

#ifndef __TK_SOCKET_H__
#define __TK_SOCKET_H__

#include <tk/tkernel.h>

/*----------------------------------------------------------------------
 * ソケット API (μT-Kernel ネイティブ)
 *
 * 全 API がブロッキング動作。待機中は tk_wai_flg() でタスクがスリープし、
 * W5100S 割り込み → 処理タスク → ソケットイベントフラグで起床される。
 *
 * 戻り値規約:
 *   ER 戻り (open/close/listen/accept/connect/disconnect): E_OK or 負値エラー
 *   W 戻り  (send/recv/sendto/recvfrom): 正値=転送バイト数, 0=切断, 負値=エラー
 */

/* ソケットオープン (TCP/UDP) */
ER tk_sock_open(UB sn, UB protocol, UH port);

/* ソケットクローズ */
ER tk_sock_close(UB sn);

/* TCP リッスン (接続受付待ちは tk_sock_accept で行う) */
ER tk_sock_listen(UB sn);

/* TCP 接続受付待ち (ESTABLISHED まで割り込みで待つ) */
ER tk_sock_accept(UB sn, TMO tmout);

/* TCP 接続 (ESTABLISHED まで割り込みで待つ) */
ER tk_sock_connect(UB sn, UB *ip, UH port, TMO tmout);

/* TCP 切断 */
ER tk_sock_disconnect(UB sn, TMO tmout);

/* データ送信 (TX バッファ空きを割り込みで待つ) */
W tk_sock_send(UB sn, const UB *buf, UH len, TMO tmout);

/* データ受信 (RX データ到着を割り込みで待つ) */
W tk_sock_recv(UB sn, UB *buf, UH len, TMO tmout);

/* UDP データグラム送信 (宛先アドレス・ポート指定) */
W tk_sock_sendto(UB sn, const UB *buf, UH len, UB *addr, UH port, TMO tmout);

/* UDP データグラム受信 (送信元アドレス・ポートを取得) */
W tk_sock_recvfrom(UB sn, UB *buf, UH len, UB *addr, UH *port, TMO tmout);

/*
 * TCP キープアライブ設定
 *   sn:  ソケット番号 (TCP ESTABLISHED 状態であること)
 *   sec: キープアライブ間隔 (秒, 5秒単位に丸める, 0=無効)
 *
 * W5100S の Sn_KPALVTR レジスタを使用 (HW キープアライブ)。
 * ソフトウェアタイマー不要で CPU 負荷ゼロ。
 */
ER tk_sock_set_keepalive(UB sn, UH sec);

/*----------------------------------------------------------------------
 * Phase 1: T2EX 仕様準拠 API (A-1, A-2, A-3, B-4)
 */

/* A-1: 接続先アドレス/ポート取得 (so_getpeername 相当) */
ER tk_sock_getpeer(UB sn, UB *ip, UH *port);

/* A-1: ローカルアドレス/ポート取得 (so_getsockname 相当) */
ER tk_sock_getlocal(UB sn, UB *ip, UH *port);

/*
 * A-2: ソケット操作中止 (so_break 相当)
 *   ブロック中の tk_sock_recv/send/connect/accept を強制中止。
 *   指定ソケットのイベントフラグに BREAK を設定し、
 *   tk_wai_flg() で待機中のタスクを E_ABORT で起床させる。
 */
ER tk_sock_break(UB sn);

/* A-3: ホスト名設定/取得 (so_sethostname/so_gethostname 相当) */
ER tk_net_sethostname(const char *name, INT len);
ER tk_net_gethostname(char *name, INT len);

/* B-4: 受信可能バイト数取得 (so_ioctl FIONREAD 相当) */
W tk_sock_available(UB sn);

/*----------------------------------------------------------------------
 * Phase 2: T2EX 仕様準拠 API (B-1, B-2)
 */

/*
 * B-1: 簡易 I/O 多重化 (so_select 相当)
 *   sn_mask:  監視対象ソケットのビットマスク (bit0=sn0, bit1=sn1, ...)
 *   evt_mask: 待つイベント (W5100S_SOCK_EVT_RECV | _CON | _SENDOK 等)
 *   result:   発生したイベント (ソケット番号 << 8 | イベント)
 *   tmout:    タイムアウト (TMO_FEVR = 永久待ち, 0 = ポーリング)
 *
 *   戻り値: E_OK=イベント発生, E_TMOUT=タイムアウト
 */
ER tk_sock_select(UB sn_mask, UINT evt_mask, UINT *result, TMO tmout);

/*
 * B-2: ソケットオプション取得/設定 (so_getsockopt/so_setsockopt 相当)
 *
 * W5100S でサポート可能なオプションのみ。
 * optname: TK_SO_xxx 定数
 */
#define TK_SO_KEEPALIVE		1	/* キープアライブ間隔 (秒) */
#define TK_SO_RCVTIMEO		2	/* 受信タイムアウト (ms) */
#define TK_SO_SNDTIMEO		3	/* 送信タイムアウト (ms) */
#define TK_SO_ERROR		4	/* 保留中エラー (R/O) */
#define TK_SO_TYPE		5	/* ソケットタイプ (R/O) */
#define TK_SO_RCVBUF		6	/* 受信バッファサイズ (R/O) */
#define TK_SO_SNDBUF		7	/* 送信バッファサイズ (R/O) */

ER tk_sock_getopt(UB sn, INT optname, void *optval, INT *optlen);
ER tk_sock_setopt(UB sn, INT optname, const void *optval, INT optlen);

/* select 用統合イベントフラグ (w5100s_task から通知される) */
extern ID select_flgid;

/* ソケット初期化 (dev_init_w5100s から呼ばれる) */
void tk_sock_init(void);

#endif /* __TK_SOCKET_H__ */
