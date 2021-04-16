#ifndef _SMS_CODING_H
#define _SMS_CODING_H

#include <stdint.h>
#include <smsRegister.h>
#include <phoneBook.h>

void changeOnEuroWhenSnedMessage(uint16_t *unicodeMsg, uint16_t len);
void ucs2ToGsm7Bit(uint16_t ucs2,  char *utf8);
void ucs2ToUtf8 (uint16_t ucs2,  char *utf8);
void convertExpanded7bitAlphabet(uint16_t inputChar, char *utf8);
int char2gsm(char ch, char *newch);
int gsm2char(char ch, char *newch);
void ucs2Encoding(received_sms_t *recSms, message_t *newSMSRegMessage);
void sms7bitEncoding(received_sms_t *recSms, message_t *newSMSRegMessage);
uint16_t countCharsWithCharsFromExtendedTable(char *inputChar, uint16_t textLength);
uint16_t replaceExtendedCharactersToGsm(char *inputString, char *outputString, uint16_t textLength);
uint16_t replaceExtendedCharactersFromGsm(received_sms_t *recSms, message_t *newSMSRegMessage);
void changeOnEuroWhenSnedMessage(uint16_t *unicodeMsg, uint16_t len);
uint16_t replaceCodingDifferencesFromGsm(char *message);
uint16_t replaceExtCharFromGsmInString(char *inputMessage, uint16_t inputMessageLength, char *outputMessage);
char *replaceCodingDifferencesToGsm(char *message);
uint8_t detectSMSCoding(char *text, uint16_t textLength);


#define UNICODE_STRING_END_NULL_LENGTH 2
#define ESCAPE_CHAR 0x1B

#define MAX_NUMBER_LENGTH NO_PREFIX_NUMBER_LENGTH_MAX
#define MAX_UDH_FRAME_LENGTH 18
#define MAX_NO_OF_SUB_SMS 5
#define SINGLE_SMS_TEXT_LENGTH _7_BIT_ONE_SMS_ONLY
#define SINGLE_SMS_PDU_LENGTH 350

#define NUMBER_TYPE_UNKNOWN 0x81
#define NUMBER_TYPE_INTERNATIONAL 0x91
#define NUMBER_TYPE_NATIONAL 0x81

#define BYTES_SWAP 1
#define NO_BYTES_SWAP 0

#define SMS_MAX_VALIDITY_PERIOD 170

#define ALPHABET_7BIT 0
#define ALPHABET_8BIT 1
#define ALPHABET_16BIT 2
#define ALPHABET_UNKNOWN 3

#define UDH_FRAME_LENGTH 7
#define SMS_MAX_SUBMESSAGES 5

// gsm 7 bit:
#define _7_BIT_ONE_SMS_ONLY 160
#define _7_BIT_CONCENTRATED_SMS_LENGTH 153
#define _7_BIT_SMS_MAX_LENGTH  5 * _7_BIT_CONCENTRATED_SMS_LENGTH + 5 * UDH_FRAME_LENGTH // (5 submessages)

// ucs2:
#define UCS2_ONE_SMS_ONLY 70
#define UCS2_CONCENTRATED_SMS_LENGTH 63
#define UCS2_SMS_MAX_LENGTH  5 * UCS2_CONCENTRATED_SMS_LENGTH + 5 * UDH_FRAME_LENGTH // (5 submessages)



#endif