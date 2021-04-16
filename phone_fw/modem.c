#include <stm32l4xx_hal.h>
#include <stm32l4xx_hal_uart.h>
#include <cmsis_os.h>
#include <xprintf.h>
#include <phoneTask.h>
#include <communicationAPI.h>
#include <buffers.h>
#include <board.h>
#include <stringUtf8.h>
#include <modemApi.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include <generated/communicationGen.h>
#include <generated/phoneGen.h>
#include <libHL6528smsEncoder.h>
#include <stdint.h>
#include <pinmux.h>
#include <libTime.h>
#include <libUSART.h>
#include <hal.h>
#include <stm32l476xx.h>
#include <languages.h>
#include <smsCoding.h>

//#define MODEM_DEBUG

#ifndef MODEM_DEBUG
#define modemPrintf(...)
#else
#define modemPrintf(...) mprintf("[MODEM DEBUG] " __VA_ARGS__)
#endif

typedef enum answerType_e {SYNC_ANSWER, ASYNC_ANSWER} answerType_t;

static modemCmdCnt_t cmdStats =
{
  .modemErrorCommandsCounter = -1,
  .modemSuccessCommandsCounter = -1,
  .modemSentCommandsCounter = -1,
  .modemSentErrorCommandsCounter = -1,
};

modemCmdCnt_t getCmdStats(void)
{
  return cmdStats;
}

typedef struct
{
  const char *prefix;
  uint8_t prefixLen;
  answerType_t answerType;
} modemAnswCmdList_t;

typedef struct
{
  const char *code;
  uint8_t codeLen;
} modemErrorList_t;

typedef struct sentCommand_s sentCommand_t;
typedef struct sentCommand_s
{
  sentCommand_t *next;
  uint64_t timestamp;
  uint32_t responseTimeout;
  uint16_t transmissions;
  uint16_t maxRetriesNumber;
  simNumber_t simNumber;
  commandResult_t commandResult;
  char sentCommand[];
} __attribute__((packed)) sentCommand_t;

static sentCommand_t *newSentCommand(char *command, simNumber_t simNumber, uint32_t responseTimeout, uint16_t maxRetriesNumber)
{
  sentCommand_t *sentCommand = dataAlloc(sizeof(sentCommand_t) + sizeInBytes(command) + 1);
  if (!sentCommand)
    {
      mprintf("newSentCommand allocation failed!\n");
      return NULL;
    }

  strCpy(sentCommand->sentCommand, command);
  sentCommand->timestamp = 0;
  sentCommand->simNumber = simNumber;
  sentCommand->commandResult = COMMAND_NO_RESPONSE;
  sentCommand->responseTimeout = responseTimeout;
  sentCommand->maxRetriesNumber = maxRetriesNumber;
  sentCommand->transmissions = 0;
  return sentCommand;
}

int queuedCommandsCounter = 0;
sentCommand_t *sentCommandsHead = NULL;
sentCommand_t *sentCommandsTail = NULL;
static void insertSentCommandToList(sentCommand_t *sentCommand)
{
  if (sentCommandsTail)
    {
      sentCommandsTail->next = sentCommand;
      sentCommandsTail = sentCommand;
    }
  else
    sentCommandsHead = sentCommandsTail = sentCommand;
  queuedCommandsCounter++;
}

static void removeSentCommandFromList(sentCommand_t *commandToDelete)
{
  sentCommand_t *sentCommand = sentCommandsHead;
  sentCommand_t *previousCommand = NULL;
  while (sentCommand)
    {
      sentCommand_t *nextCommand = sentCommand->next;
      if (sentCommand == commandToDelete)
        {
          if (previousCommand && nextCommand) // middle element of the list
            {
              previousCommand->next = nextCommand;
            }
          else if (!previousCommand && nextCommand) // the first element of the list
            {
              sentCommandsHead = nextCommand;
            }
          else if (!previousCommand && !nextCommand) // the first and the last element of the list
            {
              sentCommandsHead = sentCommandsTail = NULL;
            }
          else if (previousCommand && !nextCommand) // the last element of the list
            {
              sentCommandsTail = previousCommand;
            }

          queuedCommandsCounter--;
          dataFree(commandToDelete);
          break;
        }
      previousCommand = sentCommand;
      sentCommand = nextCommand;
    }
}

/**
 * @brief      Clear sent to modem commands list.
 */
void cleanSentCommandsList(void)
{
  while (sentCommandsHead)
    removeSentCommandFromList(sentCommandsHead);
}


static void addSentCommandToList(char *command, simNumber_t simNumber, uint32_t responseTimeout, uint16_t maxRetriesNumber)
{
  if (queuedCommandsCounter < MAX_QUEUED_COMMAND)
    {
      sentCommand_t *sentCommand = newSentCommand(command, simNumber, responseTimeout, maxRetriesNumber);
      if (sentCommand)
        insertSentCommandToList(sentCommand);
    }
  else
    mprintf("COMMANDS QUEUE OVERFLOWN! [%s] command dropped\n", command);
}

uint64_t lastModemActivity = 0;

/**
 * @brief      Updates last modem activity timestamp.
 */
void modemKeepAlive(void)
{
  lastModemActivity = NS2MS(getUptime());
}

/**
 * @brief      Gets the modem last activity time.
 *
 * @return     The modem last activity time.
 */
uint64_t getModemLastActivityTime(void)
{
  return lastModemActivity;
}

static sentCommand_t *markCommandStatus(commandResult_t result)
{
  sentCommand_t *sentCommand = sentCommandsHead;
  while (sentCommand)
    {
      if (sentCommand->commandResult == COMMAND_NO_RESPONSE)
        {
          modemKeepAlive();
          sentCommand->commandResult = result;
          if (sentCommand->commandResult == COMMAND_ERROR)
            {
              mprintf(C_RED "-- %s %s\n" C_NORMAL, sentCommand->sentCommand, (result == COMMAND_SUCCESS) ? "OK" : "FAILED");
              errorCommandCallbackHandler(sentCommand->sentCommand);
              cmdStats.modemErrorCommandsCounter++;
            }
          else
            {
              successCommandCallbackHandler(sentCommand->sentCommand);
              cmdStats.modemSuccessCommandsCounter++;
            }
          return sentCommand;
          break;
        }
      sentCommand = sentCommand->next;
    }
  return NULL;
}

static char *commandResultToString(commandResult_t commandResult)
{
  switch (commandResult)
    {
    case COMMAND_SUCCESS:
      return "OK";
      break;
    case COMMAND_NO_RESPONSE:
      return "NO RESPONSE";
      break;
    case COMMAND_ERROR:
      return "ERROR";
      break;
    default:
      return "UNKNOWN";
      break;
    }
}

/**
 * @brief      Printf current command list sent to modem.
 */
void printCurrentCommandsList(void)
{
  sentCommand_t *sentCommand = sentCommandsHead;
  mprintf("\nMODEM SENT COMMANDS LIST\n");
  while (sentCommand)
    {
      mprintf("%s | \"%s\" | %s\n", sentCommand->simNumber == SIM1 ? "SIM1" : "SIM2", sentCommand->sentCommand, commandResultToString(sentCommand->commandResult));
      sentCommand = sentCommand->next;
    }
  mprintf("Init stage: %d\n\n", getModemInitializationStatus());
}

/**
 * @brief      Change last command status as succeeded and remove it from list.
 */
void commandSucceded(void)
{
  sentCommand_t *sentCommand = markCommandStatus(COMMAND_SUCCESS);
  removeSentCommandFromList(sentCommand);
}

/**
 * @brief      Change last command status as error and remove it from list.
 */
void commandError(void)
{
  sentCommand_t *sentCommand = markCommandStatus(COMMAND_ERROR);
  removeSentCommandFromList(sentCommand);
}

/**
 * @brief      Check sent commands status.
 *
 * @return     If there are no commands on the list or command was marked
 *             successed, function also returns success! Function returns error
 *             when sent command was marked as error.
 */
commandResult_t checkAllSentCommands(void)
{
  sentCommand_t *sentCommand = sentCommandsHead;
  while (sentCommand)
    {
      if (sentCommand->commandResult != COMMAND_SUCCESS)
        return COMMAND_ERROR;

      sentCommand = sentCommand->next;
    }
  return COMMAND_SUCCESS; //
}

