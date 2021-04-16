#ifndef _USER_INPUT_H_
#define _USER_INPUT_H_

/****************** CONSOLE COMMANDS *******************/
#define CMD_HELP "help"
#define CMD_DEVICE_INFO "info"
#define CMD_DEVICE_STATUS "status"
#define CMD_DEVICE_STATUS_CHANGE "change"
#define CMD_DEBUG "debug"
#define CMD_DUT_VOLTAGES_CHECK "dutVoltagesCheck"
#define CMD_EXT_RELAY_SET "extRelaySet"
#define CMD_OPTO_INPUTS_CHECK "optoInputsCheck"
#define CMD_DUT_SUPPLY_SET "dutSupplySet"
#define CMD_STM32_RESET "stm32Reset"
#define CMD_PLATFORM_5V_SET "platform5VSet"
#define CMD_OPTO_OUT_SET "optoOutputSet"
#ifdef PLATFORM_WASHER
#define CMD_DOOR_CONTROL "doorControl"
#define CMD_UNIV_MOTOR_POWER "motorPower"
#define CMD_OPTI_DOSE_1 "optiDoseSet1"
#define CMD_OPTI_DOSE_2 "optiDoseSet2"
#define CMD_DOOR_COIL_CHECK "doorCheck"
#define CMD_DOOR_SET "doorSet"
#define CMD_PUMP_CHECK "pumpCheck"
#define CMD_SOFTENER_VALVE_CHECK "softenerCheck"
#define CMD_DETERGENT_VALVE_CHECK "detergentCheck"
#define CMD_STEAM_GEN_VALVE_CHECK "steamCheck"
#define CMD_WATER_VALVE_CHECK "waterValveCheck"
#define CMD_WASH_HEATER_CHECK "washHeaterCheck"
#define CMD_DRY_HEATER_CHECK "dryHeaterCheck"
#define CMD_DRY_FAN_CHECK "dryFanCheck"
#define CMD_TEMP_SET "tempSet"
#define CMD_UNIV_MOTOR_CHECK "univMotorCheck"
#define CMD_BLDC_MOTOR_CHECK "bldcMotorCheck"
#define CMD_WATER_LEVEL_SET "waterLevelSet"
#define CMD_FLOW_METER_SET "fmSet"
#elif PLATFORM_DISHWASHER
#define CMD_DUT_1_DUPPLY_SET "dut1SupplySet"
#define CMD_DUT_2_DUPPLY_SET "dut2SupplySet"
#define CMD_DOOR_SET "doorSet"
#define CMD_RE_SET "reSet"
#define CMD_FM_SET "fmSet"
#define CMD_IAQS_SET "iaqsSet"
#define CMD_ISS_SET "issSet"
#define CMD_ISB_SET "isbSet"
#define CMD_LED_CHECK "ledCheck"
#define CMD_FAN_CHECK "fanCheck"
#define CMD_DIV_SET "divSet"
#define CMD_TURB_SET "turbSet"
#define CMD_R_CHECK "rCheck"
#define CMD_D_ED_CHECK "dEdCheck"
#define CMD_DRAIN_PUMP_CHECK "drainPumpCheck"
#define CMD_WASH_PUMP_CHECK "washPumpCheck"
#define CMD_EV1_CHECK "ev1Check"
#define CMD_EV2_CHECK "ev2Check"
#define CMD_EV3_CHECK "ev3Check"
#define CMD_P1_SET "p1Set"
#define CMD_P2_SET "p2Set"
#define CMD_BLDC_WASH_PUMP_CHECK "bldcWashPumpCheck"
#endif
/*******************************************************/

#define _1_INT 1
#define _2_INT 2
#define _3_INT 3
#define _4_INT 4
#define _5_INT 5

#define CMD_PARSE(arg_string, arg_func, arg_param) if (StrCmp(arg_string, dataBuffer)) {arg_func(arg_param); cmdSuccessCnt++; return; }

#define CMD_ANSWER(arg_int_pos, arg_cmd) int robotTag = getIntFromString(buf, arg_int_pos); \
if (robotTag == 0)  {robotTag = -1; }\
char pBuf[STAT_BUF_SIZE] = {0};\
xsprintf(pBuf, "tag: %d-answ: %s %d", robotTag, arg_cmd, pSt);\
uint32_t crc = crc32(0, (void *)pBuf, strlen(pBuf));\
xprintf("0x%08X %s\n", crc, pBuf);

#define CMD_CHECK_AND_ANSWER(arg_int_pos, arg_var, arg_port, arg_pin, arg_cmd) GPIO_PinState pSt = HAL_GPIO_ReadPin(arg_port, arg_pin);\
arg_var = (uint8_t)pSt;\
int robotTag = getIntFromString(buf, arg_int_pos);\
if (robotTag == 0)\
  robotTag = -1; \
char pBuf[STAT_BUF_SIZE] = {0};\
xsprintf(pBuf, "tag: %d-answ: %s %d", robotTag, arg_cmd, pSt); \
uint32_t crc = crc32(0, (void *)pBuf, strlen(pBuf));\
xprintf("0x%08X %s\n", crc, pBuf);

#define CMD_SET(arg_int_pos, arg_var, arg_port, arg_pin) int val = getIntFromString(buf, arg_int_pos);\
arg_var = (uint8_t)val;\
HAL_GPIO_WritePin(arg_port, arg_pin, (GPIO_PinState)val);\
GPIO_PinState pSt = HAL_GPIO_ReadPin(arg_port, arg_pin);


#define GET_VARIABLE_NAME(var, str)  xsprintf(str, "%s", #var)

#define GPIO_INPUT_CHECK(arg_port, arg_pin, arg_var_act, arg_var_last, arg_name)\
pSt = HAL_GPIO_ReadPin(arg_port, arg_pin);\
if (pSt != arg_var_act) { arg_var_last = arg_var_act; arg_var_act = pSt; retStat = 1; dbgxprintf(C_GREEN"%s -> %d\n"C_NORMAL, arg_name, pSt);}

void processConsoleInput(void);
int getIntFromString(char *string, uint8_t whichInt);
int bufferPut(char ch);
int bufferGet(void);
void showDebugStatistics(char *buf);
void showHelp(char *buf);

#endif