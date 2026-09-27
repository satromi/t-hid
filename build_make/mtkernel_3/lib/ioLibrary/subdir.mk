################################################################################
# Wiznet ioLibrary_Driver (W5100S) — ヘッダ参照のみ
#
# 全 .c ファイルは device/w5100s/ の独自実装で完全置換済み:
#   wizchip_conf.c → w5100s_chip.c
#   socket.c       → tk_socket.c
#   dhcp.c         → tk_dhcp.c
#   w5100s.c       → w5100s_io.c
#
# ioLibrary から使用するのは w5100s.h / wizchip_conf.h のレジスタ定義のみ。
################################################################################

IOLIB_DIR := ../lib/ioLibrary/src/Ethernet
IOLIB_INCPATH := -I"$(IOLIB_DIR)" -I"$(IOLIB_DIR)/W5100S"

# ビルド対象のオブジェクトなし (ヘッダのみ参照)
