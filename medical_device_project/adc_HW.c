#include <stm32f4xx.h>
#include "gpio.h"
#include <string.h>
#include "pulseControlModulePinmux.h"
#include "pulseControlModule_API.h"
#include "pulseControlModule_HW.h"
#include "debugModule_API.h"
#include "sys_time.h"

extern pulseControl_module_t _API_pulseControl;

static const GPIO_InitDef_t adcPinMux[] =
{
	{VHT_PORT, VHT_PIN, MODE_ANALOG, TYPE_NOT_RELEVANT, SPEED_NOT_RELEVANT, PULL_NONE, AF_NONE},
	{IPAT_PORT, IPAT_PIN, MODE_ANALOG, TYPE_NOT_RELEVANT, SPEED_NOT_RELEVANT, PULL_NONE, AF_NONE},
	{VPAT_PORT, VPAT_PIN, MODE_ANALOG, TYPE_NOT_RELEVANT, SPEED_NOT_RELEVANT, PULL_NONE, AF_NONE},
};

/**
 * @brief ADC module pinmux init
 */
void adcPinMuxInit(void)
{
	int gpioConfigSize = sizeof(adcPinMux) / sizeof(GPIO_InitDef_t);
	pinmuxInit(adcPinMux, gpioConfigSize);
}
#define ADC_DMA_BUFFER_SIZE 800
static volatile uint16_t adcSamples[ADC_DMA_BUFFER_SIZE] = {0};

/**
 * @brief ADC DMA init
 */
static void adcDmaInit(void)
{
	DMA2_Stream0->CR =  DMA_SxCR_CHSEL_1 | DMA_SxCR_MSIZE_0 | DMA_SxCR_PSIZE_0 | DMA_SxCR_MINC;
	DMA2_Stream0->NDTR = ADC_DMA_BUFFER_SIZE;
	DMA2_Stream0->PAR = (uint32_t)&ADC3->DR;
	DMA2_Stream0->M0AR = (uint32_t)adcSamples;
}

/**
 * @brief timer for adc dma init
 */
static void initTimerForAdcDma(void)
{
	/*
	f = HCLK/2 /((PSC + 1) * (ARR + 1))
	f = 100 kHz; T = 10 us
	*/

	RCC->APB2ENR |= RCC_APB2ENR_TIM1EN;
	__DSB();
	TIM1->PSC = 1680 - 1; // prescaler
	TIM1->ARR = 10 - 1; // auto-reload register
	TIM1->CCR1 = 10 - 1; // capture/compare register ch.1
	TIM1->CNT = 0;
	TIM1->CR1 |= TIM_CR1_ARPE ;// Auto-reload preload enable
	TIM1->CCMR1 = TIM_CCMR1_OC1M_2 | TIM_CCMR1_OC1M_1 | TIM_CCMR1_OC1PE; //preload enable
	TIM1->CCER |= TIM_CCER_CC1E;  //output enable
	TIM1->EGR |= TIM_EGR_UG; // Force update generation
	TIM1->CR1 |= TIM_CR1_CEN;
	RCC->AHB1ENR |= RCC_AHB1ENR_DMA2EN;
	__DSB();
}

/**
 * @brief main DMA init function
 */
void adcDMAInit(void)
{
	initTimerForAdcDma();
	adcDmaInit();

	RCC->APB2ENR |= RCC_APB2ENR_ADC3EN;
	__DSB();

	ADC->CCR = ADC_CCR_DMA_0 | ADC_CCR_ADCPRE_0 | ADC_CCR_ADCPRE_1; // prescaler 168M/8 (PCLK2/8) = 21 MHz

	const uint32_t awd_htr = 3900; //3,14V=>46mA
	const uint32_t awd_ltr = 0;
	ADC3->LTR = awd_ltr; // AWD configuration
	ADC3->HTR = awd_htr; // AWD configuration

	ADC3->CR2 = ADC_CR2_ADON | ADC_CR2_EXTEN_0 | ADC_CR2_DDS | ADC_CR2_JEXTSEL | ADC_CR2_JEXTEN | ADC_CR2_CONT | ADC_CR2_DMA;
	ADC3->CR1 = ADC_CR1_SCAN | ADC_CR1_AWDEN | ADC_CR1_AWDIE | ADC_CR1_AWDSGL | ADC_CR1_AWDCH_2 | ADC_CR1_AWDCH_0; // AWDG analog watchodg - IN5 - IPAT protection
	ADC3->SMPR2 = ADC_SMPR2_SMP4_2 | ADC_SMPR2_SMP4_1 | ADC_SMPR2_SMP5_2 | ADC_SMPR2_SMP5_1;//144 cycles
	ADC3->SQR1 = ADC_SQR1_L_0;
	ADC3->SQR3 = ADC_SQR3_SQ1_2 | ADC_SQR3_SQ2_2 | ADC_SQR3_SQ2_0;
	ADC3->JSQR = ADC_JSQR_JSQ4_1 | ADC_JSQR_JSQ4_2;
	NVIC_EnableIRQ(ADC_IRQn);
}

