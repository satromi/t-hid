################################################################################
# lwIP Lightweight IP Stack — μT-Kernel + Pico W (CYW43439) 用
#
# NO_SYS=1 (ポーリングモード) で最小構成のみビルド
# ソース: lib/btstack/3rd-party/lwip/core/src/
################################################################################

LWIP_DIR := ../lib/btstack/3rd-party/lwip/core/src
LWIP_INCPATH := -I"$(LWIP_DIR)/include" -I"../device/wifi"

LWIP_OBJS = \
./mtkernel_3/lib/lwip/init.o \
./mtkernel_3/lib/lwip/def.o \
./mtkernel_3/lib/lwip/dns.o \
./mtkernel_3/lib/lwip/inet_chksum.o \
./mtkernel_3/lib/lwip/ip.o \
./mtkernel_3/lib/lwip/mem.o \
./mtkernel_3/lib/lwip/memp.o \
./mtkernel_3/lib/lwip/netif.o \
./mtkernel_3/lib/lwip/pbuf.o \
./mtkernel_3/lib/lwip/raw.o \
./mtkernel_3/lib/lwip/stats.o \
./mtkernel_3/lib/lwip/sys.o \
./mtkernel_3/lib/lwip/tcp.o \
./mtkernel_3/lib/lwip/tcp_in.o \
./mtkernel_3/lib/lwip/tcp_out.o \
./mtkernel_3/lib/lwip/timeouts.o \
./mtkernel_3/lib/lwip/udp.o \
./mtkernel_3/lib/lwip/dhcp.o \
./mtkernel_3/lib/lwip/etharp.o \
./mtkernel_3/lib/lwip/icmp.o \
./mtkernel_3/lib/lwip/ip4.o \
./mtkernel_3/lib/lwip/ip4_addr.o \
./mtkernel_3/lib/lwip/ip4_frag.o \
./mtkernel_3/lib/lwip/acd.o \
./mtkernel_3/lib/lwip/ethernet.o \
./mtkernel_3/lib/lwip/cyw43_lwip.o \
./mtkernel_3/lib/lwip/sys_arch.o

OBJS += $(LWIP_OBJS)
C_DEPS += $(LWIP_OBJS:.o=.d)

# --- Core files ---
./mtkernel_3/lib/lwip/init.o: $(LWIP_DIR)/core/init.c
	@echo 'Building lwIP: $<'
	@mkdir -p ./mtkernel_3/lib/lwip
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(LWIP_INCPATH) -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

./mtkernel_3/lib/lwip/def.o: $(LWIP_DIR)/core/def.c
	@mkdir -p ./mtkernel_3/lib/lwip
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(LWIP_INCPATH) -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

./mtkernel_3/lib/lwip/dns.o: $(LWIP_DIR)/core/dns.c
	@mkdir -p ./mtkernel_3/lib/lwip
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(LWIP_INCPATH) -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

./mtkernel_3/lib/lwip/inet_chksum.o: $(LWIP_DIR)/core/inet_chksum.c
	@mkdir -p ./mtkernel_3/lib/lwip
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(LWIP_INCPATH) -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

./mtkernel_3/lib/lwip/ip.o: $(LWIP_DIR)/core/ip.c
	@mkdir -p ./mtkernel_3/lib/lwip
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(LWIP_INCPATH) -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

./mtkernel_3/lib/lwip/mem.o: $(LWIP_DIR)/core/mem.c
	@mkdir -p ./mtkernel_3/lib/lwip
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(LWIP_INCPATH) -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

./mtkernel_3/lib/lwip/memp.o: $(LWIP_DIR)/core/memp.c
	@mkdir -p ./mtkernel_3/lib/lwip
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(LWIP_INCPATH) -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

./mtkernel_3/lib/lwip/netif.o: $(LWIP_DIR)/core/netif.c
	@mkdir -p ./mtkernel_3/lib/lwip
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(LWIP_INCPATH) -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

./mtkernel_3/lib/lwip/pbuf.o: $(LWIP_DIR)/core/pbuf.c
	@mkdir -p ./mtkernel_3/lib/lwip
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(LWIP_INCPATH) -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

