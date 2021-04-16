#ifndef _WASHER_H_
#define _WASHER_H_

typedef enum
{
	STATE_OFF = 0,
	STATE_ON = 1,
} state_t;

typedef struct
{
	uint8_t optiDose1_status;
	uint8_t optiDose2_status;
	uint8_t fan_status;
	uint8_t softenerValve_status;
	uint8_t detergentValve_status;
	uint8_t waterValve_status;
	uint8_t steamGeneratorValve_status;
	uint8_t waterTemp_value;
	int waterLevel_value;
	uint8_t washHeater_status;
	uint8_t dryHeater_status;
	uint8_t universalMotor_status[2];
	uint8_t bldcMotor_status;
	uint32_t bldcMotor_speed;
	uint8_t extRelay_status;
	uint8_t platform5V_status;
	uint8_t dutSupply_status;
	uint32_t programStartUptime;
	uint16_t dutVoltages[4];
	uint8_t isoInputs_status[3];
	uint8_t isoOutput_status;
	uint8_t doorLock_status;
	uint8_t doorLockCoil_value;
	uint8_t pump_status;
	uint16_t univMotorSpeed;

} dutStatus_t;

typedef struct
{
	uint8_t status;
	int actTemp;
	int startTemp;
	int stopTemp;
	uint32_t risingTime;
	uint32_t risingStart;
} ntc_t;

typedef struct
{
	uint8_t status;
	int amountOfWater;
	int pulsesGenerated;
	int pulsesToGenerate;
} fm_t;

typedef enum
{
	ADC_IN_1 = 0,
	ADC_IN_2 = 1,
	ADC_IN_3 = 2,
	ADC_IN_4 = 3,
} adcInput_t;

typedef enum
{
	NO_WATER = 0,
	HALF_WATER = 50,
	FULL_WATER = 100,
} waterLevel_t;

typedef enum
{
	BLDC_0_RPM = 0,
	BLDC_45_RPM_R,
	BLDC_45_RPM_L,
	BLDC_200_RPM,
	BLDC_400_RPM,
	BLDC_600_RPM,
	BLDC_800_RPM,
	BLDC_1000_RPM,
	BLDC_1100_RPM,
	BLDC_1200_RPM,
	BLDC_1300_RPM,
	BLDC_1400_RPM,
	BLDC_1500_RPM,
} bldcState_t;

int bldcBufferPut(char ch);
int bldcBufferGet(void);
void platformBackground(void);
void setUnivMotorType(char *buf);
void doorControl(char *buf);
uint8_t updateDutGPIOStatus(void);
void fmSet(char *buf);
void checkWasherStatusChange(char *buf);
void printfWasherStatus(char *buf);
void washerPlatformInit(void);
void dutVoltagesCheck(char *buf);
void optoOutputSet(char *buf);
void extRelaySet(char *buf);
void isoInputsCheck(char *buf);
void dutSupplySet(char *buf);
void stm32Reset(char *buf);
void platform5VSet(char *buf);
void tempSet(char *buf);
void doorSet(char *buf);
void waterLevelSet(char *buf);
void doorCoilCheck(char *buf);
void optiDose1(char *buf);
void optiDose2(char *buf);
void pumpCheck(char *buf);
void dryFanCheck(char *buf);
void univMotorCheck(char *buf);
void softenerCheck(char *buf);
void detergentCheck(char *buf);
void steamGenCheck(char *buf);
void waterValveCheck(char *buf);
void washHeaterCheck(char *buf);
void dryHeaterCheck(char *buf);
void tachoF(char *buf);

#endif