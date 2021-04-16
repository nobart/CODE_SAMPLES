#include "xprintf.h"
#include "stdlib.h"
#include "string.h"
#include "i2c.h"
#include "eeprom.h"
#include "motor.h"
#include "board.h"
#include "log.h"
#include "gpio.h"
#include "checksum.h"
#include "iwdg.h"

extern I2C_HandleTypeDef hi2c1;

#define I2C_TIMEOUT_VAL 250
#define EEPR_WRITE_STRUCT_PRINTF 0
#define I2C_RETRY_CNT 10
void __attribute__((optimize("-O0")))writeCrc32ToEeprom(uint32_t crcVal)
{
  int retryCnt = I2C_RETRY_CNT;
  HAL_StatusTypeDef stat;
  do
    {
      stat = HAL_I2C_Mem_Write(&hi2c1, _EEPROM_ADDR, EEPROM_CRC32_ADDRESS, 1,  (uint8_t*)&crcVal, sizeof(crcVal), I2C_TIMEOUT_VAL);
      if (stat)
        {
          logE(EEPROM_LOG, "i2c crc32 write error\n");
          delay_ms(100);
          iwdgFeed();
          retryCnt--;
        }
      else
        {
          retryCnt = 0;
        }
    }
  while (retryCnt);
}

void __attribute__((optimize("-O0")))writeLanguageToEeprom(lang_t actLang)
{
  int retryCnt = I2C_RETRY_CNT;
  HAL_StatusTypeDef stat;
  do
    {
      stat = HAL_I2C_Mem_Write(&hi2c1, _EEPROM_ADDR, EEPROM_LANGUAGE_ADDRESS, 1,  (uint8_t*)&actLang, sizeof(actLang), I2C_TIMEOUT_VAL);
      if (stat)
        {
          logE(EEPROM_LOG, "i2c LANGUAGE write error %d\n", stat);
          delay_ms(100);
          iwdgFeed();
          retryCnt--;
        }
      else
        {
          retryCnt = 0;
        }
    }
  while (retryCnt);
}

lang_t __attribute__((optimize("-O0")))readLanguageFromEeprom(void)
{
  lang_t actLangFromEeprom = langNum;

  int retryCnt = I2C_RETRY_CNT;
  HAL_StatusTypeDef stat;
  uint8_t okFlag = 0;
  do
    {
      stat = HAL_I2C_Mem_Read(&hi2c1, _EEPROM_ADDR, EEPROM_LANGUAGE_ADDRESS, 1, (uint8_t*)&actLangFromEeprom, sizeof(actLangFromEeprom), I2C_TIMEOUT_VAL);
      if (stat)
        {
          logE(EEPROM_LOG, "i2c language read error %d\n", stat);
          delay_ms(100);
          iwdgFeed();
          retryCnt--;
        }
      else
        {
          retryCnt = 0;
          okFlag = 1;
        }
    }
  while (retryCnt);

  if (okFlag)
    {
      logI(EEPROM_LOG, "actLangFromEeprom = 0x%x\n", actLangFromEeprom);
      return actLangFromEeprom;
    }
  else
    {
      return L_PL;
    }
}

uint32_t __attribute__((optimize("-O0")))readCrc32FromEeprom(void)
{
  uint8_t rBuf[CRC32_DATA_SIZE_BYTES] = {0};

  uint8_t okFlag = 0;

  int retryCnt = I2C_RETRY_CNT;
  HAL_StatusTypeDef stat;
  do
    {
      stat = HAL_I2C_Mem_Read(&hi2c1, _EEPROM_ADDR, EEPROM_CRC32_ADDRESS, 1, (uint8_t*)&rBuf, CRC32_DATA_SIZE_BYTES, I2C_TIMEOUT_VAL);
      if (stat)
        {
          logE(EEPROM_LOG, "i2c crc32 read error %d\n", stat);
          delay_ms(100);
          iwdgFeed();
          retryCnt--;
        }
      else
        {
          retryCnt = 0;
          okFlag = 1;
        }
    }
  while (retryCnt);

  if (okFlag)
    {
      uint32_t crcDataBuf = rBuf[0] | (rBuf[1] << 8) | (rBuf[2] << 16) | (rBuf[3] << 24);
      logI(EEPROM_LOG, "crcDataBuf = 0x%x\n", crcDataBuf);
      return crcDataBuf;
    }
  else
    return 0x00;
}