#define ANSWER_INIT(code, async) code, sizeof(code)-1, async //code and number of chars
#define COMMAND_INIT(code) code, sizeof(code)-1 //code and number of chars
static const modemAnswCmdList_t answerFromModemCmdList[modem_cmd_num] = //Modem command answers list
{
  [0 ... modem_cmd_num - 1] = {ANSWER_INIT("NOERRORMESSAGE", ASYNC_ANSWER)},
  [MODEM_OK] = {ANSWER_INIT("OK", SYNC_ANSWER)},
  [MODEM_ERROR] = {ANSWER_INIT("ERROR", SYNC_ANSWER)},
  [MODEM_INCOMING_CALL_CALLER_NUMBER_SIM1] = {ANSWER_INIT("+CLIP: \"", ASYNC_ANSWER)},
  [MODEM_INCOMING_CALL_CALLER_NUMBER_SIM2] = {ANSWER_INIT("+CLIPDS: \"", ASYNC_ANSWER)},
  [MODEM_CALL_STARTED_SIM1] = {ANSWER_INIT("+KCALL: 1,1", ASYNC_ANSWER)},
  [MODEM_CALL_STARTED_SIM2] = {ANSWER_INIT("+KCALLDS: 1,1,", ASYNC_ANSWER)},
  [MODEM_CALL_STARTED_FROM_CALLER_SIDE_SIM2] = {ANSWER_INIT("+KCALLDS: 1,1", ASYNC_ANSWER)},
  [MODEM_CALL_ENDED_SIM1] = {ANSWER_INIT("+KCALL: 1,0", ASYNC_ANSWER)},
  [MODEM_CALL_ENDED_SIM2] = {ANSWER_INIT("+KCALLDS: 1,0", ASYNC_ANSWER)},
  [MODEM_CALL_ENDED_FROM_CALLER_SIDE_SIM1] = {ANSWER_INIT("+KCALL: 0,0", ASYNC_ANSWER)},
  [MODEM_CALL_ENDED_FROM_CALLER_SIDE_SIM2] = {ANSWER_INIT("+KCALLDS: 0,0", ASYNC_ANSWER)},
  [MODEM_NETWORK_REGISTATION_OK_SIM1] = {ANSWER_INIT("+COPS: 0,0,", ASYNC_ANSWER)},
  [MODEM_NETWORK_REGISTATION_OK_SIM2] = {ANSWER_INIT("+COPSDS: 0,0,", ASYNC_ANSWER)},
  [MODEM_SIGNAL_QUALITY_SIM1] = {ANSWER_INIT("+CSQ: ", SYNC_ANSWER)},
  [MODEM_SIGNAL_QUALITY_SIM2] = {ANSWER_INIT("+CSQDS: ", SYNC_ANSWER)},
  [MOBILE_START_UP_REPORTING_SIM1] = {ANSWER_INIT("+KSUP: ", ASYNC_ANSWER)},
  [MOBILE_START_UP_REPORTING_SIM2] = {ANSWER_INIT("+KSUPDS: ", ASYNC_ANSWER)},
  [MODEM_NETWORK_REGISTRATION_INFO_SIM1] = {ANSWER_INIT("+CREG:", ASYNC_ANSWER)},
  [MODEM_NETWORK_REGISTRATION_INFO_SIM2] = {ANSWER_INIT("+CREGDS:", ASYNC_ANSWER)},
  [MODEM_SET_SPEAKER_VOLUME] = {ANSWER_INIT("+CLVL: ", SYNC_ANSWER)},
  [MODEM_NEW_SMS_SIM1] = {ANSWER_INIT("+CMTI:", ASYNC_ANSWER)},
  [MODEM_NEW_SMS_SIM2] = {ANSWER_INIT("+CMTIDS:", ASYNC_ANSWER)},
  [MODEM_SMS_SEND_OK_SIM1] = {ANSWER_INIT("+CMGS: ", ASYNC_ANSWER)},
  [MODEM_SMS_SEND_OK_SIM2] = {ANSWER_INIT("+CMGSDS: ", ASYNC_ANSWER)},
  [MODEM_CMD_SMS_READ_SIM1] = {ANSWER_INIT("+CMGR: ", SYNC_ANSWER)},
  [MODEM_CMD_SMS_READ_SIM2] = {ANSWER_INIT("+CMGRDS: ", SYNC_ANSWER)},
  [MODEM_CMD_SMS_SMSC_SIM1] = {ANSWER_INIT("+CSCA: ", SYNC_ANSWER)},
  [MODEM_CMD_SMS_SMSC_SIM2] = {ANSWER_INIT("+CSCADS: ", SYNC_ANSWER)},
  [MODEM_NETWORK_GET_NETWORK_TIME] = {ANSWER_INIT("+CCLK: ", SYNC_ANSWER)},
  [MODEM_NETWORK_TIME_ZONE_CHANGE] = {ANSWER_INIT("+CTZV: ", ASYNC_ANSWER)},
  [MODEM_USSD_RESP_SIM1] = {ANSWER_INIT("+CUSD: ", ASYNC_ANSWER)},
  [MODEM_USSD_RESP_SIM2] = {ANSWER_INIT("+CUSDDS: ", ASYNC_ANSWER)},
  [MODEM_FIELD_STRENGTH] = {ANSWER_INIT("*PSFS: ", ASYNC_ANSWER)},
  [MODEM_ACTUAL_BAND_SIM1] = {ANSWER_INIT("+KBND: ", SYNC_ANSWER)},
  [MODEM_ACTUAL_BAND_SIM2] = {ANSWER_INIT("+KBNDDS: ", SYNC_ANSWER)},
  [MODEM_POWER_CLASS] = {ANSWER_INIT("+CPWC: ", SYNC_ANSWER)},
  [CMS_ERROR] = {ANSWER_INIT("+CMS ERROR: ", ASYNC_ANSWER)},
  [CME_ERROR] = {ANSWER_INIT("+CME ERROR: ", ASYNC_ANSWER)},
  [CME_IMEI_SIM1] = {ANSWER_INIT("35", SYNC_ANSWER)}, //(TAC) Type Allocation Code allocated by body appointed by GSMA
  [CME_IMEI_SIM2] = {ANSWER_INIT("+KDSIMEI: ", SYNC_ANSWER)},
  [CME_BAND_CLASS] = {ANSWER_INIT("*PSRDBS:", SYNC_ANSWER)},
  [CME_PHONE_BOOK_IS_READY_SIM1] = {ANSWER_INIT("+PBREADY", ASYNC_ANSWER)},
  [CME_PHONE_BOOK_IS_READY_SIM2] = {ANSWER_INIT("+PBREADYDS", ASYNC_ANSWER)},
  [MODEM_READ_PHONEBOOK_CONFIG_SIM1] = {ANSWER_INIT("+CPBW: (", SYNC_ANSWER)},
  [MODEM_READ_PHONEBOOK_CONFIG_SIM2] = {ANSWER_INIT("+CPBWDS: (", SYNC_ANSWER)},
  [MODEM_READ_PHONEBOOK_FIND_SIM1] = {ANSWER_INIT("+CPBF: ", SYNC_ANSWER)},
  [MODEM_READ_PHONEBOOK_FIND_SIM2] = {ANSWER_INIT("+CPBFDS: ", SYNC_ANSWER)},
  [MODEM_READ_PHONEBOOK_ENTRY_SIM1] = {ANSWER_INIT("+CPBR: ", SYNC_ANSWER)},
  [MODEM_READ_PHONEBOOK_ENTRY_SIM2] = {ANSWER_INIT("+CPBRDS: ", SYNC_ANSWER)},
  [MODEM_SERVICE_READY_SIM1] = {ANSWER_INIT("*PSREADY: 0", ASYNC_ANSWER)},
  [MODEM_SERVICE_READY_SIM2] = {ANSWER_INIT("*PSREADYDS: 0", ASYNC_ANSWER)},
  [MODEM_SERVICE_READY_GENERIC_SIM1] = {ANSWER_INIT("*PSREADY:", ASYNC_ANSWER)},
  [MODEM_SERVICE_READY_GENERIC_SIM2] = {ANSWER_INIT("*PSREADYDS:", ASYNC_ANSWER)},
  [MODEM_ECHO] = {ANSWER_INIT("AT", SYNC_ANSWER)},
  [MODEM_RING_SIM1] = {ANSWER_INIT("+RING", ASYNC_ANSWER)},
  [MODEM_RING_SIM2] = {ANSWER_INIT("+RINGDS", ASYNC_ANSWER)},
  [MODEM_CRING_SIM1] = {ANSWER_INIT("+CRING", ASYNC_ANSWER)},
  [MODEM_CRING_SIM2] = {ANSWER_INIT("+CRINGDS", ASYNC_ANSWER)},
  [MODEM_NO_CARRIER] = {ANSWER_INIT("NO CARRIER", ASYNC_ANSWER)},
  [MODEM_NO_DIALTONE] = {ANSWER_INIT("NO DIALTONE", ASYNC_ANSWER)},
  [MODEM_DIAL_BUSY] = {ANSWER_INIT("BUSY", ASYNC_ANSWER)},
  [MODEM_NO_ANSWER] = {ANSWER_INIT("NO ANSWER", ASYNC_ANSWER)},
  [MODEM_BUSY] = {ANSWER_INIT("BUSY", ASYNC_ANSWER)},
  [MODEM_REPORTING_STATUS_SIM1] = {ANSWER_INIT("+KSREP:", ASYNC_ANSWER)},
  [MODEM_REPORTING_STATUS_SIM2] = {ANSWER_INIT("+KSREPDS:", ASYNC_ANSWER)},
  [MODEM_SMS_MEMORY] = {ANSWER_INIT("+CPMS:", ASYNC_ANSWER)},
  [MODEM_ZONE_INDICATION_SIM1] = {ANSWER_INIT("*PSUTTZ:", ASYNC_ANSWER)},
  [MODEM_ZONE_INDICATION_SIM2] = {ANSWER_INIT("*PSUTTZDS:", ASYNC_ANSWER)},
  [MODEM_CALLS_IN_PROGRESS_SIM1] = {ANSWER_INIT("+CLCC:", SYNC_ANSWER)},
  [MODEM_CALLS_IN_PROGRESS_SIM2] = {ANSWER_INIT("+CLCCDS:", SYNC_ANSWER)},
  [MODEM_PROMPT] = {ANSWER_INIT("> ", ASYNC_ANSWER)},
  [MODEM_PHONEBOOK_MEMORY_USAGE_SIM1] = {ANSWER_INIT("+CPBS: ", SYNC_ANSWER)},
  [MODEM_PHONEBOOK_MEMORY_USAGE_SIM2] = {ANSWER_INIT("+CPBSDS: ", SYNC_ANSWER)},
  [MODEM_FW_VERSION] = {ANSWER_INIT("SIERRA", SYNC_ANSWER)},
  [MODEM_PIN_REQUIRED_SIM1] = {ANSWER_INIT("+CPIN: SIM PIN", SYNC_ANSWER)},
  [MODEM_PIN_REQUIRED_SIM2] = {ANSWER_INIT("+CPINDS: SIM PIN", SYNC_ANSWER)},
  [MODEM_PIN_NOT_REQUIRED_SIM1] = {ANSWER_INIT("+CPIN: READY", SYNC_ANSWER)},
  [MODEM_PIN_NOT_REQUIRED_SIM2] = {ANSWER_INIT("+CPINDS: READY", SYNC_ANSWER)},
  [MODEM_PUK_REQUIRED_SIM1] = {ANSWER_INIT("+CPIN: SIM PUK", SYNC_ANSWER)},
  [MODEM_PUK_REQUIRED_SIM2] = {ANSWER_INIT("+CPINDS: SIM PUK", SYNC_ANSWER)},
  [MODEM_PASSWORD_ATTEMPTS_LEFT_SIM1] = {ANSWER_INIT("*PSPRAS: ", SYNC_ANSWER)},
  [MODEM_PASSWORD_ATTEMPTS_LEFT_SIM2] = {ANSWER_INIT("*PSPRASDS: ", SYNC_ANSWER)},
  [MODEM_EXTENDED_CALL_ERROR_REPORT] = {ANSWER_INIT("+CEER: ", SYNC_ANSWER)},
};

static const modemErrorList_t modemCmeErrorList[modem_cme_err_num] = //Modem command answers list
{
  [CME_PHONE_FAILURE] = {COMMAND_INIT("0")},
  [CME_NO_CONNECTION_TO_PHONE] = {COMMAND_INIT("1")},
  [CME_PHONE_ADAPTER_LINK_RESERVED] = {COMMAND_INIT("2")},
  [CME_OPERATION_NOT_ALLOWED] = {COMMAND_INIT("3")},
  [CME_OPERATION_NOT_SUPPORTED] = {COMMAND_INIT("4")},
  [CME_PH_SIM_PIN_REQUIRED] = {COMMAND_INIT("5")},
  [CME_PH_FSIM_PIN_REQUIRED] = {COMMAND_INIT("6")},
  [CME_PH_FSIM_PUK_REQUIRED] = {COMMAND_INIT("7")},
  [CME_SIM_NOT_INSERTED] = {COMMAND_INIT("10")},
  [CME_SIM_PIN_REQUIRED] = {COMMAND_INIT("11")},
  [CME_SIM_PUK_REQUIRED] = {COMMAND_INIT("12")},
  [CME_SIM_FAILURE] = {COMMAND_INIT("13")},
  [CME_SIM_BUSY] = {COMMAND_INIT("14")},
  [CME_SIM_WRONG] = {COMMAND_INIT("15")},
  [CME_INCORRECT_PASSWORD] = {COMMAND_INIT("16")},
  [CME_SIM_PIN2_REQUIRED] = {COMMAND_INIT("17")},
  [CME_SIM_PUK2_REQUIRED] = {COMMAND_INIT("18")},
  [CME_MEMORY_FULL] = {COMMAND_INIT("20")},
  [CME_INVALID_INDEX] = {COMMAND_INIT("21")},
  [CME_NOT_FOUND] = {COMMAND_INIT("22")},
  [CME_MEMORY_FAILURE] = {COMMAND_INIT("23")},
  [CME_TEXT_STRING_TOO_LONG] = {COMMAND_INIT("24")},
  [CME_INVALID_CHARACTERS_IN_TEXT_STRING] = {COMMAND_INIT("25")},
  [CME_DIAL_STRING_TOO_LONG] = {COMMAND_INIT("26")},
  [CME_INVALID_CHARACTERS_IN_DIAL_STRING] = {COMMAND_INIT("27")},
  [CME_NOT_NETWORK_SERVICE] = {COMMAND_INIT("30")},
  [CME_NETWORK_TIMEOUT] = {COMMAND_INIT("31")},
  [CME_EMERGENCY_CALL_ONLY] = {COMMAND_INIT("32")},
  [CME_NETWORK_PERSONALIZATION_PIN_REQUIRED] = {COMMAND_INIT("40")},
  [CME_NETWORK_PERSONALIZATION_PUK_REQUIRED] = {COMMAND_INIT("41")},
  [CME_NETWORK_SUBSET_PERSONALIZATION_PIN_REQUIRED] = {COMMAND_INIT("42")},
  [CME_NETWORK_SUBSET_PERSONALIZATION_PUK_REQUIRED] = {COMMAND_INIT("43")},
  [CME_SERVICE_PROVIDER_PERSONALIZATION_PIN_REQUIRED] = {COMMAND_INIT("44")},
  [CME_SERVICE_PROVIDER_PERSONALIZATION_PUK_REQUIRED] = {COMMAND_INIT("45")},
  [CME_CORPORATE_PERSONALIZATION_PIN_REQUIRED] = {COMMAND_INIT("46")},
  [CME_CORPORATE_PERSONALIZATION_PUK_REQUIRED] = {COMMAND_INIT("47")},
  [CME_RESOURCE_LIMITATION] = {COMMAND_INIT("99")},
  [CME_SYNCHRONIZATIN_ERROR] = {COMMAND_INIT("100")},
  [CME_GPRS_SERVICES_NOT_ALLOWED] = {COMMAND_INIT("107")},
  [CME_PLMN_NOT_ALLOWED] = {COMMAND_INIT("111")},
  [CME_LOCATION_AREA_NOT_ALLOWED] = {COMMAND_INIT("112")},
  [CME_ROAMING_NOT_ALLOWERD] = {COMMAND_INIT("113")},
  [CME_SERVICE_OPTION_NOT_SUPPORTED] = {COMMAND_INIT("132")},
  [CME_REQUEST_SERVICE_OPTION_NOT_SUBSCRIBED] = {COMMAND_INIT("133")},
  [CME_SERVICE_OPTION_TEMP_OUT_OF_ORDER] = {COMMAND_INIT("134")},
  [CME_UNSPECIFIED_GPRS_ERROR] = {COMMAND_INIT("148")},
  [CME_PDP_AUTHENTICATION_FAILURE] = {COMMAND_INIT("149")},
  [CME_INVALID_MOBILE_CLASS] = {COMMAND_INIT("150")},
  [CME_NO_MORE_SOCETS_AVAILABLE] = {COMMAND_INIT("902")},
  [CME_MEMORY_PROBLEM] = {COMMAND_INIT("903")},
  [CME_DNS_ERROR] = {COMMAND_INIT("904")},
  [CME_TCP_DISCONNECTION_BY_SERVER] = {COMMAND_INIT("905")},
  [CME_TCP_UDP_CONNECTION_ERROR] = {COMMAND_INIT("906")},
  [CME_GENERIC_ERROR] = {COMMAND_INIT("907")},
  [CME_FAIL_TO_ACCEPT_CLIENT_REQUEST] = {COMMAND_INIT("908")},
  [CME_INCOHERENT_DATA] = {COMMAND_INIT("909")},
  [CME_BAD_SESSION_ID] = {COMMAND_INIT("910")},
  [CME_SESSION_ALREADY_RUNNING] = {COMMAND_INIT("911")},
  [CME_NO_MORE_SESSIONS_AVAILABLE] = {COMMAND_INIT("912")},
  [CME_SOCKET_CONNECTION_TIMEOUT] = {COMMAND_INIT("913")},
  [CME_CONTROL_SOCKET_CONNECTION_TIMEOUT] = {COMMAND_INIT("914")},
  [CME_PARAMETER_NOT_EXPECTED] = {COMMAND_INIT("915")},
  [CME_PARAMETER_INVALID_VALUE_RANGE] = {COMMAND_INIT("916")},
  [CME_PARAMETER_MISSING] = {COMMAND_INIT("917")},
  [CME_FEATURE_NOT_SUPPORTED] = {COMMAND_INIT("918")},
  [CME_FEATURE_NOT_AVAILABLE] = {COMMAND_INIT("919")},
  [CME_PROTOCOL_NOT_SUPPORTED] = {COMMAND_INIT("920")},
  [CME_INVALID_BEARER_CONNECTION_STATE] = {COMMAND_INIT("921")},
  [CME_INVALID_SESSION_STATE] = {COMMAND_INIT("922")},
  [CME_INVALID_TERMINAL_PORT_STATE] = {COMMAND_INIT("923")},
  [CME_SESSION_BUSY] = {COMMAND_INIT("924")},
  [CME_HTTP_HEADER_NAME_MISSING] = {COMMAND_INIT("925")},
  [CME_HTTP_HEADER_VALUE_MISSING] = {COMMAND_INIT("926")},
  [CME_HTTP_HEADER_NAME_EMPTY_STRING] = {COMMAND_INIT("927")},
  [CME_HTTP_HEADER_VALUE_EMPTY_STRING] = {COMMAND_INIT("928")},
  [CME_INPUT_DATA_INVALID_FORMAT] = {COMMAND_INIT("929")},
  [CME_INPUT_DATA_INVALID_CONTENT] = {COMMAND_INIT("930")},
  [CME_INVALID_PARAMETER_LENGTH] = {COMMAND_INIT("931")},
  [CME_INVALID_PARAMETER_FORMAT] = {COMMAND_INIT("932")},
};

