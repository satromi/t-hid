################################################################################
# CYW43 HAL (WiFi-only mode) — BLE/BTstack なしで CYW43 を動作させる
#
# CYW43 ドライバ本体 (cyw43_ll, cyw43_ctrl, cyw43_stats) +
# uT-Kernel HAL (cyw43_arch_tkernel, cyw43_spi_pio) のみビルド。
################################################################################

BLE_DIR := ../device/ble_hid
BLE_SYS_DIR := ../device/ble_hid/sysdepend/rp2040
CYW43_SRC := ../lib/cyw43-driver/src

CYW43_HAL_CFLAGS := $(CFLAGS) -D$(TARGET) $(INCPATH) \
    -Wno-unused-parameter -Wno-sign-compare -Wno-cast-align

OBJS += \
./mtkernel_3/device/ble_hid/cyw43_arch_tkernel.o \
./mtkernel_3/device/ble_hid/cyw43_spi_pio.o \
./mtkernel_3/device/ble_hid/cyw43_ll.o \
./mtkernel_3/device/ble_hid/cyw43_ctrl.o \
./mtkernel_3/device/ble_hid/cyw43_stats.o \
./mtkernel_3/device/ble_hid/cybt_shared_bus.o \
./mtkernel_3/device/ble_hid/cybt_shared_bus_driver.o

C_DEPS += \
./mtkernel_3/device/ble_hid/cyw43_arch_tkernel.d \
./mtkernel_3/device/ble_hid/cyw43_spi_pio.d \
./mtkernel_3/device/ble_hid/cyw43_ll.d \
./mtkernel_3/device/ble_hid/cyw43_ctrl.d \
./mtkernel_3/device/ble_hid/cyw43_stats.d \
./mtkernel_3/device/ble_hid/cybt_shared_bus.d \
./mtkernel_3/device/ble_hid/cybt_shared_bus_driver.d

mtkernel_3/device/ble_hid/cyw43_arch_tkernel.o: $(BLE_SYS_DIR)/cyw43_arch_tkernel.c
	@echo 'Building CYW43 HAL: $<'
	$(GCC) $(CYW43_HAL_CFLAGS) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

mtkernel_3/device/ble_hid/cyw43_spi_pio.o: $(BLE_SYS_DIR)/cyw43_spi_pio.c
	@echo 'Building CYW43 SPI: $<'
	$(GCC) $(CYW43_HAL_CFLAGS) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

mtkernel_3/device/ble_hid/cyw43_ll.o: $(CYW43_SRC)/cyw43_ll.c
	@echo 'CYW43 driver: $<'
	$(GCC) $(CYW43_HAL_CFLAGS) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

mtkernel_3/device/ble_hid/cyw43_ctrl.o: $(CYW43_SRC)/cyw43_ctrl.c
	@echo 'CYW43 driver: $<'
	$(GCC) $(CYW43_HAL_CFLAGS) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

mtkernel_3/device/ble_hid/cyw43_stats.o: $(CYW43_SRC)/cyw43_stats.c
	@echo 'CYW43 driver: $<'
	$(GCC) $(CYW43_HAL_CFLAGS) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

mtkernel_3/device/ble_hid/cybt_shared_bus.o: $(BLE_DIR)/cybt_shared_bus.c
	@echo 'CYW43 BT bus: $<'
	$(GCC) $(CYW43_HAL_CFLAGS) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

mtkernel_3/device/ble_hid/cybt_shared_bus_driver.o: $(BLE_DIR)/cybt_shared_bus_driver.c
	@echo 'CYW43 BT bus driver: $<'
	$(GCC) $(CYW43_HAL_CFLAGS) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
