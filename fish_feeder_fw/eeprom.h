#ifndef EEPROM_H
#define EEPROM_H

#include <stdint.h>

#define _EEPROM_ADDR 0xA0
#define EEPROM_SETTINGS_ADDRESS 0x00
#define EEPROM_CRC32_ADDRESS 0x60
#define EEPROM_LANGUAGE_ADDRESS 0x64

#define DATA_LEN 32
#define DUAL_BYTE_LEN 16
#define CRC32_DATA_SIZE_BYTES 4

//#define _EEPROM_ADDR 0x50
void __attribute__((optimize("-O0")))writeCrc32ToEeprom(uint32_t crcVal);
uint32_t __attribute__((optimize("-O0")))readCrc32FromEeprom(void);
void __attribute__((optimize("-O0")))writeLanguageToEeprom(lang_t actLang);
lang_t __attribute__((optimize("-O0")))readLanguageFromEeprom(void);
void __attribute__((optimize("-O0")))backupMotorDataToEeprom(void);
void __attribute__((optimize("-O0")))restoreMotorDataFromEeprom(void);
void __attribute__((optimize("-O0")))restoreDefaultSettings(void);

#endif /* end of include guard: EEPROM_H */
