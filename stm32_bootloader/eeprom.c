#include "i2c.h"
#include "eeprom.h"
#include "stm32l0xx_hal.h"
#include "usart.h"
#include "gpio.h"
#include "xprintf.h"
#include <string.h>
#include <stdlib.h>
#include "checksum.h"
#include "eeprom.h"

typedef struct _crc32retval_t {
  uint32_t checksum;
  int status;
} crc32retval_t;

static int eepromReadHeaders(uint8_t bankNum);
static crc32retval_t eepromCRCcalc(uint8_t bankNum);
static uint8_t eepromToFlash(uint16_t memEepAdd, uint32_t flashAdd, uint8_t bankNum);
static uint8_t appCopy (uint32_t crcChecksum, uint8_t bankNum);
static uint8_t writeImageLength(uint8_t bankNum, uint32_t imageLength);
static uint32_t flashCRCcalc (uint8_t bankNum);
static void flashErase (uint16_t flashEraseAdd);
static void printfEepromHeaders(uint8_t bankNum);
static uint8_t updateMainEepromHeadersAfterRestore(void);
static uint8_t writeCrcToEeprom(uint8_t bankNum, uint32_t val);
static uint8_t restoreBckpImgToFlash(void);
static uint8_t eeepromBackupBankCrcIsCorrect(void);
static uint8_t eepromCheckAndProgramFlashIfNeeded(void);
static int copyDataToEeprom(uint32_t address,  uint8_t *data, int size);
static int copyMemoryFromFlashToEepromBackupBank(void);
static void eepromsCleanup(void);

#define ERROR_CHECK(arg_cond) if ((arg_cond) != 0){return arg_cond;}
#define READ_ERROR_CHECK(arg_sts) if ((arg_sts) >= 0x100){return arg_sts;}

static volatile eepromHeaders_t eeprom[EEPROM_BANK_NUMBERS] =
{
  {
    .exist = 0,
    .length = 0,
    .confirm = 0,
    .count = 0,
    .readcrc = 0,
    .memAddr = EEPROM_MEM_ADDRESS,
    .addr = EEPROM_ADD1,
  },
  {
    .exist = 0,
    .length = 0,
    .confirm = 0,
    .count = 0,
    .readcrc = 0,
    .memAddr = EEPROM_MEM_ADDRESS,
    .addr = EEPROM_ADD2,
  }
};

static int eepromReadHeaders(uint8_t bankNum)
{
  uint16_t sts;
  sts = SW_I2C_ReadByte(eeprom[bankNum].addr, EXIST, 2);
  eeprom[bankNum].exist = sts;
  READ_ERROR_CHECK(sts);
  HAL_Delay(_DELAY_NUM);
  sts = SW_I2C_ReadByte(eeprom[bankNum].addr, CONFIRM, 2);
  eeprom[bankNum].confirm = sts;
  READ_ERROR_CHECK(sts);
  HAL_Delay(_DELAY_NUM);
  sts = SW_I2C_ReadByte(eeprom[bankNum].addr, COUNTBYTE, 2);
  eeprom[bankNum].count = sts;
  READ_ERROR_CHECK(sts);
  HAL_Delay(_DELAY_NUM);
  uint8_t buffer[2] = {0};
  sts = SW_I2C_ReadBytes(eeprom[bankNum].addr, LENGTHBYTES, 2, buffer, 2);
  READ_ERROR_CHECK(sts);
  eeprom[bankNum].length = (buffer [1] << 8) + buffer [0];
  HAL_Delay(_DELAY_NUM);
  sts = SW_I2C_ReadBytes(eeprom[bankNum].addr, CRC32BYTES, 2, (uint8_t*)&eeprom[bankNum].readcrc, 4);
  READ_ERROR_CHECK(sts);
  HAL_Delay(_DELAY_NUM);

  return 0;
}

