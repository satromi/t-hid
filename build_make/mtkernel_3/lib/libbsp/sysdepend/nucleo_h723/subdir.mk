################################################################################
# BSP library for Nucleo-144 STM32H723
################################################################################

OBJS += \
./mtkernel_3/lib/libbsp/sysdepend/nucleo_h723/libbsp_gpio.o

C_DEPS += \
./mtkernel_3/lib/libbsp/sysdepend/nucleo_h723/libbsp_gpio.d

mtkernel_3/lib/libbsp/sysdepend/nucleo_h723/%.o: ../lib/libbsp/sysdepend/nucleo_h723/%.c
	@echo 'Building BSP: $<'
	@mkdir -p ./mtkernel_3/lib/libbsp/sysdepend/nucleo_h723
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '
