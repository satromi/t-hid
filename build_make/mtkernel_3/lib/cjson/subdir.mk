# cJSON library build rules

CJSON_SRC_DIR = ../lib/cjson
CJSON_OBJ_DIR = mtkernel_3/lib/cjson

CJSON_OBJS = $(CJSON_OBJ_DIR)/cJSON.o
CJSON_DEPS = $(CJSON_OBJ_DIR)/cJSON.d

OBJS += $(CJSON_OBJS)
DEPS += $(CJSON_DEPS)

$(CJSON_OBJ_DIR)/cJSON.o: $(CJSON_SRC_DIR)/cJSON.c
	@echo 'Building cJSON: $<'
	@mkdir -p $(CJSON_OBJ_DIR)
	$(GCC) $(CFLAGS) -DCJSON_NESTING_LIMIT=20 -D$(TARGET) -I"../include" -I"../config" -I"../kernel/knlinc" -MF"$(CJSON_OBJ_DIR)/cJSON.d" -MT"$@" -c -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '
