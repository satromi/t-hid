################################################################################
# WIZnet MQTT/MCP — WiFi (CYW43 + lwIP) 向けビルド
#
# WizNet チップ固有コード (SPI, chip, IO, socket, DHCP, DNS) は除外。
# ソケット API は device/wifi/tk_socket_lwip.c が提供。
# DHCP/DNS は lwIP が提供。
# getSHAR 等は device/wifi/tk_net_compat.c が提供。
################################################################################

WIZNET_WIFI_INCPATH := -I"../device/wiznet" -I"../device/wifi"

WIZNET_WIFI_OBJS = \
./mtkernel_3/device/wiznet/tk_mqtt.o \
./mtkernel_3/device/wiznet/tk_mcp_core.o \
./mtkernel_3/device/wiznet/tk_mcp_tools.o \
./mtkernel_3/device/wiznet/tk_mcp_vm.o \
./mtkernel_3/device/wiznet/tk_sha256.o \
./mtkernel_3/device/wiznet/tk_wasm.o \
./mtkernel_3/device/wiznet/tk_wasm_host.o \
./mtkernel_3/device/wiznet/tk_wasm_mcp.o

OBJS += $(WIZNET_WIFI_OBJS)
C_DEPS += $(WIZNET_WIFI_OBJS:.o=.d)

./mtkernel_3/device/wiznet/tk_mqtt.o: ../device/wiznet/tk_mqtt.c
	@echo 'Building MQTT (WiFi): $<'
	@mkdir -p ./mtkernel_3/device/wiznet
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(WIZNET_WIFI_INCPATH) -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

./mtkernel_3/device/wiznet/tk_mcp_core.o: ../device/wiznet/tk_mcp_core.c ./mtkernel_3/device/wiznet/mcp_apikey_embedded.h
	@echo 'Building MCP core (WiFi): $<'
	@mkdir -p ./mtkernel_3/device/wiznet
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(WIZNET_WIFI_INCPATH) -I"../lib/cjson" -I"./mtkernel_3/device/wiznet" -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

# .mcp_api_key は setup.sh で ../.mcp_api_key にコピーされる。無い場合は no-op ヘッダを生成。
./mtkernel_3/device/wiznet/mcp_apikey_embedded.h:
	@echo 'Generating embedded API key header'
	@mkdir -p ./mtkernel_3/device/wiznet
	python gen_apikey_header.py ../.mcp_api_key $@

./mtkernel_3/device/wiznet/tk_mcp_tools.o: ../device/wiznet/tk_mcp_tools.c
	@echo 'Building MCP tools (WiFi): $<'
	@mkdir -p ./mtkernel_3/device/wiznet
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(WIZNET_WIFI_INCPATH) -I"../lib/cjson" -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

./mtkernel_3/device/wiznet/tk_mcp_vm.o: ../device/wiznet/tk_mcp_vm.c
	@echo 'Building MCP VM (WiFi): $<'
	@mkdir -p ./mtkernel_3/device/wiznet
	$(GCC) $(CFLAGS) -g0 -D$(TARGET) $(INCPATH) $(WIZNET_WIFI_INCPATH) -I"../lib/cjson" -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

./mtkernel_3/device/wiznet/tk_sha256.o: ../device/wiznet/tk_sha256.c
	@echo 'Building SHA256 (WiFi): $<'
	@mkdir -p ./mtkernel_3/device/wiznet
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(WIZNET_WIFI_INCPATH) -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

./mtkernel_3/device/wiznet/tk_wasm.o: ../device/wiznet/tk_wasm.c
	@echo 'Building WASM runtime: $<'
	@mkdir -p ./mtkernel_3/device/wiznet
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(WIZNET_WIFI_INCPATH) -I"../lib/wasm3/source" -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

./mtkernel_3/device/wiznet/tk_wasm_host.o: ../device/wiznet/tk_wasm_host.c
	@echo 'Building WASM host functions: $<'
	@mkdir -p ./mtkernel_3/device/wiznet
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(WIZNET_WIFI_INCPATH) -I"../lib/wasm3/source" -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

./mtkernel_3/device/wiznet/tk_wasm_mcp.o: ../device/wiznet/tk_wasm_mcp.c
	@echo 'Building WASM MCP tools: $<'
	@mkdir -p ./mtkernel_3/device/wiznet
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(WIZNET_WIFI_INCPATH) -I"../lib/cjson" -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"
