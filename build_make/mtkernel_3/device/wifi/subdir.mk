################################################################################
# WiFi Device Driver — CYW43439 + lwIP ソケット
################################################################################

WIFI_INCPATH := -I"../device/wifi"

WIFI_OBJS = \
./mtkernel_3/device/wifi/tk_wifi.o \
./mtkernel_3/device/wifi/tk_wifi_reconnect.o \
./mtkernel_3/device/wifi/tk_socket_lwip.o \
./mtkernel_3/device/wifi/tk_net_compat.o \
./mtkernel_3/device/wifi/tk_wifi_creds.o

OBJS += $(WIFI_OBJS)
C_DEPS += $(WIFI_OBJS:.o=.d)

./mtkernel_3/device/wifi/tk_wifi.o: ../device/wifi/tk_wifi.c
	@echo 'Building WiFi: $<'
	@mkdir -p ./mtkernel_3/device/wifi
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(WIFI_INCPATH) -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

./mtkernel_3/device/wifi/tk_wifi_reconnect.o: ../device/wifi/tk_wifi_reconnect.c
	@echo 'Building WiFi reconnect: $<'
	@mkdir -p ./mtkernel_3/device/wifi
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(WIFI_INCPATH) -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

./mtkernel_3/device/wifi/tk_socket_lwip.o: ../device/wifi/tk_socket_lwip.c
	@echo 'Building WiFi socket: $<'
	@mkdir -p ./mtkernel_3/device/wifi
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(WIFI_INCPATH) -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

./mtkernel_3/device/wifi/tk_net_compat.o: ../device/wifi/tk_net_compat.c
	@echo 'Building WiFi compat: $<'
	@mkdir -p ./mtkernel_3/device/wifi
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(WIFI_INCPATH) -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

./mtkernel_3/device/wifi/tk_wifi_creds.o: ../device/wifi/tk_wifi_creds.c
	@echo 'Building WiFi creds: $<'
	@mkdir -p ./mtkernel_3/device/wifi
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) $(WIFI_INCPATH) -I"../device/wiznet" -I"../lib/cjson" -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"