static const modemErrorList_t modemCmsErrorList[modem_cms_err_num] = //Modem command answers list
{
  [CMS_UNASSIGNED_NUMBER] = {COMMAND_INIT("1")},
  [CMS_OPERATOR_DETERMINED_BARRING] = {COMMAND_INIT("8")},
  [CMS_CALL_BARRED] = {COMMAND_INIT("10")},
  [CMS_SHORT_MESSAGE_TRANSFER_REJECTED] = {COMMAND_INIT("21")},
  [CMS_DESTINATION_OUT_OF_SERVICE] = {COMMAND_INIT("27")},
  [CMS_UNIDENTYFIED_SUBSCRIBER] = {COMMAND_INIT("28")},
  [CMS_FACILITY_REJECTED] = {COMMAND_INIT("29")},
  [CMS_UNKNOWN_SUBSCRIBER] = {COMMAND_INIT("30")},
  [CMS_NETWORK_OUT_OF_ORDER] = {COMMAND_INIT("38")},
  [CMS_TEMPORARY_FAILURE] = {COMMAND_INIT("41")},
  [CMS_CONGESTION] = {COMMAND_INIT("42")},
  [CMS_RESOURCES_UNAVAILABLE] = {COMMAND_INIT("47")},
  [CMS_FACILITY_BOT_SUBSCRIBED] = {COMMAND_INIT("50")},
  [CMS_FACILITY_NOT_IMPLEMENTED] = {COMMAND_INIT("69")},
  [CMS_INVALID_SHORT_MESSAGE_REFERENCE_VALUE] = {COMMAND_INIT("81")},
  [CMS_INVALID_MESSAGE] = {COMMAND_INIT("95")},
  [CMS_INVALID_MANDATORY_INFORMATION] = {COMMAND_INIT("96")},
  [CMS_MESSAGE_TYPE_NON_EXISTENT] = {COMMAND_INIT("97")},
  [CMS_MESSAGE_NOT_COMPATIBILE_WITH_PROTOCOL] = {COMMAND_INIT("98")},
  [CMS_INFORMATION_ELEMENT_NON_EXISTENT] = {COMMAND_INIT("99")},
  [CMS_PROTOCOL_ERROR] = {COMMAND_INIT("111")},
  [CMS_INTERWORKING] = {COMMAND_INIT("127")},
  [CMS_TELEMATIC_INTERWORKING] = {COMMAND_INIT("128")},
  [CMS_SHORT_MESSAGE_TYPE0_NOT_SUPPORTED] = {COMMAND_INIT("129")},
  [CMS_CANNOT_REPLACE_SHORT_MESSAGE] = {COMMAND_INIT("130")},
  [CMS_UNSPECIFIED_TP_PID_ERROR] = {COMMAND_INIT("143")},
  [CMS_ALPHABET_NOT_SUPPORTED] = {COMMAND_INIT("144")},
  [CMS_MESSAGE_CLASS_NOT_SUPPORTED] = {COMMAND_INIT("145")},
  [CMS_UNSPECIFIED_TP_DCS_ERROR] = {COMMAND_INIT("159")},
  [CMS_COMMAND_CANNOT_BE_EXECUTED] = {COMMAND_INIT("160")},
  [CMS_COMMAND_UNSUPPORTED] = {COMMAND_INIT("161")},
  [CMS_UNSPECIFIED_TP_COMMAND_ERROR] = {COMMAND_INIT("175")},
  [CMS_TPDU_NOT_SUPPORTED] = {COMMAND_INIT("176")},
  [CMS_SC_BUSY] = {COMMAND_INIT("192")},
  [CMS_NO_SC_SUBSCRIPTION] = {COMMAND_INIT("193")},
  [CMS_SC_SYSTEM_FAILUER] = {COMMAND_INIT("194")},
  [CMS_INVALID_SME_ADDRESS] = {COMMAND_INIT("195")},
  [CMS_DESTINATION_SME_BARRED] = {COMMAND_INIT("196")},
  [CMS_SM_REJECTED] = {COMMAND_INIT("197")},
  [CMS_VPF_NOT_SUPPORTED] = {COMMAND_INIT("198")},
  [CMS_TP_BP_NOT_SUPPORTED] = {COMMAND_INIT("199")},
  [CMS_D0_SIM_STORAGE_FULL] = {COMMAND_INIT("208")},
  [CMS_NO_SMS_STORAGE_CAPABILITY] = {COMMAND_INIT("209")},
  [CMS_ERROR_IN_MS] = {COMMAND_INIT("210")},
  [CMS_MEMORY_CAPACITY_EXCEEDED] = {COMMAND_INIT("211")},
  [CMS_SIM_APPLICATION_TOOLKIT_BUSY] = {COMMAND_INIT("212")},
  [CMS_SIM_DATA_DOWNLOAD_ERROR] = {COMMAND_INIT("213")},
  [CMS_UNSPECIFIED_ERROR_CAUSE] = {COMMAND_INIT("255")},
  [CMS_ME_FAILUER] = {COMMAND_INIT("300")},
  [CMS_SMS_SERVICE_OF_ME_RESERVED] = {COMMAND_INIT("301")},
  [CMS_OPERATION_NOT_ALLOWED] = {COMMAND_INIT("302")},
  [CMS_OPERATION_NOT_SUPPORTED] = {COMMAND_INIT("303")},
  [CMS_INVALID_PDU_MODE_PARAMETER] = {COMMAND_INIT("304")},
  [CMS_INVALID_TEXT_MODE_PARAMETER] = {COMMAND_INIT("305")},
  [CMS_SIM_NOT_INSERTED] = {COMMAND_INIT("310")},
  [CMS_SIM_PIN_REQUIRED] = {COMMAND_INIT("311")},
  [CMS_PH_SIM_PIN_REQUIRED] = {COMMAND_INIT("312")},
  [CMS_SIM_FAILURE] = {COMMAND_INIT("313")},
  [CMS_SIM_BUSY] = {COMMAND_INIT("314")},
  [CMS_SIM_WRONG] = {COMMAND_INIT("315")},
  [CMS_SIM_PUK_REQUIRED] = {COMMAND_INIT("316")},
  [CMS_SIM_PIN2_REQUIRED] = {COMMAND_INIT("317")},
  [CMS_SIM_PUK2_REQUIRED] = {COMMAND_INIT("318")},
  [CMS_MEMORY_FAILURE] = {COMMAND_INIT("320")},
  [CMS_INVALID_MEMORY_INDEX] = {COMMAND_INIT("321")},
  [CMS_MEMORY_FULL] = {COMMAND_INIT("322")},
  [CMS_SMSC_ADDRESS_UNKNOWN] = {COMMAND_INIT("330")},
  [CMS_NO_NETWORK_SERVICE] = {COMMAND_INIT("331")},
  [CMS_NETWORK_TIMEOUT] = {COMMAND_INIT("332")},
  [CMS_NO_CNMA_ACK_EXPECTED] = {COMMAND_INIT("340")},
  [CMS_UNKNOWN_ERROR] = {COMMAND_INIT("500")},
};

/**
 * @brief      Search command on possible modem answers list.
 *
 * @param[in]  cmdName  The command name.
 *
 * @return     Answer position in table.
 */
int searchForCmdOnList(char *cmdName)
{
  int cmdNum = 0;
  for (cmdNum = 0; cmdNum < modem_cmd_num; cmdNum++)
    {
      if (strncmpAscii(cmdName, answerFromModemCmdList[cmdNum].prefix, answerFromModemCmdList[cmdNum].prefixLen) == 0)
        {
          break;
        }
    }
  return cmdNum;
}

extern UART_HandleTypeDef hlpuart1; //from libUSART

/**
 * @brief      Modem hardware reset (by gpio).
 */
void modemReset(void)
{
  cleanSentCommandsList();
  mprintf("modem hw reset\n");
  HAL_GPIO_WritePin(M_RESET_PORT, M_RESET_PIN, GPIO_PIN_SET);
  osDelay(500);
  HAL_GPIO_WritePin(M_RESET_PORT, M_RESET_PIN, GPIO_PIN_RESET);
  osDelay(2000);
}

///////////////////////////////generic-functions/////////////////////////
char *atCommandSendData = NULL;
static void sendRawCommandToModem(char *atCmd)
{
  setPhoneTaskTimeout(MODEM_ACTIVE_PHONE_TASK_TIMEOUT);

  while (atCommandSendData) osDelay(10);
  int bufferLenght = strlenAscii(atCmd) + 1;
  atCommandSendData = dataAlloc(bufferLenght);
  strCpy(atCommandSendData, atCmd);

  HAL_UART_Transmit_IT(&hlpuart1, (uint8_t *)atCommandSendData, strlenAscii(atCommandSendData));
}

/**
 * @brief      Send a command by UART to modem completed.
 */
void sendCommandToModemCompleted(void)
{
  if (atCommandSendData)
    {
      osStatus sendStatus = commSend(MODULE_PHONE, MODULE_PHONE, CMD_INTERNAL_INT, CMD_PHONE_INT_TX_COMPLETE, CMD_REQ_NO_REPLY, atCommandSendData, NULL, NULL, NULL);
      massert((sendStatus == osOK), "Sending command to modem failed.\n");
    }
  atCommandSendData = NULL;
}

static void hardModemReset(void)
{
  modemReset();
  modemPowerOn();
  osDelay(2000);
  mprintf("Hard modem restart done!\n");
  modemKeepAlive();
}

#define MODEM_RESPONSE_TIMEOUT (5*60)*1000*2 //must be more than NETWORK_UPDATE_TIMEOUT in phoneTask

/**
 * @brief      Check modem last activity.
 */
void checkModem(void)
{
  if (lastModemActivity != 0 && systemState.phoneMode == NORMAL_MODE)
    {
      if ((lastModemActivity + MODEM_RESPONSE_TIMEOUT) < NS2MS(getUptime()))
        {
          mprintf(C_RED "MODEM HAVENT SENT ANYTHING FOR %d. Executing restart...\n" C_NORMAL, MODEM_RESPONSE_TIMEOUT);
          hardModemReset();
        }
    }
}

char *lastCommandToWhichModemReplied = NULL;

/**
 * @brief      Gets the last command to which modem replied.
 *
 * @return     The last command to which modem replied.
 */
char *getLastCommandToWhichModemReplied(void)
{
  if (lastCommandToWhichModemReplied)
    return lastCommandToWhichModemReplied;
  else
    return 0;
}

/**
 * @brief      Add "| ERR" to last command to which modem replied with error.
 */
void lastCommandToWhichModemRepliedError(void)
{
  if (lastCommandToWhichModemReplied)
    {
      strCat(lastCommandToWhichModemReplied, "| ERR");
    }
}

/**
 * @brief      Add "| OK" string to last command to which modem replied with
 *             success.
 */
void lastCommandToWhichModemRepliedSuccess(void)
{
  if (lastCommandToWhichModemReplied)
    {
      strCat(lastCommandToWhichModemReplied, "| OK");
    }
}

static void transmitCommandToModem(sentCommand_t *command)
{
  if (command)
    {
      modemWakeUp();
      char *str = dataAlloc(64);
      if (!str)
        {
          mprintf("[MODEM] cannot alloc buffer\n");
          return;
        }
      strCat (str, command->sentCommand);
      const char *const strEnd = "\r\n";
      strCat (str, strEnd);
      sendRawCommandToModem(str);
      dataFree(str);
      command->transmissions++;
      command->timestamp = NS2MS(getUptime());
      modemKeepAlive();
    }
}

#define RESPONSE_INFO 11 // space to add result to command - ' OK' or ' ERROR'
static void updateLastSendedComantForDebug(char *command)
{
  if (lastCommandToWhichModemReplied)
    {
      dataFree(lastCommandToWhichModemReplied);
      lastCommandToWhichModemReplied = NULL;
    }
  lastCommandToWhichModemReplied = dataAlloc(sizeInBytes(command) + RESPONSE_INFO + 1);
  memCpy(lastCommandToWhichModemReplied, command, sizeInBytes(command) + 1);
}

/**
 * @brief      Send next command to modem from commands list.
 */
void processNextCommand(void)
{
  sentCommand_t *command = sentCommandsHead;
  if (command)
    {
      uint64_t currentTimestamp = NS2MS(getUptime());
      if (!command->timestamp)
        {
          transmitCommandToModem(command);
          updateLastSendedComantForDebug(command->sentCommand);
        }
      else if ((currentTimestamp - command->timestamp) > command->responseTimeout)
        {
          if (command->transmissions < command->maxRetriesNumber)
            transmitCommandToModem(command);
          else
            {
              mprintf("%s NO RESP! comm was sent %d times-skipping\n", command->sentCommand, command->transmissions);
              removeSentCommandFromList(command);
            }
        }
    }
}

static void sendCmdToModem(simNumber_t simCardIndex, char *atCmd, timeoutValue_t responseTimeout, uint16_t retriesNumber)
{
  massert(strlenAscii(atCmd) < STORED_COMMAND_MAX_SIZE, "atCmd command length is greater than STORED_COMMAND_MAX_SIZE!\n");

  if (simCardIndex == SIM2 && getSim2PresenceStatus() == true)
    {
      addSentCommandToList("AT+KSS", simCardIndex, responseTimeout, retriesNumber);
    }
  addSentCommandToList(atCmd, simCardIndex, responseTimeout, retriesNumber);
}

#define COMMANDS_RETRIES 2

/**
 * @brief      Sends a command to modem.
 *
 * @param[in]  simCardIndex  The sim card index
 * @param[in]  atCmd         The at command
 * @param[in]  respTimeout   The response timeout
 */
void sendCommandToModem(simNumber_t simCardIndex, char *atCmd, timeoutValue_t respTimeout)
{
  if (getModemPowerState() == MODEM_ON)
    {
      sendCmdToModem(simCardIndex, atCmd, respTimeout, COMMANDS_RETRIES);
      cmdStats.modemSentCommandsCounter++;
    }
  else
    {
      cmdStats.modemSentErrorCommandsCounter++;
      cleanSentCommandsList();
      mprintf("Command |%s| has NOT been sent. Modem if OFF!\n", atCmd);
    }
}


/**
 * @brief      Gets the ussd string from ussd message string buffer.
 *
 * @param[in]  cmd   The input buffer
 *
 * @return     The clean ussd message from string buffer without redundant
 *             signs.
 */
