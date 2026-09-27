################################################################################
# micro T-Kernel 3.0 BSP  makefile
################################################################################

OBJS += \
./mtkernel_3/kernel/sysdepend/cpu/core/armv6m/cpu_cntl.o \
./mtkernel_3/kernel/sysdepend/cpu/core/armv6m/exc_hdr.o \
./mtkernel_3/kernel/sysdepend/cpu/core/armv6m/interrupt.o \
./mtkernel_3/kernel/sysdepend/cpu/core/armv6m/reset_hdl.o \
./mtkernel_3/kernel/sysdepend/cpu/core/armv6m/dispatch.o \

C_DEPS += \
./mtkernel_3/kernel/sysdepend/cpu/core/armv6m/cpu_cntl.d \
./mtkernel_3/kernel/sysdepend/cpu/core/armv6m/exc_hdr.d \
./mtkernel_3/kernel/sysdepend/cpu/core/armv6m/interrupt.d \
./mtkernel_3/kernel/sysdepend/cpu/core/armv6m/reset_hdl.d \

ASM_DEPS += \
./mtkernel_3/kernel/sysdepend/cpu/core/armv6m/dispatch.d \

mtkernel_3/kernel/sysdepend/cpu/core/armv6m/%.o: ../kernel/sysdepend/cpu/core/armv6m/%.c
	@echo 'Building file: $<'
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

mtkernel_3/kernel/sysdepend/cpu/core/armv6m/%.o: ../kernel/sysdepend/cpu/core/armv6m/%.S
	@echo 'Building file: $<'
	$(AS) $(ASFLAGS) -D$(TARGET) $(INCPATH) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '
