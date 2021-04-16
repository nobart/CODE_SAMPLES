#include "stm32f2xx_hal.h"
#include "userInput.h"
#include "dishwasher.h"
#include "xprintf.h"
#include "i2c.h"
#include "sys_time.h"

#define ACT_DS 0
#define LAST_DS 1
#define DUT_STATUS_NUM 2

dutStatus_t dut[DUT_STATUS_NUM] =
{
  {
    .DOOR_status = 0,
    .RE_status = 0,
    .IAQS_status = 0,
    .ISS_status = 0,
    .ISB_status = 0,
    .LED_status = 1,
    .FAN_status = 1,
    .DIV_status = 0,
    .TURB_value = 0,
    .R_status = 1,
    .D_Ed_status = 1,
    .DRAIN_PUMP_status = 1,
    .EV1_status = 1,
    .EV2_status = 1,
    .EV3_status = 1,
    .P1_status = 0,
    .P2_status = 0,
    .BLDC_WASH_PUMP_status = 0,
    .waterTemp_value = 0,
    .waterLevel_value = 0,
    .extRelay_status = 0,
    .platform5V_status = 0,
    .dutSupply_status = 0,
    .programStartUptime = 0,
    .isoOutput_status = 0,
  },
  {
    .DOOR_status = 0,
    .RE_status = 0,
    .IAQS_status = 0,
    .ISS_status = 0,
    .ISB_status = 0,
    .LED_status = 1,
    .FAN_status = 1,
    .DIV_status = 0,
    .TURB_value = 0,
    .R_status = 1,
    .D_Ed_status = 1,
    .DRAIN_PUMP_status = 1,
    .EV1_status = 1,
    .EV2_status = 1,
    .EV3_status = 1,
    .P1_status = 0,
    .P2_status = 0,
    .BLDC_WASH_PUMP_status = 0,
    .waterTemp_value = 0,
    .waterLevel_value = 0,
    .extRelay_status = 0,
    .platform5V_status = 0,
    .dutSupply_status = 0,
    .programStartUptime = 0,
    .isoOutput_status = 0,
  }
};

static ntc_t ntc =
{
  .status = 0,
  .startTemp = 0,
  .stopTemp = 0,
  .risingTime = 0,
  .risingStart = 0,
};

