################################################################################
# BTstack BLE Stack — Minimum sources for BLE HID Keyboard
#
# LE Peripheral (HOGP) に必要なソースのみコンパイル。
# Classic Bluetooth, Audio, GATT Client 等は除外。
################################################################################

BTSTACK_SRC := ../lib/btstack/src
BTSTACK_BLE := ../lib/btstack/src/ble
BTSTACK_GATT := ../lib/btstack/src/ble/gatt-service
BTSTACK_EMBED := ../lib/btstack/platform/embedded
CYW43_SRC := ../lib/cyw43-driver/src

BTSTACK_CFLAGS := $(CFLAGS) -D$(TARGET) $(INCPATH) \
    -Wno-unused-parameter -Wno-sign-compare -Wno-cast-align

OBJS += \
./mtkernel_3/lib/btstack/btstack_util.o \
./mtkernel_3/lib/btstack/btstack_linked_list.o \
./mtkernel_3/lib/btstack/btstack_memory.o \
./mtkernel_3/lib/btstack/btstack_memory_pool.o \
./mtkernel_3/lib/btstack/btstack_run_loop.o \
./mtkernel_3/lib/btstack/btstack_run_loop_base.o \
./mtkernel_3/lib/btstack/btstack_run_loop_embedded.o \
./mtkernel_3/lib/btstack/btstack_crypto.o \
./mtkernel_3/lib/btstack/btstack_tlv.o \
./mtkernel_3/lib/btstack/hci.o \
./mtkernel_3/lib/btstack/hci_cmd.o \
./mtkernel_3/lib/btstack/hci_dump.o \
./mtkernel_3/lib/btstack/hci_event.o \
./mtkernel_3/lib/btstack/hci_event_builder.o \
./mtkernel_3/lib/btstack/l2cap.o \
./mtkernel_3/lib/btstack/l2cap_signaling.o \
./mtkernel_3/lib/btstack/ad_parser.o \
./mtkernel_3/lib/btstack/btstack_hid_parser.o \
./mtkernel_3/lib/btstack/sm.o \
./mtkernel_3/lib/btstack/att_db.o \
./mtkernel_3/lib/btstack/att_server.o \
./mtkernel_3/lib/btstack/att_dispatch.o \
./mtkernel_3/lib/btstack/gatt_client.o \
./mtkernel_3/lib/btstack/le_device_db_tlv.o \
./mtkernel_3/lib/btstack/hids_device.o \
./mtkernel_3/lib/btstack/battery_service_server.o \
./mtkernel_3/lib/btstack/device_information_service_server.o \
./mtkernel_3/lib/btstack/cyw43_ll.o \
./mtkernel_3/lib/btstack/cyw43_ctrl.o \
./mtkernel_3/lib/btstack/cyw43_stats.o \
./mtkernel_3/lib/btstack/cyw43_bthci_uart.o \
./mtkernel_3/lib/btstack/rijndael.o \

C_DEPS += \
./mtkernel_3/lib/btstack/btstack_util.d \
./mtkernel_3/lib/btstack/btstack_linked_list.d \
./mtkernel_3/lib/btstack/btstack_memory.d \
./mtkernel_3/lib/btstack/btstack_memory_pool.d \
./mtkernel_3/lib/btstack/btstack_run_loop.d \
./mtkernel_3/lib/btstack/btstack_run_loop_base.d \
./mtkernel_3/lib/btstack/btstack_run_loop_embedded.d \
./mtkernel_3/lib/btstack/btstack_crypto.d \
./mtkernel_3/lib/btstack/btstack_tlv.d \
./mtkernel_3/lib/btstack/hci.d \
./mtkernel_3/lib/btstack/hci_cmd.d \
./mtkernel_3/lib/btstack/hci_dump.d \
./mtkernel_3/lib/btstack/hci_event.d \
./mtkernel_3/lib/btstack/hci_event_builder.d \
./mtkernel_3/lib/btstack/l2cap.d \
./mtkernel_3/lib/btstack/l2cap_signaling.d \
./mtkernel_3/lib/btstack/ad_parser.d \
./mtkernel_3/lib/btstack/btstack_hid_parser.d \
./mtkernel_3/lib/btstack/sm.d \
./mtkernel_3/lib/btstack/att_db.d \
./mtkernel_3/lib/btstack/att_server.d \
./mtkernel_3/lib/btstack/att_dispatch.d \
./mtkernel_3/lib/btstack/gatt_client.d \
./mtkernel_3/lib/btstack/le_device_db_tlv.d \
./mtkernel_3/lib/btstack/hids_device.d \
./mtkernel_3/lib/btstack/battery_service_server.d \
./mtkernel_3/lib/btstack/device_information_service_server.d \
./mtkernel_3/lib/btstack/cyw43_ll.d \
./mtkernel_3/lib/btstack/cyw43_ctrl.d \
./mtkernel_3/lib/btstack/cyw43_stats.d \
./mtkernel_3/lib/btstack/cyw43_bthci_uart.d \
./mtkernel_3/lib/btstack/rijndael.d \