uint8_t __attribute__((optimize("-O0")))saveDataToEeprom(void)
{
  motorTimeSettings_t mT = getMTim();
  uint8_t* addressOfStruct = (uint8_t*)(&mT);
  uint16_t sizeOfStruct  = sizeof(motorTimeSettings_t);
  uint8_t tempStructData[128] = {0};
  memcpy(tempStructData, addressOfStruct, sizeOfStruct);
  uint8_t dataBuffer[DUAL_BYTE_LEN] = {0};

  int delayVal = 0;
  if (getMotorOnSettingsValue() != 0)
    {
      delayVal = 100;
      logI(EEPROM_LOG, "------------------------------------------\n");
      logI(EEPROM_LOG, "write to eeprom\n");
      logI(EEPROM_LOG, "------------------------------------------\n");
      logI(EEPROM_LOG, "lastOffTime.day = %d\n", mT.lastOffTime.day);
      logI(EEPROM_LOG, "lastOffTime.month = %d\n", mT.lastOffTime.month);
      logI(EEPROM_LOG, "lastOffTime.year = %d\n", mT.lastOffTime.year);
      logI(EEPROM_LOG, "incrValChangeTime.day = %d\n", mT.incrValChangeTime.day);
      logI(EEPROM_LOG, "incrValChangeTime.month = %d\n", mT.incrValChangeTime.month);
      logI(EEPROM_LOG, "incrValChangeTime.year = %d\n", mT.incrValChangeTime.year);
      logI(EEPROM_LOG, "motorTimeOnSetting = %d\n", mT.motorTimeOnSetting);
      logI(EEPROM_LOG, "motorTimeOffSetting = %d\n", mT.motorTimeOffSetting);
      logI(EEPROM_LOG, "motorTimeOnIncreaseSetting = %d\n", mT.motorTimeOnIncreaseSetting);
      logI(EEPROM_LOG, "minToNextPwr = %d\n", mT.minToNextPwr);
      logI(EEPROM_LOG, "secToOff = %d\n", mT.secToOff);
      logI(EEPROM_LOG, "motorRealIncrVal = %d\n", mT.motorRealIncrVal);
      logI(EEPROM_LOG, "debugMotorWorkingTime = %d\n", mT.debugMotorWorkingTime);
      logI(EEPROM_LOG, "debugDeviceOnTime = %d\n", mT.debugDeviceOnTime);
      logI(EEPROM_LOG, "debugEscKeyPressedNum = %d\n", mT.debugEscKeyPressedNum);
      logI(EEPROM_LOG, "debugUpKeyPressedNum = %d\n", mT.debugUpKeyPressedNum);
      logI(EEPROM_LOG, "debugDownKeyPressedNum = %d\n", mT.debugDownKeyPressedNum);
      logI(EEPROM_LOG, "debugOkKeyPressedNum = %d\n", mT.debugOkKeyPressedNum);
      logI(EEPROM_LOG, "debugAccidentsNum = %d\n", mT.debugAccidentsNum);
      logI(EEPROM_LOG, "------------------------------------------\n");
    }
  else
    delayVal = 30;

  HAL_StatusTypeDef stat;

  uint16_t writeOffset = 0;
  uint16_t wholeDualBytes = sizeOfStruct / DUAL_BYTE_LEN;
  logD(EEPROM_LOG, "wholeDualBytes = %d\n", wholeDualBytes);

  int i, j;
  for (i = 0; i < wholeDualBytes; i++)
    {
      for (j = 0; j < DUAL_BYTE_LEN; j++)
        dataBuffer[j] = tempStructData[i * 16 + j];

      writeOffset = EEPROM_SETTINGS_ADDRESS + i * 16;
      logD(EEPROM_LOG, "i2c write offset = %d\n", writeOffset);

      stat = HAL_I2C_Mem_Write(&hi2c1, _EEPROM_ADDR, writeOffset, 1,  (uint8_t*)&dataBuffer, DUAL_BYTE_LEN, I2C_TIMEOUT_VAL);
      memset(dataBuffer, 0, DUAL_BYTE_LEN);
      delay_ms(delayVal);
      iwdgFeed();
      if (stat)
        {
          logE(EEPROM_LOG, "i2c write error %d\n", stat);
          return 1;
        }

    }
  if(getMotorOnSettingsValue() != 0)
    delay_ms(delayVal);
  iwdgFeed();
  uint8_t bytesLeftToWrite = sizeOfStruct - (wholeDualBytes * DUAL_BYTE_LEN);

  logD(EEPROM_LOG, "bytesLeftToWrite = %d | i=%d | writeOffset=%d \n", bytesLeftToWrite, i, writeOffset);

  if (bytesLeftToWrite)
    {
      for (j = 0; j < bytesLeftToWrite; j++)
        dataBuffer[j] = tempStructData[i * 16 + j];

      logD(EEPROM_LOG, "222 write offset = %d\n", writeOffset);
      stat = HAL_I2C_Mem_Write(&hi2c1, _EEPROM_ADDR, writeOffset, 1,  (uint8_t*)&dataBuffer, bytesLeftToWrite, I2C_TIMEOUT_VAL);

      if (stat)
        {
          logE(EEPROM_LOG, "i2c write error %d\n", stat);
          return 1;
        }
    }

    delay_ms(delayVal);

  iwdgFeed();
  uint32_t crc32Calc = 0;
  crc32Calc = crc32(crc32Calc, addressOfStruct, sizeOfStruct);
  logI(EEPROM_LOG, "SAVE DATA TO EEPR %dbytes, crc32=0x%X\n", sizeOfStruct, crc32Calc);

  writeCrc32ToEeprom(crc32Calc);
  delay_ms(delayVal);
  writeLanguageToEeprom(getActiveLanguage());
  delay_ms(delayVal);
  return 0;
}