char *getUssdStringFromStringBuffer(comm_t *cmd)
{
  char *startIndex = strChr(cmd->messageBuffer, '"');
  char *endIndex = strChr(startIndex + 1, '\"');
  char *buff = NULL;

  if (endIndex == NULL)
    endIndex = strChr(cmd->messageBuffer + 1, '\0');

  uint32_t len = endIndex - 2 - startIndex;
  buff = dataAlloc(len * sizeof(char) + 1);

  strncpyAscii(buff, startIndex + 1, len + 1);
  buff[len + 1] = 0;

  return buff;
}

/**
 * @brief      Gets the string from string buffer.
 *
 * @param[in]  cmd   The input buffer
 *
 * @return     The clean string from string buffer without redundant signs
 *             received from modem.
 */
char *getStringFromStringBuffer(comm_t *cmd)
{
  char *startIndex;
  char *endIndex;
  char *defaultText = translateToActiveLanguage(STR_UNKNOWN);
  size_t stringLength;
  char *coppiedData = NULL;

  startIndex = strChr(cmd->messageBuffer, '"');

  if (startIndex != NULL)
    {
      endIndex = strChr(startIndex + 1, '"');
      if (endIndex == NULL)
        {
          endIndex = strChr(cmd->messageBuffer + 1, '\0');
        }
    }

  if ((startIndex != NULL) && (endIndex != NULL))
    {
      stringLength = endIndex - startIndex - 1;

      if (stringLength == 0)
        {
          stringLength = strlenAscii(defaultText);
          coppiedData = dataAlloc(sizeof(char) * stringLength + 1);

          if (coppiedData == NULL)
            return defaultText;

          strncpyAscii(coppiedData, defaultText, stringLength );
        }
      else
        {
          coppiedData = dataAlloc(sizeof(char) * stringLength + 1);

          if (coppiedData == NULL)
            return defaultText;

          strncpyAscii(coppiedData, startIndex + 1, stringLength );
        }
      return coppiedData;
    }
  else
    {
      stringLength = strlenAscii(defaultText);
      coppiedData = dataAlloc(sizeof(char) * stringLength + 1);

      if (coppiedData == NULL)
        return defaultText;

      strncpyAscii(coppiedData, defaultText, stringLength );
      return coppiedData;
    }
}
/////////////////////////////////////////////////////////////////////////
///////////////////////////////voice calls///////////////////////////////

/**
 * @brief      Dial to given number
 *
 * @param[in]  simCardIndex  The sim card index
 * @param[in]  number        The phone number
 */
void dialToNumber(simNumber_t simCardIndex, char *number)
{
  char str[50] = "ATD";
  char endLine[] = ";";
  strCat (str, number);
  strCat (str, endLine);
  sendCommandToModem(simCardIndex, str, SECONDS_2);

  loudSpeakerModeOff();
  mprintf("SIM%d calling to number: %s\n", simCardIndex, number);
}

/**
 * @brief      Hangs up voice call.
 *
 * @param[in]  simNumber  The sim card number.
 */
void voiceCallHangUp(simNumber_t simNumber)
{
  sendCommandToModem(simNumber, "ATH0", SECONDS_30);
}

/**
 * @brief      Turns on hands free mode.
 */
void handsFreeModeOn(void)
{
  sendCommandToModem(SIM1, "AT+VIP=1", SECONDS_2);
}

/**
 * @brief      Turns off hands free mode.
 */
void handsFreeModeOff(void)
{
  sendCommandToModem(SIM1, "AT+VIP=0", SECONDS_2);
}

/**
 * @brief      Pick up the incoming voice call.
 *
 * @param[in]  simNumber  The sim card number
 */
void voiceCallPickUp(simNumber_t simNumber)
{
  sendCommandToModem(simNumber, "ATA", SECONDS_2);
  loudSpeakerModeOff();
}

typedef enum state_e {INITIAL_STATE = 0, SET_SMS_ONLY, RESET_SMS_ONLY} state_t;
state_t setSMSOnlyAfterInit = INITIAL_STATE;


/**
 * @brief      Turn on modem sms only mode.
 */
void setOnModeSMSOnly(void)
{
  if (getModemInitializationStatus() == CONF_STAGE_FINISHED_OK)
    {
      sendCommandToModem(SIM1, "AT+CSNS=4", SECONDS_2); // Data only (calls will be rejected automatically by module)
      turnOffRingIndicator();
    }
  else
    setSMSOnlyAfterInit = SET;
}

/**
 * @brief      Turn off modem sms only mode.
 */
void setOffModeSMSOnly(void)
{
  if (getModemInitializationStatus() == CONF_STAGE_FINISHED_OK)
    {
      sendCommandToModem(SIM1, "AT+CSNS=0", SECONDS_2); // Voice only (no data transmission)
      turnOnRingIndicator();
    }
  else
    setSMSOnlyAfterInit = RESET;
}

/////////////////////modem init and config functions/////////////////////
modemInitStatus_t modemInitializationStatus = CONF_STAGE_INITIAL_MODEM_STATE;

/**
 * @brief      Sets the modem initialization status.
 *
 * @param[in]  newModemStatus  The new modem status
 */
void setModemInitializationStatus(modemInitStatus_t newModemStatus)
{
  modemInitializationStatus = newModemStatus;
}

/**
 * @brief      Gets the modem initialization status.
 *
 * @return     The modem initialization status.
 */
modemInitStatus_t getModemInitializationStatus(void)
{
  return modemInitializationStatus;
}

static uint8_t modemConfigurationState = 0;
static void setModemConfigStateDone(void)
{
  writeBackupRegister(RTC_BKP_DR25, RTC_REG_MODEM_CONFIGURED_OK);
}

static void updateModemConfigStateFromRtcReg(void)
{
  // modemConfigurationState = readBackupRegister(RTC_BKP_DR25);
#pragma message("Full modem configuration everytime")
  modemConfigurationState = 0;

  if (modemConfigurationState == RTC_REG_MODEM_CONFIGURED_OK)
    mprintf(C_GREEN "[PHONE] Modem already configured! 0x%x\n" C_NORMAL, modemConfigurationState);
  else
    mprintf(C_RED "[PHONE] Modem needs configuration! 0x%x\n" C_NORMAL, modemConfigurationState);

}

static bool modemNeedToBeConfigured(void)
{
  if (modemConfigurationState == RTC_REG_MODEM_CONFIGURED_OK)
    return false;
  else
    return true;
}

static void modemResetOldSettings(void)
{
  WDOG_IM_ALIVE(osThreadGetIdx(osThreadGetId()));

  sendCommandToModem(SIM1, "AT+CSNS=4", SECONDS_2); // Data only (calls will be rejected automatically by module)

  checkPinOrPukLockState(SIM1);
  checkPinOrPukLockState(SIM2);

  if (modemNeedToBeConfigured())
    {
      sendCommandToModem(SIM1, "ATV1", SECONDS_2); //1 Long result code format: numeric code for TA Response Format
      sendCommandToModem(SIM1, "ATE1", SECONDS_2); //1 modem echo on
      sendCommandToModem(SIM1, "AT*PSSEAV=1", SECONDS_2);//service avalibility notify on
      sendCommandToModem(SIM1, "AT+CMEE=2", SECONDS_2); //=2  result code and use verbose for Report Mobile Termination Error
      sendCommandToModem(SIM1, "AT+KSREP=1", SECONDS_2); //=1  Mobile Start-up Reporting, The module will send an unsolicited code
      sendCommandToModem(SIM1, "AT+KSLEEP=0", SECONDS_2); // DTR control sleep
      sendCommandToModem(SIM1, "AT+CPWC=4,0", SECONDS_2); //GSM850 and GSM900: 4-2W, 5-800mW ,0
      sendCommandToModem(SIM1, "AT+CPWC=1,1", SECONDS_2); //GSM1800: 1-1W, 2-250mW           ,1
      sendCommandToModem(SIM1, "AT+CPWC=1,2", SECONDS_2); //GSM1900: 1-1W, 2-250mW           ,2
      turnOnRingIndicator();
    }
}

static void modemNetworkReportingConfig(void)
{
  WDOG_IM_ALIVE(osThreadGetIdx(osThreadGetId()));
  if (modemNeedToBeConfigured())
    {
      sendCommandToModem(SIM1, "AT+CREG=1", SECONDS_2); //=1 enable network registration and location info
      sendCommandToModem(SIM1, "AT*PSFSNT=0", SECONDS_2); //=0 turn off Field Strength Notification with Threshold
      sendCommandToModem(SIM1, "AT+CTZR=1", SECONDS_2); //=1 enable Automatic Time Zone Reporting
      sendCommandToModem(SIM1, "AT*PSUTTZ=1", SECONDS_2); //=1 Enable time zone indication
      sendCommandToModem(SIM1, "AT+CTZU=1", SECONDS_2); //=1 enable Automatic Time Zone Update via NITZ;
      sendCommandToModem(SIM1, "AT+COPS=0,0", SECONDS_120); //=0 automatic operator selection
      sendCommandToModem(SIM1, "AT*PSRDBS=1,31", SECONDS_30); //set auto band mode (GSM 850, GSM 900, E-GSM, DCS 1800, PCS 1900)
    }
  sendCommandToModem(SIM1, "AT*PSSTKI=0", SECONDS_30); //SIM ToolKit Interface Configuration
  sendCommandToModem(SIM1, "AT+CGMR", SECONDS_30); //Request Revision Identification
  sendCommandToModem(SIM1, "AT*PSRDBS?", SECONDS_30); //check band class
  sendCommandToModem(SIM1, "AT+KSREP?", SECONDS_2);
}

static void modemPhoneBookConfig(void)
{
  if (modemNeedToBeConfigured())
    {
      sendCommandToModem(SIM1, "AT+CPBS=\"SM\"", SECONDS_2);
      sendCommandToModem(SIM1, "AT+CSCS=\"IRA\"", SECONDS_2); //Set TE Character Set
    }
  WDOG_IM_ALIVE(osThreadGetIdx(osThreadGetId()));
}

static void modemDualSimConfiguration(void)
{
  WDOG_IM_ALIVE(osThreadGetIdx(osThreadGetId()));
  if (modemNeedToBeConfigured())
    {
      sendCommandToModem(SIM1, "AT+KSIMDET=0,1", SECONDS_2); //=0,1 disable SIM1 detection
      sendCommandToModem(SIM1, "AT+KSIMDET=0,2", SECONDS_2); //0,2 disable SIM2 detection
      sendCommandToModem(SIM1, "AT+KSIMSLOT=1", SECONDS_2); //=1 sim2 activation
    }
  sendCommandToModem(SIM1, "AT+KSDS=1", SECONDS_2); // set default sim
  sendCommandToModem(SIM1, "AT+CGSN", SECONDS_2); //show IMEI associated to slot1
  sendCommandToModem(SIM1, "AT+KDSIMEI?", SECONDS_2); //show IMEI associated to slot2
}

static void modemAudioConfiguration(void)
{
  WDOG_IM_ALIVE(osThreadGetIdx(osThreadGetId()));
  if (modemNeedToBeConfigured())
    {
      sendCommandToModem(SIM1, "AT+CRSL=0", SECONDS_2); //=0 turn off ringer Sound
      sendCommandToModem(SIM1, "AT+KECHO=2", SECONDS_2); //=2 Echo cancellation on with UL patch
      sendCommandToModem(SIM1, "AT+KNOISE=1,1", SECONDS_2); //=1,1 Noise Cancellation transmiter on, receiver on
      sendCommandToModem(SIM1, "AT+KPC=0", SECONDS_2); //=0 disable Peak Compressor
    }
  sendCommandToModem(SIM1, "AT+CLVL=6", SECONDS_2); // Loud Volume Level
}

static void modemSmsConfig(void)
{
  WDOG_IM_ALIVE(osThreadGetIdx(osThreadGetId()));
  if (modemNeedToBeConfigured())
    {
      sendCommandToModem(SIM1, "AT+CMGF=0", SECONDS_2); // SMS PDU mode
      sendCommandToModem(SIM1, "AT+CNMI=2,1,0,1,0", SECONDS_2); // New SMS Message Indication
      sendCommandToModem(SIM1, "AT+CPMS=\"ME\",\"ME\",\"ME\"", SECONDS_2); // set SMS memory to Modem (ME message storage)
      //sendCommandToModem(SIM1, "AT+CMGD=1,4", SECONDS_30); // delete all messages (for "debug" mode). It prevents to fill modem memory. (100 positions)
    }
}

static void modemVoiceCallsConfig(void)
{
  WDOG_IM_ALIVE(osThreadGetIdx(osThreadGetId()));
  if (modemNeedToBeConfigured())
    {
      sendCommandToModem(SIM1, "AT+KCCDN=1", SECONDS_2); //=1 enable Call Connection and Disconnection Notification
      sendCommandToModem(SIM1, "AT+CRC=1", SECONDS_2); //=1 Enable extended format cellular result codes for Incoming Call Indication
      sendCommandToModem(SIM1, "AT+KATH=0", SECONDS_2); //set ATH mode 0 = (default) user busy
      sendCommandToModem(SIM1, "AT+CSNS=0", SECONDS_2); //Single Numbering Scheme. Used for sms-only mode
      sendCommandToModem(SIM1, "AT+CLIP=1", SECONDS_2); //=1 enable Calling Line Identification Presentation
    }
  loudSpeakerModeOff();
  sendCommandToModem(SIM1, "AT+CLVL?", SECONDS_2); //get actual loudspeaker Volume Level
}

static void startModemConfigurationProcedure(void)
{
  setModemInitializationStatus(CONF_STAGE_INITIAL_RESET_COMMAND);
}

