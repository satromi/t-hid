# USB Device Driver for μT-Kernel 3.0 BSP

# Target-dependent HAL source
ifeq ($(TARGET), _PICO_RP2040_)
USB_HID_HAL_SRC = ../device/usb_hid/sysdepend/rp2040/usb_hid_rp2040.c
USB_HID_HAL_OBJ = usb_hid_rp2040.o
endif
ifeq ($(TARGET), _PICO_W_)
USB_HID_HAL_SRC = ../device/usb_hid/sysdepend/rp2040/usb_hid_rp2040.c
USB_HID_HAL_OBJ = usb_hid_rp2040.o
endif
ifeq ($(TARGET), _NUCLEO_H723_)
USB_HID_HAL_SRC = ../device/usb_hid/sysdepend/stm32h7/usb_hid_stm32h7.c
USB_HID_HAL_OBJ = usb_hid_stm32h7.o
endif

USB_HID_OBJS = $(addprefix ./mtkernel_3/device/usb_hid/, \
	$(USB_HID_HAL_OBJ) usb_hid.o usb_hid_class.o)

OBJS += $(USB_HID_OBJS)
C_DEPS += $(USB_HID_OBJS:.o=.d)

USB_HID_INCPATH = -I"../device/usb_hid"

./mtkernel_3/device/usb_hid/$(USB_HID_HAL_OBJ): $(USB_HID_HAL_SRC)
	@echo 'Building USB Device: $<'
	@mkdir -p ./mtkernel_3/device/usb_hid
	$(GCC) $(CFLAGS) $(EXTRA_DEFS) -D$(TARGET) $(INCPATH) $(USB_HID_INCPATH) -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"

./mtkernel_3/device/usb_hid/%.o: ../device/usb_hid/%.c
	@echo 'Building USB Device: $<'
	@mkdir -p ./mtkernel_3/device/usb_hid
	$(GCC) $(CFLAGS) $(EXTRA_DEFS) -D$(TARGET) $(INCPATH) $(USB_HID_INCPATH) -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"