static crc32retval_t eepromCRCcalc(uint8_t bankNum)
{
  int sts;
  uint16_t tmpLength;
  uint32_t crc = 0x0;
  uint16_t tmpMemAdd = eeprom[bankNum].memAddr;
  uint8_t buffer[2] = {0};
  crc32retval_t retval;

  sts = SW_I2C_ReadBytes(eeprom[bankNum].addr, LENGTHBYTES, 2, buffer, 2);
  if (sts >= 0x100)
  {
    xprintf("e1!\n");
    retval.status = -1;
    return retval;
  }
  HAL_Delay(_DELAY_NUM);
  eeprom[bankNum].length = (buffer [1] << 8) + buffer [0];

  uint8_t eepromBuffer[4];
  int tmpBankLength = eeprom[bankNum].length;
  while (tmpBankLength > 0)
  {
    tmpLength = (tmpBankLength > 4) ? 4 : tmpBankLength;
    sts = SW_I2C_ReadBytes(eeprom[bankNum].addr, tmpMemAdd, 2, eepromBuffer, tmpLength);
    if (sts >= 0x100)
    {
      xprintf("e2!\n");
      retval.status = -1;
      return retval;
    }
    HAL_Delay(_DELAY_NUM);
    crc = crc32_slow (crc, eepromBuffer, tmpLength);
    tmpBankLength -= tmpLength;
    tmpMemAdd += tmpLength;
    if (!(tmpBankLength % 64)) xprintf ("*");
  }
  xprintf("\n");
  xprintf ("EEPROM bank%d CRC 0x%08X\n", bankNum, crc);
  retval.status = 0;
  retval.checksum = crc;
  return retval;
}

static uint8_t eepromToFlash(uint16_t memEepAdd, uint32_t flashAdd, uint8_t bankNum)
{
  uint32_t tmpBuffer;
  uint16_t tmpLength;
  uint8_t buffer[2] = {0};
  int sts;
  sts = SW_I2C_ReadBytes(eeprom[bankNum].addr, LENGTHBYTES, 2, buffer, 2);
  HAL_Delay(_DELAY_NUM);
  if (sts >= 0x100)
  {
    xprintf("e10!\n");
  }

  eeprom[bankNum].length = (buffer [1] << 8) + buffer [0];
  flashErase (flashAdd);
  xprintf ("CPY img eeprom(BANK %d)->flash\n" , bankNum);
  HAL_FLASH_Unlock();
  int cnt = 0;
  int err = 100;
  uint32_t tmpBankLength = eeprom[bankNum].length;
  while (tmpBankLength > 0)
  {
    tmpLength = (tmpBankLength > 4) ? 4 : tmpBankLength;
    do
    {
      sts = SW_I2C_ReadBytes(eeprom[bankNum].addr, memEepAdd, 2, (uint8_t*)&tmpBuffer, tmpLength);
      HAL_Delay(_DELAY_NUM);
      if (sts >= 0x100)
      {
        err--;
        if (!(err & 10))
          xprintf("e11");
      }
    }
    while ( (err > 0) && (sts >= 0x100) );

    if (err == 0)
    {
      xprintf("eep2FLASH err\n");
      return 1;
    }

    HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, flashAdd, tmpBuffer);
    flashAdd += tmpLength;
    memEepAdd += tmpLength;
    tmpBankLength -= tmpLength;
    cnt++;
    if (!(cnt % 512))
      xprintf (".");
  }
  xprintf("CP DATA DONE\n");
  HAL_FLASH_Lock();
  return 0;
}