#define MAX_STAGE_TIME 60000
void continueFirstModemConfiguration(void)
{
  static uint64_t lastStageChangeTime = 0;
  if (checkAllSentCommands() == COMMAND_SUCCESS)
    {
      lastStageChangeTime = NS2MS(getUptime());
      switch (getModemInitializationStatus())
        {
        case CONF_STAGE_INITIAL_MODEM_STATE:
          break;
        case CONF_STAGE_INITIAL_RESET_COMMAND:
          updateModemConfigStateFromRtcReg();

          if (modemNeedToBeConfigured())
            {
              sendCommandToModem(SIM1, "AT+IPR=9600", SECONDS_2);
              sendCommandToModem(SIM1, "AT*PSSEAV=1", SECONDS_2);//service avalibility notify on
              sendCommandToModem(SIM1, "AT+CMEE=2", SECONDS_2); //=2  result code and use verbose for Report Mobile Termination Error
              sendCommandToModem(SIM1, "AT+KSREP=1", SECONDS_2); //=1  Mobile Start-up Reporting, The module will send an unsolicited code
            }
          //sendCommandToModem(SIM1, "AT+CFUN=1,1", SECONDS_30);
          //setModemInitializationStatus(CONF_STAGE_INITIAL_RESET);
          setModemInitializationStatus(CONF_STAGE_SEND_RESET_OLD_CONFIG);
          break;
        case CONF_STAGE_INITIAL_RESET:
          break;
        case CONF_STAGE_SEND_RESET_OLD_CONFIG:
          cleanSentCommandsList();
          modemResetOldSettings();
          setModemInitializationStatus(CONF_STAGE_ONE_RESET_OLD_CONFIG);
          break;
        case CONF_STAGE_ONE_RESET_OLD_CONFIG:
          cleanSentCommandsList();
          modemNetworkReportingConfig();
          setModemInitializationStatus(CONF_STAGE_TWO_VERBOSE_CONFIG);
          break;
        case CONF_STAGE_TWO_VERBOSE_CONFIG:
          cleanSentCommandsList();
          modemPhoneBookConfig();
          setModemInitializationStatus(CONF_STAGE_THREE_PHONEBOOK);
          break;
        case CONF_STAGE_THREE_PHONEBOOK:
          cleanSentCommandsList();
          modemDualSimConfiguration();
          setModemInitializationStatus(CONF_STAGE_FOUR_DUAL_SIM);
          break;
        case CONF_STAGE_FOUR_DUAL_SIM:
          cleanSentCommandsList();
          modemAudioConfiguration();
          setModemInitializationStatus(CONF_STAGE_FIVE_AUDIO);
          break;
        case CONF_STAGE_FIVE_AUDIO:
          cleanSentCommandsList();
          modemSmsConfig();
          setModemInitializationStatus(CONF_STAGE_SIX_SMS);
          break;
        case CONF_STAGE_SIX_SMS:
          cleanSentCommandsList();
          modemVoiceCallsConfig();
          setModemInitializationStatus(CONF_MODEM_VOICE_CALLS);
          break;
        case CONF_MODEM_VOICE_CALLS:
          sendCommandToModem(SIM1, "AT+CFUN=1,1", SECONDS_30);
          setModemInitializationStatus(CONF_STAGE_MODEM_POWER_RESET);
        case CONF_STAGE_MODEM_POWER_RESET:
          break;
        case CONF_STAGE_MODEM_CONFIG_AFTER_POWER_UP:
          checkPinOrPukLockState(SIM1);
          checkPinOrPukLockState(SIM2);

          sendCommandToModem(SIM1, "AT+CSNS=0", SECONDS_2);

          switch (setSMSOnlyAfterInit)
            {
            case SET:
              sendCommandToModem(SIM1, "AT+CSNS=4", SECONDS_2); // sms only mode
              turnOffRingIndicator();
              setSMSOnlyAfterInit = INITIAL_STATE;
              break;
            case RESET:
              turnOnRingIndicator();
              sendCommandToModem(SIM1, "AT+CSNS=0", SECONDS_2); // normal mode
              turnOnRingIndicator();
              setSMSOnlyAfterInit = INITIAL_STATE;
              break;
            default:
              break;
            }
          setModemInitializationStatus(CONF_STAGE_FINISHED_OK);

          sendCommandToModem(SIM1, "AT+COPS?", SECONDS_2);

          if (modemNeedToBeConfigured())
            setModemConfigStateDone();


          break;
        case CONF_STAGE_FINISHED_OK:
          cleanSentCommandsList();
          break;
        case modemInitStatus_num:
          break;
        }
    }
  else if ((lastStageChangeTime + MAX_STAGE_TIME < NS2MS(getUptime())) && (getModemInitializationStatus() != CONF_STAGE_FINISHED_OK))
    {
      lastStageChangeTime = NS2MS(getUptime());
      mprintf("MODEM INIT FAILED! Restarting and repeating initialization...\n");
      cleanSentCommandsList();
      hardModemReset();
      setModemInitializationStatus(CONF_STAGE_INITIAL_RESET_COMMAND);
    }
}

/**
 * @brief      Check network operator name.
 *
 * @param[in]  simNumber  The sim card number
 */
void checkOperator(simNumber_t simNumber)
{
  sendCommandToModem(simNumber, "AT+COPS?", SECONDS_2);
}

/**
 * @brief      Check network accessibility.
 *
 * @param[in]  simNumber  The sim card number
 */
void checkNetworkAccessibility(simNumber_t simNumber)
{
  sendCommandToModem(simNumber, "AT+CREG?", SECONDS_2);
}

modemPowerState_t modemPowerState = MODEM_OFF;
static void setModemPowerState(modemPowerState_t state)
{
  modemPowerState = state;
}

/**
 * @brief      Gets the modem power state.
 *
 * @return     The modem power state.
 */
modemPowerState_t getModemPowerState(void)
{
  return modemPowerState;
}

/**
 * @brief      Modem power on action.
 */
void modemPowerOn(void)
{
  HAL_GPIO_WritePin(M_LEV_SHIFT_OE_PORT, M_LEV_SHIFT_OE_PIN, GPIO_PIN_RESET); //turn on level shifter
  osDelay(500);
  HAL_GPIO_WritePin(M_PWR_ON_PORT, M_PWR_ON_PIN, GPIO_PIN_SET);
  osDelay(500);

  HAL_GPIO_WritePin(M_DTR_PORT, M_DTR_PIN, GPIO_PIN_RESET);
  osDelay(1500);
  HAL_GPIO_WritePin(M_PWR_ON_PORT, M_PWR_ON_PIN, GPIO_PIN_RESET);
  osDelay(2000);

  mprintf(C_GREEN "[modem.c] MODEM POWER ON\n" C_NORMAL);
  setModemPowerState(MODEM_ON);
  osDelay(500);
  turnOnRingIndicator();
}

/**
 * @brief      Modem power off action.
 */
void modemPowerOff(void)
{
  turnOffRingIndicator();
  cleanSentCommandsList();
  sendCommandToModem(SIM1, "AT*PSCPOF", SECONDS_5);
  mprintf("[modem.c] MODEM POWER OFF\n");
  HAL_GPIO_WritePin(M_DTR_PORT, M_DTR_PIN, GPIO_PIN_RESET);
  setModemPowerState(MODEM_OFF);
}

/**
 * @brief      Turns on modem ring indicator RI functionality.
 */
void turnOnRingIndicator(void)
{
  sendCommandToModem(SIM1, "AT+KRIC=11,0", SECONDS_2);
  // 0x01 RI activated on incoming calls (+CRING, RING)
  // 0x02 RI activated on SMS (+CMT, +CMTI)
  // 0x08 RI activated on USSD (+CUSD)
  // ,0 Repeat pulses
}

/**
 * @brief      Turns off modem ring indicator RI functionality.
 */
void turnOffRingIndicator(void)
{
  sendCommandToModem(SIM1, "AT+KRIC=0,0", SECONDS_2);
}

static void swModemReset(void)
{
  cleanSentCommandsList();
  modemWakeUp();
  osDelay(1000);
  HAL_GPIO_WritePin(M_RESET_PORT, M_RESET_PIN, GPIO_PIN_SET);
  osDelay(1000);
  HAL_GPIO_WritePin(M_RESET_PORT, M_RESET_PIN, GPIO_PIN_RESET);
  osDelay(1000);
  modemPrintf("[MODEM] RESET!\n");
}

/**
 * @brief      Gets the int from string buffer.
 *
 * @param[in]  cmd             The command buffer
 * @param[in]  whichIntReturn  The which int function have to return if string
 *                             buffer contains few numbers.
 *
 * @return     The selected int from string buffer.
 */
int getIntFromString(comm_t *cmd, uint8_t whichIntReturn)
{
  char *string;
  string = cmd->messageBuffer;

  uint8_t array[20] = {0};
  uint8_t each = 0;
  uint8_t saved = 1;
  uint8_t length = 0;
  uint8_t temporary = 0, i = 0;

  length = strlenAscii (string);
  for (i = 0; i < length; i++)
    {
      if ((string[i] >= '0') && (string[i] <= '9'))  //found a digit
        {
          temporary = (temporary * 10) + ( string[i] - '0');
          saved = 0;//flag not saved. temporary value may change
        }
      else  //found NOT a digit
        {
          if (i > 0 && !saved)  //not at first character and not saved
            {
              array[each] = temporary;//save integer
              each++;
              saved = 1;//flag saved
              temporary = 0;//reset
            }
        }
    }
  if (!saved)  //end of string is an integer. save it
    {
      array[each] = temporary;
      each++;
    }

  int tempIntFromString = 1;
  tempIntFromString = array[whichIntReturn];
  return tempIntFromString;
}

static void listCurrentCall(simNumber_t simNumber)
{
  sendCommandToModem(simNumber, "AT+CLCC", SECONDS_2);
}

/**
 * @brief      List current voice call state.
 *
 * @param[in]  simNumber  The simulation number
 */
void listVoiceCallState(simNumber_t simNumber)
{
  listCurrentCall(simNumber);
}

static void checkSmsCenterNumber(simNumber_t simNumber)
{
  if (simNumber == SIM1)
    sendCommandToModem(SIM1, "AT+CSCA?", SECONDS_2);
  else if (simNumber == SIM2)
    sendCommandToModem(SIM2, "AT+CSCA?", SECONDS_2);
}

smsCenter_Number_t smsCenterNumber;

/**
 * @brief      Update signal quality and network statistics.
 *
 * @param[in]  simNumber  The sim card number
 */
void updateSignalQualityAndNetworkStat(simNumber_t simNumber)
{
  sendCommandToModem(simNumber, "AT+CSQ", SECONDS_2);
}

/* Function should be transparent for second sim.*/
void updateSignalQualityAndNetStatus(simStatus_t statusSim1, simStatus_t statusSim2)
{
  if (getModemInitializationStatus() >= CONF_STAGE_MODEM_POWER_RESET)
    {
      if (statusSim1 == SIM_OK_NO_PIN) //SIM1
        {
          sendCommandToModem(SIM1, "AT+KSREP?", SECONDS_2);
          if (smsCenterNumber.smsCenterNumberSim1[0] == '\0')
            {
              sendCommandToModem(SIM1, "AT+CSCA?", SECONDS_2);
            }
          sendCommandToModem(SIM1, "AT+CSQ", SECONDS_2);
        }
      if (statusSim2 == SIM_OK_NO_PIN) //SIM2
        {
          if (smsCenterNumber.smsCenterNumberSim2[0] == '\0')
            {
              sendCommandToModem(SIM2, "AT+CSCA?", SECONDS_2);
            }
          sendCommandToModem(SIM2, "AT+CSQ", SECONDS_2);
        }
    }
}

/**
 * @brief      Check current modem power class.
 */
void checkCurrentModemPowerClass(void)
{
  sendCommandToModem(SIM1, "AT+CPWC?", SECONDS_2); //check Current Modem Power Class (for SIM1 + SIM2)
}

/**
 * @brief      Check current modem band for specified sim if it is available.
 *
 * @param[in]  statusSim1  The SIM1 card status
 * @param[in]  statusSim2  The status simulation 2
 */
void checkCurrentModemBand(simStatus_t statusSim1, simStatus_t statusSim2)
{
  if (statusSim1 == SIM_OK_NO_PIN) //SIM1
    sendCommandToModem(SIM1, "AT+KBND?", SECONDS_2); //check Current Networks Band Indicator SIM1
  if (statusSim2 == SIM_OK_NO_PIN) //SIM2
    sendCommandToModem(SIM2, "AT+KBND?", SECONDS_2); //check Current Networks Band Indicator SIM2
}

/**
 * @brief      Gets the actual time and date from network.
 */
void getActualTimeAndDateFromNetwork(void)
{
  sendCommandToModem(SIM1, "AT+CCLK?", SECONDS_2);
}

/**
 * @brief      Sets the modem speaker volume.
 *
 * @param[in]  speakerVolume  The speaker volume
 */
void setSpeakerVolume(int speakerVolume)
{
  char str[16] = {0};
  speakerVolume = speakerVolume > 10 ? 10 : speakerVolume;
  speakerVolume = speakerVolume < 0 ? 0 : speakerVolume;

  xsprintf(str, "AT+CLVL=%d\r\n", speakerVolume);
  mprintf("%s\n", str);
  sendCommandToModem(SIM1, str, SECONDS_2);
}

/**
 * @brief      Microphone mute on.
 */
void microphoneMute(void)
{
  sendCommandToModem(SIM1, "AT+KMAP=1", SECONDS_2);
}

/**
 * @brief      Microphone mute off.
 */
void microphoneUnmute(void)
{
  sendCommandToModem(SIM1, "AT+KMAP=0", SECONDS_2);
}

/**
 * @brief      Loudspeaker mode on.
 */
void loudSpeakerModeOn(void)
{
  handsFreeModeOn();
  sendCommandToModem(SIM1, "AT+KMAP=0,2,7", SECONDS_2); //Microphone Analog Parameters (0 to 3, 0 to 10)
  sendCommandToModem(SIM1, "AT+KVGR=\"12\"", SECONDS_2); // Receive Gain Selection (-20 to 18) - SPK
  sendCommandToModem(SIM1, "AT+KVGT=\"12\"", SECONDS_2); // Transmit Gain Seletion (-20 to 18) - MIC
}

/**
 * @brief      Loudspeaker mode off.
 */
