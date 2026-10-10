#pragma once
void ec_control_start(void);
const char *ec_ir_control(const char *operation);
#include "cJSON.h"
char *ec_ir_learn_begin(void);
char *ec_ir_learn_get(void);
const char *ec_ir_register(const char *name, cJSON *signal);

const char *ec_ir_fast_volume(const char *operation);