static uint16_t adcGetValue(adcInput_t pin);
void turbSet(char *buf)
{
#pragma message "todo turb value ~55"
  commandSucceded();
  int nSt = getIntFromString(buf, _1_INT);

  if (nSt)
  {
    dbgxprintf("Turbidity sensor feedback set to 50%%\n");

    HAL_GPIO_WritePin(TURB_DIV_EN_GPIO_Port, TURB_DIV_EN_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(TURB_CPU_GPIO_Port, TURB_CPU_Pin, GPIO_PIN_SET);
    dut[ACT_DS].TURB_value = 50;
  }
  else
  {
    dbgxprintf("Turbidity sensor feedback set to 0%%\n");

    HAL_GPIO_WritePin(TURB_DIV_EN_GPIO_Port, TURB_DIV_EN_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(TURB_CPU_GPIO_Port, TURB_CPU_Pin, GPIO_PIN_RESET);
    dut[ACT_DS].TURB_value = 0;
  }

  int robotTag = getIntFromString(buf, _2_INT);
  if (robotTag == 0)
    robotTag = -1;
  xprintf("tag: %d-answ: %s %d\n", robotTag, CMD_TURB_SET, dut[ACT_DS].TURB_value);
}

void dut1SupplySet(char *buf)
{
  commandSucceded();
  int state = getIntFromString(buf, _2_INT);
  dut[ACT_DS].dut1Supply_status = state;

  if (state == 1)
    HAL_GPIO_WritePin(DUT_1_SUPPLY_GPIO_Port, DUT_1_SUPPLY_Pin, GPIO_PIN_RESET);
  else
    HAL_GPIO_WritePin(DUT_1_SUPPLY_GPIO_Port, DUT_1_SUPPLY_Pin, GPIO_PIN_SET);

  int robotTag = getIntFromString(buf, _3_INT);
  if (robotTag == 0)
    robotTag = -1;
  xprintf("tag: %d-answ: %s %d\n", robotTag, CMD_DUT_1_DUPPLY_SET, state);
}

void dut2SupplySet(char *buf)
{
  commandSucceded();
  int state = getIntFromString(buf, _2_INT);
  dut[ACT_DS].dut2Supply_status = state;

  if (state == 1 )
    HAL_GPIO_WritePin(DUT_2_SUPPLY_GPIO_Port, DUT_2_SUPPLY_Pin, GPIO_PIN_RESET);
  else
    HAL_GPIO_WritePin(DUT_2_SUPPLY_GPIO_Port, DUT_2_SUPPLY_Pin, GPIO_PIN_SET);

  int robotTag = getIntFromString(buf, _3_INT);
  if (robotTag == 0)
    robotTag = -1;
  xprintf("tag: %d-answ: %s %d\n", robotTag, CMD_DUT_2_DUPPLY_SET, state);
}

void divSet(char *buf)
{
  CMD_SET(_1_INT, dut[ACT_DS].DIV_status, DIV_CPU_GPIO_Port, DIV_CPU_Pin);
  CMD_ANSWER(_2_INT, CMD_DIV_SET);
}

void dEdCheck(char *buf)
{
  CMD_CHECK_AND_ANSWER(_1_INT, dut[ACT_DS].D_Ed_status, DISPENSER_GPIO_Port, DISPENSER_Pin, CMD_D_ED_CHECK);
}

void drainPumpCheck(char *buf)
{
  CMD_CHECK_AND_ANSWER(_1_INT, dut[ACT_DS].DRAIN_PUMP_status, DRAIN_PUMP_GPIO_Port, DRAIN_PUMP_Pin, CMD_DRAIN_PUMP_CHECK);
}

void washPumpCheck(char *buf)
{
  GPIO_PinState pSt = GPIO_PIN_RESET;

  pSt = HAL_GPIO_ReadPin(WASH_PUMP_LO_GPIO_Port, WASH_PUMP_LO_Pin);
  dut[ACT_DS].WASH_PUMP_status[0] = pSt;
  pSt = HAL_GPIO_ReadPin(WASH_PUMP_HI_GPIO_Port, WASH_PUMP_HI_Pin);
  dut[ACT_DS].WASH_PUMP_status[1] = pSt;

  commandSucceded();

  int robotTag = getIntFromString(buf, _1_INT);
  if (robotTag == 0)
    robotTag = -1;
  xprintf("tag: %d-answ: %s %d;%d\n", robotTag, CMD_WASH_PUMP_CHECK, dut[ACT_DS].WASH_PUMP_status[0], dut[ACT_DS].WASH_PUMP_status[1]);
}

void bldcWshPumpCheck(char *buf)
{
  commandSucceded();
  dut[ACT_DS].dutVoltages[3] = adcGetValue(ADC_IN_3);

  if (dut[ACT_DS].dutVoltages[3] < 10000)
    dut[ACT_DS].BLDC_WASH_PUMP_status = 1;
  else
    dut[ACT_DS].BLDC_WASH_PUMP_status = 0;

  int robotTag = getIntFromString(buf, _1_INT);
  if (robotTag == 0)
    robotTag = -1;
  xprintf("tag: %d-answ: %s %d\n", robotTag, CMD_BLDC_WASH_PUMP_CHECK, dut[ACT_DS].BLDC_WASH_PUMP_status);
}

void rCheck(char *buf)
{
  CMD_CHECK_AND_ANSWER(_1_INT, dut[ACT_DS].R_status, HEATER_GPIO_Port, HEATER_Pin, CMD_R_CHECK);
}

void fanCheck(char *buf)
{
  CMD_CHECK_AND_ANSWER(_1_INT, dut[ACT_DS].FAN_status, FAN_CPU_GPIO_Port, FAN_CPU_Pin, CMD_FAN_CHECK);
}

void ledCheck(char *buf)
{
  CMD_CHECK_AND_ANSWER(_1_INT, dut[ACT_DS].LED_status, LIGHT_CPU_GPIO_Port, LIGHT_CPU_Pin, CMD_LED_CHECK);
}

void isbSet(char *buf)
{
  CMD_SET(_1_INT, dut[ACT_DS].ISB_status, ISB_CPU_GPIO_Port, ISB_CPU_Pin);
  CMD_ANSWER(_2_INT, CMD_ISB_SET);
}

void issSet(char *buf)
{
  CMD_SET(_1_INT, dut[ACT_DS].ISS_status, ISS_CPU_GPIO_Port, ISS_CPU_Pin);
  CMD_ANSWER(_2_INT, CMD_ISS_SET);
}

void iaqsSet(char *buf)
{
  CMD_SET(_1_INT, dut[ACT_DS].IAQS_status, IAQS_CPU_GPIO_Port, IAQS_CPU_Pin);
  CMD_ANSWER(_2_INT, CMD_IAQS_SET);
}

extern I2C_HandleTypeDef hi2c1;
#define MAX_RES_VALE 50000
static void i2cResistorSetValue(int res)
{
  if (res > MAX_RES_VALE)
    res = MAX_RES_VALE;

  uint16_t val = 255 * res / MAX_RES_VALE;

  if (val > 255)
    massert(0);

  uint8_t instr[2] = {0};
  instr[1] = val;
  HAL_I2C_Mem_Write(&hi2c1, (uint16_t)(I2C_RESISTOR_ADDR << 1), 0x0, 1,  (uint8_t*)&instr, sizeof(instr), 1000);
}

const uint16_t ntc_table[200] =
{
  1, 37544, 2, 35575, 3, 33700, 4, 31916, 5, 30219, 6, 28606, 7, 27075, 8, 25622, 9, 24244, 10, 22939,
  11, 21704, 12, 20535, 13, 19431, 14, 18389, 15, 17406, 16, 16479, 17, 15606, 18, 14785, 19, 14013, 20, 13289,
  21, 12608, 22, 11971, 23, 11374, 24, 10815, 25, 10292, 26, 9804, 27, 9348, 28, 8923, 29, 8526, 30, 8156,
  31, 7812, 32, 7491, 33, 7192, 34, 6914, 35, 6654, 36, 6413, 37, 6187, 38, 5976, 39, 5780, 40, 5595,
  41, 5422, 42, 5259, 43, 5105, 44, 4960, 45, 4822, 46, 4690, 47, 4564, 48, 4443, 49, 4326, 50, 4212,
  51, 4101, 52, 3993, 53, 3887, 54, 3782, 55, 3679, 56, 3576, 57, 3474, 58, 3372, 59, 3270, 60, 3169,
  61, 3067, 62, 2966, 63, 2865, 64, 2764, 65, 2664, 66, 2564, 67, 2466, 68, 2369, 69, 2274, 70, 2181,
  71, 2091, 72, 2004, 73, 1922, 74, 1844, 75, 1771, 76, 1705, 77, 1645, 78, 1594, 79, 1552, 80, 1520,
  81, 1498, 82, 1489, 83, 1494, 84, 1440, 85, 1430, 86, 1420, 87, 1410, 88, 1400, 89, 1390, 90, 1380,
  91, 1360, 92, 1350, 93, 1300, 94, 1290, 95, 1280, 96, 1270, 97, 1260, 98, 1250, 99, 1245, 100, 1240
};

#define TEMPERATURE_MULTIPLIER 100
static void ntcSetTempValue(int temp)
{
  ntc.actTemp = temp;
  temp = temp / TEMPERATURE_MULTIPLIER;
  dut[ACT_DS].waterTemp_value = temp;

  int res = ntc_table[(temp * 2) - 1];

  dbgxprintf("NTC act T=%d = R=%d\n", temp, res);

  i2cResistorSetValue(res);
}

#define ROOM_DEFAULT_TEMPERATURE 20
void reSet(char *buf)
{
  ntc.status = getIntFromString(buf, _1_INT);
  if (ntc.status == 0)
  {
    ntcSetTempValue(ROOM_DEFAULT_TEMPERATURE * TEMPERATURE_MULTIPLIER);

    dbgxprintf("Default temp set as 20\n");

    return;
  }
  ntc.startTemp = getIntFromString(buf, _2_INT) * TEMPERATURE_MULTIPLIER;
  ntc.stopTemp = getIntFromString(buf, _3_INT) * TEMPERATURE_MULTIPLIER;
  ntc.risingTime = getIntFromString(buf, _4_INT);
  int robotTag = getIntFromString(buf, _5_INT);

  if (ntc.risingTime == 0)
    ntc.risingTime = 1;

  ntc.risingStart = NS2S(getUptime());
  ntcSetTempValue(ntc.startTemp);

  if (robotTag == 0)
    robotTag = -1;
  xprintf("tag: %d-answ: %s %d\n", robotTag, CMD_RE_SET, ntc.status);
  commandSucceded();
}

#define NTC_UPDATE_TIME 1
#define NTC_MAX_TEMP 100
uint32_t upTimeUpdate_s = 0;
static void ntcSensorHandler(void)
{
  if (ntc.status == 0)
    return;

  uint32_t actualUpTime_s = NS2S(getUptime());

  if (actualUpTime_s - ntc.risingStart >= ntc.risingTime)
  {
    dbgxprintf("NTC rising finished\n");

    ntc.status = 0;
    ntc.startTemp = 0;
    ntc.stopTemp = 0;
    ntc.risingTime = 0;
    ntc.risingStart = 0;
    return;
  }

  if ((actualUpTime_s - upTimeUpdate_s) >= NTC_UPDATE_TIME)
  {
    int delta = (ntc.stopTemp - ntc.startTemp) / ntc.risingTime;

    if (delta > NTC_MAX_TEMP)
      delta = NTC_MAX_TEMP;

    upTimeUpdate_s = actualUpTime_s;

    dbgxprintf("delta=%d|ntc.actTemp=%d|%ds left\n", delta, ntc.actTemp,  ntc.risingTime - actualUpTime_s + ntc.risingStart);

    if (ntc.stopTemp > ntc.startTemp)
      ntcSetTempValue(ntc.actTemp + delta);
    else
      ntcSetTempValue(ntc.actTemp - delta);
  }
}

static fm_t fm =
{
  .status = 0,
  .amountOfWater = 0,
  .pulsesGenerated = 0,
  .pulsesToGenerate = 0
};

#define FM_PULSES_PER_1000_ML 474 //237*2
#define FM_PULSES_PRINTF (int)(237/2)

#define FM_ML_IN_1L 1000
void fmSet(char *buf)
{
  commandSucceded();
  fm.status = getIntFromString(buf, _1_INT);
  int tmp = getIntFromString(buf, _2_INT);

  if (tmp == 0)
    tmp = 1;

  fm.pulsesToGenerate = tmp * FM_PULSES_PER_1000_ML;
  fm.pulsesToGenerate = fm.pulsesToGenerate / FM_ML_IN_1L;
  fm.amountOfWater = tmp;
  if (fm.status == 1)
  {
    dbgxprintf("Amount of water: %dml -> %d pulses\n", fm.amountOfWater, fm.pulsesToGenerate);
  }
}

#define FM_TOGGLE_TIME_ms 20
uint32_t fmUpTimeUpdate_ms = 0;
static void fmHandler(void)
{
  if (fm.status == 0)
    return;

  uint32_t act_ms = NS2MS(getUptime());

  if ((act_ms - fmUpTimeUpdate_ms) >= FM_TOGGLE_TIME_ms)
  {
    fmUpTimeUpdate_ms = act_ms;
    fm.pulsesGenerated += 1;
    fm.pulsesToGenerate -= 1;
    fm.amountOfWater = (int)(fm.pulsesGenerated / FM_PULSES_PER_1000_ML);
    int watT = fm.pulsesGenerated * FM_ML_IN_1L / FM_PULSES_PER_1000_ML;

    dut[ACT_DS].waterLevel_value = watT;

    if (fm.pulsesGenerated % (FM_PULSES_PRINTF) == 0)
      dbgxprintf("FM amount of water = %d ml |act:%d | %d left\n", watT, fm.pulsesGenerated, fm.pulsesToGenerate);

    HAL_GPIO_TogglePin(FM_CPU_GPIO_Port, FM_CPU_Pin);
  }

  if (fm.pulsesToGenerate == 0)
  {
    dbgxprintf("Water filling finished\n");

    fm.status = 0;
    fm.pulsesToGenerate = 0;
    return;
  }
}

void doorSet(char *buf)
{
  CMD_SET(_1_INT, dut[ACT_DS].DOOR_status, DOOR_SW_GPIO_Port, DOOR_SW_Pin);
  CMD_ANSWER(_2_INT, CMD_DOOR_SET);
}

void ev1Check(char *buf)
{
  CMD_CHECK_AND_ANSWER(_1_INT, dut[ACT_DS].EV1_status, EV1_DET_GPIO_Port, EV1_DET_Pin, CMD_EV1_CHECK);
}

void ev2Check(char *buf)
{
  CMD_CHECK_AND_ANSWER(_1_INT, dut[ACT_DS].EV2_status, EV2_DET_GPIO_Port, EV2_DET_Pin, CMD_EV2_CHECK);
}

void ev3Check(char *buf)
{
  CMD_CHECK_AND_ANSWER(_1_INT, dut[ACT_DS].EV3_status, EV3_DET_GPIO_Port, EV3_DET_Pin, CMD_EV3_CHECK);
}

void p1Set(char *buf)
{
  CMD_SET(_2_INT, dut[ACT_DS].P1_status, P1_GPIO_Port, P1_Pin);
  CMD_ANSWER(_3_INT, CMD_P1_SET);
}

void p2Set(char *buf)
{
  CMD_SET(_2_INT, dut[ACT_DS].P2_status, P2_GPIO_Port, P2_Pin);
  CMD_ANSWER(_3_INT, CMD_P2_SET);
}

void optoOutputSet(char *buf)
{
  CMD_SET(_1_INT, dut[ACT_DS].isoOutput_status, OPTO_CPU_GPIO_Port, OPTO_CPU_Pin);
  CMD_ANSWER(_2_INT, CMD_OPTO_OUT_SET);
}

void extRelaySet(char *buf)
{
  CMD_SET(_1_INT, dut[ACT_DS].extRelay_status, LED_RELAY_GPIO_Port, LED_RELAY_Pin);
  CMD_ANSWER(_2_INT, CMD_EXT_RELAY_SET);
}

static void adcInit(void)
{
  xprintf("External ADC init\n");

  uint8_t buf = 0;
  HAL_StatusTypeDef stat = 0;

  buf = 0xD1; // setup: internal reference 2.048V, internal clock, unipolar (AIN = 0..Vref)
  stat = HAL_I2C_Mem_Write(&hi2c1, (uint16_t)(I2C_ADC_ADDR << 1), 0x0, 1,  (uint8_t*)&buf, sizeof(buf), 1000);
  if (stat)
    xprintf(C_RED"1 ADC init error 0x%x\n"C_NORMAL, stat);

  buf = 0x07; // scan AIN0-AIN3
  stat = HAL_I2C_Mem_Write(&hi2c1, (uint16_t)(I2C_ADC_ADDR << 1), 0x0, 1,  (uint8_t*)&buf, sizeof(buf), 1000);

  if (stat)
    xprintf(C_RED"2 ADC init error 0x%x\n"C_NORMAL, stat);
}

#define V_REF     12000 // voltage reference = 2.048V
static uint16_t adcGetValue(adcInput_t pin)
{
  uint8_t buf = 0x0;
  buf = (0x61 + (2 * pin));

  HAL_StatusTypeDef stat = HAL_I2C_Master_Transmit(&hi2c1, (uint16_t)(I2C_ADC_ADDR << 1), &buf, 1, 1000);

  if (stat)
    xprintf(C_RED"ADC error 0x%x\n"C_NORMAL, stat);

  stat = HAL_I2C_Master_Receive(&hi2c1, (uint16_t)(I2C_ADC_ADDR << 1), &buf, 1, 1000);

  if (stat)
    xprintf(C_RED"ADC error 0x%x\n"C_NORMAL, stat);


  uint16_t voltage = ((buf * V_REF) / 256); //in mV
  xprintf("pin%d|buf=0x%02x|stat=%d|VOLTAGE=%04d\n", pin, buf, stat, voltage);

  if (voltage > V_REF)
    massert(0);

  return voltage;
}

void dutVoltagesCheck(char *buf)
{
  commandSucceded();

  adcInit();

  dut[ACT_DS].dutVoltages[0] = adcGetValue(ADC_IN_1);
  dut[ACT_DS].dutVoltages[1] = adcGetValue(ADC_IN_2);
  dut[ACT_DS].dutVoltages[2] = adcGetValue(ADC_IN_3);
  dut[ACT_DS].dutVoltages[3] = adcGetValue(ADC_IN_4);

  int robotTag = getIntFromString(buf, _1_INT);
  if (robotTag == 0)
    robotTag = -1;
  xprintf("tag: %d-answ: %d;%d;%d;%d\n", robotTag, dut[ACT_DS].dutVoltages[0], dut[ACT_DS].dutVoltages[1], dut[ACT_DS].dutVoltages[2], dut[ACT_DS].dutVoltages[3]);
}

void platform5VSet(char *buf)
{
  //CMD_SET(_2_INT, dut[ACT_DS].platform5V_status, V5P0_ENABLE_GPIO_Port, V5P0_ENABLE_Pin);

  int val = getIntFromString(buf, _2_INT);
  dut[ACT_DS].platform5V_status = (uint8_t)val;
  HAL_GPIO_WritePin(V5P0_ENABLE_GPIO_Port, V5P0_ENABLE_Pin, (GPIO_PinState)val);
  GPIO_PinState pSt = HAL_GPIO_ReadPin(V5P0_ENABLE_GPIO_Port, V5P0_ENABLE_Pin);
  (pSt == (GPIO_PinState)val) ? commandSucceded() :  commandError();


  //CMD_ANSWER(_3_INT, CMD_PLATFORM_5V_SET);

  int robotTag = getIntFromString(buf, _3_INT);
  if (robotTag == 0)
  {
    robotTag = -1;
  }

  xprintf("tag: %d-answ: %s %d\n", robotTag, CMD_PLATFORM_5V_SET, pSt);


  if (dut[ACT_DS].platform5V_status == 1)
  {
    adcInit();
    dutVoltagesCheck("dutVoltagesCheck 12");
  }
}

void isoInputsCheck(char *buf)
{
  GPIO_PinState pSt = GPIO_PIN_RESET;

  dut[ACT_DS].isoInputs_status[0] = pSt;
  HAL_GPIO_ReadPin(GP0_CPU_GPIO_Port, GP0_CPU_Pin);
  pSt = HAL_GPIO_ReadPin(GP1_CPU_GPIO_Port, GP1_CPU_Pin);
  dut[ACT_DS].isoInputs_status[1] = pSt;

  pSt = HAL_GPIO_ReadPin(GP2_CPU_GPIO_Port, GP2_CPU_Pin);
  dut[ACT_DS].isoInputs_status[2] = pSt;

  int robotTag = getIntFromString(buf, _2_INT);
  if (robotTag == 0)
  {
    robotTag = -1;
  }
  xprintf("tag: %d-answ: %d;%d;%d\n", robotTag, dut[ACT_DS].isoInputs_status[0], dut[ACT_DS].isoInputs_status[1], dut[ACT_DS].isoInputs_status[2]);
}

void dutSupplySet(char *buf)
{
  CMD_SET(_1_INT, dut[ACT_DS].dutSupply_status, V_RELAY_GPIO_Port, V_RELAY_Pin)
  CMD_ANSWER(_2_INT, CMD_DUT_SUPPLY_SET);
}

void stm32Reset(char *buf)
{
  NVIC_SystemReset();
}

#define VARIABLE_NAME_LEN 32
volatile uint8_t gpioChangeFlag = 0;
uint8_t updateDutGPIOStatus(void)
{
  uint8_t retStat = 0;
  GPIO_PinState pSt = GPIO_PIN_RESET;

  char tmp[VARIABLE_NAME_LEN] = {0};

  GET_VARIABLE_NAME(EV3_DET_Pin, tmp);
  GPIO_INPUT_CHECK(EV3_DET_GPIO_Port, EV3_DET_Pin, dut[ACT_DS].EV3_status, dut[LAST_DS].EV3_status, tmp);

  GET_VARIABLE_NAME(EV2_DET_Pin, tmp);
  GPIO_INPUT_CHECK(EV2_DET_GPIO_Port, EV2_DET_Pin, dut[ACT_DS].EV2_status, dut[LAST_DS].EV2_status, tmp);

  GET_VARIABLE_NAME(EV1_DET_Pin, tmp);
  GPIO_INPUT_CHECK(EV1_DET_GPIO_Port, EV1_DET_Pin, dut[ACT_DS].EV1_status, dut[LAST_DS].EV1_status, tmp);

  GET_VARIABLE_NAME(WASH_PUMP_LO_Pin, tmp);
  GPIO_INPUT_CHECK(WASH_PUMP_LO_GPIO_Port, WASH_PUMP_LO_Pin, dut[ACT_DS].WASH_PUMP_status[0], dut[ACT_DS].WASH_PUMP_status[0], tmp);

  GET_VARIABLE_NAME(WASH_PUMP_HI_Pin, tmp);
  GPIO_INPUT_CHECK(WASH_PUMP_HI_GPIO_Port, WASH_PUMP_HI_Pin, dut[ACT_DS].WASH_PUMP_status[1], dut[ACT_DS].WASH_PUMP_status[1], tmp);

  GET_VARIABLE_NAME(DRAIN_PUMP_Pin, tmp);
  GPIO_INPUT_CHECK(DRAIN_PUMP_GPIO_Port, DRAIN_PUMP_Pin, dut[ACT_DS].DRAIN_PUMP_status, dut[LAST_DS].DRAIN_PUMP_status, tmp);

  GET_VARIABLE_NAME(DISPENSER_Pin, tmp);
  GPIO_INPUT_CHECK(DISPENSER_GPIO_Port, DISPENSER_Pin, dut[ACT_DS].D_Ed_status, dut[LAST_DS].D_Ed_status, tmp);

  GET_VARIABLE_NAME(HEATER_Pin, tmp);
  GPIO_INPUT_CHECK(HEATER_GPIO_Port, HEATER_Pin, dut[ACT_DS].R_status, dut[LAST_DS].R_status, tmp);

  GET_VARIABLE_NAME(GP0_CPU_Pin, tmp);
  GPIO_INPUT_CHECK(GP0_CPU_GPIO_Port, GP0_CPU_Pin, dut[ACT_DS].isoInputs_status[0], dut[LAST_DS].isoInputs_status[0], tmp);

  GET_VARIABLE_NAME(GP1_CPU_Pin, tmp);
  GPIO_INPUT_CHECK(GP1_CPU_GPIO_Port, GP1_CPU_Pin, dut[ACT_DS].isoInputs_status[1], dut[LAST_DS].isoInputs_status[1], tmp);

  GET_VARIABLE_NAME(GP2_CPU_Pin, tmp);
  GPIO_INPUT_CHECK(GP2_CPU_GPIO_Port, GP2_CPU_Pin, dut[ACT_DS].isoInputs_status[2], dut[LAST_DS].isoInputs_status[2], tmp);

  GET_VARIABLE_NAME(LIGHT_CPU_Pin, tmp);
  GPIO_INPUT_CHECK(LIGHT_CPU_GPIO_Port, LIGHT_CPU_Pin, dut[ACT_DS].LED_status, dut[LAST_DS].LED_status, tmp);

  GET_VARIABLE_NAME(FAN_CPU_Pin, tmp);
  GPIO_INPUT_CHECK(FAN_CPU_GPIO_Port, FAN_CPU_Pin, dut[ACT_DS].FAN_status, dut[LAST_DS].FAN_status, tmp);

  if (retStat == 1)
  {
    if (gpioChangeFlag == 0) // its not allowed to clear flag automatically
      gpioChangeFlag = retStat;
  }

  return retStat;
}

void checkDishWasherStatusChange(char *buf)
{
  commandSucceded();
  int robotTag = getIntFromString(buf, _1_INT);
  if (robotTag == 0)
    robotTag = -1;
  xprintf("tag: %d-change: %d\n", robotTag, gpioChangeFlag);

  xprintf("L:-");
  dut[ACT_DS].DOOR_status != dut[LAST_DS].DOOR_status ? xprintf("%d-", dut[ACT_DS].DOOR_status) : xprintf(" -");
  dut[ACT_DS].RE_status != dut[LAST_DS].RE_status ? xprintf("%d-", dut[ACT_DS].RE_status) : xprintf(" -");
  dut[ACT_DS].IAQS_status != dut[LAST_DS].IAQS_status ? xprintf("%d-", dut[ACT_DS].IAQS_status) : xprintf(" -");
  dut[ACT_DS].ISS_status != dut[LAST_DS].ISS_status ? xprintf("%d-", dut[ACT_DS].ISS_status) : xprintf(" -");
  dut[ACT_DS].ISB_status != dut[LAST_DS].ISB_status ? xprintf("%d-", dut[ACT_DS].ISB_status) : xprintf(" -");
  dut[ACT_DS].LED_status != dut[LAST_DS].LED_status ? xprintf("%d-", dut[ACT_DS].LED_status) : xprintf(" -");
  dut[ACT_DS].FAN_status != dut[LAST_DS].FAN_status ? xprintf("%d-", dut[ACT_DS].FAN_status) : xprintf(" -");
  dut[ACT_DS].DIV_status != dut[LAST_DS].DIV_status ? xprintf("%d-", dut[ACT_DS].DIV_status) : xprintf(" -");
  dut[ACT_DS].TURB_value != dut[LAST_DS].TURB_value ? xprintf("%02d-", dut[ACT_DS].TURB_value) : xprintf("  -");
  dut[ACT_DS].R_status != dut[LAST_DS].R_status ? xprintf("%d-", dut[ACT_DS].R_status) : xprintf(" -");
  dut[ACT_DS].D_Ed_status != dut[LAST_DS].D_Ed_status ? xprintf("%d-", dut[ACT_DS].D_Ed_status) : xprintf(" -");
  dut[ACT_DS].DRAIN_PUMP_status != dut[LAST_DS].DRAIN_PUMP_status ? xprintf("%d-", dut[ACT_DS].DRAIN_PUMP_status) : xprintf(" -");
  dut[ACT_DS].WASH_PUMP_status[0] != dut[LAST_DS].WASH_PUMP_status[0] ? xprintf("%d-", dut[ACT_DS].WASH_PUMP_status[0]) : xprintf(" -");
  dut[ACT_DS].WASH_PUMP_status[1] != dut[LAST_DS].WASH_PUMP_status[1] ? xprintf("%d-", dut[ACT_DS].WASH_PUMP_status[1]) : xprintf(" -");
  dut[ACT_DS].EV1_status != dut[LAST_DS].EV1_status ? xprintf("%d-", dut[ACT_DS].EV1_status) : xprintf(" -");
  dut[ACT_DS].EV2_status != dut[LAST_DS].EV2_status ? xprintf("%d-", dut[ACT_DS].EV2_status) : xprintf(" -");
  dut[ACT_DS].EV3_status != dut[LAST_DS].EV3_status ? xprintf("%d-", dut[ACT_DS].EV3_status) : xprintf(" -");
  dut[ACT_DS].P1_status != dut[LAST_DS].P1_status ? xprintf("%d-", dut[ACT_DS].P1_status) : xprintf(" -");
  dut[ACT_DS].P2_status != dut[LAST_DS].P2_status ? xprintf("%d", dut[ACT_DS].P2_status) : xprintf(" ");
  xprintf("\nH:-");
  dut[ACT_DS].BLDC_WASH_PUMP_status != dut[LAST_DS].BLDC_WASH_PUMP_status ? xprintf("%d-", dut[ACT_DS].BLDC_WASH_PUMP_status) : xprintf(" -");
  dut[ACT_DS].waterTemp_value != dut[LAST_DS].waterTemp_value ? xprintf("%02d-", dut[ACT_DS].waterTemp_value) : xprintf("  -");
  dut[ACT_DS].waterLevel_value != dut[LAST_DS].waterLevel_value ? xprintf("%04d-", dut[ACT_DS].waterLevel_value) : xprintf("    -");
  dut[ACT_DS].extRelay_status != dut[LAST_DS].extRelay_status ? xprintf("%d-", dut[ACT_DS].extRelay_status) : xprintf(" -");
  dut[ACT_DS].platform5V_status != dut[LAST_DS].platform5V_status ? xprintf("%d-", dut[ACT_DS].platform5V_status) : xprintf(" -");
  dut[ACT_DS].dutSupply_status != dut[LAST_DS].dutSupply_status ? xprintf("%d-", dut[ACT_DS].dutSupply_status) : xprintf(" -");
  dut[ACT_DS].programStartUptime != dut[LAST_DS].programStartUptime ? xprintf("%08d-", dut[ACT_DS].programStartUptime) : xprintf("        -");
  dut[ACT_DS].dutVoltages[0] != dut[LAST_DS].dutVoltages[0] ? xprintf("%04d-", dut[ACT_DS].dutVoltages[0]) : xprintf("    -");
  dut[ACT_DS].dutVoltages[1] != dut[LAST_DS].dutVoltages[1] ? xprintf("%04d-", dut[ACT_DS].dutVoltages[1]) : xprintf("    -");
  dut[ACT_DS].dutVoltages[2] != dut[LAST_DS].dutVoltages[2] ? xprintf("%04d-", dut[ACT_DS].dutVoltages[2]) : xprintf("    -");
  dut[ACT_DS].dutVoltages[3] != dut[LAST_DS].dutVoltages[3] ? xprintf("%04d-", dut[ACT_DS].dutVoltages[3]) : xprintf("    -");
  dut[ACT_DS].isoInputs_status[0] != dut[LAST_DS].isoInputs_status[0] ? xprintf("%d-", dut[ACT_DS].isoInputs_status[0]) : xprintf(" -");
  dut[ACT_DS].isoInputs_status[1] != dut[LAST_DS].isoInputs_status[1] ? xprintf("%d-", dut[ACT_DS].isoInputs_status[1]) : xprintf(" -");
  dut[ACT_DS].isoInputs_status[2] != dut[LAST_DS].isoInputs_status[2] ? xprintf("%d-", dut[ACT_DS].isoInputs_status[2]) : xprintf(" -");
  dut[ACT_DS].isoOutput_status != dut[LAST_DS].isoOutput_status ? xprintf("%d", dut[ACT_DS].isoOutput_status) : xprintf(" ");
  dut[ACT_DS].dut1Supply_status != dut[LAST_DS].dut1Supply_status ? xprintf("%d", dut[ACT_DS].dut1Supply_status) : xprintf(" ");
  dut[ACT_DS].dut2Supply_status != dut[LAST_DS].dut2Supply_status ? xprintf("%d", dut[ACT_DS].dut2Supply_status) : xprintf(" ");
  xprintf("\n");

  memcpy(&dut[LAST_DS], &dut[ACT_DS], sizeof(dut[LAST_DS]));
  if (gpioChangeFlag == 1)
    gpioChangeFlag = 0;
}

void printfDishWasherStatus(char *buf)
{
  int robotTag = getIntFromString(buf, _1_INT);
  if (robotTag == 0)
    robotTag = -1;
  xprintf("tag: %d\n", robotTag);

#define STAT_BUF_SIZE 128
  char outBuf[STAT_BUF_SIZE] = {0};
  xsprintf(outBuf, "L:-%d-%d-%d-%d-%d-%d-%d-%d-%d-%d-%d-%d-%d-%d-%d-%d-%d-%d-%d\n",
           dut[ACT_DS].DOOR_status,
           dut[ACT_DS].RE_status,
           dut[ACT_DS].IAQS_status,
           dut[ACT_DS].ISS_status,
           dut[ACT_DS].ISB_status,
           dut[ACT_DS].LED_status,
           dut[ACT_DS].FAN_status,
           dut[ACT_DS].DIV_status,
           dut[ACT_DS].TURB_value,
           dut[ACT_DS].R_status,
           dut[ACT_DS].D_Ed_status,
           dut[ACT_DS].DRAIN_PUMP_status,
           dut[ACT_DS].WASH_PUMP_status[0],
           dut[ACT_DS].WASH_PUMP_status[1],
           dut[ACT_DS].EV1_status,
           dut[ACT_DS].EV2_status,
           dut[ACT_DS].EV3_status,
           dut[ACT_DS].P1_status,
           dut[ACT_DS].P2_status);
  xprintf("%s", outBuf);

  xsprintf(outBuf, "H:-%d-%02d-%04d-%d-%d-%d-%08d-%04d-%04d-%04d-%04d-%d-%d-%d-%d\n",
           dut[ACT_DS].BLDC_WASH_PUMP_status,
           dut[ACT_DS].waterTemp_value,
           dut[ACT_DS].waterLevel_value,
           dut[ACT_DS].extRelay_status,
           dut[ACT_DS].platform5V_status,
           dut[ACT_DS].dutSupply_status,
           dut[ACT_DS].programStartUptime,
           dut[ACT_DS].dutVoltages[0],
           dut[ACT_DS].dutVoltages[1],
           dut[ACT_DS].dutVoltages[2],
           dut[ACT_DS].dutVoltages[3],
           dut[ACT_DS].isoInputs_status[0],
           dut[ACT_DS].isoInputs_status[1],
           dut[ACT_DS].isoInputs_status[2],
           dut[ACT_DS].isoOutput_status,
           dut[ACT_DS].dut1Supply_status,
           dut[ACT_DS].dut2Supply_status);

  xprintf("%s", outBuf);
  commandSucceded();
}

static void variablesInit(void)
{
  dut[ACT_DS].WASH_PUMP_status[0] = 1;
  dut[ACT_DS].WASH_PUMP_status[1] = 1;
  dut[ACT_DS].isoInputs_status[0] = 1;
  dut[ACT_DS].isoInputs_status[1] = 1;
  dut[ACT_DS].isoInputs_status[2] = 1;
  dut[ACT_DS].isoInputs_status[3] = 1;
}

void dishwasherPlatformInit(void)
{
  variablesInit();
  xprintf("Dishwasher Platform Init...\n");
  ntcSetTempValue(ROOM_DEFAULT_TEMPERATURE * TEMPERATURE_MULTIPLIER);

#ifdef PROJECT_DEBUG
  dutSupplySet("dutSupplySet 1 111\n");
  platform5VSet("platform5VSet 1 10\n");
#endif
  xprintf("Done\n");
}

void platformBackground(void)
{
  dut[ACT_DS].programStartUptime = (int)(NS2S(getUptime()));
  ntcSensorHandler();
  fmHandler();
}