void loudSpeakerModeOff(void)
{
  handsFreeModeOff();
  sendCommandToModem(SIM1, "AT+KMAP=0,2,7", SECONDS_2); //Microphone Analog Parameters (0 to 3, 0 to 10
  sendCommandToModem(SIM1, "AT+KVGR=\"-13\"", SECONDS_2); // Receive Gain Selection (-20 to 18) - SPK
  sendCommandToModem(SIM1, "AT+KVGT=\"3\"", SECONDS_2); // Transmit Gain Selection (-20 to 18) - MIC
}
/////////////////////////////////////////////////////////////////////////
static void smsDelete(uint16_t memoryPosition)
{
  char tempBuff[16] = "";
  xsprintf(tempBuff, "AT+CMGD=%d", memoryPosition);
  sendCommandToModem(SIM1, tempBuff, SECONDS_30);
}

/**
 * @brief      Gets the new sms in pdu (raw) format from modem.
 *
 * @param[in]  cmd   The input buffer
 */
void getNewSmsInPduFromModem(comm_t *cmd)
{
  int memoryPosition = getIntFromString(cmd, FIRST_INT_IN_STRING);
  char *memoryType = getStringFromStringBuffer(cmd);
  dataFree(memoryType);
  char tempBuff[16] = "";
  xsprintf(tempBuff, "AT+CMGR=%d", memoryPosition);
  sendCommandToModem(SIM1, tempBuff, SECONDS_30);
  osDelay(1500);
  smsDelete(memoryPosition);
}

/**
 * @brief      Sends a Ctrl-Z sign (0x1A) to modem.
 */
void sendCtrlZToModem(void)
{
  sendRawCommandToModem("\x1A"); // send CtrlZ

  sendRawCommandToModem("\r\n");

}

/**
 * @brief      Send outcoming sms
 *
 * @param[in]  simNumber  The sim card number
 * @param[in]  pduLength  The pdu frame length
 */
void smsSend(simNumber_t simNumber, uint16_t pduLength)
{
  char tempBuff[16] = "";
  xsprintf(tempBuff, "AT+CMGS=%d", pduLength);
  sendCommandToModem(simNumber, tempBuff, SECONDS_30);
}

static void sendPduFrame(send_pdu_frame_t *sms)
{
  if (sms->actualSendingMessage < MAX_NO_OF_SUB_SMS)
    {
      sendRawCommandToModem(sms->pdu[sms->actualSendingMessage]);
    }
}

static char *smscNumberCopy(comm_t *cmd)
{
  char *startIndex;
  char *endIndex;

  startIndex = strChr(cmd->messageBuffer, '"');
  endIndex = strChr(startIndex + 1, '"');

  if ((startIndex != NULL) && (startIndex != NULL))
    {
      size_t stringLength;
      stringLength = endIndex - startIndex - 1;

      char *coppiedData = dataAlloc(sizeof(char) * stringLength + 1);
      strncpyAscii(coppiedData, startIndex + 2, stringLength - 1);

      return coppiedData;
    }
  return 0;
}

/**
 * @brief      Updates sms center number for specified sim number.
 *
 * @param[in]  cmd        The input buffer which contains sms number
 * @param[in]  simNumber  The sim card number
 */
void updateSmsCenterNumber(comm_t *cmd, simNumber_t simNumber)
{
  char *smscNumber = NULL;
  smscNumber = smscNumberCopy(cmd);

  if (simNumber == SIM1)
    memCpy(smsCenterNumber.smsCenterNumberSim1, smscNumber, sizeof(smsCenterNumber.smsCenterNumberSim1));
  else if (simNumber == SIM2)
    memCpy(smsCenterNumber.smsCenterNumberSim2, smscNumber, sizeof(smsCenterNumber.smsCenterNumberSim2));
  dataFree(smscNumber);
}

send_pdu_frame_t *sendingSmsInternal = NULL;

/**
 * @brief      Determines if it is last sms part.
 *
 * @return     True if it is last sms part, False otherwise.
 */
uint8_t isItLastSmsPart(void)
{
  osDelay(50);
  if (sendingSmsInternal)
    {
      if (sendingSmsInternal->actualSendingMessage < sendingSmsInternal->subMessagesNumber - 1)
        return 0;
      else
        return 1;
    }
  else
    return 1;
}

/**
 * @brief      Sends a next sms sub-message.
 */
void sendNextSmsMessage(void)
{
  if (sendingSmsInternal)
    {
      if (sendingSmsInternal->actualSendingMessage < sendingSmsInternal->subMessagesNumber - 1)
        sendingSmsInternal->actualSendingMessage++;
      smsSend(sendingSmsInternal->whihSimHaveToSendSms, sendingSmsInternal->lenghtCmgs[sendingSmsInternal->actualSendingMessage]);
    }
}

/**
 * @brief      Cleans sent sms temporary data.
 */
void cleanSentSMS(void)
{
  int i;
  for (i = 0; i < (sendingSmsInternal->subMessagesNumber); i++)
    {
      if (sendingSmsInternal->pdu[i])
        {
          dataFree(sendingSmsInternal->pdu[i]);
          sendingSmsInternal->pdu[i] = NULL;
        }
    }

  if (sendingSmsInternal->pdu)
    {
      dataFree(sendingSmsInternal->pdu);
      sendingSmsInternal->pdu = NULL;
    }

  if (sendingSmsInternal->recipientNumber)
    {
      dataFree(sendingSmsInternal->recipientNumber);
      sendingSmsInternal->recipientNumber = NULL;
    }

  if (sendingSmsInternal->messageToSend)
    {
      dataFree(sendingSmsInternal->messageToSend);
      sendingSmsInternal->messageToSend = NULL;
    }

  if (sendingSmsInternal)
    {
      dataFree(sendingSmsInternal);
      sendingSmsInternal = NULL;
    }
}

/**
 * @brief      Sends a pdu frame to modem.
 */
void sendPduFrameToModem(void)
{
  sendPduFrame(sendingSmsInternal);
}

static void printfSmsSendingDebugInfo(send_pdu_frame_t *outSms)
{
  mprintf("SIM%d to number: %s\n", outSms->whihSimHaveToSendSms, outSms->recipientNumber);
  mprintf("SIM%d to number type: %d\n", outSms->whihSimHaveToSendSms, outSms->recipientNumberType);
  mprintf("Submessages number: %d\n", outSms->subMessagesNumber);
  mprintf("SMS coding type: %d\n", outSms->alphabet);

  mprintf("SMS length: %d\n", (int)checkMessageLength(outSms));

  mprintf("SMS out message: \"");

  uint8_t printfLength = 0;
  if ((int)checkMessageLength(outSms) > SMS_PRINTF_LENGTH_FOR_DEBUG)
    {
      printfLength = SMS_PRINTF_LENGTH_FOR_DEBUG;
      for (int i = 0; i < printfLength; i++)
        {
          mprintf("0x%02x ", outSms->messageToSend[i]);
        }
      mprintf("(...)\"\n");
    }
  else
    {
      printfLength = (int)checkMessageLength(outSms);
      for (int i = 0; i < printfLength; i++)
        {
          mprintf("0x%02x ", outSms->messageToSend[i]);
        }
      mprintf("\"\n");
    }
}

static void sendSmsPdu(void)
{
  if (sendingSmsInternal)
    {
      sendingSmsInternal->pdu = pduFrameEncode(sendingSmsInternal, &smsCenterNumber);
      printfSmsSendingDebugInfo(sendingSmsInternal);
      smsSend(sendingSmsInternal->whihSimHaveToSendSms, sendingSmsInternal->lenghtCmgs[sendingSmsInternal->actualSendingMessage]);
    }
}

int copyFromPlace = 0;
received_pdu_frame_t *receivedSmsInternal = NULL;
static void readNewSmsMessage(char *smsReceivedMessage, received_pdu_frame_t *sms)
{
  if (((receivedSmsInternal->pduType == 0x40) || (receivedSmsInternal->pduType == 0x44)) && (receivedSmsInternal->udh_frame.thisPart <= receivedSmsInternal->udh_frame.total))
    {
      mprintf("SMS with UDH data. SMS %d from %d.\n", receivedSmsInternal->udh_frame.thisPart, receivedSmsInternal->udh_frame.total);
      memCpy (&receivedSmsInternal->smsMessage[copyFromPlace], smsReceivedMessage, receivedSmsInternal->receivedSubMessageLength);
      copyFromPlace = copyFromPlace + receivedSmsInternal->receivedSubMessageLength;
      receivedSmsInternal->receivedMessageLength = receivedSmsInternal->receivedMessageLength + receivedSmsInternal->receivedSubMessageLength;
      modemPrintf("received Message Length = %d\n", receivedSmsInternal->receivedMessageLength);

      if (receivedSmsInternal->udh_frame.thisPart == receivedSmsInternal->udh_frame.total)
        {
          copyFromPlace = 0;
          receivedSmsInternal->receivedSubMessageLength = 0;
          receivedSmsInternal->wholeMessageReceivedFlag = 1;
        }
    }
  else //single message
    {
      modemPrintf("--->Received single message:\n");
      memCpy(receivedSmsInternal->smsMessage, smsReceivedMessage, receivedSmsInternal->receivedSubMessageLength);
      receivedSmsInternal->receivedMessageLength = receivedSmsInternal->receivedSubMessageLength;
      receivedSmsInternal->receivedSubMessageLength = 0;
      receivedSmsInternal->wholeMessageReceivedFlag = 1;
      return;
    }
}

/**
 * @brief      Check is modem received whole sms.
 *
 * @param[in]  newRegisterSms  The new register sms
 *
 * @return     True if modem received whole message or received all
 *             sub-messages, false otherwise.
 */
uint8_t modemReceivedWholeSms(received_sms_t *newRegisterSms)
{
  if (newRegisterSms->wholeMessageReceivedFlag == 1)
    {
      newRegisterSms->wholeMessageReceivedFlag = 0;
      return 1;
    }
  else
    {
      return 0;
    }
}

/**
 * @brief      Reads and decode new incoming sms.
 *
 * @param[in]  cmd   The input buffer
 *
 * @return     Returns new decoded sms in specified data structure if modem
 *             received whole multipart sms or it is only one-part message.
 */
received_sms_t *readNewSms(comm_t *cmd)
{
  if (receivedSmsInternal == NULL)
    {
      receivedSmsInternal = dataAlloc(sizeof(received_pdu_frame_t));
    }

  char smsReceivedMessage[SINGLE_SMS_TEXT_LENGTH] = "";
  pduFrameDecode(cmd->messageBuffer, smsReceivedMessage, receivedSmsInternal);
  readNewSmsMessage(smsReceivedMessage, receivedSmsInternal);

  if (receivedSmsInternal->wholeMessageReceivedFlag == 1)
    {
      received_sms_t *receivedMessage = dataAlloc(sizeof(received_sms_t) + receivedSmsInternal->receivedMessageLength + 1);
      receivedMessage->wholeMessageReceivedFlag = 1;

      receivedMessage->simNumber = receivedSmsInternal->whihSimReceivedSms;
      memCpy(receivedMessage->senderNumber, receivedSmsInternal->senderNumber, MAX_NUMBER_LENGTH);

      receivedMessage->smsTime = receivedSmsInternal->smsTime;
      receivedMessage->smsDate = receivedSmsInternal->smsDate;
      receivedMessage->receivedMessageLength = receivedSmsInternal->receivedMessageLength;
      receivedMessage->smsAlphabet = receivedSmsInternal->smsAlphabet;
      memCpy(receivedMessage->smsMessage, receivedSmsInternal->smsMessage, receivedSmsInternal->receivedMessageLength);

      //printfReceivedSmsInfo(receivedSmsInternal);

      receivedSmsInternal->receivedMessageLength = 0;
      receivedSmsInternal->receivedSubMessageLength = 0;

      if (receivedSmsInternal)
        {
          dataFree(receivedSmsInternal);
          receivedSmsInternal = NULL;
        }

      return receivedMessage;
    }
  return 0;
}


/**
 * @brief      Sends new outcoming sms.
 *
 * @param[in]  sms   The sms to send.
 */
void sendSms(send_sms_t *sms)
{
  if (sms->simNumber == SIM1)
    {
      if (smsCenterNumber.smsCenterNumberSim1[0] == '\0')
        checkSmsCenterNumber(SIM1);
    }
  else if (sms->simNumber == SIM2)
    {
      if (smsCenterNumber.smsCenterNumberSim2[0] == '\0')
        checkSmsCenterNumber(SIM2);
    }

  if (sendingSmsInternal == NULL)
    {
      sendingSmsInternal = dataAlloc(sizeof(send_pdu_frame_t));
    }
  checkSmsCenterNumber(SIM1);

  if (!sendingSmsInternal)
    return;

  if (sendingSmsInternal->recipientNumber == NULL)
    {
      sendingSmsInternal->recipientNumber = dataAlloc(sizeof(char) * sizeInBytes(sms->recipientNumber) + 1);
    }

  if (!sendingSmsInternal->recipientNumber)
    return;
  else
    {
      sendingSmsInternal->whihSimHaveToSendSms = sms->simNumber;
      sendingSmsInternal->alphabet = sms->alphabet;

      memCpy(sendingSmsInternal->recipientNumber, sms->recipientNumber, sizeInBytes(sms->recipientNumber));

      sendingSmsInternal->actualSendingMessage = 0;
      sendingSmsInternal->subMessagesNumber = 0;
      sendingSmsInternal->flash = 0;
      sendingSmsInternal->withUdh = 0;
      sendingSmsInternal->report = 0;
      sendingSmsInternal->validity = 170;
      sendingSmsInternal->systemMsg = 0;
      sendingSmsInternal->replaceSendingMsg = 0;
      sendingSmsInternal->recipientNumberType = 2;

      switch (sendingSmsInternal->alphabet)
        {
        case ALPHABET_7BIT:
        {
          if (sendingSmsInternal->messageToSend == NULL)
            {
              sendingSmsInternal->messageToSend = dataAlloc(sizeof(char) * sizeInBytes(sms->messageToSend) + 1);
            }
          if (sendingSmsInternal->messageToSend)
            memCpy(sendingSmsInternal->messageToSend, sms->messageToSend, sizeInBytes(sms->messageToSend));
          break;
        }
        case ALPHABET_8BIT:
          break;
        case ALPHABET_16BIT:
        {
          char *messageToEncodeToPdu = NULL;
          uint16_t *firstUnicodeChar = utf8ToUnicode(sms->messageToSend);
          changeOnEuroWhenSnedMessage(firstUnicodeChar, unicodeTextLength(firstUnicodeChar));
          uint16_t messageInUnicodeLength = 2 * unicodeTextLength(firstUnicodeChar);

          //mprintf("--------------------- messageInUnicodeLength + 2 = %d\n", messageInUnicodeLength + UNICODE_STRING_END_NULL_LENGTH);

          messageToEncodeToPdu = dataAlloc(sizeof(char) * messageInUnicodeLength + UNICODE_STRING_END_NULL_LENGTH);

          memSet(messageToEncodeToPdu, 0, messageInUnicodeLength + UNICODE_STRING_END_NULL_LENGTH);

          for (int i = 0; i < messageInUnicodeLength;)
            {
              messageToEncodeToPdu[i] = (*firstUnicodeChar >> 8);
              i++;
              messageToEncodeToPdu[i] = *firstUnicodeChar & 0xff;
              i++;
              firstUnicodeChar++;
            }

          sendingSmsInternal->messageToSend = dataAlloc(sizeof(char) * messageInUnicodeLength + UNICODE_STRING_END_NULL_LENGTH);
          memSet(sendingSmsInternal->messageToSend, 0, messageInUnicodeLength + UNICODE_STRING_END_NULL_LENGTH);


          if (sendingSmsInternal->messageToSend)
            memCpy(sendingSmsInternal->messageToSend, messageToEncodeToPdu, messageInUnicodeLength);


          dataFree(messageToEncodeToPdu);

          modemPrintf("messageToSend:\n");
          for (int i = 0; i < messageInUnicodeLength; i++)
            {
              modemPrintf("i = %d |0x%02x\n", i, sendingSmsInternal->messageToSend[i]);
            }
          break;
        }
        default:
          break;

        }

      sendSmsPdu();
      dataFree(sms);
    }
}

