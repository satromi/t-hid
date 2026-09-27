################################################################################
# W5100S Ethernet Device Driver — μT-Kernel 完全独自実装
#
# ioLibrary 依存: なし (w5100s_reg.h で全レジスタ定義を自前で提供)
################################################################################

W5100S_INCPATH := -I"../device/w5100s"

W5100S_OBJS = \
./mtkernel_3/device/w5100s/w5100s_dev.o \
./mtkernel_3/device/w5100s/w5100s_spi.o \
./mtkernel_3/device/w5100s/w5100s_chip.o \
./mtkernel_3/device/w5100s/w5100s_io.o \
./mtkernel_3/device/w5100s/tk_socket.o \
./mtkernel_3/device/w5100s/tk_dhcp.o \
./mtkernel_3/device/w5100s/tk_dns.o \
./mtkernel_3/device/w5100s/tk_mqtt.o \
./mtkernel_3/device/w5100s/tk_sntp.o \
./mtkernel_3/device/w5100s/tk_netmon.o \
./mtkernel_3/device/w5100s/tk_httpd.o \
./mtkernel_3/device/w5100s/tk_mcp_core.o \
./mtkernel_3/device/w5100s/tk_mcp_tools.o \
./mtkernel_3/device/w5100s/tk_mcp_vm.o \
./mtkernel_3/device/w5100s/tk_sha256.o

OBJS += $(W5100S_OBJS)
C_DEPS += $(W5100S_OBJS:.o=.d)

./mtkernel_3/device/w5100s/w5100s_dev.o: ../device/w5100s/w5100s_dev.c
	@echo 'Building W5100S: $<'
	@mkdir -p ./mtkernel_3/device/w5100s
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(W5100S_INCPATH) -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

./mtkernel_3/device/w5100s/w5100s_spi.o: ../device/w5100s/sysdepend/rp2040/w5100s_spi.c
	@echo 'Building W5100S SPI: $<'
	@mkdir -p ./mtkernel_3/device/w5100s
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(W5100S_INCPATH) -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

./mtkernel_3/device/w5100s/w5100s_chip.o: ../device/w5100s/w5100s_chip.c
	@echo 'Building W5100S chip: $<'
	@mkdir -p ./mtkernel_3/device/w5100s
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(W5100S_INCPATH) -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

./mtkernel_3/device/w5100s/tk_socket.o: ../device/w5100s/tk_socket.c
	@echo 'Building tk_socket: $<'
	@mkdir -p ./mtkernel_3/device/w5100s
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(W5100S_INCPATH) -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

./mtkernel_3/device/w5100s/w5100s_io.o: ../device/w5100s/w5100s_io.c
	@echo 'Building W5100S IO: $<'
	@mkdir -p ./mtkernel_3/device/w5100s
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(W5100S_INCPATH) -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

./mtkernel_3/device/w5100s/tk_dhcp.o: ../device/w5100s/tk_dhcp.c
	@echo 'Building tk_dhcp: $<'
	@mkdir -p ./mtkernel_3/device/w5100s
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(W5100S_INCPATH) -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

./mtkernel_3/device/w5100s/tk_dns.o: ../device/w5100s/tk_dns.c
	@echo 'Building tk_dns: $<'
	@mkdir -p ./mtkernel_3/device/w5100s
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(W5100S_INCPATH) -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

./mtkernel_3/device/w5100s/tk_mqtt.o: ../device/w5100s/tk_mqtt.c
	@echo 'Building tk_mqtt: $<'
	@mkdir -p ./mtkernel_3/device/w5100s
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(W5100S_INCPATH) -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

./mtkernel_3/device/w5100s/tk_sntp.o: ../device/w5100s/tk_sntp.c
	@echo 'Building tk_sntp: $<'
	@mkdir -p ./mtkernel_3/device/w5100s
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(W5100S_INCPATH) -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

./mtkernel_3/device/w5100s/tk_netmon.o: ../device/w5100s/tk_netmon.c
	@echo 'Building tk_netmon: $<'
	@mkdir -p ./mtkernel_3/device/w5100s
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(W5100S_INCPATH) -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

./mtkernel_3/device/w5100s/tk_httpd.o: ../device/w5100s/tk_httpd.c
	@echo 'Building tk_httpd: $<'
	@mkdir -p ./mtkernel_3/device/w5100s
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(W5100S_INCPATH) -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

./mtkernel_3/device/w5100s/tk_mcp_core.o: ../device/w5100s/tk_mcp_core.c
	@echo 'Building tk_mcp_core: $<'
	@mkdir -p ./mtkernel_3/device/w5100s
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(W5100S_INCPATH) -I"../lib/cjson" -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

./mtkernel_3/device/w5100s/tk_mcp_tools.o: ../device/w5100s/tk_mcp_tools.c
	@echo 'Building tk_mcp_tools: $<'
	@mkdir -p ./mtkernel_3/device/w5100s
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(W5100S_INCPATH) -I"../lib/cjson" -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

./mtkernel_3/device/w5100s/tk_mcp_vm.o: ../device/w5100s/tk_mcp_vm.c
	@echo 'Building tk_mcp_vm: $<'
	@mkdir -p ./mtkernel_3/device/w5100s
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(W5100S_INCPATH) -I"../lib/cjson" -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

./mtkernel_3/device/w5100s/tk_sha256.o: ../device/w5100s/tk_sha256.c
	@echo 'Building tk_sha256: $<'
	@mkdir -p ./mtkernel_3/device/w5100s
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(W5100S_INCPATH) -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"
