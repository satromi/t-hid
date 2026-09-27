################################################################################
# WIZnet Ethernet Device Driver — チップ抽象化レイヤー
#
# device/wiznet/ を使用。W5100S チップ固有コードは sysdepend/w5100s/ 配下。
# ioLibrary 依存: なし (w5100s_reg.h で全レジスタ定義を自前で提供)
#
# チップ選択: WIZCHIP_W5100S マクロで W5100S を指定
################################################################################

WIZNET_INCPATH := -I"../device/wiznet" -I"../device/wiznet/sysdepend/w5100s"
WIZNET_CHIPDEF := -DWIZCHIP_W5100S

WIZNET_OBJS = \
./mtkernel_3/device/wiznet/wiznet_dev.o \
./mtkernel_3/device/wiznet/w5100s_spi.o \
./mtkernel_3/device/wiznet/w5100s_chip.o \
./mtkernel_3/device/wiznet/w5100s_io.o \
./mtkernel_3/device/wiznet/tk_socket.o \
./mtkernel_3/device/wiznet/tk_dhcp.o \
./mtkernel_3/device/wiznet/tk_dns.o \
./mtkernel_3/device/wiznet/tk_mqtt.o \
./mtkernel_3/device/wiznet/tk_sntp.o \
./mtkernel_3/device/wiznet/tk_netmon.o \
./mtkernel_3/device/wiznet/tk_httpd.o \
./mtkernel_3/device/wiznet/tk_sha256.o

# MCP サーバ (MCP=1 のときのみ)
ifeq ($(MCP),1)
WIZNET_OBJS += \
./mtkernel_3/device/wiznet/tk_mcp_core.o \
./mtkernel_3/device/wiznet/tk_mcp_tools.o \
./mtkernel_3/device/wiznet/tk_mcp_vm.o
endif

# WebAssembly ランタイムと wasm 系 MCP ツール (WASM=1 のときのみ)
ifeq ($(WASM),1)
WIZNET_OBJS += \
./mtkernel_3/device/wiznet/tk_wasm.o \
./mtkernel_3/device/wiznet/tk_wasm_host.o \
./mtkernel_3/device/wiznet/tk_wasm_mcp.o
endif

OBJS += $(WIZNET_OBJS)
C_DEPS += $(WIZNET_OBJS:.o=.d)

./mtkernel_3/device/wiznet/wiznet_dev.o: ../device/wiznet/wiznet_dev.c
	@echo 'Building WIZnet: $<'
	@mkdir -p ./mtkernel_3/device/wiznet
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(WIZNET_INCPATH) $(WIZNET_CHIPDEF) -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

./mtkernel_3/device/wiznet/w5100s_spi.o: ../device/wiznet/sysdepend/w5100s/w5100s_spi.c
	@echo 'Building WIZnet SPI: $<'
	@mkdir -p ./mtkernel_3/device/wiznet
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(WIZNET_INCPATH) $(WIZNET_CHIPDEF) -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

./mtkernel_3/device/wiznet/w5100s_chip.o: ../device/wiznet/sysdepend/w5100s/w5100s_chip.c
	@echo 'Building WIZnet chip: $<'
	@mkdir -p ./mtkernel_3/device/wiznet
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(WIZNET_INCPATH) $(WIZNET_CHIPDEF) -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

./mtkernel_3/device/wiznet/w5100s_io.o: ../device/wiznet/sysdepend/w5100s/w5100s_io.c
	@echo 'Building WIZnet IO: $<'
	@mkdir -p ./mtkernel_3/device/wiznet
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(WIZNET_INCPATH) $(WIZNET_CHIPDEF) -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

./mtkernel_3/device/wiznet/tk_socket.o: ../device/wiznet/tk_socket.c
	@echo 'Building tk_socket: $<'
	@mkdir -p ./mtkernel_3/device/wiznet
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(WIZNET_INCPATH) $(WIZNET_CHIPDEF) -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

./mtkernel_3/device/wiznet/tk_dhcp.o: ../device/wiznet/tk_dhcp.c
	@echo 'Building tk_dhcp: $<'
	@mkdir -p ./mtkernel_3/device/wiznet
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(WIZNET_INCPATH) $(WIZNET_CHIPDEF) -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

./mtkernel_3/device/wiznet/tk_dns.o: ../device/wiznet/tk_dns.c
	@echo 'Building tk_dns: $<'
	@mkdir -p ./mtkernel_3/device/wiznet
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(WIZNET_INCPATH) $(WIZNET_CHIPDEF) -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

./mtkernel_3/device/wiznet/tk_mqtt.o: ../device/wiznet/tk_mqtt.c
	@echo 'Building tk_mqtt: $<'
	@mkdir -p ./mtkernel_3/device/wiznet
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(WIZNET_INCPATH) $(WIZNET_CHIPDEF) -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

./mtkernel_3/device/wiznet/tk_sntp.o: ../device/wiznet/tk_sntp.c
	@echo 'Building tk_sntp: $<'
	@mkdir -p ./mtkernel_3/device/wiznet
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(WIZNET_INCPATH) $(WIZNET_CHIPDEF) -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

./mtkernel_3/device/wiznet/tk_netmon.o: ../device/wiznet/tk_netmon.c
	@echo 'Building tk_netmon: $<'
	@mkdir -p ./mtkernel_3/device/wiznet
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(WIZNET_INCPATH) $(WIZNET_CHIPDEF) -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

./mtkernel_3/device/wiznet/tk_httpd.o: ../device/wiznet/tk_httpd.c
	@echo 'Building tk_httpd: $<'
	@mkdir -p ./mtkernel_3/device/wiznet
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(WIZNET_INCPATH) $(WIZNET_CHIPDEF) -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

./mtkernel_3/device/wiznet/tk_mcp_core.o: ../device/wiznet/tk_mcp_core.c ./mtkernel_3/device/wiznet/mcp_apikey_embedded.h
	@echo 'Building tk_mcp_core: $<'
	@mkdir -p ./mtkernel_3/device/wiznet
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(WIZNET_INCPATH) $(WIZNET_CHIPDEF) -I"../lib/cjson" -I"./mtkernel_3/device/wiznet" -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

# .mcp_api_key は setup.sh で ../.mcp_api_key にコピーされる。無い場合は no-op ヘッダを生成。
./mtkernel_3/device/wiznet/mcp_apikey_embedded.h:
	@echo 'Generating embedded API key header'
	@mkdir -p ./mtkernel_3/device/wiznet
	python gen_apikey_header.py ../.mcp_api_key $@

./mtkernel_3/device/wiznet/tk_mcp_tools.o: ../device/wiznet/tk_mcp_tools.c
	@echo 'Building tk_mcp_tools: $<'
	@mkdir -p ./mtkernel_3/device/wiznet
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(WIZNET_INCPATH) $(WIZNET_CHIPDEF) -I"../lib/cjson" -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

./mtkernel_3/device/wiznet/tk_mcp_vm.o: ../device/wiznet/tk_mcp_vm.c
	@echo 'Building tk_mcp_vm: $<'
	@mkdir -p ./mtkernel_3/device/wiznet
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(WIZNET_INCPATH) $(WIZNET_CHIPDEF) -I"../lib/cjson" -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

./mtkernel_3/device/wiznet/tk_sha256.o: ../device/wiznet/tk_sha256.c
	@echo 'Building tk_sha256: $<'
	@mkdir -p ./mtkernel_3/device/wiznet
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(WIZNET_INCPATH) $(WIZNET_CHIPDEF) -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

# ---- WebAssembly (WASM=1 のときのみ使われる) ----
./mtkernel_3/device/wiznet/tk_wasm.o: ../device/wiznet/tk_wasm.c
	@echo 'Building WASM runtime: $<'
	@mkdir -p ./mtkernel_3/device/wiznet
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(WIZNET_INCPATH) -I"../lib/wasm3/source" -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

./mtkernel_3/device/wiznet/tk_wasm_host.o: ../device/wiznet/tk_wasm_host.c
	@echo 'Building WASM host functions: $<'
	@mkdir -p ./mtkernel_3/device/wiznet
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(WIZNET_INCPATH) -I"../lib/wasm3/source" -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

./mtkernel_3/device/wiznet/tk_wasm_mcp.o: ../device/wiznet/tk_wasm_mcp.c
	@echo 'Building WASM MCP tools: $<'
	@mkdir -p ./mtkernel_3/device/wiznet
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(WIZNET_INCPATH) -I"../lib/cjson" -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"