/**
 * @brief      Modem startup init after power on.
 */
void modemStartupInit(void)
{
  sendCtrlZToModem();
  modemReset();
  modemPowerOn();
  startModemConfigurationProcedure(); // modem initial configuration
}

static void printfReceivedUssdInfo(cmd_phone_ussd_t *message)
{
  if (message)
    {
      mprintf("SIM%d Received USSD: \"", message->simIndx);

      for (int i = 0; i < SMS_PRINTF_LENGTH_FOR_DEBUG; i++)
        {
          mprintf("%c", message->message[i]);
        }
      mprintf("(...)\"\n");
    }
  else
    {
      mprintf("USSD ERROR");
    }
}

/**
 * @brief      Gets the extended call error report if is needed.
 *
 * @param[in]  simIndx  The sim card index
 */
void getExtendedCallErrorReport(simNumber_t simIndx)
{
  sendCommandToModem(simIndx, "AT+CEER", SECONDS_2);
}

static cmd_phone_ussd_t *ussdResponseFormat(cmd_phone_ussd_t *messageToSend)
{
  cmd_phone_ussd_t *finalUssd = dataAlloc(sizeof(cmd_phone_ussd_t) + strlenAscii(messageToSend->message) + 1);

  if (finalUssd)
    {
      finalUssd->simIndx = messageToSend->simIndx;

      char *copyStartIndex = NULL;
      char *copyNoEndIndex = NULL;
      size_t stringToCopyLength = 0;

      copyStartIndex = strStr(messageToSend->message, "");
      copyNoEndIndex = strStr(copyStartIndex + 1, "\",");

      if (copyNoEndIndex == NULL)
        strncpyAscii(finalUssd->message, messageToSend->message, strlenAscii(messageToSend->message) + 1);
      else
        {
          stringToCopyLength = copyNoEndIndex - copyStartIndex;
          strncpyAscii(finalUssd->message, copyStartIndex, stringToCopyLength);
        }

      return finalUssd;
    }
  else
    return 0;
}

/**
 * @brief      Ussd response type handler.
 *
 * @param[in]  respType       The ussd response type
 * @param[in]  messageToSend  The received uusd message
 */
void ussdRespTypeHandler(uint8_t respType, cmd_phone_ussd_t *messageToSend)
{
  switch (respType)
    {
    case USSD_NO_FURTHER_USER_ACTION_REQ:
      checkCommSend(MODULE_PHONE, MODULE_MANAGEMENT, CMD_PHONE_USSD, CMD_PHONE_USSD_RECEIVED_ANSW, CMD_REQ_NO_REPLY, messageToSend, NULL, NULL, NULL, 5);
      printfReceivedUssdInfo(messageToSend);
      break;
    case USSD_FURTHER_USER_ACTION_REQUIRED:
    {
      cmd_phone_ussd_t *receivedUssd = ussdResponseFormat(messageToSend);

      if (messageToSend)
        {
          dataFree(messageToSend);
          messageToSend = NULL;
        }

      checkCommSend(MODULE_PHONE, MODULE_MANAGEMENT, CMD_PHONE_USSD, CMD_PHONE_USSD_REQ_RESP, CMD_REQ_NO_REPLY, receivedUssd, NULL, NULL, NULL, 5);
      printfReceivedUssdInfo(receivedUssd);
      break;
    }
    case USSD_TERMINATED_BY_NETWORK:
      checkCommSend(MODULE_PHONE, MODULE_MANAGEMENT, CMD_PHONE_USSD, CMD_PHONE_USSD_TERMINATED_BY_NETWORK, CMD_REQ_NO_REPLY, messageToSend, NULL, NULL, NULL, 5);
      printfReceivedUssdInfo(NULL);
      break;
    case USSD_OTHER_LOCAL_CLIENT_HAS_RESPONDED:
      checkCommSend(MODULE_PHONE, MODULE_MANAGEMENT, CMD_PHONE_USSD, CMD_PHONE_USSD_OTHER_CLIENT_RESP, CMD_REQ_NO_REPLY, messageToSend, NULL, NULL, NULL, 5);
      printfReceivedUssdInfo(NULL);
      break;
    case USSD_OPERATION_NOT_SUPPORTER:
      checkCommSend(MODULE_PHONE, MODULE_MANAGEMENT, CMD_PHONE_USSD, CMD_PHONE_USSD_NOT_SUPPORTED, CMD_REQ_NO_REPLY, messageToSend, NULL, NULL, NULL, 5);
      printfReceivedUssdInfo(NULL);
      break;
    case USSD_NETWORK_TIME_OUT:
      checkCommSend(MODULE_PHONE, MODULE_MANAGEMENT, CMD_PHONE_USSD, CMD_PHONE_USSD_TIMEOUT, CMD_REQ_NO_REPLY, messageToSend, NULL, NULL, NULL, 5);
      printfReceivedUssdInfo(NULL);
      break;
    }
}

/**
 * @brief      Sends an ussd code.
 *
 * @param[in]  simCardIndex  The sim card index
 * @param[in]  code          The code to send
 */
void sendUssdCode(simNumber_t simCardIndex, char *code)
{
  sendCommandToModem(simCardIndex, "AT+CUSD=1", SECONDS_30);

  char str[50] = "AT+CUSD=1,\"";
  char endLine[] = "\"";
  strCat (str, code);
  strCat (str, endLine);
  sendCommandToModem(simCardIndex, str, SECONDS_30);

  mprintf("SIM%d ussd send: %s\n", simCardIndex, code);
}

/**
 * @brief      Cancel current ussd sesion.
 */
void cancelUssdSession(void)
{
  sendCommandToModem(SIM1, "ATH", SECONDS_30);
  sendCommandToModem(SIM1, "AT+CUSD=2", SECONDS_30);
}

/**
 * @brief      Determines if it is whole ussd response.
 *
 * @param[in]      cmd   The input buffer
 *
 * @return     True if it is whole ussd response, False otherwise.
 */
int isItWholeUssdResponse(comm_t *cmd)
{
  static char pattern[] = "\",";
  if (strStr((char*)cmd->messageBuffer, pattern) != NULL)
    return 1;
  return 0;
}

#define USSD_MAX_RESP_LENGTH 182 + 1 // 160 bytes for GSM 7 bit message = 182 chars

/**
 * @brief      Ussd answer parser.
 *
 * @param[in]  ussdRespType          The ussd resp type
 * @param[in]  cmd                   The input buffer
 * @param[in]  simIndx               The sim card index
 * @param      receivedUssdResponse  The received ussd response
 *
 * @return     Returns actual modem multipart answer type
 */
multipartAnsw_t ussdParser(uint8_t ussdRespType, comm_t *cmd, simNumber_t simIndx, cmd_phone_ussd_t **receivedUssdResponse)
{
  multipartAnsw_t multipartModemAnswer = MULTIPART_MODEM_ANSWER_NULL;
  if ((ussdRespType == 0) || (ussdRespType == 1))
    {
      if (*receivedUssdResponse == NULL)
        {
          *receivedUssdResponse = dataAlloc(sizeof(cmd_phone_ussd_t) + USSD_MAX_RESP_LENGTH + 1);
          (*receivedUssdResponse)->simIndx = simIndx;
        }
      if (!isItWholeUssdResponse(cmd))
        {
          multipartModemAnswer = MULTIPART_MODEM_ANSWER_USSD_RESP;
          memSet((*receivedUssdResponse)->message, 0, USSD_MAX_RESP_LENGTH);
          char *tempString = getUssdStringFromStringBuffer(cmd);
          strCat ((*receivedUssdResponse)->message, tempString);
          dataFree(tempString);
        }
      else
        {
          memSet((*receivedUssdResponse)->message, 0, USSD_MAX_RESP_LENGTH);
          char *tempString = getUssdStringFromStringBuffer(cmd);
          strCat ((*receivedUssdResponse)->message, tempString);
          dataFree(tempString);
          ussdRespTypeHandler(ussdRespType, *receivedUssdResponse);
          *receivedUssdResponse = NULL;
        }
    }
  else
    {
      ussdRespTypeHandler(ussdRespType, NULL);
    }
  return multipartModemAnswer;
}

static void parseNetworkDateAndSendToManager(comm_t *cmd)
{
  sysDate_t *newDate = dataAlloc(sizeof(sysDate_t));
  newDate->year = getIntFromString(cmd, FIRST_INT_IN_STRING);
  newDate->month = getIntFromString(cmd, SECOND_INT_IN_STRING);
  newDate->day = getIntFromString(cmd, THIRD_INT_IN_STRING);
  checkCommSend(MODULE_PHONE, MODULE_MANAGEMENT, CMD_PHONE_CLOCK, CMD_PHONE_CLOCK_SEND_DATE, CMD_REQ_NO_REPLY, newDate, NULL, NULL, NULL, 5);
}

static void parseNetworkTimeAndSendToManager(comm_t *cmd)
{
  sysTime_t *newTime = dataAlloc(sizeof(sysTime_t));

  newTime->hour = getIntFromString(cmd, FOURTH_INT_IN_STRING);
  newTime->minute = getIntFromString(cmd, FIFTH_INT_IN_STRING);
  newTime->second = getIntFromString(cmd, SIXTH_INT_IN_STRING);
  checkCommSend(MODULE_PHONE, MODULE_MANAGEMENT, CMD_PHONE_CLOCK, CMD_PHONE_CLOCK_SEND_TIME, CMD_REQ_NO_REPLY, newTime, NULL, NULL, NULL, 5);
}

/**
 * @brief      Set up date an time from network.
 *
 * @param[in]      cmd   The input buffer
 */
void setUpDateAndTimeFromNetwork(comm_t *cmd)
{
  parseNetworkTimeAndSendToManager(cmd);
  parseNetworkDateAndSendToManager(cmd);
}

modemSleepState_t modempowerStatus = MODEM_AWAKEN;
static void setModemSleepStatus(modemSleepState_t newModemStatus)
{
  modempowerStatus = newModemStatus;
}

static modemSleepState_t getModemSleepStatus(void)
{
  return modempowerStatus;
}

/**
 * @brief      Modem ring indicator (RI) external gpio interrupt configuration.
 */
void modemRingIndicatorIntConfig(void)
{
  SYSCFG->EXTICR[2] |= SYSCFG_EXTICR3_EXTI8_PC;
  EXTI->FTSR1 |= M_RING_IND_PIN;
  NVIC_ClearPendingIRQ(EXTI9_5_IRQn);
  HAL_NVIC_SetPriority(EXTI9_5_IRQn, 6, 0);
}

/**
 * @brief      Modem wake up from sleep.
 */
void modemWakeUp(void)
{
  setModuleState(modBusyFast);
  if (getModemSleepStatus() == MODEM_SLEEP)
    {
      HAL_GPIO_WritePin(M_DTR_PORT, M_DTR_PIN, GPIO_PIN_RESET);
      osDelay(100);
      setModemSleepStatus(MODEM_AWAKEN);
      //mprintf("[modem.c] MODEM WAKE UP... %d ms\n", (uint32_t)(NS2MS(getUptime())));
    }
  modemKeepAlive();
}

/**
 * @brief      Enter modem into sleep mode.
 */
void modemSleep(void)
{
  if (getModemPowerState() == MODEM_ON)
    {
      if (getModemSleepStatus() == MODEM_AWAKEN)
        {
          HAL_GPIO_WritePin(M_DTR_PORT, M_DTR_PIN, GPIO_PIN_SET);
          setModemSleepStatus(MODEM_SLEEP);
          //mprintf("[modem.c] MODEM SLEEP...   %d ms\n", (uint32_t)(NS2MS(getUptime())));
        }
    }
  setModuleState(modBusyAuto);
}
/////////////////// phone book ///////////////////

/**
 * @brief      Search specified entry in phonebook by pattern.
 *
 * @param[in]  inputString  The search string
 * @param[in]  pattern      The search pattern
 *
 * @return     Return null if inputString not contains pattern.
 */
char *phonebookSearchParser(char *inputString, char *pattern)
{
  if (inputString && pattern)
    {
      char patternWithQuotationMarks[128] = {0};
      xsprintf(patternWithQuotationMarks, "\"%s\"", pattern);

      char *result = strStr(inputString, patternWithQuotationMarks);

      return result;
    }
  else
    return NULL;
}

/**
 * @brief      Read phonebook parameters from modem.
 *
 * @param[in]  simNumber  The sim card number
 */