static uint8_t appCopy (uint32_t crcChecksum, uint8_t bankNum)
{
  uint32_t flashAddr = FLASH_APP_ADDRESS;
  uint32_t flashCrcVal = flashCRCcalc(bankNum);
  int sts = 0;

  if (crcChecksum != flashCrcVal)
  {
    xprintf ("NEW IMG\n");
    int sts = eepromToFlash(eeprom[bankNum].memAddr, flashAddr, EEPROM_MAIN_BANK);
    ERROR_CHECK(sts);
  }
  if (crcChecksum == flashCrcVal)
  {
    HAL_Delay(_DELAY_NUM);
    eeprom[EEPROM_MAIN_BANK].exist = 0;
    int sts = SW_I2C_WriteByte(eeprom[EEPROM_MAIN_BANK].addr, EXIST, 2, eeprom[EEPROM_MAIN_BANK].exist);
    HAL_Delay(_DELAY_NUM);
    ERROR_CHECK(sts);
  }
  return sts;
}

static uint8_t writeImageLength(uint8_t bankNum, uint32_t imageLength)
{
  int sts;
  uint8_t buffer [2] = {0};
  buffer [0] = (uint8_t)imageLength;
  buffer [1] = (uint8_t)(imageLength >> 8);
  sts = SW_I2C_WriteBytes(eeprom[bankNum].addr, LENGTHBYTES, 2, buffer, 2);
  HAL_Delay(_DELAY_NUM);
  ERROR_CHECK(sts);
  return 0;
}

static uint32_t flashCRCcalc (uint8_t bankNum)
{
  int sts;
  uint16_t tmpLength;
  uint32_t crc = 0x0;
  uint32_t *flashCRCadd;
  flashCRCadd = (uint32_t *) FLASH_APP_ADDRESS;
  uint8_t buffer[2] = {0};
  sts = SW_I2C_ReadBytes(eeprom[bankNum].addr, LENGTHBYTES, 2, buffer, 2);
  HAL_Delay(_DELAY_NUM);
  if (sts >= 0x100) { xprintf("e!\n"); }
  eeprom[bankNum].length = (buffer [1] << 8) + buffer [0];

  if (bankNum == EEPROM_BACKUP_BANK)
    eeprom[bankNum].length = FLASH_MAX_MEM - FLASH_APP_ADDRESS;

  if (eeprom[bankNum].length == 0xFFFF)
    return crc;

  uint32_t tmpBankLength = eeprom[bankNum].length;
  while (tmpBankLength > 0)
  {
    tmpLength = (tmpBankLength > 4) ? 4 : tmpBankLength;
    crc = crc32_slow (crc, flashCRCadd, tmpLength);
    ++flashCRCadd;
    tmpBankLength -= tmpLength;
    if (!(tmpBankLength % 1024)) xprintf ("Fc");
  }
  xprintf("\n");
  xprintf ("FLASH bank %d CRC=0x%08X\n", bankNum, crc );
  return crc;
}

static void flashErase (uint16_t flashEraseAdd)
{
  uint32_t tmpFlashAdd = flashEraseAdd;
  uint16_t nbpages = 0;
  uint32_t PageError;
  while ( tmpFlashAdd <= FLASH_MAX_MEM )
  {
    nbpages ++;
    tmpFlashAdd += 0x80;
  }
  HAL_FLASH_Unlock();
  FLASH_EraseInitTypeDef FLASH_EraseInit;
  FLASH_EraseInit.NbPages = nbpages;
  FLASH_EraseInit.PageAddress = flashEraseAdd;
  FLASH_EraseInit.TypeErase = FLASH_PROC_PAGEERASE;
  HAL_FLASHEx_Erase (&FLASH_EraseInit, &PageError);
  HAL_FLASH_Lock();
}

static void printfEepromHeaders(uint8_t bankNum)
{
  if (bankNum == EEPROM_MAIN_BANK)
    xprintf("MAIN EEPROM\n");
  else if (bankNum == EEPROM_BACKUP_BANK)
    xprintf("BACKUP EEPROM\n");
  xprintf("exist=%d\n", eeprom[bankNum].exist);
  xprintf("length=%d\n", eeprom[bankNum].length);
  xprintf("confirm=%d\n", eeprom[bankNum].confirm);
  xprintf("count=%d\n", eeprom[bankNum].count);
  xprintf("readcrc=0x%08x\n", eeprom[bankNum].readcrc);
  xprintf("memAddr=0x%02x\n", eeprom[bankNum].memAddr);
  xprintf("addr=0x%02x\n\n" , eeprom[bankNum].addr);
}

