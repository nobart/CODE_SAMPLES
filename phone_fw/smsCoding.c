#include <stdint.h>
#include <stdbool.h>
#include <buffers.h>
#include <xprintf.h>
#include <board.h>
#include <GuiVar.h>
#include <GuiStruct.h>
#include <smsCoding.h>
#include <phoneTask.h>
#include <guiTask.h>
#include <communicationAPI.h>
#include <generated/managerGen.h>
#include <managerTask.h>
#include <managerState.h>
#include <smsEdit.h>
#include <generated/phoneGen.h>
#include <managerTaskPhoneCallbacks.h>
#include <GuiFont.h>
#include <phoneBook.h>
#include <notificationCenter.h>
#include <textField.h>
#include <phoneTask.h>
#include <smsRegister.h>

//#define SMS_CODING_DEBUG
#ifndef SMS_CODING_DEBUG
#define smsCodingPrintf(...)
#else
#define smsCodingPrintf(...) mprintf(C_GREEN "" C_NORMAL __VA_ARGS__)
#endif

char extendedCharset[] =
{
  0x0C, 0x0A, // <FF>
  '^' , 0x14, // CIRCUMFLEX ACCENT
  '{' , 0x28, // LEFT CURLY BRACKET
  '}' , 0x29, // RIGHT CURLY BRACKET
  '\\', 0x2F, // REVERSE SOLIDUS
  '[' , 0x3C, // LEFT SQUARE BRACKET
  '~' , 0x3D, // TILDE
  ']' , 0x3E, // RIGHT SQUARE BRACKET
  0x7C, 0x40, // VERTICAL LINE
  0xA4, 0x65, // EURO SIGN
  0   , 0     // End marker
};

char asciiGsmDifferenceTable[] =
{
  '_' , 0x11, // LOW LINE
  '@' , 0x00, // COMMERCIAL AT
  0   , 0     // End marker
};

// 1 = char found 'regular' char
// 2 = char found char from extended table
uint16_t countCharsWithCharsFromExtendedTable(char *inputString, uint16_t textLength)
{
  uint8_t tableRow = 0;
  uint16_t realTextLength = textLength;

  int i;
  for (i = 0; i < textLength; i++)
    {
      while (extendedCharset[tableRow * 2])
        {
          if (extendedCharset[tableRow * 2] == inputString[i])
            {
              realTextLength = realTextLength + 1;
              break;
            }
          tableRow++;
        }
      tableRow = 0;
    }
  return realTextLength;
}

uint16_t replaceExtendedCharactersToGsm(char *inputString, char *outputString, uint16_t textLength)
{
  uint8_t tableRow = 0;
  uint16_t realTextLength = textLength;

  int i, j;
  for (i = j = 0; i < textLength; i++, j++)
    {
      while (extendedCharset[tableRow * 2])
        {
          if (extendedCharset[tableRow * 2] == inputString[i])
            {
              if (outputString)
                {
                  outputString[j] = ESCAPE_CHAR;
                  j++;
                  outputString[j] = extendedCharset[tableRow * 2 + 1];
                }
              realTextLength = realTextLength + 1;
              break;
            }
          else
            {
              outputString[j] = inputString[i];
            }
          tableRow++;
        }
      tableRow = 0;
    }
  return realTextLength;
}

//0x17D is char euro in unicode
void changeOnEuroWhenSnedMessage(uint16_t *unicodeMsg, uint16_t len)
{
  uint16_t i = 0;

  for(i = 0; i < len; i++)
  {
    if(unicodeMsg[i] == 0x17D)
      { unicodeMsg[i] = 0x20AC; }
  }
}