#define ADC_VHT_VOTLAGE_DIVIDER_NU 83  //R82/(R82+R86) voltage divider
#define ADC_VPAT_VOTLAGE_DIVIDER_NU 1887  //1/(R114/(R111+R114))*1000 voltage divider
#define ADC_VPAT_VOTLAGE_DIVIDER_DE 100
#define ADC_IPAT_RESISTOR 68 //R112 
#define ADC_DMA_TEMP_BUFFER (ADC_DMA_BUFFER_SIZE / 2)
#define _12BIT_ADC_MAX_VAL 4095 //12bit ADC
#define MIN_TREATMENT_OUTPUT_VOLTAGE_RAW_ADC 3 // 27*3.3/4096*1/(2.2k/102.2k) ~= 1 V
#define MIN_TREATMENT_OUTPUT_CURRENT_RAW_ADC 3
#define _ADC_VREF_NU 3300
#define _ADC_VREF_DE 1000

/**
 * @brief VHT measurment
 */
uint16_t adcMeasureVHTVoltage(void)
{
	uint32_t TimeOut = ADC_TIMEOUT_VAL;

	uint16_t adcResult = 0;
	uint32_t adcSum = 0;

	int i;
#define VHT_ADC_SAMPLES_CNT 10

	for (i = 0; i < VHT_ADC_SAMPLES_CNT; i++)
	{
		ADC3->CR2 |= ADC_CR2_JSWSTART;

		while (!(ADC3->SR & ADC_SR_JEOC)) //end of conversion flag check
		{
			if (!(TimeOut--))
			{
				_API_logE("ADC3_IN6:VHT timeout!\n");
				return 0;
			}
		};

		if (ADC3->SR & ADC_SR_JEOC) //end of conversion flag check
		{
			ADC3->SR &= ~ADC_SR_JEOC;
			adcResult = (uint16_t)ADC3->JDR1;
			adcSum += adcResult; // avg value calculation 1
		}
	}
	adcSum = adcSum / VHT_ADC_SAMPLES_CNT; // avg value calculation 2

	uint32_t retVal = adcSum * _ADC_VREF_NU * ADC_VHT_VOTLAGE_DIVIDER_NU; //calculate voltage value
	retVal = retVal / ((_12BIT_ADC_MAX_VAL + 1) * _ADC_VREF_DE);

	return retVal;
}

#define VPAT_ARRAY_INDEX(_arg_)   (_arg_)
#define IPAT_ARRAY_INDEX(_arg_)   (_arg_+1)
/**
 * @brief ZPAT calculation
 */
uint16_t calculateImpedance(void)
{
	int i;
	uint32_t arrSum = 0;
	uint16_t arrCnt = 0;
	uint32_t numerator = 0;

	volatile uint32_t tempImpedance = 0;

	for (i = 0; i < ADC_DMA_BUFFER_SIZE; i += 2) // iterate whole DMA buffer
	{
		if (adcSamples[VPAT_ARRAY_INDEX(i)] > MIN_TREATMENT_OUTPUT_VOLTAGE_RAW_ADC && adcSamples[IPAT_ARRAY_INDEX(i)] > MIN_TREATMENT_OUTPUT_CURRENT_RAW_ADC)
		{
			numerator = ADC_IPAT_RESISTOR * adcSamples[VPAT_ARRAY_INDEX(i)] * ADC_VPAT_VOTLAGE_DIVIDER_NU;
			numerator = numerator / ADC_VPAT_VOTLAGE_DIVIDER_DE;
			tempImpedance =  (numerator / adcSamples[IPAT_ARRAY_INDEX(i)]);


			arrSum += tempImpedance;
			arrCnt++;
		}
	}
	if (arrCnt < 10) //no correct data
		return 9999;
	else
		return (uint16_t)(arrSum / arrCnt); //divide array sum to samples number
}

/**
 * @brief adc dma stream start
 */