static uint8_t updateMainEepromHeadersAfterRestore(void)
{
  int retVal = 0;
  HAL_Delay(_DELAY_NUM);
  eeprom[EEPROM_MAIN_BANK].confirm = 1;
  retVal = SW_I2C_WriteByte(eeprom[EEPROM_MAIN_BANK].addr, CONFIRM, 2, eeprom[EEPROM_MAIN_BANK].confirm);
  HAL_Delay(_DELAY_NUM);
  if (retVal) return retVal;
  eeprom[EEPROM_MAIN_BANK].count = 0;
  retVal =  SW_I2C_WriteByte(eeprom[EEPROM_MAIN_BANK].addr, COUNTBYTE, 2, eeprom[EEPROM_MAIN_BANK].count);
  HAL_Delay(_DELAY_NUM);
  if (retVal) return retVal;
  eeprom[EEPROM_MAIN_BANK].exist = 0;
  retVal =  SW_I2C_WriteByte(eeprom[EEPROM_MAIN_BANK].addr, EXIST, 2, eeprom[EEPROM_MAIN_BANK].exist);
  HAL_Delay(_DELAY_NUM);
  if (retVal) return retVal;
  return 0;
}



static uint8_t writeCrcToEeprom(uint8_t bankNum, uint32_t val)
{
  HAL_Delay(_DELAY_NUM);

  uint16_t status;
  status = SW_I2C_WriteBytes(eeprom[bankNum].addr, CRC32BYTES, 2, (uint8_t*)&val, 4);
  if (status >= 0x100) {
    status = SW_I2C_WriteBytes(eeprom[bankNum].addr, CRC32BYTES, 2, (uint8_t*)&val, 4);
  }
  HAL_Delay(_DELAY_NUM);
  if (status >= 0x100)
  {
    xprintf("I2C WRITE ERROR\n");
    return status;
  }

  HAL_Delay(_DELAY_NUM);

  uint32_t tempCrc;
  status = SW_I2C_ReadBytes(eeprom[bankNum].addr, CRC32BYTES, 2, (uint8_t*)&tempCrc, 4);
  if (status >= 0x100) {
    status = SW_I2C_ReadBytes(eeprom[bankNum].addr, CRC32BYTES, 2, (uint8_t*)&tempCrc, 4);
  }
  HAL_Delay(_DELAY_NUM);
  if (status >= 0x100)
  {
    xprintf("I2C READ ERROR\n");
    return status;
  }

  if (tempCrc != val)
  {
    xprintf("CRC ERROR\n");
  }
  return 0;
}

static uint8_t eeepromBackupBankCrcIsCorrect(void)
{
  int retVal = eepromReadHeaders(EEPROM_BACKUP_BANK);
  ERROR_CHECK(retVal);

  crc32retval_t crc32retval = eepromCRCcalc(EEPROM_BACKUP_BANK);
  uint32_t eepromCrc = crc32retval.checksum;

  if (crc32retval.status >= 0 && eepromCrc == eeprom[EEPROM_BACKUP_BANK].readcrc)
  {
    xprintf("OK\n");
    return 1;
  }
  else
  {
    xprintf("NO\n");
    return 0;
  }
}