uint16_t replaceExtendedCharactersFromGsm(received_sms_t *recSms, message_t *newSMSRegMessage)
{
  int tableRow = 0;
  uint16_t realTextLength = recSms->receivedMessageLength;
  int i, j;
  for (i = j = 0; i < recSms->receivedMessageLength; i++, j++)
    {
      if (recSms->smsMessage[i] == ESCAPE_CHAR)
        {
          // realTextLength--;
          i++;
          while (extendedCharset[tableRow * 2])
            {
              if (extendedCharset[tableRow * 2 + 1] == recSms->smsMessage[i])
                {
                  if(extendedCharset[tableRow * 2 + 1] == 0x65)//euro char detect!!
                  {
                    newSMSRegMessage->data[j] = 0xC5;
                    j++;
                    newSMSRegMessage->data[j] = 0xBD;
                  }
                  else
                  {
                    realTextLength--;
                    newSMSRegMessage->data[j] = extendedCharset[tableRow * 2];
                  }
                  break;
                }
              tableRow++;
            }
        }
      else
        {
          newSMSRegMessage->data[j] = recSms->smsMessage[i];
        }
      tableRow = 0;
    }
  for (i = realTextLength; i < recSms->receivedMessageLength; i++)
    {
      newSMSRegMessage->data[i] = '\0';
    }
  return realTextLength;
}

uint16_t replaceExtCharFromGsmInString(char *inputMessage, uint16_t inputMessageLength, char *outputMessage)
{
  int tableRow = 0;
  uint16_t realTextLength = inputMessageLength;
  int i, j;
  for (i = j = 0; i < inputMessageLength; i++, j++)
    {
      if (inputMessage[i] == ESCAPE_CHAR)
        {
          i++;
          realTextLength--;
          while (extendedCharset[tableRow * 2])
            {
              if (extendedCharset[tableRow * 2 + 1] == inputMessage[i])
                {
                  outputMessage[j] = extendedCharset[tableRow * 2];
                  break;
                }
              tableRow++;
            }
        }
      else
        {
          outputMessage[j] = inputMessage[i];
        }
      tableRow = 0;
    }
  for (i = realTextLength; i < inputMessageLength; i++)
    {
      outputMessage[i] = '\0';
    }

  return realTextLength;
}

uint16_t replaceCodingDifferencesFromGsm(char *message)
{
  uint16_t tableRow = 0;
  uint16_t messageLength = strlenAscii(message);
  int i = 0;
  for (i = 0 ; i < messageLength ; i++)
    {
      while (asciiGsmDifferenceTable[tableRow * 2])
        {
          if (asciiGsmDifferenceTable[tableRow * 2 + 1] == message[i])
            {
              message[i] = asciiGsmDifferenceTable[tableRow * 2];
              break;
            }
          tableRow++;
        }
      tableRow = 0;
    }

  return i;
}

char *replaceCodingDifferencesToGsm(char *message)
{
  uint16_t tableRow = 0;
  uint16_t messageLength = strlenAscii(message);

  char *outputMessage = dataAlloc(sizeof(char) * messageLength + 1);

  if (outputMessage)
    {
      memSet(outputMessage, 0, messageLength);
      int i;
      for (i = 0 ; i < messageLength ; i++)
        {
          int noAssignFlag = 0;
          while (asciiGsmDifferenceTable[tableRow * 2])
            {
              if (asciiGsmDifferenceTable[tableRow * 2] == message[i])
                {
                  if (message[i - 1] != ESCAPE_CHAR)
                    {
                      outputMessage[i] = asciiGsmDifferenceTable[tableRow * 2 + 1];
                      noAssignFlag = 1;
                    }
                  break;
                }
              else
                {
                  noAssignFlag = 0;
                }
              tableRow++;
            }
          tableRow = 0;
          if (noAssignFlag == 0)
            {
              outputMessage[i] =  message[i];
            }
        }
      return outputMessage;
    }
  else
    return 0;
}