void __attribute__((optimize("-O0")))backupMotorDataToEeprom(void)
{
  int retryCnt = I2C_RETRY_CNT;
  do
    {
      if (saveDataToEeprom())
        {
          logE(EEPROM_LOG, "i2c crc32 read error\n");
          delay_ms(100);
          iwdgFeed();
          retryCnt--;
        }
      else
        {
          retryCnt = 0;
        }
    }
  while (retryCnt);
}

void __attribute__((optimize("-O0")))restoreMotorDataFromEeprom(void)
{
  uint16_t sizeOfBuffer = sizeof(motorTimeSettings_t);
  uint8_t rBuf[sizeOfBuffer + 1];

  int retryCnt = I2C_RETRY_CNT;
  HAL_StatusTypeDef stat;
  do
    {
      stat = HAL_I2C_Mem_Read(&hi2c1, _EEPROM_ADDR, EEPROM_SETTINGS_ADDRESS, 1, (uint8_t*)&rBuf, sizeOfBuffer, I2C_TIMEOUT_VAL);
      if (stat)
        {
          logE(EEPROM_LOG, "read error %d\n", stat);
          delay_ms(100);
          iwdgFeed();
          retryCnt--;
        }
      else
        {
          retryCnt = 0;
        }
    }
  while (retryCnt);

  /*  if (EEPR_WRITE_STRUCT_PRINTF)
      {
        logI(EEPROM_LOG, "------------------------------------------\n");
        logI(EEPROM_LOG, "read from eeprom\n");
        logI(EEPROM_LOG, "------------------------------------------\n");
        logI(EEPROM_LOG, "lastOffTime.day = %d\n", ((motorTimeSettings_t*)(rBuf))->lastOffTime.day);
        logI(EEPROM_LOG, "lastOffTime.month = %d\n", ((motorTimeSettings_t*)(rBuf))->lastOffTime.month);
        logI(EEPROM_LOG, "lastOffTime.year = %d\n", ((motorTimeSettings_t*)(rBuf))->lastOffTime.year);
        logI(EEPROM_LOG, "incrValChangeTime.day = %d\n", ((motorTimeSettings_t*)(rBuf))->incrValChangeTime.day);
        logI(EEPROM_LOG, "incrValChangeTime.month = %d\n", ((motorTimeSettings_t*)(rBuf))->incrValChangeTime.month);
        logI(EEPROM_LOG, "incrValChangeTime.year = %d\n", ((motorTimeSettings_t*)(rBuf))->incrValChangeTime.year);
        logI(EEPROM_LOG, "motorTimeOnSetting = %d\n", ((motorTimeSettings_t*)(rBuf))->motorTimeOnSetting);
        logI(EEPROM_LOG, "motorTimeOffSetting = %d\n", ((motorTimeSettings_t*)(rBuf))->motorTimeOffSetting);
        logI(EEPROM_LOG, "motorTimeOnIncreaseSetting = %d\n", ((motorTimeSettings_t*)(rBuf))->motorTimeOnIncreaseSetting);
        logI(EEPROM_LOG, "minToNextPwr = %d\n", ((motorTimeSettings_t*)(rBuf))->minToNextPwr);
        logI(EEPROM_LOG, "secToOff = %d\n", ((motorTimeSettings_t*)(rBuf))->secToOff);
        logI(EEPROM_LOG, "motorRealIncrVal = %d\n", ((motorTimeSettings_t*)(rBuf))->motorRealIncrVal);
        logI(EEPROM_LOG, "debugMotorWorkingTime = %d\n", ((motorTimeSettings_t*)(rBuf))->debugMotorWorkingTime);
        logI(EEPROM_LOG, "debugDeviceOnTime = %d\n", ((motorTimeSettings_t*)(rBuf))->debugDeviceOnTime);
        logI(EEPROM_LOG, "debugEscKeyPressedNum = %d\n", ((motorTimeSettings_t*)(rBuf))->debugEscKeyPressedNum);
        logI(EEPROM_LOG, "debugUpKeyPressedNum = %d\n", ((motorTimeSettings_t*)(rBuf))->debugUpKeyPressedNum);
        logI(EEPROM_LOG, "debugDownKeyPressedNum = %d\n", ((motorTimeSettings_t*)(rBuf))->debugDownKeyPressedNum);
        logI(EEPROM_LOG, "debugOkKeyPressedNum = %d\n", ((motorTimeSettings_t*)(rBuf))->debugOkKeyPressedNum);
        logI(EEPROM_LOG, "debugAccidentsNum = %d\n", ((motorTimeSettings_t*)(rBuf))->debugAccidentsNum);
        logI(EEPROM_LOG, "------------------------------------------\n");
      }*/

  uint16_t sizeOfStruct  = sizeof(motorTimeSettings_t);

  uint32_t eepromCrc32Calc = 0x00;
  eepromCrc32Calc = crc32(eepromCrc32Calc, (motorTimeSettings_t*)(rBuf), sizeOfStruct);

  uint8_t error = 0;
  if (readCrc32FromEeprom() != eepromCrc32Calc)
    {
      logE(EEPROM_LOG, "from eepr=0x%X calculated=0x%X\n", readCrc32FromEeprom(), eepromCrc32Calc);
      error = 1;
    }

  if (error == 0)
    {
      setDefaultmTimValuesFromEeprom((motorTimeSettings_t*)(rBuf));
      logI(EEPROM_LOG, "CRC32 OK, READ EEPR %d bytes\n", sizeOfBuffer);
    }
  else
    {
      logE(EEPROM_LOG, "ERROR:%d CRC32 NOT MATCH\n", error);
      backupMotorDataToEeprom();
    }
  delay_ms(100);
}

void __attribute__((optimize("-O0")))restoreDefaultSettings(void)
{
  writeLanguageToEeprom(L_PL);
  mTimSetDefaultValues();
  delay_ms(100);
  backupMotorDataToEeprom();
  setDefaultDateAndTime();
  restoreMotorDataFromEeprom();
  readLanguageFromEeprom();
  updateIncrValChangeTime();
  setSecToOffDefaultVal();
}
