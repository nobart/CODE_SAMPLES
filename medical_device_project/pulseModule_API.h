#ifndef _ANALOG_API_H_
#define _ANALOG_API_H_
#include <stm32f4xx.h>

/**
 * BIT for pulse module typedef declaration
 */
typedef enum
{
  PULSE_TEST_INIT = 0,
  PULSE_TEST_RUN,
  PULSE_TEST_OK,
  PULSE_TEST_FAIL,
} pulseControlModuleTestStatus_t;

/**
 * pulse module _API_ struct
 */
typedef struct
{
  uint32_t outputVoltageInVolts;
  uint8_t dacEnableFlag;
  uint8_t adcEnableFlag;
  uint8_t electrodesConnetedFlag;
  uint8_t adcCalculationsFlag;
  uint16_t impedanceValue;
  uint16_t highVoltageValue;
  uint32_t currentValue;
  uint32_t outRelay1State;
  uint32_t polarityRelay2State;
  uint32_t photoChargerState;
  uint32_t photoChargerReadyFlag;
  uint32_t realOutputVoltage;
  pulseControlModuleTestStatus_t voltageTest;
  pulseControlModuleTestStatus_t dacTest;
  pulseControlModuleTestStatus_t pulseTimeTest;
} __attribute__ ((packed)) pulseControl_module_t;

/**
 * Pulse module handler.
 */
void _API_pulseControlModuleHandler(void);

/**
 * Module initialisation.
 */
void _API_pulseControlInit(void);

/**
 * Emergmency shutdown handler.
 */
void _API_pulseControlModuleEmergencyShutdown(void);

#endif /* _ANALOG_API_H_ */