void ucs2ToGsm7Bit(uint16_t ucs2, char * utf8)
{
  utf8[1] = '\0';
  switch (ucs2)
    {
    case 0x0105: // ą
      utf8[0] = 'a';
      break;
    case 0x0107: // ć
      utf8[0] = 'c';
      break;
    case 0x0119: // ę
      utf8[0] = 'e';
      break;
    case 0x0142: // ł
      utf8[0] = 'l';
      break;
    case 0x0144: // ń
      utf8[0] = 'n';
      break;
    case 0x00F3: // ó
      utf8[0] = 'o';
      break;
    case 0x015B: // ś
      utf8[0] = 's';
      break;
    case 0x017A: // ź
    case 0x017C: // ż
      utf8[0] = 'z';
      break;
    case 0x0104: // Ą
      utf8[0] = 'A';
      break;
    case 0x0106: // Ć
      utf8[0] = 'C';
      break;
    case 0x0118: // Ę
      utf8[0] = 'E';
      break;
    case 0x0141: // Ł
      utf8[0] = 'L';
      break;
    case 0x0143: // Ń
      utf8[0] = 'N';
      break;
    case 0x00D3: // Ó
      utf8[0] = 'O';
      break;
    case 0x015A: // Ś
      utf8[0] = 'S';
      break;
    case 0x0179: // Ź
    case 0x017B: // Ż
      utf8[0] = 'Z';
      break;
    case 0x201E: // „
    case 0x201D: // "
      utf8[0] = ' ';
      break;
    case 0x2000: // 'space'
      utf8[0] = ' ';
      break;
    case 0x005B: // '['
      utf8[0] = '[';
      break;
    case 0x005D: // ']'
      utf8[0] = ']';
      break;
    case 0x005C : // '\'
      utf8[0] = '\\';
      break;
    case 0x007B  : // '{'
      utf8[0] = '{';
      break;
    case 0x007D  : // '}'
      utf8[0] = '}';
      break;
    case 0x007C  : // '|'
      utf8[0] = '|';
      break;
    case 0x007E  : // '~'
      utf8[0] = '~';
      break;
    default: // other symbols
      mprintf("Unknown ucs2 char 0x%x is dumping...\n", ucs2);
      utf8[0] = ' ';
      utf8[1] = ' ';
      break;
    }
}

void ucs2ToUtf8 (uint16_t ucs2, char * utf8)
{
  if (ucs2 < 0x80)
    {
      utf8[0] = ucs2;
      utf8[1] = '\0';
    }
  else if (ucs2 >= 0x80  && ucs2 < 0x201D)
    {
      ucs2ToGsm7Bit(ucs2, utf8);
    }
  else
    {
      utf8[0] = ' ';
      utf8[1] = ' ';
    }
}

void ucs2Encoding(received_sms_t *recSms, message_t *newSMSRegMessage)
{
  mprintf("Converting SMS from UCS2 to GSM 7 bit alphabet!\n");
  int i, j;
  for (i = j = 0; i < recSms->receivedMessageLength; j++, i += 2)
    {
      int tempInputChar = 0x000000FF & newSMSRegMessage->data[i + 1];
      tempInputChar |= (0x0000FF00 & (newSMSRegMessage->data[i] << 8));

      char tempOutChar[2] = "";
      ucs2ToUtf8(tempInputChar, &tempOutChar[0]);

      (newSMSRegMessage->data)[j] = tempOutChar[0];
    }
  for (i = j; i < recSms->receivedMessageLength; i++)
    {
      newSMSRegMessage->data[i] = ' ';
    }
  recSms->receivedMessageLength = (recSms->receivedMessageLength) / 2;
}

void convertExpanded7bitAlphabet(uint16_t inputChar, char * utf8)
{
  mprintf("---------- inputChar = 0x%x\n", inputChar);
  switch (inputChar)
    {
    case 0x14: // ^
      utf8[0] = '^';
      break;
    case 0x28: // {
      utf8[0] = '{';
      break;
    case 0x29: // }
      utf8[0] = '}';
      break;
    case 0x2F: // 'backslash'
      utf8[0] = '\\';
      break;
    case 0x3C: // [
      utf8[0] = '[';
      break;
    case 0x3D: // ~
      utf8[0] = '~';
      break;
    case 0x3E: // ]
      utf8[0] = ']';
      break;
    case 0x40: // |
      utf8[0] = '|';
      break;
    case 0x65: // euro sign
      utf8[0] = ' ';
      break;
    default: // other symbols
      mprintf("Unknown extended 7bit char 0x%x. Dumping...\n", inputChar);
      utf8[0] = ' ';
      break;
    }
}

uint8_t detectSMSCoding(char *text, uint16_t textLength)
{
  uint8_t codingType = ALPHABET_7BIT;
  for (int i = 0; i < textLength; i++)
    {
      if (isCharacterMultiByte(text[i]))
        {
          codingType = ALPHABET_16BIT;
          break;
        }
    }
  return codingType;
}


