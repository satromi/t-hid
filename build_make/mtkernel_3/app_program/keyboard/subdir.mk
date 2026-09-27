################################################################################
# micro T-Kernel 3.0 BSP  makefile
# Keyboard framework sources
#
# Usage:
#   make KEYMAP=default all     (QWERTY, default)
#   make KEYMAP=dvorak  all     (Dvorak)
################################################################################

KB_DIR := ../app_program/keyboard

# Keymap selection (default: "default")
KEYMAP ?= default
KB_KEYMAP_FILE := \"keymaps/$(KEYMAP)/keymap.c\"

OBJS += \
./mtkernel_3/app_program/keyboard/kb_matrix.o \
./mtkernel_3/app_program/keyboard/kb_keymap.o \
./mtkernel_3/app_program/keyboard/kb_process.o \
./mtkernel_3/app_program/keyboard/kb_split.o \
./mtkernel_3/app_program/keyboard/split_transport.o \
./mtkernel_3/app_program/keyboard/kb_output.o \
./mtkernel_3/app_program/keyboard/kb_output_usb.o \
./mtkernel_3/app_program/keyboard/kb_output_ble.o \
./mtkernel_3/app_program/keyboard/kb_wiring_test.o \
./mtkernel_3/app_program/keyboard/kb_main.o \

C_DEPS += \
./mtkernel_3/app_program/keyboard/kb_matrix.d \
./mtkernel_3/app_program/keyboard/kb_keymap.d \
./mtkernel_3/app_program/keyboard/kb_process.d \
./mtkernel_3/app_program/keyboard/kb_split.d \
./mtkernel_3/app_program/keyboard/split_transport.d \
./mtkernel_3/app_program/keyboard/kb_output.d \
./mtkernel_3/app_program/keyboard/kb_output_usb.d \
./mtkernel_3/app_program/keyboard/kb_output_ble.d \
./mtkernel_3/app_program/keyboard/kb_wiring_test.d \
./mtkernel_3/app_program/keyboard/kb_main.d \

mtkernel_3/app_program/keyboard/%.o: $(KB_DIR)/%.c
	@echo 'Building keyboard: $< [keymap=$(KEYMAP)]'
	$(GCC) $(CFLAGS) $(EXTRA_DEFS) -D$(TARGET) -DKEYMAP_FILE=$(KB_KEYMAP_FILE) $(INCPATH) -I"../device/usbdev" -I"$(KB_DIR)" -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '
