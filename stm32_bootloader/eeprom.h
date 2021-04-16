/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef _EEPROM_H
#define _EEPROM_H

/* Includes ------------------------------------------------------------------*/
#include "stm32l0xx_hal.h"
#include "stdio.h"
#include "string.h"
#include <stdbool.h>
#include "xprintf.h"
#include "i2c.h"
#include "usart.h"
#include "gpio.h"
#include <stdlib.h>
#include "checksum.h"
#include "eeprom.h"

#define EEPROM_BANK_NUMBERS 2
#define EEPROM_MAIN_BANK 0
#define EEPROM_BACKUP_BANK 1

#define FLASH_APP_ADDRESS 				(uint32_t) 0x08005000 // 20K
#define FLASH_MAX_MEM   				(uint32_t) 0x0800FFFF // 64K
#define EEPROM_ADD1     				(0xA4) //main
#define EEPROM_ADD2     				(0xA6) //backup
#define EEPROM_MEM_ADDRESS  			0x0000
#define EEPROM_MAX_MEM    				0xFFFF

#define EXIST       					0xFFF0
#define CONFIRM       					0xFFF1 //1 if was set by user via vcp
#define COUNTBYTE     					0xFFF2 //power-up after flash update counter. if 0 backup apk will be written into flash 
#define LENGTHBYTES     				0xFFF3 //image.bin bytes number
#define CRC32BYTES      				0xFFF9 //eeprom crc value

#define _DELAY_NUM 10
typedef struct
{
  uint8_t exist;
  uint16_t length;
  uint8_t confirm;
  uint8_t count;
  uint32_t readcrc;
  uint32_t memAddr;
  uint8_t addr;
} eepromHeaders_t;

#define Y_CHAR         					0x59
#define y_CHAR         					0x79

int updateFromEepromHandler(void);

#endif  /* _EEPROM_H */

//*****************************************************************************
