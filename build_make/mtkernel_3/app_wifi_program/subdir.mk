################################################################################
# WiFi + BLE Keyboard App — usermain (WiFi 接続 + kb_start)
################################################################################

WIFI_APP_INCPATH := -I"../device/wifi" -I"../device/wiznet" -I"./mtkernel_3/app_wifi_program"

# WiFi 認証情報ヘッダ (リポジトリ直下の .wifi_config から生成。無ければ SSID 空)
WIFI_CONFIG_HDR := ./mtkernel_3/app_wifi_program/wifi_config_embedded.h

OBJS += ./mtkernel_3/app_wifi_program/app_main.o

C_DEPS += ./mtkernel_3/app_wifi_program/app_main.d

./mtkernel_3/app_wifi_program/app_main.o: ../app_wifi_program/app_main.c $(WIFI_CONFIG_HDR)
	@echo 'Building app (WiFi + keyboard): $<'
	@mkdir -p ./mtkernel_3/app_wifi_program
	$(GCC) $(CFLAGS) $(EXTRA_DEFS) -D$(TARGET) $(INCPATH) $(WIFI_APP_INCPATH) -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

$(WIFI_CONFIG_HDR): $(wildcard ../.wifi_config)
	@mkdir -p ./mtkernel_3/app_wifi_program
	python gen_wifi_header.py ../.wifi_config $@
