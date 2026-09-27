################################################################################
# BLE Keyboard App — usermain (kb_start のみ、ネットワークなし)
################################################################################

OBJS += ./mtkernel_3/app_kb_program/app_main.o

C_DEPS += ./mtkernel_3/app_kb_program/app_main.d

./mtkernel_3/app_kb_program/app_main.o: ../app_kb_program/app_main.c
	@echo 'Building app (BLE keyboard): $<'
	@mkdir -p ./mtkernel_3/app_kb_program
	$(GCC) $(CFLAGS) $(EXTRA_DEFS) -D$(TARGET) $(INCPATH) -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"
