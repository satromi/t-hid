################################################################################
# BLE HID Device Driver (CYW43439 + BTstack port)
################################################################################

BLE_DIR := ../device/ble_hid
BLE_SYS_DIR := ../device/ble_hid/sysdepend/rp2040

OBJS += \
./mtkernel_3/device/ble_hid/btstack_port_tkernel.o \
./mtkernel_3/device/ble_hid/ble_hid_gatt.o \
./mtkernel_3/device/ble_hid/hci_transport_cyw43.o \
./mtkernel_3/device/ble_hid/btstack_tlv_flash.o \
./mtkernel_3/device/ble_hid/ble_split_service.o \
./mtkernel_3/device/ble_hid/ble_split_central.o \
./mtkernel_3/device/ble_hid/cyw43_arch_tkernel.o \
./mtkernel_3/device/ble_hid/cyw43_spi_pio.o \
./mtkernel_3/device/ble_hid/cybt_shared_bus.o \
./mtkernel_3/device/ble_hid/cybt_shared_bus_driver.o \

C_DEPS += \
./mtkernel_3/device/ble_hid/btstack_port_tkernel.d \
./mtkernel_3/device/ble_hid/ble_hid_gatt.d \
./mtkernel_3/device/ble_hid/hci_transport_cyw43.d \
./mtkernel_3/device/ble_hid/btstack_tlv_flash.d \
./mtkernel_3/device/ble_hid/ble_split_service.d \
./mtkernel_3/device/ble_hid/ble_split_central.d \
./mtkernel_3/device/ble_hid/cyw43_arch_tkernel.d \
./mtkernel_3/device/ble_hid/cyw43_spi_pio.d \
./mtkernel_3/device/ble_hid/cybt_shared_bus.d \
./mtkernel_3/device/ble_hid/cybt_shared_bus_driver.d \

mtkernel_3/device/ble_hid/btstack_port_tkernel.o: $(BLE_DIR)/btstack_port_tkernel.c
	@echo 'Building BLE: $<'
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

mtkernel_3/device/ble_hid/ble_hid_gatt.o: $(BLE_DIR)/ble_hid_gatt.c
	@echo 'Building BLE: $<'
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

mtkernel_3/device/ble_hid/btstack_tlv_flash.o: $(BLE_DIR)/btstack_tlv_flash.c
	@echo 'Building BLE: $<'
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

mtkernel_3/device/ble_hid/hci_transport_cyw43.o: $(BLE_DIR)/hci_transport_cyw43.c
	@echo 'Building BLE: $<'
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

mtkernel_3/device/ble_hid/ble_split_service.o: $(BLE_DIR)/ble_split_service.c
	@echo 'Building BLE: $<'
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

mtkernel_3/device/ble_hid/ble_split_central.o: $(BLE_DIR)/ble_split_central.c
	@echo 'Building BLE: $<'
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

mtkernel_3/device/ble_hid/cyw43_arch_tkernel.o: $(BLE_SYS_DIR)/cyw43_arch_tkernel.c
	@echo 'Building BLE: $<'
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

mtkernel_3/device/ble_hid/cyw43_spi_pio.o: $(BLE_SYS_DIR)/cyw43_spi_pio.c
	@echo 'Building BLE: $<'
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

mtkernel_3/device/ble_hid/cybt_shared_bus.o: $(BLE_DIR)/cybt_shared_bus.c
	@echo 'Building BLE: $<'
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

mtkernel_3/device/ble_hid/cybt_shared_bus_driver.o: $(BLE_DIR)/cybt_shared_bus_driver.c
	@echo 'Building BLE: $<'
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