static uint8_t restoreBckpImgToFlash(void)
{
  if (eeepromBackupBankCrcIsCorrect() == 0)
    return 1;

  uint32_t flashAddr = FLASH_APP_ADDRESS;
  eepromToFlash(eeprom[EEPROM_BACKUP_BANK].memAddr, flashAddr, EEPROM_BACKUP_BANK);
  uint32_t flashCrcVal = flashCRCcalc(EEPROM_BACKUP_BANK);

  int retVal = 0;

  if (eeprom[EEPROM_BACKUP_BANK].readcrc == flashCrcVal)
  {
    retVal = writeCrcToEeprom(EEPROM_MAIN_BANK, eeprom[EEPROM_BACKUP_BANK].readcrc);
    ERROR_CHECK(retVal);
    retVal = writeImageLength(EEPROM_MAIN_BANK, eeprom[EEPROM_BACKUP_BANK].length);
    ERROR_CHECK(retVal);
    retVal = updateMainEepromHeadersAfterRestore();
    ERROR_CHECK(retVal);
    retVal = eepromReadHeaders(EEPROM_MAIN_BANK);
    ERROR_CHECK(retVal);
    retVal = eepromReadHeaders(EEPROM_BACKUP_BANK);
    ERROR_CHECK(retVal);
    printfEepromHeaders(EEPROM_MAIN_BANK);
    printfEepromHeaders(EEPROM_BACKUP_BANK);

    xprintf("FLASH RESTORING done\n");
  }
  else
  {
    xprintf( "EEPROM wrong CRC\n");
    return 1;
  }
  return 0;
}

static uint8_t eepromCheckAndProgramFlashIfNeeded(void)
{
  int sts;
  if (eeprom[EEPROM_MAIN_BANK].readcrc == 0xFFFFFFFF)
  {
    return 0; // no new image
  }

  if (eeprom[EEPROM_MAIN_BANK].confirm == 1 && eeprom[EEPROM_MAIN_BANK].exist == 0)
  {
    return 0; // update confirmed
  }

  if (eeprom[EEPROM_MAIN_BANK].exist == 1) // new image!
  {
    sts = appCopy(eeprom[EEPROM_MAIN_BANK].readcrc, EEPROM_MAIN_BANK);
    if (sts > 0)
      return 1;
  }
  else // new image in flash but with no confirm
  {
    if (eeprom[EEPROM_MAIN_BANK].count > 0)
    {
      eeprom[EEPROM_MAIN_BANK].count--;
      sts = SW_I2C_WriteByte(eeprom[EEPROM_MAIN_BANK].addr, COUNTBYTE, 2, eeprom[EEPROM_MAIN_BANK].count);
      HAL_Delay (_DELAY_NUM);
      ERROR_CHECK(sts);
    }
  }

  if (eeprom[EEPROM_MAIN_BANK].confirm == 0 && eeprom[EEPROM_MAIN_BANK].count == 0 && eeprom[EEPROM_MAIN_BANK].length < 0xFFFF)
  {
    xprintf("User not conf. fw restore bckp\n");
    sts = restoreBckpImgToFlash(); //user didnt confirm fw update so restore backup
    return sts;
  }
  return 0;
}

static int copyDataToEeprom(uint32_t address,  uint8_t *data, int size)
{
  uint32_t chunkSize;
  uint32_t offset = 0;
  int sts,max_retry;
  max_retry=10;
  while (size)
  {
    chunkSize = (size > 128) ? 128 : size;
    int overflow = ((address + offset) % 128) + chunkSize - 128;
    if (overflow > 0)
    {
      chunkSize -= overflow;
    }
    do {
       sts = SW_I2C_WriteBytes(eeprom[EEPROM_BACKUP_BANK].addr, (address + offset), 2, &data[offset], chunkSize);
       HAL_Delay (_DELAY_NUM);
       if (sts>=0x100) { max_retry--; }
    } while ((sts>=0x100) && (max_retry>0));
    if (max_retry==0) { return sts; }
    size -= chunkSize;
    offset += chunkSize;
  }
  // xprintf("I2C Retry=%d\n",max_retry);
  return 0;
}