./mtkernel_3/lib/lwip/raw.o: $(LWIP_DIR)/core/raw.c
	@mkdir -p ./mtkernel_3/lib/lwip
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(LWIP_INCPATH) -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

./mtkernel_3/lib/lwip/stats.o: $(LWIP_DIR)/core/stats.c
	@mkdir -p ./mtkernel_3/lib/lwip
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(LWIP_INCPATH) -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

./mtkernel_3/lib/lwip/sys.o: $(LWIP_DIR)/core/sys.c
	@mkdir -p ./mtkernel_3/lib/lwip
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(LWIP_INCPATH) -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

./mtkernel_3/lib/lwip/tcp.o: $(LWIP_DIR)/core/tcp.c
	@mkdir -p ./mtkernel_3/lib/lwip
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(LWIP_INCPATH) -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

./mtkernel_3/lib/lwip/tcp_in.o: $(LWIP_DIR)/core/tcp_in.c
	@mkdir -p ./mtkernel_3/lib/lwip
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(LWIP_INCPATH) -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

./mtkernel_3/lib/lwip/tcp_out.o: $(LWIP_DIR)/core/tcp_out.c
	@mkdir -p ./mtkernel_3/lib/lwip
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(LWIP_INCPATH) -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

./mtkernel_3/lib/lwip/timeouts.o: $(LWIP_DIR)/core/timeouts.c
	@mkdir -p ./mtkernel_3/lib/lwip
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(LWIP_INCPATH) -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

./mtkernel_3/lib/lwip/udp.o: $(LWIP_DIR)/core/udp.c
	@mkdir -p ./mtkernel_3/lib/lwip
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(LWIP_INCPATH) -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

# --- IPv4 ---
./mtkernel_3/lib/lwip/dhcp.o: $(LWIP_DIR)/core/ipv4/dhcp.c
	@mkdir -p ./mtkernel_3/lib/lwip
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(LWIP_INCPATH) -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

./mtkernel_3/lib/lwip/etharp.o: $(LWIP_DIR)/core/ipv4/etharp.c
	@mkdir -p ./mtkernel_3/lib/lwip
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(LWIP_INCPATH) -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

./mtkernel_3/lib/lwip/icmp.o: $(LWIP_DIR)/core/ipv4/icmp.c
	@mkdir -p ./mtkernel_3/lib/lwip
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(LWIP_INCPATH) -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

./mtkernel_3/lib/lwip/ip4.o: $(LWIP_DIR)/core/ipv4/ip4.c
	@mkdir -p ./mtkernel_3/lib/lwip
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(LWIP_INCPATH) -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

./mtkernel_3/lib/lwip/ip4_addr.o: $(LWIP_DIR)/core/ipv4/ip4_addr.c
	@mkdir -p ./mtkernel_3/lib/lwip
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(LWIP_INCPATH) -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

./mtkernel_3/lib/lwip/ip4_frag.o: $(LWIP_DIR)/core/ipv4/ip4_frag.c
	@mkdir -p ./mtkernel_3/lib/lwip
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(LWIP_INCPATH) -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

./mtkernel_3/lib/lwip/acd.o: $(LWIP_DIR)/core/ipv4/acd.c
	@mkdir -p ./mtkernel_3/lib/lwip
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(LWIP_INCPATH) -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

# --- Netif ---
./mtkernel_3/lib/lwip/ethernet.o: $(LWIP_DIR)/netif/ethernet.c
	@mkdir -p ./mtkernel_3/lib/lwip
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(LWIP_INCPATH) -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

# --- CYW43 lwIP glue ---
./mtkernel_3/lib/lwip/cyw43_lwip.o: ../lib/cyw43-driver/src/cyw43_lwip.c
	@echo 'Building CYW43 lwIP glue: $<'
	@mkdir -p ./mtkernel_3/lib/lwip
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(LWIP_INCPATH) -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

# --- sys_arch (NO_SYS=1 用最小実装) ---
./mtkernel_3/lib/lwip/sys_arch.o: ../device/wifi/sys_arch.c
	@echo 'Building lwIP sys_arch: $<'
	@mkdir -p ./mtkernel_3/lib/lwip
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(LWIP_INCPATH) -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"