void checkPhoneBookParameters(simNumber_t simNumber)
{
  //+CPBW: (1-500),40,(129,145),16,0,0,40
  //+CPBW: (1-250),40,(129,145),14,24,0,0
  if (simNumber == SIM1)
    sendCommandToModem(SIM1, "AT+CPBW=?", SECONDS_30);
  else if (simNumber == SIM2)
    sendCommandToModem(SIM2, "AT+CPBW=?", SECONDS_30);
}

/**
 * @brief      Read phone book memory usage from modem.
 *
 * @param[in]  simNumber  The sim card number
 */
void checkPhoneBookMemoryUsage(simNumber_t simNumber)
{
  //+CPBS: "SM",49,250
  //+CPBSDS: "SM",44,500
  if (simNumber == SIM1)
    sendCommandToModem(SIM1, "AT+CPBS?", SECONDS_2);
  else if (simNumber == SIM2)
    sendCommandToModem(SIM2, "AT+CPBS?", SECONDS_2);
}

/**
 * @brief      Reads a phone book entry from specified sim card number.
 *
 * @param[in]  simNumber    The sim card number
 * @param[in]  contactIndx  The contact to reaq index
 */
void readPhoneBookEntryFromSim(simNumber_t simNumber, uint16_t contactIndx)
{
  char tempBuff[20] = "";
  xsprintf(tempBuff, "AT+CPBR=%d", contactIndx);

  if (simNumber == SIM1)
    sendCommandToModem(SIM1, tempBuff, SECONDS_30);
  else if (simNumber == SIM2)
    sendCommandToModem(SIM2, tempBuff, SECONDS_30);
}

void modemAudioLoopTestControl(uint8_t state)
{
  if (state)
    {
      sendCommandToModem(SIM1, "AT+WMAUDIOLOOP=1,0,0", SECONDS_2);
    }
  else
    {
      sendCommandToModem(SIM1, "AT+WMAUDIOLOOP=0,0,0", SECONDS_2);
    }
}

#define PHONEBOOK_UCS2_CODING 2

/**
 * @brief      Writes a new phone book entry into sim card.
 *
 * @param[in]  phonebookContact  The phonebook contact structure
 * @param[in]  maxTextLength     The phonebook text max length specific for sim
 *                               card
 */
void writeNewPhoneBookEntry(phoneBookContact_t *phonebookContact, uint16_t maxTextLength)
{
  if ((phonebookContact->phoneNumber1Type != NATIONAL_NUMBER_TYPE) && (phonebookContact->phoneNumber1Type != INTERNATIONAL_NUMBER_TYPE))
    phonebookContact->phoneNumber1Type = NATIONAL_NUMBER_TYPE;

  char tempBuff[100] = "";
  char firstAndSecondName[64] = "";
  strCat (firstAndSecondName, phonebookContact->firstName);

  int firstNameLength = strlenAscii(phonebookContact->firstName);

  uint8_t phoneBookContactCodingType = detectSMSCoding(phonebookContact->firstName, firstNameLength);

  if ((phonebookContact->secondName[0] == '\0') || (phonebookContact->firstName[firstNameLength - 1] == ' '))
    strCat (firstAndSecondName, "");
  else
    strCat (firstAndSecondName, " ");

  strCat (firstAndSecondName, phonebookContact->secondName);

  if (phoneBookContactCodingType == PHONEBOOK_UCS2_CODING)
    convertUtf8ToAscii(firstAndSecondName);

  if (strlenAscii(firstAndSecondName) > maxTextLength)
    {
      char tempShortName[32] = "";

      uint16_t i;
      for (i = 0; i < maxTextLength; i++)
        {
          tempShortName[i] = firstAndSecondName[i];
        }

      if (phonebookContact->phoneNumber1[0] != '\0')
        xsprintf(tempBuff, "AT+CPBW=,\"%s\",%d,\"%s\"", phonebookContact->phoneNumber1, phonebookContact->phoneNumber1Type, tempShortName);
      else
        xsprintf(tempBuff, "AT+CPBW=,\"%s\",%d,\"%s\"", phonebookContact->phoneNumber2, phonebookContact->phoneNumber1Type, tempShortName);
    }
  else
    {
      if (phonebookContact->phoneNumber1[0] != '\0')
        xsprintf(tempBuff, "AT+CPBW=,\"%s\",%d,\"%s\"", phonebookContact->phoneNumber1, phonebookContact->phoneNumber1Type, firstAndSecondName);
      else
        xsprintf(tempBuff, "AT+CPBW=,\"%s\",%d,\"%s\"", phonebookContact->phoneNumber2, phonebookContact->phoneNumber1Type, firstAndSecondName);
    }

  if (phonebookContact->simNumber == SIM1)
    sendCommandToModem(SIM1, tempBuff, SECONDS_30);
  else if (phonebookContact->simNumber == SIM2)
    sendCommandToModem(SIM2, tempBuff, SECONDS_30);
}

/**
 * @brief      Reads phone book entry.
 *
 * @param[in]  cmd        The input string buffer
 * @param      phoneBook  The phone book contact
 */
void parsePhoneBookEntry(comm_t *cmd, cmd_phone_pbook_t *phoneBook)
{
  char *copyStartIndex;
  char *copyNoEndIndex;
  size_t stringToCopyLength;

  phoneBook->pbIdx = getIntFromString(cmd, FIRST_INT_IN_STRING);

  if (strncmpAscii(cmd->messageBuffer, "+CPBR: ", sizeof("+CPBR: ") - 1) == 0)
    phoneBook->simIndx = SIM1;
  else if (strncmpAscii(cmd->messageBuffer, "+CPBRDS: ", sizeof("+CPBRDS: ") - 1) == 0)
    phoneBook->simIndx = SIM2;

  copyStartIndex = strStr(cmd->messageBuffer, ",\"");
  copyNoEndIndex = strStr(copyStartIndex + 1, "\",");
  stringToCopyLength = copyNoEndIndex - copyStartIndex - 1;
  memSet (phoneBook->phoneNo, 0, sizeof(phoneBook->phoneNo));
  strncpyAscii(phoneBook->phoneNo, copyStartIndex + 2, stringToCopyLength - 1);

  copyStartIndex = strStr(copyNoEndIndex, ",");
  copyNoEndIndex = strStr(copyStartIndex + 1, ",");
  stringToCopyLength = copyNoEndIndex - copyStartIndex - 1;
  strncpyAscii(cmd->messageBuffer, copyStartIndex + 1, stringToCopyLength);
  phoneBook->numType = (numberType_t)getIntFromString(cmd, FIRST_INT_IN_STRING);

  copyStartIndex = strStr(copyNoEndIndex, "\"");
  copyNoEndIndex = strStr(copyStartIndex + 1, "\"");
  stringToCopyLength = copyNoEndIndex - copyStartIndex - 1;
  memSet (phoneBook->name, 0, sizeof(phoneBook->name));
  strncpyAscii(phoneBook->name, copyStartIndex + 1, stringToCopyLength);
}

/**
 * @brief      Delete specified phonebook entry
 *
 * @param[in]  simNumber      The sim card number
 * @param[in]  indexToDelete  The index to delete
 */
void deletePhoneBookEntry(simNumber_t simNumber, uint16_t indexToDelete)
{
  char tempBuff[16] = "";
  xsprintf(tempBuff, "AT+CPBW=%d", indexToDelete);
  if (simNumber == SIM1)
    sendCommandToModem(SIM1, tempBuff, SECONDS_30);
  else if (simNumber == SIM2)
    sendCommandToModem(SIM2, tempBuff, SECONDS_30);
}

/**
 * @brief      Find phonebook entry by contact name
 *
 * @param[in]  simNumber  The sim card number
 * @param[in]  name       The search name
 */
void findPhoneBookEntryByContactName(simNumber_t simNumber, char *name)
{
  char tempBuff[64] = {0};
  xsprintf(tempBuff, "AT+CPBF=\"%s\"", name);
  if (simNumber == SIM1)
    sendCommandToModem(SIM1, tempBuff, SECONDS_30);
  else if (simNumber == SIM2)
    sendCommandToModem(SIM2, tempBuff, SECONDS_30);
}

//////////////////////////// dial tone dtmf ///////////////////////////////////

/**
 * @brief      Sends a dial tone data.
 *
 * @param[in]  simNumber  The sim card number
 * @param[in]  keyTone    The key tone
 */
void sendDialToneData(simNumber_t simNumber, kbd_io_key_t keyTone)
{
  char cmd[20];

  switch (keyTone)
    {
    case IO_KEY_HASH:
      strCpy(cmd, "AT+VTS=\"#\"");
      break;
    case IO_KEY_STAR:
      strCpy(cmd, "AT+VTS=\"*\"");
      break;
    case IO_KEY_0 ... IO_KEY_9:
      xsprintf(cmd, "AT+VTS=\"%d\"", keyTone);
      break;
    default:
      break;
    }

  sendCommandToModem(simNumber, "AT+VTD=0", SECONDS_2);
  sendCommandToModem(simNumber, cmd, SECONDS_2);
}

/////////////////////////////// pin & puk ///////////////////////////////////

/**
 * @brief      Sends pin code to modem.
 *
 * @param[in]  simIndx  The sim card index
 * @param      atCmd    The pin code
 */
void pinEnter(simNumber_t simIndx, char *atCmd)
{
  char *pinCheckModemCommand = dataAlloc(sizeof(char) * sizeInBytes(atCmd) + 10);
  xsprintf(pinCheckModemCommand, "AT+CPIN=\"%s\"", atCmd);
  sendCommandToModem(simIndx, pinCheckModemCommand, SECONDS_30);
  dataFree(pinCheckModemCommand);
}

/**
 * @brief      Sends puk code to modem.
 *
 * @param[in]  simIndx  The sim card index
 * @param      atCmd    The puk code
 */
void pukEnter(simNumber_t simIndx, char *atCmd)
{
  char *pukCheckModemCommand = dataAlloc(sizeof(char) * sizeInBytes(atCmd) + 17);
  xsprintf(pukCheckModemCommand, "AT+CPIN=\"%s\",\"1234\"", atCmd);
  sendCommandToModem(simIndx, pukCheckModemCommand, SECONDS_30);
  dataFree(pukCheckModemCommand);
}

/**
 * @brief      Checks pin and puk attempts left number.
 *
 * @param[in]  simIndx  The sim card index
 */
void checkPinAndPukAttemptsLeft(simNumber_t simIndx)
{
  sendCommandToModem(simIndx, "AT*PSPRAS?", SECONDS_2);
}

/**
 * @brief      Changes pin number.
 *
 * @param[in]  simIndx  The sim card index
 * @param[in]  pinData  Old and new pin data
 */
void pinChange(simNumber_t simIndx, cmd_phone_pin_change_t *pinData)
{
  char *pinChangeModemCommand = dataAlloc(sizeof(char) * (strlenAscii(pinData->oldPin) + strlenAscii(pinData->newPin)) + 19);
  xsprintf(pinChangeModemCommand, "AT+CPWD=\"SC\",\"%s\",\"%s\"", pinData->oldPin, pinData->newPin);
  mprintf("PIN change command = %s\n", pinChangeModemCommand);
  sendCommandToModem(simIndx, pinChangeModemCommand, SECONDS_2);
  dataFree(pinChangeModemCommand);
}

/**
 * @brief      Enables pin protection.
 *
 * @param[in]  simIndx  The sim card index
 * @param      pin      The pin code
 */
void pinEnable(simNumber_t simIndx, char *pin)
{
  char *enablePinModemCommand = dataAlloc(sizeof(char) * (strlenAscii(pin) + 19));
  xsprintf(enablePinModemCommand, "AT+CLCK=\"SC\",1,\"%s\",1", pin);
  sendCommandToModem(simIndx, enablePinModemCommand, SECONDS_2);
  dataFree(enablePinModemCommand);
}

/**
 * @brief      Disables pin protection.
 *
 * @param[in]  simIndx  The sim card index
 * @param      pin      The pin code
 */
void pinDisable(simNumber_t simIndx, char *pin)
{
  char *disablePinModemCommand = dataAlloc(sizeof(char) * (strlenAscii(pin) + 19));
  xsprintf(disablePinModemCommand, "AT+CLCK=\"SC\",0,\"%s\",1", pin);
  sendCommandToModem(simIndx, disablePinModemCommand, SECONDS_2);
  dataFree(disablePinModemCommand);
}

/**
 * @brief      Checks pin or puk lock state.
 *
 * @param[in]  simIndx  The sim card index
 */
void checkPinOrPukLockState(simNumber_t simIndx)
{
  sendCommandToModem(simIndx, "AT+CPIN?", SECONDS_30);
}

extern DMA_HandleTypeDef hdma_lpuart1_rx;
extern volatile uint8_t dmaRxBuffer[LPUART_DMA_BUFFER_LENGTH];

/**
 * @brief      Reads a LPUART DMA buffer.
 */
void readLpuartDmaBuffer(void)
{
  static int32_t currentBuffPos = -1;
  static int32_t dmaCntNew = LPUART_DMA_BUFFER_LENGTH;
  static int32_t dmaCntPrev = LPUART_DMA_BUFFER_LENGTH;
  static int32_t bytesToRead = 0;
  static int32_t i = 0;

  do
    {
      dmaCntNew = getLpuartDmaCounter();
      if (dmaCntPrev == dmaCntNew)
        break;
      else if (dmaCntPrev - dmaCntNew > 0)
        bytesToRead = dmaCntPrev - dmaCntNew;
      else
        bytesToRead = dmaCntPrev - dmaCntNew + LPUART_DMA_BUFFER_LENGTH;

      setPhoneTaskTimeout(MODEM_ACTIVE_PHONE_TASK_TIMEOUT);

      if (bytesToRead > maxDmaBuffUsage)
        maxDmaBuffUsage = bytesToRead;

      for (i = 0; i < bytesToRead; i++)
        {
          currentBuffPos++;
          currentBuffPos %= LPUART_DMA_BUFFER_LENGTH;
          receiveModemData(dmaRxBuffer[currentBuffPos]);
        }
      dmaCntPrev = dmaCntNew;
    }
  while (1);
}