void adcEnableDmaStream(void)
{
	TIM1->CR1 &= ~TIM_CR1_CEN;
	TIM1->CNT = 0;

	ADC3->CR2 &= ~(ADC_CR2_SWSTART | ADC_CR2_DMA);
	DMA2_Stream0->CR &= ~DMA_SxCR_EN;
	DMA2->LIFCR |= DMA_LIFCR_CTCIF0 | DMA_LIFCR_CHTIF0 | DMA_LIFCR_CTEIF0 | DMA_LIFCR_CDMEIF0 | DMA_LIFCR_CFEIF0;

	DMA2_Stream0->NDTR = ADC_DMA_BUFFER_SIZE;
	DMA2_Stream0->M0AR = (uint32_t)adcSamples;
	DMA2_Stream0->CR |= DMA_SxCR_EN;

	ADC3->SR = 0;
	ADC3->CR2 |= ADC_CR2_SWSTART | ADC_CR2_DMA;
	TIM1->CR1 = TIM_CR1_CEN;
}

/**
 * @brief pulse Avg voltage calculation
 */
uint16_t calculatePulseAvgVoltage(void) // returns value in mV
{
	int i;
	uint32_t arrSum = 0;
	uint16_t arrCnt = 0;

	volatile uint32_t tempVoltage = 0;

	for (i = 0; i < ADC_DMA_BUFFER_SIZE; i += 2) // iterate whole DMA buffer
	{
		if (adcSamples[i] > MIN_TREATMENT_OUTPUT_VOLTAGE_RAW_ADC && adcSamples[i] < _12BIT_ADC_MAX_VAL)
		{
			tempVoltage =  ((adcSamples[VPAT_ARRAY_INDEX(i)] * _ADC_VREF_NU) / ((_12BIT_ADC_MAX_VAL + 1)));
			arrSum += tempVoltage;
			arrCnt++;
		}
	}
	return (uint16_t)(arrSum / arrCnt);
}
#define VPAT_VOLTAGE_DIV_1 53 //voltage divider R111 to R114
#define VPAT_VOLTAGE_DIV_2 1000
/**
 * @brief pulse Max voltage calculation
 */
uint32_t calculatePulseMaxVoltage(void) // returns value in mV
{
	int i;
	uint32_t maxVal = 0;

	volatile uint32_t tempVoltage = 0;

	for (i = 0; i < ADC_DMA_BUFFER_SIZE; i += 2) // iterate whole DMA buffer
	{
		if (adcSamples[i] > MIN_TREATMENT_OUTPUT_VOLTAGE_RAW_ADC && adcSamples[i] < _12BIT_ADC_MAX_VAL)
		{
			tempVoltage =  adcSamples[VPAT_ARRAY_INDEX(i)];
			tempVoltage = tempVoltage * 3300;
			tempVoltage = tempVoltage / 4096;//value in mV

			tempVoltage = tempVoltage * 1000;
			tempVoltage = tempVoltage / 53;

//vpat measure doesnt work properly because zener diode D18
//D18 have Vz(min)=2.4V => Vpat=45V and higher

			tempVoltage = tempVoltage * 1086; // linearity correction
			tempVoltage = tempVoltage / 1000;

			if (tempVoltage > maxVal)
				maxVal = tempVoltage;
		}
	}
	return maxVal;
}

/**
 * @brief pulse current calculation (avg value for whole pulse)
 */
uint16_t calculatePulseCurrent(void) // returns value in uA
{
	int i;
	uint32_t counter = 0;
	uint32_t denominator = 0;
	uint32_t maxVal = 0;
	volatile uint32_t tempCurrent = 0;

	for (i = 0; i < ADC_DMA_BUFFER_SIZE; i += 2) // iterate whole DMA buffer
	{
		if (adcSamples[i] > MIN_TREATMENT_OUTPUT_VOLTAGE_RAW_ADC && adcSamples[i] < _12BIT_ADC_MAX_VAL)
		{
			counter = adcSamples[IPAT_ARRAY_INDEX(i)] * 33000;
			denominator = 4095 * 68;

			tempCurrent = counter / denominator;
			tempCurrent = tempCurrent * 100;;

			if (tempCurrent > maxVal)
				maxVal = tempCurrent;
		}
	}
	return maxVal; // pulse top value
}

/**
 * @brief NE555 trigger generation
 */
void hvSwitchSet(uint8_t nSt)
{
	if (nSt)
	{
		while (TIM2->CNT < 4000) {}; //synch NE555 pulse window with DAC
		GPIO_WritePin(HV_SWITCH_PORT, HV_SWITCH_PIN, GPIO_PIN_SET);
	}
	else
		GPIO_WritePin(HV_SWITCH_PORT, HV_SWITCH_PIN, GPIO_PIN_RESET);
}

