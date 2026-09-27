################################################################################
# I2C Slave Device Driver
################################################################################

I2CS_DIR := ../device/i2c_slave
I2CS_SYS := ../device/i2c_slave/sysdepend/rp2040

OBJS += \
./mtkernel_3/device/i2c_slave/i2c_slave.o \
./mtkernel_3/device/i2c_slave/i2c_slave_rp2040.o \

C_DEPS += \
./mtkernel_3/device/i2c_slave/i2c_slave.d \
./mtkernel_3/device/i2c_slave/i2c_slave_rp2040.d \

mtkernel_3/device/i2c_slave/i2c_slave.o: $(I2CS_DIR)/i2c_slave.c
	@echo 'Building I2C slave: $<'
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

mtkernel_3/device/i2c_slave/i2c_slave_rp2040.o: $(I2CS_SYS)/i2c_slave_rp2040.c
	@echo 'Building I2C slave: $<'
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
