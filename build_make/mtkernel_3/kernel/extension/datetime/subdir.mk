################################################################################
# μT-Kernel Standard Extension — 日時管理 (datetime)
################################################################################

DATETIME_OBJS = \
./mtkernel_3/kernel/extension/datetime/datetime.o

OBJS += $(DATETIME_OBJS)
C_DEPS += $(DATETIME_OBJS:.o=.d)

./mtkernel_3/kernel/extension/datetime/datetime.o: ../kernel/extension/datetime/datetime.c
	@echo 'Building datetime: $<'
	@mkdir -p ./mtkernel_3/kernel/extension/datetime
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"