/**
 * @brief waits for dma
 */
void waitForDmaAdc(void)
{
	while (!(DMA2->LISR & DMA_LISR_TCIF0)) {};

	if (DMA2->LISR & DMA_LISR_TCIF0)
		DMA2->LIFCR |= DMA_LIFCR_CTCIF0;
}



/**
 * @brief zPAT linearity correction
 * @param val input value
 * @return output value
 */
static uint16_t z_measurmentLinearityCorrectionImp(uint16_t inputImp)
{
	uint16_t outputImp;
	outputImp = inputImp;

	switch (inputImp) // correction due to analog nonlinearity
	{
	//
	case TR_IMPEDANCE_0R ... TR_IMPEDANCE_2000R: outputImp = (inputImp * VPAT_MEASURE_TRIANGLE_CORR_94) / VPAT_MEASURE_TRIANGLE_CORR_DEN; break;
	case TR_IMPEDANCE_2001R ... TR_IMPEDANCE_4000R: outputImp = (inputImp * VPAT_MEASURE_TRIANGLE_CORR_99) / VPAT_MEASURE_TRIANGLE_CORR_DEN; break;
	case TR_IMPEDANCE_4001R ... TR_IMPEDANCE_6000R: outputImp = (inputImp * VPAT_MEASURE_TRIANGLE_CORR_101) / VPAT_MEASURE_TRIANGLE_CORR_DEN; break;
	case TR_IMPEDANCE_6001R ... TR_IMPEDANCE_8000R: outputImp = (inputImp * VPAT_MEASURE_TRIANGLE_CORR_102) / VPAT_MEASURE_TRIANGLE_CORR_DEN; break;
	}
	return outputImp;
}

/**
 * @brief zPAT linearity correction
 * @param val input value
 * @return output value
 */
static uint16_t z_measurmentLinearityCorrectionVolt(uint16_t inputImp)
{
	uint16_t outputImp = inputImp;

	switch (_API_pulseControl.outputVoltageInVolts) // correction due to analog nonlinearity
	{
	case TR_VOLT_1V ... TR_VOLT_25V: outputImp = (inputImp * VPAT_MEASURE_TRIANGLE_CORR_100) / VPAT_MEASURE_TRIANGLE_CORR_DEN; break;
	case TR_VOLT_26V ... TR_VOLT_30V: outputImp = (inputImp * VPAT_MEASURE_TRIANGLE_CORR_101) / VPAT_MEASURE_TRIANGLE_CORR_DEN; break;
	case TR_VOLT_31V ... TR_VOLT_35V: outputImp = (inputImp * VPAT_MEASURE_TRIANGLE_CORR_102) / VPAT_MEASURE_TRIANGLE_CORR_DEN; break;
	case TR_VOLT_36V ... TR_VOLT_40V: outputImp = (inputImp * VPAT_MEASURE_TRIANGLE_CORR_103) / VPAT_MEASURE_TRIANGLE_CORR_DEN; break;
	case TR_VOLT_41V ... TR_VOLT_45V: outputImp = (inputImp * VPAT_MEASURE_TRIANGLE_CORR_106) / VPAT_MEASURE_TRIANGLE_CORR_DEN; break;
	case TR_VOLT_46V ... TR_VOLT_50V: outputImp = (inputImp * VPAT_MEASURE_TRIANGLE_CORR_107) / VPAT_MEASURE_TRIANGLE_CORR_DEN; break;
	case TR_VOLT_51V ... TR_VOLT_55V: outputImp = (inputImp * VPAT_MEASURE_TRIANGLE_CORR_109) / VPAT_MEASURE_TRIANGLE_CORR_DEN; break;
	case TR_VOLT_56V ... TR_VOLT_60V: outputImp = (inputImp * VPAT_MEASURE_TRIANGLE_CORR_111) / VPAT_MEASURE_TRIANGLE_CORR_DEN; break;
	}

	return outputImp;
}

#define ADC_IPAT_REAL_MAX_IMP 60000 // limitted due to HW
#define IPAT_MEASURE_TRIANGLE_CORR_DEN 100