# BTstack core
mtkernel_3/lib/btstack/btstack_util.o: $(BTSTACK_SRC)/btstack_util.c
	@echo 'BTstack: $<'
	$(GCC) $(BTSTACK_CFLAGS) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

mtkernel_3/lib/btstack/btstack_linked_list.o: $(BTSTACK_SRC)/btstack_linked_list.c
	@echo 'BTstack: $<'
	$(GCC) $(BTSTACK_CFLAGS) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

mtkernel_3/lib/btstack/btstack_memory.o: $(BTSTACK_SRC)/btstack_memory.c
	@echo 'BTstack: $<'
	$(GCC) $(BTSTACK_CFLAGS) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

mtkernel_3/lib/btstack/btstack_memory_pool.o: $(BTSTACK_SRC)/btstack_memory_pool.c
	@echo 'BTstack: $<'
	$(GCC) $(BTSTACK_CFLAGS) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

mtkernel_3/lib/btstack/btstack_run_loop.o: $(BTSTACK_SRC)/btstack_run_loop.c
	@echo 'BTstack: $<'
	$(GCC) $(BTSTACK_CFLAGS) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

mtkernel_3/lib/btstack/btstack_run_loop_base.o: $(BTSTACK_SRC)/btstack_run_loop_base.c
	@echo 'BTstack: $<'
	$(GCC) $(BTSTACK_CFLAGS) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

mtkernel_3/lib/btstack/btstack_run_loop_embedded.o: $(BTSTACK_EMBED)/btstack_run_loop_embedded.c
	@echo 'BTstack: $<'
	$(GCC) $(BTSTACK_CFLAGS) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

mtkernel_3/lib/btstack/btstack_crypto.o: $(BTSTACK_SRC)/btstack_crypto.c
	@echo 'BTstack: $<'
	$(GCC) $(BTSTACK_CFLAGS) -mlong-calls -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

mtkernel_3/lib/btstack/btstack_tlv.o: $(BTSTACK_SRC)/btstack_tlv.c
	@echo 'BTstack: $<'
	$(GCC) $(BTSTACK_CFLAGS) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

# HCI
mtkernel_3/lib/btstack/hci.o: $(BTSTACK_SRC)/hci.c
	@echo 'BTstack: $<'
	$(GCC) $(BTSTACK_CFLAGS) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

mtkernel_3/lib/btstack/hci_cmd.o: $(BTSTACK_SRC)/hci_cmd.c
	@echo 'BTstack: $<'
	$(GCC) $(BTSTACK_CFLAGS) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

mtkernel_3/lib/btstack/hci_dump.o: $(BTSTACK_SRC)/hci_dump.c
	@echo 'BTstack: $<'
	$(GCC) $(BTSTACK_CFLAGS) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

mtkernel_3/lib/btstack/hci_event.o: $(BTSTACK_SRC)/hci_event.c
	@echo 'BTstack: $<'
	$(GCC) $(BTSTACK_CFLAGS) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

mtkernel_3/lib/btstack/hci_event_builder.o: $(BTSTACK_SRC)/hci_event_builder.c
	@echo 'BTstack: $<'
	$(GCC) $(BTSTACK_CFLAGS) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

# L2CAP
mtkernel_3/lib/btstack/l2cap.o: $(BTSTACK_SRC)/l2cap.c
	@echo 'BTstack: $<'
	$(GCC) $(BTSTACK_CFLAGS) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

mtkernel_3/lib/btstack/l2cap_signaling.o: $(BTSTACK_SRC)/l2cap_signaling.c
	@echo 'BTstack: $<'
	$(GCC) $(BTSTACK_CFLAGS) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

# GAP / AD
mtkernel_3/lib/btstack/ad_parser.o: $(BTSTACK_SRC)/ad_parser.c
	@echo 'BTstack: $<'
	$(GCC) $(BTSTACK_CFLAGS) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

mtkernel_3/lib/btstack/btstack_hid_parser.o: $(BTSTACK_SRC)/btstack_hid_parser.c
	@echo 'BTstack: $<'
	$(GCC) $(BTSTACK_CFLAGS) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

# Security Manager
mtkernel_3/lib/btstack/sm.o: $(BTSTACK_BLE)/sm.c
	@echo 'BTstack: $<'
	$(GCC) $(BTSTACK_CFLAGS) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

# ATT / GATT
mtkernel_3/lib/btstack/att_db.o: $(BTSTACK_BLE)/att_db.c
	@echo 'BTstack: $<'
	$(GCC) $(BTSTACK_CFLAGS) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

mtkernel_3/lib/btstack/att_server.o: $(BTSTACK_BLE)/att_server.c
	@echo 'BTstack: $<'
	$(GCC) $(BTSTACK_CFLAGS) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

mtkernel_3/lib/btstack/att_dispatch.o: $(BTSTACK_BLE)/att_dispatch.c
	@echo 'BTstack: $<'
	$(GCC) $(BTSTACK_CFLAGS) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

mtkernel_3/lib/btstack/gatt_client.o: $(BTSTACK_BLE)/gatt_client.c
	@echo 'BTstack: $<'
	$(GCC) $(BTSTACK_CFLAGS) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

mtkernel_3/lib/btstack/le_device_db_tlv.o: $(BTSTACK_BLE)/le_device_db_tlv.c
	@echo 'BTstack: $<'
	$(GCC) $(BTSTACK_CFLAGS) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

# GATT Services (HID, Battery, Device Info)
mtkernel_3/lib/btstack/hids_device.o: $(BTSTACK_GATT)/hids_device.c
	@echo 'BTstack: $<'
	$(GCC) $(BTSTACK_CFLAGS) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

mtkernel_3/lib/btstack/battery_service_server.o: $(BTSTACK_GATT)/battery_service_server.c
	@echo 'BTstack: $<'
	$(GCC) $(BTSTACK_CFLAGS) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

mtkernel_3/lib/btstack/device_information_service_server.o: $(BTSTACK_GATT)/device_information_service_server.c
	@echo 'BTstack: $<'
	$(GCC) $(BTSTACK_CFLAGS) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

# CYW43 driver
mtkernel_3/lib/btstack/cyw43_ll.o: $(CYW43_SRC)/cyw43_ll.c
	@echo 'CYW43: $<'
	$(GCC) $(BTSTACK_CFLAGS) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

mtkernel_3/lib/btstack/cyw43_ctrl.o: $(CYW43_SRC)/cyw43_ctrl.c
	@echo 'CYW43: $<'
	$(GCC) $(BTSTACK_CFLAGS) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

mtkernel_3/lib/btstack/cyw43_spi.o: $(CYW43_SRC)/cyw43_spi.c
	@echo 'CYW43: $<'
	$(GCC) $(BTSTACK_CFLAGS) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

mtkernel_3/lib/btstack/cyw43_stats.o: $(CYW43_SRC)/cyw43_stats.c
	@echo 'CYW43: $<'
	$(GCC) $(BTSTACK_CFLAGS) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

mtkernel_3/lib/btstack/cyw43_bthci_uart.o: $(CYW43_SRC)/cyw43_bthci_uart.c
	@echo 'CYW43: $<'
	$(GCC) $(BTSTACK_CFLAGS) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

# 3rd-party: AES (rijndael)
mtkernel_3/lib/btstack/rijndael.o: ../lib/btstack/3rd-party/rijndael/rijndael.c
	@echo 'BTstack 3rd-party: $<'
	$(GCC) $(BTSTACK_CFLAGS) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

# 3rd-party: micro-ecc (uECC)
mtkernel_3/lib/btstack/uECC.o: ../lib/btstack/3rd-party/micro-ecc/uECC.c
	@echo 'BTstack 3rd-party: $<'
	$(GCC) $(BTSTACK_CFLAGS) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