static int copyMemoryFromFlashToEepromBackupBank(void)
{
  xprintf( "CP FLASH 2 backupEEPROM\n" );

  uint32_t *flashCRCadd;
  flashCRCadd = (uint32_t *) FLASH_APP_ADDRESS;

  volatile uint16_t memLength = FLASH_MAX_MEM - FLASH_APP_ADDRESS;

  int len = 0;
  int cnt = 0;
  int sts;
  uint16_t tmpLength;
  while (memLength > 0)
  {
    tmpLength = (memLength > 4) ? 4 : memLength;

    if (tmpLength > 0)
    {
      sts = copyDataToEeprom(eeprom[EEPROM_BACKUP_BANK].memAddr + len, (uint8_t*)flashCRCadd, tmpLength);
      if (sts != 0)
      {
        xprintf("F2E CP failed (%d)\n" , len);
        return sts;
      }
      len = len + tmpLength;
    }

    memLength -= tmpLength;

    if (!(cnt % 64)) xprintf ("B");
    ++flashCRCadd;
    cnt++;
  }
  return 0;
}

int updateFromEepromHandler(void)
{
  int sts1;
  crc32retval_t crc32retval;
  uint32_t eepromCrc;

  sts1 = eepromReadHeaders(EEPROM_MAIN_BANK);
  ERROR_CHECK(sts1);

  sts1 = eepromReadHeaders(EEPROM_BACKUP_BANK);
  ERROR_CHECK(sts1);

  if (eeprom[EEPROM_BACKUP_BANK].readcrc == 0xFFFFFFFF)
  {
    sts1 = copyMemoryFromFlashToEepromBackupBank();

    ERROR_CHECK(sts1);
    sts1 = writeImageLength(EEPROM_BACKUP_BANK, FLASH_MAX_MEM - FLASH_APP_ADDRESS);
    ERROR_CHECK(sts1);

    crc32retval_t crc32retval = eepromCRCcalc(EEPROM_BACKUP_BANK);
    uint32_t eepromCrc = crc32retval.checksum;

    if (crc32retval.status < 0)
    {
      xprintf( "EEPR CRC calc\n");
      return 1;
    }

    if (eepromCrc == flashCRCcalc(EEPROM_BACKUP_BANK))
    {
      sts1 = writeCrcToEeprom(EEPROM_BACKUP_BANK, eepromCrc);
      ERROR_CHECK(sts1);
      sts1 = eepromReadHeaders(EEPROM_MAIN_BANK);
      ERROR_CHECK(sts1);
      sts1 = eepromReadHeaders(EEPROM_BACKUP_BANK);
      ERROR_CHECK(sts1);
    }
    else
      xprintf( "Bckp image created\n");
  }

  sts1 = eepromCheckAndProgramFlashIfNeeded();
  ERROR_CHECK(sts1);

  if (eeprom[EEPROM_MAIN_BANK].readcrc != 0xFFFFFFFF && eeprom[EEPROM_MAIN_BANK].length < 0xFFFF)
  {
    eepromReadHeaders(EEPROM_MAIN_BANK);

    if (flashCRCcalc(EEPROM_MAIN_BANK) != eeprom[EEPROM_MAIN_BANK].readcrc) //flash verification
    {
      xprintf( "Flash CRC =/= MAIN eepr\n" );
      crc32retval = eepromCRCcalc(EEPROM_MAIN_BANK);
      eepromCrc = crc32retval.checksum;
      if (eepromCrc==eeprom[EEPROM_MAIN_BANK].readcrc) {
	xprintf("EEPROM crc is correct - restoring to flash\n");
         sts1 = restoreBckpImgToFlash();
         ERROR_CHECK(sts1);
      } else {
	xprintf("EEPROM crc is not correct - can be corrupted\n");
      }
    }
    else
    {
      sts1 = eepromReadHeaders(EEPROM_MAIN_BANK);
      ERROR_CHECK(sts1);
      sts1 = eepromReadHeaders(EEPROM_BACKUP_BANK);
      ERROR_CHECK(sts1);
      printfEepromHeaders(EEPROM_MAIN_BANK);
      printfEepromHeaders(EEPROM_BACKUP_BANK);
      xprintf( "Flash CRC OK\n" );
      return 0;
    }
  }

  return sts1;
}