static uint16_t ipatCorr1(uint16_t inpVal)
{
	uint32_t nVal = inpVal;

	switch (_API_pulseControl.outputVoltageInVolts)
	{
	case TR_VOLT_1V ... TR_VOLT_3V: nVal -= 1000; break; //depends od pulse shape
	case TR_VOLT_4V ... TR_VOLT_7V: nVal -= 1500; break; //depends od pulse shape
	case TR_VOLT_8V ... TR_VOLT_21V: nVal += 3000; break; //depends od pulse shape
	case TR_VOLT_22V: nVal += 4000; break; //depends od pulse shape
	case TR_VOLT_23V: nVal += 5000; break; //depends od pulse shape
	case TR_VOLT_24V: nVal += 6000; break; //depends od pulse shape
	case TR_VOLT_25V: nVal += 7000; break; //depends od pulse shape
	case TR_VOLT_26V: nVal += 8000; break; //depends od pulse shape
	case TR_VOLT_27V ... TR_VOLT_60V: nVal += 9000; break; //depends od pulse shape
	}
	return nVal;
}

static uint16_t ipatCorr2(uint16_t inpVal)
{
	uint32_t nVal = inpVal;

	switch (_API_pulseControl.outputVoltageInVolts)
	{
	case TR_VOLT_1V ... TR_VOLT_3V: nVal -= 1000; break; //depends od pulse shape
	case TR_VOLT_20V ... TR_VOLT_27V: nVal += 1000; break; //depends od pulse shape
	case TR_VOLT_28V ... TR_VOLT_41V: nVal += 3000; break; //depends od pulse shape
	case TR_VOLT_42V ... TR_VOLT_45V: nVal += 4000; break; //depends od pulse shape
	case TR_VOLT_46V ... TR_VOLT_55V: nVal += 5000; break; //depends od pulse shape
	case TR_VOLT_56V ... TR_VOLT_60V: nVal += 6000; break; //depends od pulse shape
	}
	return nVal;
}

static uint16_t ipatCorr3(uint16_t inpVal)
{
	uint32_t nVal = inpVal;

	switch (_API_pulseControl.outputVoltageInVolts)
	{
	case TR_VOLT_50V ... TR_VOLT_60V: nVal += 2000; break; //depends od pulse shape
	}
	return nVal;
}

/**
 * @brief IPAT current correction - input value is an average of whole pulse current
 * @param val input value
 * @return current in uA
 */
static uint32_t ipat_measurmentMethodCorrection(uint32_t val)
{
	uint32_t retVal = val;
	switch (_API_pulseControl.impedanceValue)
	{
	case TR_IMPEDANCE_0R ... TR_IMPEDANCE_1000R:
		retVal = ipatCorr1(val);
		break;
	case TR_IMPEDANCE_1001R ... TR_IMPEDANCE_1500R:
		retVal = ipatCorr2(val);
		break;
	case TR_IMPEDANCE_1501R ... TR_IMPEDANCE_2500R:
		retVal = ipatCorr3(val);
		break;
	}
	return retVal;
}

uint32_t lastCheckTime = 0;
static void vpatMonitor(void)
{
	uint32_t actualTime = NS2MS(getUptime());

	if (actualTime - lastCheckTime >= 1000)
	{
		_API_logE("SET= %d V| REAL= %d mV\n", _API_pulseControl.outputVoltageInVolts, _API_pulseControl.realOutputVoltage);
		lastCheckTime = actualTime;
	}
}

/**
 * @brief adc pulse module handler
 */
void adcHandler(void)
{
	if (_API_pulseControl.adcEnableFlag) //enables adc dma stream due to  manager decision
	{
		adcEnableDmaStream();
		_API_pulseControl.adcEnableFlag = 0;
		//  adcBufferConsoleDump(); // DEBUG

		hvSwitchSet(1); // trigger NE555 window
		waitForDmaAdc(); //wait for adc conversion
	}
	if (_API_pulseControl.adcCalculationsFlag) //enables adc calculations due to  manager decision
	{
		_API_pulseControl.impedanceValue = calculateImpedance();
		_API_pulseControl.impedanceValue = z_measurmentLinearityCorrectionImp(_API_pulseControl.impedanceValue);
		_API_pulseControl.impedanceValue = z_measurmentLinearityCorrectionVolt(_API_pulseControl.impedanceValue);

		_API_pulseControl.currentValue = calculatePulseCurrent(); // returns avg current
		_API_pulseControl.currentValue = ipat_measurmentMethodCorrection(_API_pulseControl.currentValue); //pulse shape current correction

		_API_pulseControl.realOutputVoltage = calculatePulseMaxVoltage();

		_API_pulseControl.highVoltageValue = adcMeasureVHTVoltage();
		_API_pulseControl.adcCalculationsFlag = 0;
		memset((uint16_t*)&adcSamples[0], 0, ADC_DMA_BUFFER_SIZE);
		hvSwitchSet(0);
	}

	vpatMonitor();

}