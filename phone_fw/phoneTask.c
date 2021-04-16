#include <cmsis_os.h>
#include <xprintf.h>
#include <phoneTask.h>
#include <communicationAPI.h>
#include <buffers.h>
#include <board.h>
#include <generated/managerGen.h>
#include <generated/phoneGen.h>
#include <generated/ioGen.h>
#include <modemApi.h>
#include <stdio.h>
#include <smsRead.h>
#include <managerTaskPhoneCallbacks.h>
#include <phoneCallList.h>
#include <smsRegister.h>
#include <guiTask.h>
#include <GuiVar.h>
#include <GuiStruct.h>
#include <managerTask.h>
#include <commTask.h>
#include <logs.h>
#include <timeFunc.h>

// #define PHONE_TASK_DEBUG
#ifndef PHONE_TASK_DEBUG
#define phonePrintf(...)
#else
#define phonePrintf(...) mprintf("[PHONE DEBUG] " __VA_ARGS__)
#endif

#define NETWORK_UPDATE_TIMEOUT 60
#define RSSI_NO_SIM -1
#define BER_NO_SIM 0

typedef enum modemCommunicationState_e {NORMAL, SEND_SMS_PROMT} modemCommunicationState_t;
modemCommunicationState_t modemCommunicationState = NORMAL;

static phone_book_info_t pBookInfo[SIM_CARD_NUMBERS] =
{
  {
    .simIndx = SIM1,
    .actualImportIndx = 0,
    .startIndx = -1,
    .memUsed = -1,
    .endIndx = -1,
    .maxLengthOfText = -1,
    .maxLengthOfNumber = -1,
  },
  {
    .simIndx = SIM2,
    .actualImportIndx = 0,
    .startIndx = -1,
    .memUsed = -1,
    .endIndx = -1,
    .maxLengthOfText = -1,
    .maxLengthOfNumber = -1,
  },
};

static void sendUpdatedPhoneBookInfo(simNumber_t simId)
{
  phone_book_info_t *pBupdatedStatus = dataAlloc(sizeof(phone_book_info_t));
  memCpy(pBupdatedStatus, &(pBookInfo[simId]), sizeof(phone_book_info_t));
  checkCommSend(MODULE_PHONE, MODULE_MANAGEMENT, CMD_PHONE_PBOOK, CMD_PHONE_PBOOK_INFO, CMD_REQ_NO_REPLY, pBupdatedStatus, NULL, NULL, NULL, 5);
}

static phone_internal_network_status_t networkInfo[SIM_CARD_NUMBERS] =
{
  {
    .simIndx = SIM1,
    .rssi = RSSI_NO_SIM,
    .ber = BER_NO_SIM,
    .networkOperator = "",
    .simStatus = SIM_INITIAL_STATE,
    .networkStatus = NETWORK_INITIAL_STATE,
    .imei = "\0",
    .band = -1,
    .bandClass = -1,
  },
  {
    .simIndx = SIM2,
    .rssi = RSSI_NO_SIM,
    .ber = BER_NO_SIM,
    .networkOperator = "",
    .simStatus = SIM_INITIAL_STATE,
    .networkStatus = NETWORK_INITIAL_STATE,
    .imei = "\0",
    .band = -1,
    .bandClass = -1,
  },
};

/**
 * @brief      Gets the SIM2 card presence status.
 *
 * @return     Return true when sim2 is present, false otherwise.
 */
bool getSim2PresenceStatus(void)
{
  if (networkInfo[SIM2].simStatus == SIM_OK_NO_PIN || networkInfo[SIM2].simStatus ==  WAIT_FOR_PIN)
    return true;
  else
    return false;
}

char *lastCeerVoiceCallError = NULL;

/**
 * @brief      Gets the last voice call error type.
 *
 * @return     The last voice call error type.
 */
char *getLastVoiceCallError(void)
{
  return lastCeerVoiceCallError;
}

bool sendInfoToManagerAboutSim1Problem = false;
bool sendInfoToManagerAboutSim2Problem = false;
static void changeSimStatus(simNumber_t simId, simStatus_t status)
{
  simStatus_t oldStatus = networkInfo[simId].simStatus;
  switch (status)
    {
    case SIM_OK_NO_PIN:
      sendInfoToManagerAboutSim1Problem = false;
      sendInfoToManagerAboutSim2Problem = false;
      //networkInfo[simId].rssi = 0;
      //networkInfo[simId].ber = 0;
      networkInfo[simId].simStatus = SIM_OK_NO_PIN;
      break;
    case WAIT_FOR_PIN:
      strCpy(networkInfo[simId].networkOperator, "");
      networkInfo[simId].rssi = 0;
      networkInfo[simId].ber = 0;
      networkInfo[simId].simStatus = WAIT_FOR_PIN;
      break;
    case SIM_NOT_PRESENT:
      strCpy(networkInfo[simId].networkOperator, "");
      networkInfo[simId].rssi = 0;
      networkInfo[simId].ber = 0;
      networkInfo[simId].simStatus = SIM_NOT_PRESENT;
      break;
    case SIM_LOCK_STATE:
      strCpy(networkInfo[simId].networkOperator, "");
      networkInfo[simId].rssi = 0;
      networkInfo[simId].ber = 0;
      networkInfo[simId].simStatus = SIM_LOCK_STATE;
      break;
    case UNRECOVERABLE_ERROR:
      strCpy(networkInfo[simId].networkOperator, "");
      networkInfo[simId].rssi = 0;
      networkInfo[simId].ber = 0;
      networkInfo[simId].simStatus = UNRECOVERABLE_ERROR;
      break;
    case UNKNOWN_STATE:
      strCpy(networkInfo[simId].networkOperator, "");
      networkInfo[simId].rssi = 0;
      networkInfo[simId].ber = 0;
      networkInfo[simId].simStatus = UNKNOWN_STATE;
      break;
    default:
      networkInfo[simId].rssi = RSSI_NO_SIM;
      networkInfo[simId].ber = BER_NO_SIM;
      break;
    }
  networkInfo[simId].simStatus = status;

  simStatus_t newStatus = networkInfo[simId].simStatus;
  if (newStatus != oldStatus)
    {
      if (newStatus != oldStatus)
        mprintf("SIM%d changed SIM STATUS from %d to %d\n", simId, oldStatus, newStatus);
    }

  if (simId == SIM1)
    {
      if (newStatus != SIM_OK_NO_PIN && newStatus != WAIT_FOR_PIN) // any state
        {
          sendInfoToManagerAboutSim1Problem = true;
          mprintf(C_RED "Problem with sim%d. New status = %d. Oldstatus = %d\n" C_NORMAL, SIM1, newStatus, oldStatus);
        }
    }

  if (simId == SIM2)
    {
      if (newStatus != SIM_OK_NO_PIN && newStatus != WAIT_FOR_PIN && oldStatus == SIM_OK_NO_PIN)
        {
          sendInfoToManagerAboutSim2Problem = true;
          mprintf(C_RED "Problem with sim%d. New status = %d. Oldstatus = %d\n" C_NORMAL, SIM2, newStatus, oldStatus);
        }
    }
}

/**
 * @brief      Modem ready status.
 *
 * @return     Return true if modem is ready and after configuration, false
 *             otherwise.
 */
uint8_t modemIsReady(void)
{
  if (getModemInitializationStatus() > CONF_STAGE_MODEM_POWER_RESET)
    return 1;
  else
    return 0;
}

static void sendSimError(void)
{
  phone_internal_network_status_t *simErrorInfo = dataAlloc(sizeof(phone_internal_network_status_t));
  simErrorInfo->simIndx = SIM1;
  simErrorInfo->simStatus = UNRECOVERABLE_ERROR;
  simErrorInfo->networkStatus = NOT_REG_BUT_SEARCHING;
  checkCommSend(MODULE_PHONE, MODULE_MANAGEMENT, CMD_PHONE_CONFIG, CMD_PHONE_CONFIG_SIM_ERROR, CMD_REQ_NO_REPLY, simErrorInfo, NULL, NULL, NULL, 5);
}

static void changeNetworkStatus(simNumber_t simId, simNetwork_t networkStatus)
{
  //simNetwork_t oldNetworkStatus = networkInfo[simId].networkStatus;
  switch (networkStatus)
    {
    case NOT_OK_NOT_SEARCHING:
      strCpy(networkInfo[simId].networkOperator, "");
      networkInfo[simId].networkStatus = NOT_OK_NOT_SEARCHING;
      break;
    case REGISTERED_HOME_NETWORK:
      networkInfo[simId].networkStatus = REGISTERED_HOME_NETWORK;
      break;
    case NOT_REG_BUT_SEARCHING:
      strCpy(networkInfo[simId].networkOperator, "");
      networkInfo[simId].networkStatus = NOT_REG_BUT_SEARCHING;
      break;
    case REGISTRATION_DENIED:
      strCpy(networkInfo[simId].networkOperator, "");
      networkInfo[simId].networkStatus = REGISTRATION_DENIED;
      break;
    case UNKNOWN_ERROR:
      strCpy(networkInfo[simId].networkOperator, "");
      networkInfo[simId].networkStatus = UNKNOWN_ERROR;
      break;
    case REGISTERED_ROAMING:
      networkInfo[simId].networkStatus = REGISTERED_ROAMING;
      break;
    default:
      break;
    }
  networkInfo[simId].networkStatus = networkStatus;


}

static void sendUpdatedNetworkStatusToManager(simNumber_t simId)
{
  phone_internal_network_status_t *updatedStatus = dataAlloc(sizeof(phone_internal_network_status_t));
  memCpy(updatedStatus, &(networkInfo[simId]), sizeof(phone_internal_network_status_t));
  checkCommSend(MODULE_PHONE, MODULE_MANAGEMENT, CMD_PHONE_NETWORK, CMD_PHONE_NETWORK_STATE, CMD_REQ_NO_REPLY, updatedStatus, NULL, NULL, NULL, 5);
  checkCommSend(MODULE_PHONE, MODULE_MANAGEMENT, CMD_SAR_UPDATE, 0, CMD_REQ_NO_REPLY, NULL, NULL, NULL, NULL, 10);
}

static void phoneTaskPrepare(void)
{
  osThreadSuspend(osThreadGetId());
  commInit(MODULE_PHONE, 128);

  osThreadSuspend(osThreadGetId());
  secondBoardStartupStage(); //bord init is here because of size of stack

  osThreadSuspend(osThreadGetId());

  osThreadSuspend(osThreadGetId());

#ifdef MODEM_FW_UPGRADE
  mprintf("\n\n\n\n\n\n\n\n\n\n\n");
  mprintf("MODEM IN FW UPGRADE MODE!!!\n");
  mprintf("After upgrade, change lpuart baudrate from 115200 to 9600,\n");
  mprintf("by AT+IPR=9600 command!\n");
  modemPowerOn();
  mprintf("\n\n\n\n\n\n\n\n\n\n\n");
#endif
  sendUpdatedNetworkStatusToManager(SIM1);
  sendUpdatedNetworkStatusToManager(SIM2);
  modemStartupInit();
  modemRingIndicatorIntConfig();
}

static bBuffer_t *head = NULL;
static bBuffer_t *tail = NULL;
static bBuffer_t *current = NULL;

/**
 * @brief      Receives data from modem by UART.
 *
 * @param[in]  data  The data from DMA buffer
 */
void receiveModemData(char data)
{
  //Alloc new buffer if necessary
  if (!current || current->size == current->maxSize)
    {
      current = bufAlloc(64);
      if (current)
        {
          //First buffer
          if (tail == NULL)
            head = tail = current;
          else
            {
              //Next buffer. Chain it.
              if (head != current)
                tail = bufChain(head, tail, current);
            }
        }
      else
        {
          //Allocation failed. Release all data
          if (head)
            bufFree(head);

          head = tail = current = NULL;
        }
    }
  if (current)
    {
      //Fill buffer with data
      if (data == 0 || data == '\r')
        return;
      if (data != '\n')
        {
          current->data[current->size] = data;
          current->size++;
          head->totalSize++;
        }
      if (data == ' ' && !isStringDifferentUtf8(dataFromBuffer(head), "> ", 2))
        {
          checkCommSendFromISR(MODULE_PHONE, MODULE_PHONE, CMD_INTERNAL_INT, CMD_PHONE_INT_RX_COMPLETE, CMD_REQ_NO_REPLY, dataFromBuffer(head), NULL, NULL, NULL);
          current = head = tail = NULL;
        }
      else if (data == '\n')
        {
          if (head->totalSize > 1)
            checkCommSendFromISR(MODULE_PHONE, MODULE_PHONE, CMD_INTERNAL_INT, CMD_PHONE_INT_RX_COMPLETE, CMD_REQ_NO_REPLY, dataFromBuffer(head), NULL, NULL, NULL);
          else
            bufFree(head);
          current = head = tail = NULL;
        }
    }
}

/**
 * @brief      Uart data transmission error handler.
 */
void handleDataTransmissionError(void)
{
  bufFree(head);
  current = head = tail = NULL;
}

#define NO_KEY 0
#define NO_MASK 0
#define NO_CONTEXT NULL

static void handleIncomingCall(cmd_phone_call_t *phoneCallBuffer)
{
  if (getPhoneMode() == SMS_ONLY_MODE)
    sendPhoneCallEndCommand();
  else
    sendPhoneCallWaitingCommand(NO_KEY, NO_MASK, phoneCallBuffer);
}

static void updateCallerNumber(cmd_phone_call_t *voiceCallBuffer)
{
  checkCommSend(MODULE_PHONE, MODULE_MANAGEMENT, CMD_PHONE_CALL, CMD_PHONE_CALL_NUMBER_UPDATE, CMD_REQ_NO_REPLY, voiceCallBuffer, NULL, NULL, NULL, 10);
}

static cmd_phone_pbook_t *pBook = NULL;

simNumber_t mainSIM = SIM1;
void setMainSIM(simNumber_t *simNumber)
{
  mainSIM = *simNumber;
}

simNumber_t getMainSIM(void)
{
  if (mainSIM != SIM1 && mainSIM != SIM2)
    mprintf("WRONG SIM NUMBER !\n");
  return mainSIM;
}

static bool modemWakeFromRing = false;
uint8_t modemCallsInProgress = 0;
uint8_t modemCallsAwaiting = 0;

bool connectionHasBeenEstablished = false;
static void parseModemCallRegistry(comm_t *buffer)
{
  if (modemCallsInProgress == NO_CALL_IN_PROGRESS)
    {
      if (getIntFromString(buffer, THIRD_INT_IN_STRING) == MODEM_CALL_ACTIVE)
        {
          connectionHasBeenEstablished = true;
          modemCallsInProgress = CALL_IN_PROGRESS;
        }

      if (getIntFromString(buffer, THIRD_INT_IN_STRING) == MODEM_CALL_ALERTING)
        {
          connectionHasBeenEstablished = true;
          modemCallsInProgress = CALL_IN_PROGRESS;
        }
    }
}

static uint8_t networkCoverageUpdateFlag = 0;
static void markNetworkStatusChange(simNumber_t simNumber)
{
  networkCoverageUpdateFlag = networkCoverageUpdateFlag | (1 << simNumber);
}

bool sim1Puk1Required = false;
bool sim2Puk1Required = false;
char *sim1TempPass = NULL;
char *sim2TempPass = NULL;
static void mobileStartUpReporting(simNumber_t simId, int reportingState)
{
  switch (reportingState)
    {
    case SIM_OK_NO_PIN:
      changeSimStatus(simId, SIM_OK_NO_PIN);
      markNetworkStatusChange(SIM1);
      break;
    case WAIT_FOR_PIN:
      checkPinOrPukLockState(simId);
      if (simId == SIM1)
        {
          if (sim1TempPass)
            {
              if (!sim1Puk1Required)
                pinEnter(SIM1, sim1TempPass);
            }
          else
            {
              changeSimStatus(SIM1, WAIT_FOR_PIN);
              commSend(MODULE_PHONE, MODULE_MANAGEMENT, CMD_PHONE_PASSWORD, CMD_PHONE_PIN_REQUIRED_SIM1, CMD_REQ_NO_REPLY, NULL, NULL, NULL, NULL);
            }
        }
      else if (simId == SIM2)
        {
          if (sim2TempPass)
            {
              if (!sim2Puk1Required)
                pinEnter(SIM2, sim2TempPass);
            }
          else
            {
              changeSimStatus(SIM2, WAIT_FOR_PIN);
              commSend(MODULE_PHONE, MODULE_MANAGEMENT, CMD_PHONE_PASSWORD, CMD_PHONE_PIN_REQUIRED_SIM2, CMD_REQ_NO_REPLY, NULL, NULL, NULL, NULL);
            }
        }
      break;
    case SIM_NOT_PRESENT:
      changeSimStatus(simId, SIM_NOT_PRESENT);
      changeNetworkStatus(simId, NETWORK_INITIAL_STATE);

      markNetworkStatusChange(simId);

      if (simId == SIM1)
        commSend(MODULE_PHONE, MODULE_MANAGEMENT, CMD_PHONE_PASSWORD, CMD_PHONE_PIN_NOT_REQUIRED_SIM1, CMD_REQ_NO_REPLY, NULL, NULL, NULL, NULL);
      else if (simId == SIM2)
        commSend(MODULE_PHONE, MODULE_MANAGEMENT, CMD_PHONE_PASSWORD, CMD_PHONE_PIN_NOT_REQUIRED_SIM2, CMD_REQ_NO_REPLY, NULL, NULL, NULL, NULL);
      break;
    case SIM_LOCK_STATE:
      changeSimStatus(simId, SIM_LOCK_STATE);
      break;
    case UNRECOVERABLE_ERROR:
      changeSimStatus(simId, UNRECOVERABLE_ERROR);
      break;
    case UNKNOWN_STATE:
      changeSimStatus(simId, UNKNOWN_STATE);
      break;
    default:
      break;
    }
}

#define UPDATE_RSSI_AND_OPERATOR(arg_val) \
(arg_val == SIM1) ? updateSignalQualityAndNetStatus(networkInfo[SIM1].simStatus, SIM_NOT_PRESENT) : \
updateSignalQualityAndNetStatus(SIM_NOT_PRESENT, networkInfo[SIM2].simStatus);

static void networkRegistrationInfo(simNumber_t simId, int registrationgState)
{
  switch (registrationgState)
    {
    case NOT_OK_NOT_SEARCHING:
      changeNetworkStatus(simId, NOT_OK_NOT_SEARCHING);
      break;
    case REGISTERED_HOME_NETWORK:
      UPDATE_RSSI_AND_OPERATOR(simId);
      changeNetworkStatus(simId, REGISTERED_HOME_NETWORK);
      getActualTimeAndDateFromNetwork();
      break;
    case NOT_REG_BUT_SEARCHING:
      changeNetworkStatus(simId, NOT_REG_BUT_SEARCHING);
      break;
    case REGISTRATION_DENIED:
      changeNetworkStatus(simId, REGISTRATION_DENIED);
      break;
    case UNKNOWN_ERROR:
      changeNetworkStatus(simId, UNKNOWN_ERROR);
      break;
    case REGISTERED_ROAMING:
      UPDATE_RSSI_AND_OPERATOR(simId);
      changeNetworkStatus(simId, REGISTERED_ROAMING);
      getActualTimeAndDateFromNetwork();
      break;
    default:
      break;
    }
  markNetworkStatusChange(simId);
}

bool modemIsDuringUssdReceiving = false;
multipartAnsw_t multipartModemAnswer = MULTIPART_MODEM_ANSWER_NULL;
cmd_phone_ussd_t *receivedUssdResponse = NULL;
simNumber_t simNumberReceivedSms;
uint8_t ussdRespType;
static void multipartModemAnswerHandler(multipartAnsw_t multipModemAnsw, comm_t *cmd)
{
  switch (multipModemAnsw)
    {
    case MULTIPART_MODEM_ANSWER_READ_SMS:
    {
      received_sms_t *newRegisterSms = readNewSms(cmd);

      if (modemReceivedWholeSms(newRegisterSms))
        {
          newRegisterSms->simNumber = simNumberReceivedSms;
          checkCommSend(MODULE_PHONE, MODULE_MANAGEMENT, CMD_PHONE_SMS, CMD_PHONE_SMS_TO_REG_IN, CMD_REQ_NO_REPLY, newRegisterSms, NULL, NULL, NULL, 10);
          checkCommSend(MODULE_PHONE, MODULE_MANAGEMENT, CMD_PHONE_SMS, CMD_PHONE_SMS_RECEIVED, CMD_REQ_NO_REPLY, NULL, NULL, NULL, NULL, 10); // send info only
          checkCommSend(MODULE_PHONE, MODULE_MANAGEMENT, CMD_NOTIFICATION_UPDATE, 0, CMD_REQ_NO_REPLY, NULL, NULL, NULL, NULL, 10);
        }
      multipartModemAnswer = MULTIPART_MODEM_ANSWER_NULL;
      break;
    }
    case MULTIPART_MODEM_ANSWER_USSD_RESP:
    {
      if (isItWholeUssdResponse(cmd))
        {
          strCat (receivedUssdResponse->message, "\n");
          strCat (receivedUssdResponse->message, cmd->messageBuffer);

          modemIsDuringUssdReceiving = false;
          multipartModemAnswer = MULTIPART_MODEM_ANSWER_NULL;
          ussdRespTypeHandler(ussdRespType, receivedUssdResponse);
          receivedUssdResponse = NULL;
        }
      else
        {
          modemIsDuringUssdReceiving = true;
          strCat (receivedUssdResponse->message, "\n");
          strCat (receivedUssdResponse->message, cmd->messageBuffer);
        }
      break;
    }
    default:
      phonePrintf("Unknown answer. Please, check it.\n\r");
      break;
    }
}
int currentlyHandledSMSId = -1;
static void modemOffTimeout(void);
bool smsResenderModemOnTimeoutExpanded = false;
#define ERROR_MESSAGE_LENGTH 20
bool modemDuringSmsSendingFlag = false;
static void sendingSmsError(comm_t *cmd)
{
  sms_send_error_t *errorInfo = dataAlloc(sizeof(sms_send_error_t));
  errorInfo->errorMessage = dataAlloc(sizeof(char) * ERROR_MESSAGE_LENGTH + 1);
  errorInfo->messageId = currentlyHandledSMSId;

  strncpyAscii(errorInfo->errorMessage, ((char *)cmd->messageBuffer), ERROR_MESSAGE_LENGTH);

  checkCommSend(MODULE_PHONE, MODULE_MANAGEMENT, CMD_PHONE_SMS, CMD_PHONE_SMS_SEND_ERROR, CMD_REQ_NO_REPLY, errorInfo, NULL, NULL, NULL, 5);
  modemDuringSmsSendingFlag = false;

  if (systemState.phoneMode == SMS_ONLY_MODE && smsResenderModemOnTimeoutExpanded == true && getResenderMessagesNumber() == 0)
    {
      modemOffTimeout();
    }
}

bool modemPromptFlag = false;
bool infoAboutVoiceCallSended = false;
bool modemIsDuringVoiceCall = false;
bool modemWasInDialInState = false;
bool modemIsTryingToMakePhoneCall = false;
bool voiceCallCallerNumberDetected = false;
bool modemSendDialOutRequest = false;

phonebook_operation_t phoneBookOperation = PBOOK_INITIAL;
static uint64_t smsPromptTimestamp = 0;
static uint64_t simAbsenceTimestamp = 0;
static uint64_t voiceCallStateUpdateTimestamp = 0;
static uint64_t dialOutVoiceCallStateTimestamp = 0;
static uint64_t voiceCallDialInStatekUpdateTimestamp = 0;
simNumber_t actualVoiceCallSim = SIM1;


static void ceer0Handler(int causeNum)
{
  switch (causeNum)
    {
    default:
      mprintf(C_RED "ceer0Handler UNKNOWN CEER ERROR! |%d|\n" C_NORMAL, causeNum);
      break;
    }

  if (lastCeerVoiceCallError)
    {
      dataFree(lastCeerVoiceCallError);
      lastCeerVoiceCallError = NULL;
    }
  lastCeerVoiceCallError = dataAlloc(sizeof(char) * 16);
  xsprintf(lastCeerVoiceCallError, "%d/%d\n", CEER_CAUSE_SELECT_0, causeNum);
}

static void ceer16Handler(int causeNum)
{
  switch (causeNum)
    {
    default:
      mprintf(C_RED "ceer16Handler UNKNOWN CEER ERROR! |%d|\n" C_NORMAL, causeNum);
      break;
    }

  if (lastCeerVoiceCallError)
    {
      dataFree(lastCeerVoiceCallError);
      lastCeerVoiceCallError = NULL;
    }
  lastCeerVoiceCallError = dataAlloc(sizeof(char) * 16);
  xsprintf(lastCeerVoiceCallError, "%d/%d\n", CEER_CAUSE_SELECT_16, causeNum);
}

static void ceer65Handler(int causeNum)
{
  switch (causeNum)
    {
    case CEER_65_NETWORK_REJECTION:
    case CEER_65_LOCAL_REJECTION:
    {
      cmd_phone_call_t *callData = dataAlloc(sizeof(cmd_phone_call_t));
      callData->simIndx = actualVoiceCallSim;

      checkCommSend(MODULE_PHONE, MODULE_MANAGEMENT, CMD_PHONE_CALL, CMD_PHONE_CALL_ERROR, CMD_REQ_NO_REPLY, callData, NULL, NULL, NULL, 5);
      connectionHasBeenEstablished = true;
      modemSendDialOutRequest = false;
      break;
    }
    default:
      mprintf(C_RED "ceer65Handler UNKNOWN CEER ERROR! |%d|\n" C_NORMAL, causeNum);
      break;
    }

  if (lastCeerVoiceCallError)
    {
      dataFree(lastCeerVoiceCallError);
      lastCeerVoiceCallError = NULL;
    }
  lastCeerVoiceCallError = dataAlloc(sizeof(char) * 16);
  xsprintf(lastCeerVoiceCallError, "%d/%d\n", CEER_CAUSE_SELECT_65, causeNum);
}

static void ceer66_67_69_73Handler(int causeNum)
{
  switch (causeNum)
    {
    case CEER_66_USER_BUSY:
    case CEER_66_CALL_REJECTED:
    case CEER_66_NORMAL_UNSPECIFIED:
    case CEER_66_NORMAL_CALL_CLEARING:
      connectionHasBeenEstablished = true;
      break;
    default:
      mprintf(C_RED "ceer66_67_69_73Handler UNKNOWN CEER ERROR! |%d|\n" C_NORMAL, causeNum);
      break;
    }

  if (modemSendDialOutRequest && connectionHasBeenEstablished == false)
    {
      cmd_phone_call_t *callData = dataAlloc(sizeof(cmd_phone_call_t));
      callData->simIndx = actualVoiceCallSim;

      checkCommSend(MODULE_PHONE, MODULE_MANAGEMENT, CMD_PHONE_CALL, CMD_PHONE_CALL_ERROR, CMD_REQ_NO_REPLY, callData, NULL, NULL, NULL, 5);
      connectionHasBeenEstablished = true;
      modemSendDialOutRequest = false;
    }
  else
    {
      checkCommSend(MODULE_PHONE, MODULE_MANAGEMENT, CMD_PHONE_CALL, CMD_PHONE_CALL_END, CMD_REQ_NO_REPLY, NULL, NULL, NULL, NULL, 10);
    }

  if (lastCeerVoiceCallError)
    {
      dataFree(lastCeerVoiceCallError);
      lastCeerVoiceCallError = NULL;
    }
  lastCeerVoiceCallError = dataAlloc(sizeof(char) * 16);
  xsprintf(lastCeerVoiceCallError, "%d/%d\n", CEER_CAUSE_SELECT_66, causeNum);
}

static void ceer71Handler(int causeNum)
{
  switch (causeNum)
    {
    default:
      mprintf(C_RED "ceer71Handler UNKNOWN CEER ERROR! |%d|\n" C_NORMAL, causeNum);
      break;
    }

  if (lastCeerVoiceCallError)
    {
      dataFree(lastCeerVoiceCallError);
      lastCeerVoiceCallError = NULL;
    }
  lastCeerVoiceCallError = dataAlloc(sizeof(char) * 16);
  xsprintf(lastCeerVoiceCallError, "%d/%d\n", CEER_CAUSE_SELECT_71, causeNum);
}


phoneBookContact_t *currentPhonebookContact = NULL;
bool contactToExportAlreadyExists = false;
static void modemAnswerHandler(comm_t *cmd)
{
  cmd_phone_call_t *phoneCallBuffer = NULL;
  int cmdNum = searchForCmdOnList(cmd->messageBuffer);
  switch (cmdNum)
    {
    case MODEM_OK:
      commandSucceded();
      break;
    case MODEM_ERROR:
      if (modemDuringSmsSendingFlag == true)
        {
          sendingSmsError(cmd);
        }
      else
        {
          commandError();
        }
      break;
    case MODEM_RING_SIM1:
    case MODEM_CRING_SIM1:
      actualVoiceCallSim = SIM1;
      modemWasInDialInState = true;
      connectionHasBeenEstablished = true;
      voiceCallDialInStatekUpdateTimestamp = NS2S(getUptime());
      updateSignalQualityAndNetStatus(networkInfo[SIM1].simStatus, SIM_NOT_PRESENT);
      if (infoAboutVoiceCallSended == false)
        {
          phoneCallBuffer = dataAlloc(sizeof(cmd_phone_call_t));
          phoneCallBuffer -> simIndx = SIM1;
          phoneCallBuffer -> phoneNo = getStringFromStringBuffer(cmd);
          handleIncomingCall(phoneCallBuffer);
          infoAboutVoiceCallSended = true;
        }
      break;
    case MODEM_INCOMING_CALL_CALLER_NUMBER_SIM1:
      actualVoiceCallSim = SIM1;
      modemWasInDialInState = true;
      voiceCallDialInStatekUpdateTimestamp = NS2S(getUptime());
      if (voiceCallCallerNumberDetected == false)
        {
          updateSignalQualityAndNetStatus(networkInfo[SIM1].simStatus, SIM_NOT_PRESENT);
          phoneCallBuffer = dataAlloc(sizeof(cmd_phone_call_t));
          phoneCallBuffer -> simIndx = SIM1;
          phoneCallBuffer -> phoneNo = getStringFromStringBuffer(cmd);
          updateCallerNumber(phoneCallBuffer);
          voiceCallCallerNumberDetected = true;
        }
      break;
    case MODEM_RING_SIM2:
    case MODEM_CRING_SIM2:
      connectionHasBeenEstablished = true;
      actualVoiceCallSim = SIM2;
      modemWasInDialInState = true;
      voiceCallDialInStatekUpdateTimestamp = NS2S(getUptime());
      updateSignalQualityAndNetStatus(SIM_NOT_PRESENT, networkInfo[SIM2].simStatus);
      if (infoAboutVoiceCallSended == false)
        {
          phoneCallBuffer = dataAlloc(sizeof(cmd_phone_call_t));
          phoneCallBuffer -> simIndx = SIM2;
          phoneCallBuffer -> phoneNo = getStringFromStringBuffer(cmd);
          handleIncomingCall(phoneCallBuffer);
          infoAboutVoiceCallSended = true;
        }
      break;
    case MODEM_INCOMING_CALL_CALLER_NUMBER_SIM2:
      actualVoiceCallSim = SIM2;
      modemWasInDialInState = true;

      voiceCallDialInStatekUpdateTimestamp = NS2S(getUptime());
      if (voiceCallCallerNumberDetected == false)
        {
          updateSignalQualityAndNetStatus(SIM_NOT_PRESENT, networkInfo[SIM2].simStatus);

          phoneCallBuffer = dataAlloc(sizeof(cmd_phone_call_t));
          phoneCallBuffer -> simIndx = SIM2;
          phoneCallBuffer -> phoneNo = getStringFromStringBuffer(cmd);
          updateCallerNumber(phoneCallBuffer);
          voiceCallCallerNumberDetected = true;
        }
      break;
    case MODEM_CALL_STARTED_SIM1:
      connectionHasBeenEstablished = true;
      mprintf("SIM0 Call: STARTED\n");
      actualVoiceCallSim = SIM1;
      modemIsTryingToMakePhoneCall = false;
      modemWasInDialInState = false;
      modemIsDuringVoiceCall = true;
      voiceCallStateUpdateTimestamp = NS2S(getUptime());
      phoneCallBuffer = dataAlloc(sizeof(cmd_phone_call_t));
      phoneCallBuffer -> simIndx = SIM1;
      phoneCallBuffer -> phoneNo = getStringFromStringBuffer(cmd);
      checkCommSend(MODULE_PHONE, MODULE_MANAGEMENT, CMD_PHONE_CALL, CMD_PHONE_CALL_START, CMD_REQ_NO_REPLY, phoneCallBuffer, NULL, NULL, NULL, 10);
      updateSignalQualityAndNetStatus(networkInfo[SIM1].simStatus, SIM_NOT_PRESENT);

      break;
    case MODEM_CALL_STARTED_SIM2:
    case MODEM_CALL_STARTED_FROM_CALLER_SIDE_SIM2:
      connectionHasBeenEstablished = true;
      mprintf("SIM1 Call: STARTED\n");
      actualVoiceCallSim = SIM2;
      //modemIsTryingToMakePhoneCall = false;
      modemWasInDialInState = false;
      modemIsDuringVoiceCall = true;
      voiceCallStateUpdateTimestamp = NS2S(getUptime());
      phoneCallBuffer = dataAlloc(sizeof(cmd_phone_call_t));
      phoneCallBuffer -> simIndx = SIM2;
      phoneCallBuffer -> phoneNo = getStringFromStringBuffer(cmd);
      updateSignalQualityAndNetStatus(SIM_NOT_PRESENT, networkInfo[SIM2].simStatus);

      checkCommSend(MODULE_PHONE, MODULE_MANAGEMENT, CMD_PHONE_CALL, CMD_PHONE_CALL_START, CMD_REQ_NO_REPLY, phoneCallBuffer, NULL, NULL, NULL, 10);
      break;
    case MODEM_CALL_ENDED_SIM1:
      handsFreeModeOff();
      mprintf("SIM0 Call: ENDED\n");
      actualVoiceCallSim = SIM1;
      modemWasInDialInState = true;
      modemIsDuringVoiceCall = false;
      updateSignalQualityAndNetStatus(networkInfo[SIM1].simStatus, SIM_NOT_PRESENT);

      listVoiceCallState(actualVoiceCallSim);
      break;
    case MODEM_CALL_ENDED_SIM2:
      handsFreeModeOff();
      mprintf("SIM1 Call: ENDED\n");
      actualVoiceCallSim = SIM2;
      modemWasInDialInState = true;
      modemIsDuringVoiceCall = false;
      updateSignalQualityAndNetStatus(SIM_NOT_PRESENT, networkInfo[SIM2].simStatus);
      listVoiceCallState(actualVoiceCallSim);
      break;
    case MODEM_CALL_ENDED_FROM_CALLER_SIDE_SIM1:
      handsFreeModeOff();
      mprintf("SIM0 Call: ENDED\n");
      actualVoiceCallSim = SIM1;
      modemWasInDialInState = true;
      modemIsDuringVoiceCall = false;
      updateSignalQualityAndNetStatus(networkInfo[SIM1].simStatus, SIM_NOT_PRESENT);

      listVoiceCallState(actualVoiceCallSim);
      break;
    case MODEM_CALL_ENDED_FROM_CALLER_SIDE_SIM2:
      handsFreeModeOff();
      mprintf("SIM1 Call: ENDED\n");
      actualVoiceCallSim = SIM2;
      modemWasInDialInState = true;
      modemIsDuringVoiceCall = false;
      updateSignalQualityAndNetStatus(SIM_NOT_PRESENT, networkInfo[SIM2].simStatus);
      listVoiceCallState(actualVoiceCallSim);
      break;
    case MODEM_CALLS_IN_PROGRESS_SIM1:
    case MODEM_CALLS_IN_PROGRESS_SIM2:
      modemWasInDialInState = false;
      modemIsDuringVoiceCall = true;
      voiceCallStateUpdateTimestamp = NS2S(getUptime());
      parseModemCallRegistry(cmd);
      break;
    case MODEM_NETWORK_REGISTATION_OK_SIM1:
    {
      networkInfo[SIM1].simIndx = SIM1;
      char *operatorName =  getStringFromStringBuffer(cmd);
      memCpy(networkInfo[SIM1].networkOperator, operatorName, sizeof(networkInfo[SIM1].networkOperator));

      if (operatorName)
        dataFree(operatorName);

      markNetworkStatusChange(SIM1);
    }
    break;
    case MODEM_NETWORK_REGISTATION_OK_SIM2:
    {
      networkInfo[SIM2].simIndx = SIM2;
      char *operatorName =  getStringFromStringBuffer(cmd);
      memCpy(networkInfo[SIM2].networkOperator, operatorName, sizeof(networkInfo[SIM2].networkOperator));
      if (operatorName)
        dataFree(operatorName);
      markNetworkStatusChange(SIM2);
      break;
    }
    case MODEM_SIGNAL_QUALITY_SIM1:
      networkInfo[SIM1].simIndx = SIM1;
      networkInfo[SIM1].rssi = getIntFromString(cmd, FIRST_INT_IN_STRING);
      networkInfo[SIM1].ber = getIntFromString(cmd, SECOND_INT_IN_STRING);
      markNetworkStatusChange(SIM1);
      break;
    case MODEM_SIGNAL_QUALITY_SIM2:
      networkInfo[SIM2].simIndx = SIM2;
      networkInfo[SIM2].rssi = getIntFromString(cmd, FIRST_INT_IN_STRING);
      networkInfo[SIM2].ber = getIntFromString(cmd, SECOND_INT_IN_STRING);;
      markNetworkStatusChange(SIM2);
      break;
    case MOBILE_START_UP_REPORTING_SIM1: //+KSUP:
    {
      int stat = getIntFromString(cmd, FIRST_INT_IN_STRING);
      mobileStartUpReporting(SIM1, stat);
      break;
    }
    case MOBILE_START_UP_REPORTING_SIM2: //+KSUPDS:
    {
      int stat = getIntFromString(cmd, FIRST_INT_IN_STRING);
      mobileStartUpReporting(SIM2, stat);
      break;
    }
    case MODEM_NETWORK_REGISTRATION_INFO_SIM1: //+CREG:
    {
      int stat = getIntFromString(cmd, FIRST_INT_IN_STRING);
      networkRegistrationInfo(SIM1, stat);
      break;
    }
    case MODEM_NETWORK_REGISTRATION_INFO_SIM2: //+CREGDS:
    {
      int stat = getIntFromString(cmd, FIRST_INT_IN_STRING);
      networkRegistrationInfo(SIM2, stat);
      break;
    }
    case MODEM_SET_SPEAKER_VOLUME:
    {
      volSpeaker_t *actualSpkVol = dataAlloc(sizeof(volSpeaker_t));
      actualSpkVol->type = SPK_VOL;
      actualSpkVol->speakerVolume.spkVol = getIntFromString(cmd, FIRST_INT_IN_STRING);
      checkCommSend(MODULE_PHONE, MODULE_MANAGEMENT, CMD_PHONE_CONFIG, CMD_PHONE_CONFIG_VOLUME_SPK, CMD_REQ_NO_REPLY, actualSpkVol, NULL, NULL, NULL, 10);
      break;
    }
    case MODEM_NEW_SMS_SIM1:
      simNumberReceivedSms = SIM1;
      getNewSmsInPduFromModem(cmd);
      break;
    case MODEM_NEW_SMS_SIM2:
      simNumberReceivedSms = SIM2;
      getNewSmsInPduFromModem(cmd);
      break;
    case MODEM_SMS_SEND_OK_SIM1:
      if (!isItLastSmsPart())
        {
          sendNextSmsMessage();
        }
      modemDuringSmsSendingFlag = false;
      break;
    case MODEM_SMS_SEND_OK_SIM2:
      if (!isItLastSmsPart())
        {
          sendNextSmsMessage();
        }
      modemDuringSmsSendingFlag = false;
      break;
    case MODEM_CMD_SMS_READ_SIM1:
      multipartModemAnswer = MULTIPART_MODEM_ANSWER_READ_SMS;
      break;
    case MODEM_CMD_SMS_READ_SIM2:
      multipartModemAnswer = MULTIPART_MODEM_ANSWER_READ_SMS;
      break;
    case MODEM_CMD_SMS_SMSC_SIM1:
      updateSmsCenterNumber(cmd, SIM1);
      break;
    case MODEM_CMD_SMS_SMSC_SIM2:
      updateSmsCenterNumber(cmd, SIM2);
      break;
    case MODEM_NETWORK_GET_NETWORK_TIME:
      setUpDateAndTimeFromNetwork(cmd);
      break;
    case MODEM_NETWORK_TIME_ZONE_CHANGE:
      getActualTimeAndDateFromNetwork();
      break;
    case MODEM_USSD_RESP_SIM1:
    {
      modemIsDuringUssdReceiving = true;
      ussdRespType = getIntFromString(cmd, FIRST_INT_IN_STRING);
      multipartAnsw_t parserOutput = ussdParser(ussdRespType, cmd, SIM1, &receivedUssdResponse);
      if (parserOutput)
        {
          multipartModemAnswer = parserOutput;
        }
      break;
    }
    case MODEM_USSD_RESP_SIM2:
    {
      modemIsDuringUssdReceiving = true;
      ussdRespType = getIntFromString(cmd, FIRST_INT_IN_STRING);
      multipartAnsw_t parserOutput = ussdParser(ussdRespType, cmd, SIM2, &receivedUssdResponse);
      if (parserOutput)
        multipartModemAnswer = parserOutput;
      break;
    }
    case MODEM_READ_PHONEBOOK_ENTRY_SIM1:
      pBook = dataAlloc(sizeof(cmd_phone_pbook_t));
      if (pBook)
        {
          parsePhoneBookEntry(cmd, pBook);
          commSend(MODULE_PHONE, MODULE_MANAGEMENT, CMD_PHONE_PBOOK, CMD_PHONE_PBOOK_ADD, CMD_REQ_NO_REPLY, pBook, NULL, NULL, NULL);
        }
      break;

    case MODEM_READ_PHONEBOOK_FIND_SIM1:
    {
      if (phoneBookOperation == PBOOK_SEARCH)
        {
          char *searchResult = phonebookSearchParser(cmd->messageBuffer, currentPhonebookContact->firstName);
          if (searchResult)
            {
              contactToExportAlreadyExists = true;
            }
        }
    }
    break;
    case MODEM_READ_PHONEBOOK_FIND_SIM2:
      break;


    case MODEM_READ_PHONEBOOK_ENTRY_SIM2:
      pBook = dataAlloc(sizeof(cmd_phone_pbook_t));
      if (pBook)
        {
          parsePhoneBookEntry(cmd, pBook);
          commSend(MODULE_PHONE, MODULE_MANAGEMENT, CMD_PHONE_PBOOK, CMD_PHONE_PBOOK_ADD, CMD_REQ_NO_REPLY, pBook, NULL, NULL, NULL);
        }
      break;
    case MODEM_FIELD_STRENGTH:
      break;
    case MODEM_ACTUAL_BAND_SIM1:
      networkInfo[SIM1].band = getIntFromString(cmd, FIRST_INT_IN_STRING);
      markNetworkStatusChange(SIM1);
      break;
    case MODEM_ACTUAL_BAND_SIM2:
      networkInfo[SIM2].band = getIntFromString(cmd, FIRST_INT_IN_STRING);
      markNetworkStatusChange(SIM2);
      break;
    case MODEM_POWER_CLASS:
#define START_COPYING_FROM_7 7 // '+CPWC: ' have to be skipped
      strncpyAscii(networkInfo[SIM1].powerClass, &(((char *)cmd->messageBuffer)[START_COPYING_FROM_7]), sizeof(networkInfo[SIM1].powerClass));
      break;
    case CMS_ERROR:
    {
      mprintf(C_RED "|%s|\n" C_NORMAL, cmd->messageBuffer);
      int cmsErrorNum = getIntFromString(cmd, FIRST_INT_IN_STRING);
      if (cmsErrorNum == CMS_SIM_PIN_REQUIRED)
        {
          checkPinOrPukLockState(SIM1);
          checkPinOrPukLockState(SIM2);
        }
      else if (cmsErrorNum == CMS_SIM_NOT_INSERTED)
        {
          changeSimStatus(SIM1, SIM_NOT_PRESENT);
        }
      else if (modemDuringSmsSendingFlag == true)
        {
          sendingSmsError(cmd);
        }
      commandError();
    }
    break;
    case MODEM_EXTENDED_CALL_ERROR_REPORT:
    {
      int cmeerCauseSelectNum = getIntFromString(cmd, FIRST_INT_IN_STRING);
      int cmeerCauseNum = getIntFromString(cmd, SECOND_INT_IN_STRING);

      mprintf(C_RED "|%s|\n" C_NORMAL, cmd->messageBuffer);

      switch (cmeerCauseSelectNum)
        {
        case CEER_CAUSE_SELECT_0:
          ceer0Handler(cmeerCauseNum);
          break;

        case CEER_CAUSE_SELECT_16:
          ceer16Handler(cmeerCauseNum);
          break;

        case CEER_CAUSE_SELECT_65:
          ceer65Handler(cmeerCauseNum);
          break;

        case CEER_CAUSE_SELECT_66:
        case CEER_CAUSE_SELECT_67:
        case CEER_CAUSE_SELECT_69:
        case CEER_CAUSE_SELECT_73:
          ceer66_67_69_73Handler(cmeerCauseNum);
          break;

        case CEER_CAUSE_SELECT_71:
          ceer71Handler(cmeerCauseNum);
          break;
        }

      break;
    }
    case CME_ERROR:
      mprintf(C_RED "|%s|\n" C_NORMAL, cmd->messageBuffer);
      commandError();
      break;
    case CME_IMEI_SIM1:
      strncpyAscii(networkInfo[SIM1].imei, cmd->messageBuffer, sizeof(networkInfo[SIM1].imei) - 1);
      break;
    case CME_IMEI_SIM2:
#define START_COPYING_FROM_10 10 // '+KDSIMEI: ' have to be skipped
      strncpyAscii(networkInfo[SIM2].imei, &(((char *)cmd->messageBuffer)[START_COPYING_FROM_10]), sizeof(networkInfo[SIM2].imei) - 1);
      break;
    case CME_BAND_CLASS:
      networkInfo[SIM1].bandClass = networkInfo[SIM2].bandClass = getIntFromString(cmd, FIRST_INT_IN_STRING);
      break;
    case MODEM_SERVICE_READY_SIM1:
      if (getModemInitializationStatus() == CONF_STAGE_MODEM_POWER_RESET)
        {
          updateSignalQualityAndNetStatus(networkInfo[SIM1].simStatus, networkInfo[SIM2].simStatus);
          setModemInitializationStatus(CONF_STAGE_MODEM_CONFIG_AFTER_POWER_UP);
        }
      else if (getModemInitializationStatus() == CONF_STAGE_INITIAL_RESET)
        {
          setModemInitializationStatus(CONF_STAGE_SEND_RESET_OLD_CONFIG);
        }

      checkCurrentModemBand(networkInfo[SIM1].simStatus, SIM_NOT_PRESENT);
      checkCurrentModemPowerClass();
      checkOperator(SIM1);
      break;
    case MODEM_READ_PHONEBOOK_CONFIG_SIM1:
    {
      checkPhoneBookMemoryUsage(SIM1);
      pBookInfo[SIM1].simIndx = SIM1;
      pBookInfo[SIM1].startIndx = getIntFromString(cmd, FIRST_INT_IN_STRING);
      pBookInfo[SIM1].endIndx = getIntFromString(cmd, SECOND_INT_IN_STRING);
      pBookInfo[SIM1].maxLengthOfNumber = getIntFromString(cmd, THIRD_INT_IN_STRING);
      pBookInfo[SIM1].maxLengthOfText = getIntFromString(cmd, SIXTH_INT_IN_STRING);
      break;
    }
    case MODEM_READ_PHONEBOOK_CONFIG_SIM2:
    {
      checkPhoneBookMemoryUsage(SIM2);
      pBookInfo[SIM2].simIndx = SIM2;
      pBookInfo[SIM2].startIndx = getIntFromString(cmd, FIRST_INT_IN_STRING);
      pBookInfo[SIM2].endIndx = getIntFromString(cmd, SECOND_INT_IN_STRING);
      pBookInfo[SIM2].maxLengthOfNumber = getIntFromString(cmd, THIRD_INT_IN_STRING);
      pBookInfo[SIM2].maxLengthOfText = getIntFromString(cmd, SIXTH_INT_IN_STRING);
      break;
    }
    case MODEM_PROMPT:
      modemPromptFlag = true;
      smsPromptTimestamp = NS2S(getUptime());
      if (modemCommunicationState == SEND_SMS_PROMT)
        {
          sendPduFrameToModem();
          modemDuringSmsSendingFlag = true;
          if (isItLastSmsPart())
            {
              modemCommunicationState = NORMAL;
              cleanSentSMS();
            }
          else
            {
              modemCommunicationState = SEND_SMS_PROMT;
            }
        }
      break;
    case MODEM_PHONEBOOK_MEMORY_USAGE_SIM1:

      phoneBookOperation = PBOOK_IMPORT;
      pBookInfo[SIM1].memUsed = getIntFromString(cmd, FIRST_INT_IN_STRING);
      sendUpdatedPhoneBookInfo(SIM1);

      break;
    case MODEM_PHONEBOOK_MEMORY_USAGE_SIM2:
      phoneBookOperation = PBOOK_IMPORT;
      pBookInfo[SIM2].memUsed = getIntFromString(cmd, FIRST_INT_IN_STRING);
      sendUpdatedPhoneBookInfo(SIM2);
      break;
    case MODEM_FW_VERSION:
#define START_COPYING_REV_FROM_7 7 // 'SIERRA ' have to be skipped
      strncpyAscii(systemState.modemFwRev, &(((char *)cmd->messageBuffer)[START_COPYING_REV_FROM_7]), sizeof(systemState.modemFwRev));
      break;
    case MODEM_PASSWORD_ATTEMPTS_LEFT_SIM1:
    {
      cmd_phone_password_attempts_t *passwordAttemptsLeft = dataAlloc(sizeof(cmd_phone_password_attempts_t));
      passwordAttemptsLeft->simIndx = SIM1;

      uint8_t tempPin1 = getIntFromString(cmd, FIRST_INT_IN_STRING);
      passwordAttemptsLeft->pin1AttemptsLeft = tempPin1;

      if (tempPin1 == 0)
        sim1Puk1Required = true;
      else
        sim1Puk1Required = false;

      uint8_t tempPuk1 = getIntFromString(cmd, SECOND_INT_IN_STRING);
      passwordAttemptsLeft->puk1AttemptsLeft = tempPuk1;

      uint8_t tempPin2 = getIntFromString(cmd, THIRD_INT_IN_STRING);
      passwordAttemptsLeft->pin2AttemptsLeft = tempPin2;

      uint8_t tempPuk2 = getIntFromString(cmd, FOURTH_INT_IN_STRING);
      passwordAttemptsLeft->puk2AttemptsLeft = tempPuk2;

      commSend(MODULE_PHONE, MODULE_MANAGEMENT, CMD_PHONE_PASSWORD, CMD_PHONE_PASS_ATTEMPTS_LEFT_SIM1, CMD_REQ_NO_REPLY, passwordAttemptsLeft, NULL, NULL, NULL);
      break;
    }
    case MODEM_PASSWORD_ATTEMPTS_LEFT_SIM2:
    {
      cmd_phone_password_attempts_t *passwordAttemptsLeft = dataAlloc(sizeof(cmd_phone_password_attempts_t));
      passwordAttemptsLeft->simIndx = SIM2;

      uint8_t tempPin1 = getIntFromString(cmd, FIRST_INT_IN_STRING);
      passwordAttemptsLeft->pin1AttemptsLeft = tempPin1;

      if (tempPin1 == 0)
        sim2Puk1Required = true;
      else
        sim2Puk1Required = false;

      uint8_t tempPuk1 = getIntFromString(cmd, SECOND_INT_IN_STRING);
      passwordAttemptsLeft->puk1AttemptsLeft = tempPuk1;

      uint8_t tempPin2 = getIntFromString(cmd, THIRD_INT_IN_STRING);
      passwordAttemptsLeft->pin2AttemptsLeft = tempPin2;

      uint8_t tempPuk2 = getIntFromString(cmd, FOURTH_INT_IN_STRING);
      passwordAttemptsLeft->puk2AttemptsLeft = tempPuk2;

      commSend(MODULE_PHONE, MODULE_MANAGEMENT, CMD_PHONE_PASSWORD, CMD_PHONE_PASS_ATTEMPTS_LEFT_SIM2, CMD_REQ_NO_REPLY, passwordAttemptsLeft, NULL, NULL, NULL);
      break;
    }
    case MODEM_PIN_REQUIRED_SIM1:
      if (sim1TempPass)
        {
          if (!sim1Puk1Required)
            pinEnter(SIM1, sim1TempPass);
        }
      else
        {
          changeSimStatus(SIM1, WAIT_FOR_PIN);
          commSend(MODULE_PHONE, MODULE_MANAGEMENT, CMD_PHONE_PASSWORD, CMD_PHONE_PIN_REQUIRED_SIM1, CMD_REQ_NO_REPLY, NULL, NULL, NULL, NULL);
        }
      break;
    case MODEM_PIN_REQUIRED_SIM2:
      if (sim2TempPass)
        {
          if (!sim2Puk1Required)
            pinEnter(SIM2, sim2TempPass);
        }
      else
        {
          changeSimStatus(SIM2, WAIT_FOR_PIN);
          commSend(MODULE_PHONE, MODULE_MANAGEMENT, CMD_PHONE_PASSWORD, CMD_PHONE_PIN_REQUIRED_SIM2, CMD_REQ_NO_REPLY, NULL, NULL, NULL, NULL);
        }
      break;
    case MODEM_PIN_NOT_REQUIRED_SIM1:
      commSend(MODULE_PHONE, MODULE_MANAGEMENT, CMD_PHONE_PASSWORD, CMD_PHONE_PIN_NOT_REQUIRED_SIM1, CMD_REQ_NO_REPLY, NULL, NULL, NULL, NULL);
      break;
    case MODEM_PIN_NOT_REQUIRED_SIM2:
      commSend(MODULE_PHONE, MODULE_MANAGEMENT, CMD_PHONE_PASSWORD, CMD_PHONE_PIN_NOT_REQUIRED_SIM2, CMD_REQ_NO_REPLY, NULL, NULL, NULL, NULL);
      break;
    case MODEM_PUK_REQUIRED_SIM1:
      commSend(MODULE_PHONE, MODULE_MANAGEMENT, CMD_PHONE_PASSWORD, CMD_PHONE_PUK_REQUIRED_SIM1, CMD_REQ_NO_REPLY, NULL, NULL, NULL, NULL);
      break;
    case MODEM_PUK_REQUIRED_SIM2:
      commSend(MODULE_PHONE, MODULE_MANAGEMENT, CMD_PHONE_PASSWORD, CMD_PHONE_PUK_REQUIRED_SIM2, CMD_REQ_NO_REPLY, NULL, NULL, NULL, NULL);
      break;
    case MODEM_NO_DIALTONE:
    case MODEM_DIAL_BUSY:
    case MODEM_NO_ANSWER:
    case MODEM_NO_CARRIER:
    case MODEM_BUSY:
    {
      modemIsDuringVoiceCall = false;
      getExtendedCallErrorReport(actualVoiceCallSim);
      modemWasInDialInState = true;
      break;
    }
    case MODEM_REPORTING_STATUS_SIM1:
    {
      int stat = getIntFromString(cmd, SECOND_INT_IN_STRING);
      mobileStartUpReporting(SIM1, stat);
      break;
    }
    case MODEM_REPORTING_STATUS_SIM2:
    {

      int stat = getIntFromString(cmd, SECOND_INT_IN_STRING);
      mobileStartUpReporting(SIM2, stat);
      break;
    }
    case MODEM_SERVICE_READY_SIM2:

      break;
    case MODEM_SERVICE_READY_GENERIC_SIM1:
    case MODEM_SERVICE_READY_GENERIC_SIM2:
    case MODEM_ECHO:
    case MODEM_SMS_MEMORY:
    case MODEM_ZONE_INDICATION_SIM1:
    case MODEM_ZONE_INDICATION_SIM2:
    case CME_PHONE_BOOK_IS_READY_SIM1:
    case CME_PHONE_BOOK_IS_READY_SIM2:
      break;
    default:
      for (int i = 0; i < sizeInBytes(cmd->messageBuffer); i++)
        {
          if (((char *)cmd->messageBuffer)[i] == '\r')
            ((char *)cmd->messageBuffer)[i] = '@';
        }
      if (modemDuringSmsSendingFlag != true)
        {
          mprintf("UNKN COMM [%s]\n\n", cmd->messageBuffer);

          for (int i = 0; i < sizeInBytes(cmd->messageBuffer); i++)
            {
              mprintf("0x%02x ", ((char *)cmd->messageBuffer)[i]);
            }
          mprintf("\n");
        }
      break;
    }
}

static int receivedModemCommandCallback(comm_t *cmd)
{
  modemKeepAlive();
  int internalCommunication = cmd->subCommand;

  switch (internalCommunication)
    {
    case CMD_PHONE_RING_INDICATOR:
      mprintf("\n[MODEM] Wake from ring %d\n\n", (uint32_t)NS2MS(getUptime()));
      modemWakeUp();
      modemWakeFromRing = true;
      break;
    case CMD_PHONE_INT_TX_COMPLETE:
      //// INFO: automatic dataFree(atCommandSendData)
      break;
    case CMD_PHONE_INT_RX_COMPLETE:
    {
      cmd->messageBuffer = dataFromBuffer(bufMakeFlat(bufferFromData(cmd->messageBuffer)));

      if (multipartModemAnswer != MULTIPART_MODEM_ANSWER_NULL)
        {
          multipartModemAnswerHandler(multipartModemAnswer, cmd);
        }
      else
        {
          modemAnswerHandler(cmd);
        }
      break;
    }
    default:
      break;
    }
  return 1;
}

static int modemVoiceCallCallback(comm_t *cmd)
{
  cmd_phone_call_t *voiceCallBuff = (cmd_phone_call_t *)(cmd->messageBuffer);
  scmd_phone_call_t voiceCallState = cmd->subCommand;

  char *callingNumber = NULL;
  simNumber_t simIndex = getMainSIM();
  if (voiceCallBuff)
    {
      callingNumber = voiceCallBuff -> phoneNo;
      simIndex = voiceCallBuff -> simIndx;
    }
  actualVoiceCallSim = simIndex;

  switch (voiceCallState)
    {
    case CMD_PHONE_CALL_NUMBER_UPDATE:
    case CMD_PHONE_CALL_INCOMING:
      //do nothing...
      break;
    case CMD_PHONE_CALL_DIAL:
    {
      modemCallsInProgress = NO_CALL_IN_PROGRESS;
      connectionHasBeenEstablished = false;
      modemSendDialOutRequest = true;

      if (callingNumber)
        {
          dialToNumber(simIndex, callingNumber);
          dataFree(callingNumber);
          callingNumber = NULL;
        }
      break;
    }
    case CMD_PHONE_CALL_START:
      voiceCallPickUp(simIndex);
      break;
    case CMD_PHONE_CALL_END:
      voiceCallHangUp(simIndex);
      mprintf("SIM%d Call: ENDED\n", simIndex);
      break;
    case CMD_PHONE_CALL_GENERATE_DIAL_TONE:
      sendDialToneData(systemState.simNumber, voiceCallBuff->keyTone);
      break;
    case CMD_PHONE_CALL_ERROR:
      break;
    case scmd_phone_call_num:
      break;
    }
  return 1;
}


#define SIM_PHONEBOOK_SEARCH "AT+CPBF"
#define DIAL_OUT "ATD"
#define CALL_END "ATH0"
#define SMS_OUT_SIM1 "AT+CMGS"
#define SMS_OUT_SIM2 "AT+CMGSDS"
#define GET_CALLS_STATE "AT+CLCC"
#define CONTACT_EXPORT_TO_SIM "AT+CPBW="
#define CHECK_CONTACT_WRITE_PROPERTIES "AT+CPBW=?"
#define CONTACT_IMPORT_TO_SIM "AT+CPBR="
#define CHECK_CONTACT_READ_PROPERTIES "AT+CPBR=?"
#define SIM_PIN_CHECK "AT+CPIN=\""
#define SIM_PIN_CHANGE_SIM1 "AT+CPWD="
#define SIM_PIN_CHANGE_SIM2 "AT+CPWDDS="
#define SIM_PIN_ON_OFF_SIM1 "AT+CLCK="
#define SIM_PIN_ON_OFF_SIM2 "AT+CLCKDS="

static int parseCommandCallback(char *lastCommand, char *pattern)
{
  if (strStr(lastCommand, pattern) != NULL)
    return 1;
  else
    return 0;
}

simNumber_t actualSendingPinOrPuk = SIM1;

/**
 * @brief      Error command callback handler for modem response to sent
 *             command.
 *
 * @param[in]  command  Modem answer for sent command
 */
void errorCommandCallbackHandler(char *command)
{
  lastCommandToWhichModemRepliedError();

  if (parseCommandCallback(command, SIM_PIN_CHECK))
    {
      if (actualSendingPinOrPuk == SIM1)
        checkCommSend(MODULE_PHONE, MODULE_MANAGEMENT, CMD_PHONE_PASSWORD, CMD_PHONE_PASSWORD_ERROR_SIM1, CMD_REQ_NO_REPLY, NULL, NULL, NULL, NULL, 5);
      else if (actualSendingPinOrPuk == SIM2)
        checkCommSend(MODULE_PHONE, MODULE_MANAGEMENT, CMD_PHONE_PASSWORD, CMD_PHONE_PASSWORD_ERROR_SIM2, CMD_REQ_NO_REPLY, NULL, NULL, NULL, NULL, 5);

      checkPinAndPukAttemptsLeft(actualSendingPinOrPuk);
    }
  if (parseCommandCallback(command, CONTACT_EXPORT_TO_SIM))
    {
      checkCommSend(MODULE_PHONE, MODULE_MANAGEMENT, CMD_PHONE_PBOOK, CMD_PHONE_PBOOK_EXPORT_ERROR, CMD_REQ_NO_REPLY, NULL, NULL, NULL, NULL, 5);
    }

  if (parseCommandCallback(command, CONTACT_IMPORT_TO_SIM))
    {
      checkCommSend(MODULE_PHONE, MODULE_MANAGEMENT, CMD_PHONE_PBOOK, CMD_PHONE_PBOOK_IMPORT_ERROR, CMD_REQ_NO_REPLY, NULL, NULL, NULL, NULL, 5);
    }

  if (parseCommandCallback(command, DIAL_OUT))
    {
      cmd_phone_call_t *callData = dataAlloc(sizeof(cmd_phone_call_t));
      callData->simIndx = actualVoiceCallSim;
      checkCommSend(MODULE_PHONE, MODULE_MANAGEMENT, CMD_PHONE_CALL, CMD_PHONE_CALL_ERROR, CMD_REQ_NO_REPLY, callData, NULL, NULL, NULL, 5);
      loudSpeakerModeOff();
    }

  if (parseCommandCallback(command, SIM_PIN_CHANGE_SIM1) || parseCommandCallback(command, SIM_PIN_CHANGE_SIM2))
    {
      checkCommSend(MODULE_PHONE, MODULE_MANAGEMENT, CMD_PHONE_PASSWORD, CMD_PHONE_PIN_CHANGE_ERROR, CMD_REQ_NO_REPLY, NULL, NULL, NULL, NULL, 5);
    }

  if (parseCommandCallback(command, SIM_PIN_ON_OFF_SIM1))
    {
      checkPinAndPukAttemptsLeft(SIM1);
    }

  if (parseCommandCallback(command, SIM_PIN_ON_OFF_SIM2))
    {
      checkPinAndPukAttemptsLeft(SIM2);
    }

  if (parseCommandCallback(command, SIM_PHONEBOOK_SEARCH))
    {
      if (contactToExportAlreadyExists == false)
        {
          phoneBookOperation = PBOOK_EXPORT;
          writeNewPhoneBookEntry(currentPhonebookContact, pBookInfo[SIM1].maxLengthOfText);
        }
    }
}

#define PASSWORD_TYPE_PIN 0
#define PASSWORD_TYPE_PUK 1
uint8_t lastEnteredPasswordType = PASSWORD_TYPE_PIN;

simNumber_t phoneBookActualImportSimIndex = SIM1;

/**
 * @brief      Success command callback handler for modem response to sent
 *             command.
 *
 * @param[in]  command  Modem answer for sent command
 */
void successCommandCallbackHandler(char *command)
{
  lastCommandToWhichModemRepliedSuccess();

  if (parseCommandCallback(command, DIAL_OUT))
    {
      mprintf("SIM%d call: ongoing\n", actualVoiceCallSim);
      loudSpeakerModeOff();
      switchToPhoneCallScreen();
      dialOutVoiceCallStateTimestamp = NS2S(getUptime());
      listVoiceCallState(actualVoiceCallSim);
    }

  if (parseCommandCallback(command, SMS_OUT_SIM1) || parseCommandCallback(command, SMS_OUT_SIM2))
    {
      uint32_t *id = dataAlloc(sizeof(uint32_t));
      *id = currentlyHandledSMSId;
      checkCommSend(MODULE_PHONE, MODULE_MANAGEMENT, CMD_PHONE_SMS, CMD_PHONE_SMS_SEND_SUCCESS, CMD_REQ_NO_REPLY, id, NULL, NULL, NULL, 10);

      if (systemState.phoneMode == SMS_ONLY_MODE && smsResenderModemOnTimeoutExpanded == true && getResenderMessagesNumber() == 0)
        {
          modemOffTimeout();
        }
    }

  if (parseCommandCallback(command, GET_CALLS_STATE))
    {
      if (modemIsDuringVoiceCall == false)
        {
          checkCommSend(MODULE_PHONE, MODULE_MANAGEMENT, CMD_PHONE_CALL, CMD_PHONE_CALL_END, CMD_REQ_NO_REPLY, NULL, NULL, NULL, NULL, 10);
          infoAboutVoiceCallSended = false;
          voiceCallCallerNumberDetected = false;
          modemSendDialOutRequest = false;
          voiceCallHangUp(actualVoiceCallSim);
        }
      else
        {
          modemIsDuringVoiceCall = true;
        }
      modemCallsInProgress = NO_CALL_IN_PROGRESS;
    }

  if (parseCommandCallback(command, CALL_END))
    {
      if (modemIsDuringVoiceCall == false)
        {
          checkCommSend(MODULE_PHONE, MODULE_MANAGEMENT, CMD_PHONE_CALL, CMD_PHONE_CALL_END, CMD_REQ_NO_REPLY, NULL, NULL, NULL, NULL, 10);
          infoAboutVoiceCallSended = false;
          voiceCallCallerNumberDetected = false;
          modemSendDialOutRequest = false;
        }
      else
        {
          modemIsDuringVoiceCall = true;
        }
      modemCallsInProgress = NO_CALL_IN_PROGRESS;
    }

  if (parseCommandCallback(command, SIM_PIN_CHECK))
    {
      if (actualSendingPinOrPuk == SIM1)
        {
          if (lastEnteredPasswordType == PASSWORD_TYPE_PIN)
            checkCommSend(MODULE_PHONE, MODULE_MANAGEMENT, CMD_PHONE_PASSWORD, CMD_PHONE_PIN_OK_SIM1, CMD_REQ_NO_REPLY, NULL, NULL, NULL, NULL, 5);
          else if (lastEnteredPasswordType == PASSWORD_TYPE_PUK)
            {
              checkCommSend(MODULE_PHONE, MODULE_MANAGEMENT, CMD_PHONE_PASSWORD, CMD_PHONE_PUK_OK_SIM1, CMD_REQ_NO_REPLY, NULL, NULL, NULL, NULL, 5);
              lastEnteredPasswordType = PASSWORD_TYPE_PIN;
            }

          networkInfo[SIM1].simStatus = SIM_OK_NO_PIN;
        }
      else if (actualSendingPinOrPuk == SIM2)
        {

          if (lastEnteredPasswordType == PASSWORD_TYPE_PIN)
            checkCommSend(MODULE_PHONE, MODULE_MANAGEMENT, CMD_PHONE_PASSWORD, CMD_PHONE_PIN_OK_SIM2, CMD_REQ_NO_REPLY, NULL, NULL, NULL, NULL, 5);
          else if (lastEnteredPasswordType == PASSWORD_TYPE_PUK)
            {
              checkCommSend(MODULE_PHONE, MODULE_MANAGEMENT, CMD_PHONE_PASSWORD, CMD_PHONE_PUK_OK_SIM2, CMD_REQ_NO_REPLY, NULL, NULL, NULL, NULL, 5);
              lastEnteredPasswordType = PASSWORD_TYPE_PIN;
            }

          networkInfo[SIM2].simStatus = SIM_OK_NO_PIN;
        }
    }

  if (parseCommandCallback(command, SIM_PIN_CHANGE_SIM1) || parseCommandCallback(command, SIM_PIN_CHANGE_SIM2))
    {
      checkCommSend(MODULE_PHONE, MODULE_MANAGEMENT, CMD_PHONE_PASSWORD, CMD_PHONE_PIN_CHANGE_OK, CMD_REQ_NO_REPLY, NULL, NULL, NULL, NULL, 5);
    }

  if (parseCommandCallback(command, SIM_PIN_ON_OFF_SIM1))
    {
      checkCommSend(MODULE_PHONE, MODULE_MANAGEMENT, CMD_PHONE_PASSWORD, CMD_PHONE_PIN_ON_OFF_OK, CMD_REQ_NO_REPLY, NULL, NULL, NULL, NULL, 5);
    }

  if (parseCommandCallback(command, SIM_PIN_ON_OFF_SIM2))
    {
      checkCommSend(MODULE_PHONE, MODULE_MANAGEMENT, CMD_PHONE_PASSWORD, CMD_PHONE_PIN_ON_OFF_OK, CMD_REQ_NO_REPLY, NULL, NULL, NULL, NULL, 5);
    }

  if (parseCommandCallback(command, CHECK_CONTACT_WRITE_PROPERTIES))
    return;
  else if (parseCommandCallback(command, CONTACT_EXPORT_TO_SIM))
    {
      if (phoneBookOperation == PBOOK_EXPORT)
        {
          checkCommSend(MODULE_PHONE, MODULE_MANAGEMENT, CMD_PHONE_PBOOK, CMD_PHONE_PBOOK_EXPORT_OK, CMD_REQ_NO_REPLY, NULL, NULL, NULL, NULL, 5);
        }
    }

  if (parseCommandCallback(command, CHECK_CONTACT_READ_PROPERTIES))
    return;
  else if (parseCommandCallback(command, CONTACT_IMPORT_TO_SIM))
    {
      phone_book_info_t *pBookData = dataAlloc(sizeof(phone_book_info_t));
      if (phoneBookActualImportSimIndex == SIM1)
        pBookData->simIndx = SIM1;
      else if (phoneBookActualImportSimIndex == SIM2)
        pBookData->simIndx = SIM2;
      checkCommSend(MODULE_PHONE, MODULE_MANAGEMENT, CMD_PHONE_PBOOK, CMD_PHONE_PBOOK_IMPORT_OK, CMD_REQ_NO_REPLY, pBookData, NULL, NULL, NULL, 5);
    }

  if (parseCommandCallback(command, SIM_PHONEBOOK_SEARCH))
    {
      if (contactToExportAlreadyExists == false)
        {
          writeNewPhoneBookEntry(currentPhonebookContact, pBookInfo[SIM1].maxLengthOfText);
          phoneBookOperation = PBOOK_EXPORT;
        }
      else
        {
          checkCommSend(MODULE_PHONE, MODULE_MANAGEMENT, CMD_PHONE_PBOOK, CMD_PHONE_PBOOK_EXPORT_SKIPPED, CMD_REQ_NO_REPLY, NULL, NULL, NULL, NULL, 5);
        }
    }
}

static int modemSmsCallback(comm_t *cmd)
{
  scmd_phone_sms_t smsState = cmd->subCommand;
  send_sms_t *smsBuffer = (send_sms_t *)(cmd -> messageBuffer);
  send_sms_t *sms = dataAlloc(sizeof(send_sms_t) + sizeInBytes(smsBuffer->messageToSend) + 1);
  sms->simNumber = smsBuffer->simNumber;
  strCpy(sms->recipientNumber, smsBuffer->recipientNumber);
  sms->alphabet = smsBuffer->alphabet;
  strCpy(sms->messageToSend, smsBuffer->messageToSend);
  sms->messageId = smsBuffer->messageId;

  switch (smsState)
    {
    case CMD_PHONE_SMS_SEND:
      currentlyHandledSMSId = sms->messageId;
      sendSms(sms);

      //asm("bkpt #1");

      modemCommunicationState = SEND_SMS_PROMT;
      break;
    case CMD_PHONE_SMS_RESEND:
      getResenderMessagesNumber();
      currentlyHandledSMSId = sms->messageId;
      sendSms(sms);
      modemCommunicationState = SEND_SMS_PROMT;
      break;
    default:
      break;
    }
  return 1;
}

static void deleteContactsFromSim(simNumber_t simNumber, uint8_t contactsToDelete)
{
  int i;
  for (i = 1; i < contactsToDelete + 1; i++)
    {
      deletePhoneBookEntry(simNumber, i);
    }
}

#define CONTACTS_TO_DELETE_SIM1_NUM MAX_QUEUED_COMMAND
#define CONTACTS_TO_DELETE_SIM2_NUM (int)MAX_QUEUED_COMMAND / 2
static int modemPhoneTaskPhoneBookCallback(comm_t *cmd)
{
  scmd_phone_pbook_t phoneBookState = cmd->subCommand;
  switch (phoneBookState)
    {
    case CMD_PHONE_PBOOK_EXPORT_SKIPPED:
      break;
    case CMD_PHONE_PBOOK_INFO:
    {
      phone_book_info_t *simNumber = (phone_book_info_t *)(cmd -> messageBuffer);
      checkPhoneBookParameters(simNumber->simIndx);
    }
    break;
    case CMD_PHONE_PBOOK_ADD:
    {
      phoneBookOperation = PBOOK_SEARCH;
      currentPhonebookContact = dataAlloc(sizeof(phoneBookContact_t));

      phoneBookContact_t *phonebookContact = (phoneBookContact_t *)(cmd -> messageBuffer);
      if (phonebookContact->simNumber == SIM1)
        {
          memCpy(currentPhonebookContact, phonebookContact, sizeof(phoneBookContact_t));
          contactToExportAlreadyExists = false;
          findPhoneBookEntryByContactName(SIM1, phonebookContact->firstName);
        }
      else if (phonebookContact->simNumber == SIM2)
        {
          findPhoneBookEntryByContactName(SIM1, phonebookContact->firstName);
        }
    }
    break;
    case CMD_PHONE_PBOOK_DELETE:
    {
      cmd_phone_pbook_t *simNum = (cmd_phone_pbook_t *)(cmd -> messageBuffer);
      phoneBookOperation = PBOOK_DELETE;
      if (networkInfo[SIM1].simStatus == SIM_OK_NO_PIN)
        {
          deleteContactsFromSim(simNum->simIndx, CONTACTS_TO_DELETE_SIM1_NUM);
        }
      else if (networkInfo[SIM2].simStatus == SIM_OK_NO_PIN)
        {
          deleteContactsFromSim(simNum->simIndx, CONTACTS_TO_DELETE_SIM2_NUM);
        }
      break;
    }
    case CMD_PHONE_PBOOK_GET:
    {
      phone_book_info_t *pBookImport = (phone_book_info_t *)(cmd -> messageBuffer);
      readPhoneBookEntryFromSim(pBookImport->simIndx, pBookImport->actualImportIndx);
      break;
    }
    case CMD_PHONE_PBOOK_FIND_NEXT_NUMBER:
    case CMD_PHONE_PBOOK_FIND_NEXT_NAME:
    case CMD_PHONE_PBOOK_FIND_ALL_NUMBER:
    case CMD_PHONE_PBOOK_FIND_ALL_NAME:
    case CMD_PHONE_PBOOK_EXPORT_OK:
    case CMD_PHONE_PBOOK_EXPORT_ERROR:
    case CMD_PHONE_PBOOK_IMPORT_OK:
    case CMD_PHONE_PBOOK_IMPORT_ERROR:
      break;
    case scmd_phone_pbook_num:
      break;
    }
  return 1;
}

static int modemUssdCallback(comm_t *cmd)
{
  scmd_phone_ussd_t ussdState = cmd->subCommand;
  cmd_phone_call_t *phoneCallBuffer = (cmd_phone_call_t *)(cmd -> messageBuffer);

  switch (ussdState)
    {
    case CMD_PHONE_USSD_SEND:
    {
      char *callingNumber = phoneCallBuffer -> phoneNo;
      simNumber_t simIndex = phoneCallBuffer -> simIndx;
      sendUssdCode(simIndex, callingNumber);
      if (callingNumber)
        dataFree(callingNumber);
    }
    break;
    case CMD_PHONE_USSD_ABORT:
      cancelUssdSession();
      break;
    case CMD_PHONE_USSD_RECEIVED_ANSW:
      break;
    case CMD_PHONE_USSD_REQ_RESP:
      break;
    case CMD_PHONE_USSD_TERMINATED_BY_NETWORK:
      break;
    case CMD_PHONE_USSD_OTHER_CLIENT_RESP:
      break;
    case CMD_PHONE_USSD_NOT_SUPPORTED:
      break;
    case CMD_PHONE_USSD_TIMEOUT:
      break;
    case scmd_phone_ussd_num:
      break;
    default:
      break;
    }
  return 1;
}

static int phoneModemConfigurationCallback(comm_t *cmd)
{
  volSpeaker_t *volumeParameters = (volSpeaker_t *)cmd->messageBuffer;

  scmd_phone_config_t configState = cmd->subCommand;

  switch (configState)
    {
    case CMD_PHONE_CONFIG_SIM_ERROR:
      break;
    case CMD_PHONE_CONFIG_CHANGE_SIM:
    {
      simNumber_t *simNum = (simNumber_t *)cmd->messageBuffer;
      setMainSIM(simNum);
    }
    break;
    case CMD_PHONE_CONFIG_VOLUME_RING:
      break;
    case CMD_PHONE_CONFIG_VOLUME_MIC:
    {
      if (volumeParameters->micMuteState == 1)
        {
          microphoneMute();
        }
      else
        {
          microphoneUnmute();
        }
      break;
    }
    case CMD_PHONE_CONFIG_VOLUME_SPK:
    {
      if (volumeParameters->type == SPK_VOL)
        {
          setSpeakerVolume(volumeParameters->speakerVolume.spkVol);
        }
      else if (volumeParameters->type == LOUD_SPK)
        {
          if (volumeParameters->speakerphoneEnable.loudSpkState == SPEAKERPHONE_GAIN_HIGH)
            {
              loudSpeakerModeOn();
            }
          else if (volumeParameters->speakerphoneEnable.loudSpkState == SPEAKERPHONE_GAIN_MID)
            {
              loudSpeakerModeOff();
            }
        }
      break;
    }
    case scmd_phone_config_num:
      break;
    }
  return 1;
}

static int phoneTaskPinCallback(comm_t *cmd)
{
  if (cmd->subCommand == CMD_PHONE_PIN_ENTER_SIM1 || cmd->subCommand == CMD_PHONE_PUK_ENTER_SIM1)
    {
      if (sim1TempPass == NULL)
        sim1TempPass = dataAlloc(sizeof(char) * SIM_PIN_PUK_MAXLENGTH + 1);
    }
  else if (cmd->subCommand == CMD_PHONE_PIN_ENTER_SIM2 || cmd->subCommand == CMD_PHONE_PUK_ENTER_SIM2)
    {
      if (sim2TempPass == NULL)
        sim2TempPass = dataAlloc(sizeof(char) * SIM_PIN_PUK_MAXLENGTH + 1);
    }

  switch (cmd->subCommand)
    {
    case CMD_PHONE_PIN_ENTER_SIM1:
    {
      lastEnteredPasswordType = PASSWORD_TYPE_PIN;
      if (sim1TempPass)
        memCpy(sim1TempPass, (char *)cmd->messageBuffer, SIM_PIN_PUK_MAXLENGTH);

      pinEnter(SIM1, sim1TempPass);
      actualSendingPinOrPuk = SIM1;
    }
    break;
    case CMD_PHONE_PIN_ENTER_SIM2:
    {
      lastEnteredPasswordType = PASSWORD_TYPE_PIN;
      if (sim2TempPass)
        memCpy(sim2TempPass, (char *)cmd->messageBuffer, SIM_PIN_PUK_MAXLENGTH);

      pinEnter(SIM2, sim2TempPass);
      actualSendingPinOrPuk = SIM2;
    }
    break;
    case CMD_PHONE_PUK_ENTER_SIM1:
    {
      lastEnteredPasswordType = PASSWORD_TYPE_PUK;
      if (sim1TempPass)
        memCpy(sim1TempPass, (char *)cmd->messageBuffer, SIM_PIN_PUK_MAXLENGTH);

      pukEnter(SIM1, sim1TempPass);
      actualSendingPinOrPuk = SIM1;

      if (sim1TempPass)
        memCpy(sim1TempPass, "1234\0", 5);
    }
    break;
    case CMD_PHONE_PUK_ENTER_SIM2:
    {
      lastEnteredPasswordType = PASSWORD_TYPE_PUK;
      if (sim2TempPass)
        memCpy(sim2TempPass, (char *)cmd->messageBuffer, SIM_PIN_PUK_MAXLENGTH);
      pukEnter(SIM2, sim2TempPass);
      actualSendingPinOrPuk = SIM2;
    }
    break;
    case CMD_PHONE_PASS_ATTEMPTS_LEFT_SIM1:
    {
      checkPinAndPukAttemptsLeft(SIM1);
    }
    break;
    case CMD_PHONE_PASS_ATTEMPTS_LEFT_SIM2:
    {
      checkPinAndPukAttemptsLeft(SIM2);
    }
    break;
    case CMD_PHONE_PIN_CHANGE_SIM1:
    {
      cmd_phone_pin_change_t *pinData = dataAlloc(sizeof(cmd_phone_pin_change_t));
      if (pinData == NULL)
        return 1;

      strCpy(pinData->oldPin, ((cmd_phone_pin_change_t*)cmd->messageBuffer)->oldPin);
      strCpy(pinData->newPin, ((cmd_phone_pin_change_t*)cmd->messageBuffer)->newPin);
      pinChange(SIM1, pinData);
      dataFree(pinData);
    }
    break;
    case CMD_PHONE_PIN_CHANGE_SIM2:
    {
      cmd_phone_pin_change_t *pinData = dataAlloc(sizeof(cmd_phone_pin_change_t));
      if (pinData == NULL)
        return 1;

      strCpy(pinData->oldPin, ((cmd_phone_pin_change_t*)cmd->messageBuffer)->oldPin);
      strCpy(pinData->newPin, ((cmd_phone_pin_change_t*)cmd->messageBuffer)->newPin);
      pinChange(SIM2, pinData);
      dataFree(pinData);
    }
    break;
    case CMD_PHONE_PIN_OFF_SIM1:
    {
      pinDisable(SIM1, (char*)cmd->messageBuffer);
    }
    break;
    case CMD_PHONE_PIN_ON_SIM1:
    {
      pinEnable(SIM1, (char*)cmd->messageBuffer);
    }
    break;
    case CMD_PHONE_PIN_OFF_SIM2:
    {
      pinDisable(SIM2, (char*)cmd->messageBuffer);
    }
    break;
    case CMD_PHONE_PIN_ON_SIM2:
    {
      pinEnable(SIM2, (char*)cmd->messageBuffer);
    }
    break;
    default:
      break;
    }

  return 1;
}

static void disconnectedModemNetworkUpdate(void)  // to show proper parameters on GUI
{
  changeNetworkStatus(SIM1, NETWORK_INITIAL_STATE);
  changeNetworkStatus(SIM2, NETWORK_INITIAL_STATE);
  markNetworkStatusChange(SIM1);
  markNetworkStatusChange(SIM2);
}

static void modemOnTimeout(void);

// modem on/off duration in milliseconds
#define MODEM_ON_DURATION   60000*2
#define MODEM_OFF_DURATION  60000*15 // Default
static void modemOnTimeout(void)
{
  if (systemState.phoneMode == SMS_ONLY_MODE)
    {
      modemStartupInit();
      checkCommSend(MODULE_PHONE, MODULE_MANAGEMENT, CMD_PHONE_NETWORK, CMD_PHONE_NETWORK_SMS_ONLY_RECV_START, CMD_REQ_NO_REPLY, NULL, NULL, NULL, NULL, 10);
      setManagerTaskTimeout(modemOffTimeout, MODEM_ON_DURATION, MODEM_ON_TIMEOUT);
    }
  disableManagerTaskTimeout(MODEM_OFF_TIMEOUT);
}
static void configModemToNotNormalMode(void)
{
  cleanSentCommandsList();
  modemPowerOff();
  disconnectedModemNetworkUpdate();
}

static void modemOffTimeout(void)
{
  if (systemState.phoneMode == SMS_ONLY_MODE)
    {
      if (getResenderMessagesNumber() == 0)
        {
          configModemToNotNormalMode();
          checkCommSend(MODULE_PHONE, MODULE_MANAGEMENT, CMD_PHONE_NETWORK, CMD_PHONE_NETWORK_SMS_ONLY_RECV_FINISH, CMD_REQ_NO_REPLY, NULL, NULL, NULL, NULL, 10);
          setManagerTaskTimeout(modemOnTimeout, MODEM_OFF_DURATION, MODEM_OFF_TIMEOUT);
          smsResenderModemOnTimeoutExpanded = false;
        }
      else
        {
          smsResenderModemOnTimeoutExpanded = true;
        }
    }
  disableManagerTaskTimeout(MODEM_ON_TIMEOUT);
}

static void smsOnlyModeNetworkConfig(phoneMode_t mode)
{
  if (mode == SMS_ONLY_MODE)
    {
      setModemInitializationStatus(CONF_STAGE_FINISHED_OK);
      setOnModeSMSOnly();
      configModemToNotNormalMode();
      setManagerTaskTimeout(modemOnTimeout, MODEM_OFF_DURATION, MODEM_OFF_TIMEOUT);
    }
  if (mode == NORMAL_MODE)
    {
      setOffModeSMSOnly();
    }
}

static int modemNetworkConfigurationCallback(comm_t *cmd)
{
  scmd_phone_network_t networkState = cmd->subCommand;

  switch (networkState)
    {
    case CMD_PHONE_NETWORK_STATE:
      markNetworkStatusChange(SIM1);
      markNetworkStatusChange(SIM2);
      break;
    case CMD_PHONE_NETWORK_CONNECT:
      modemStartupInit();
      break;
    case CMD_PHONE_NETWORK_DISCONNECT:
      configModemToNotNormalMode();
      break;
    case CMD_PHONE_NETWORK_SMS_ONLY:
      smsOnlyModeNetworkConfig(systemState.phoneMode);
      break;
    case CMD_PHONE_NETWORK_SMS_ONLY_RECV_START:
      disableManagerTaskTimeout(MODEM_ON_TIMEOUT);
      disableManagerTaskTimeout(MODEM_OFF_TIMEOUT);
      modemOnTimeout();
      break;
    default:
      break;
    }
  return 1;
}

static int phoneTaskDebug(comm_t *cmd)
{
  printCurrentCommandsList();
  return 1;
}

static void printNetworkStats(simNumber_t simIdx)
{
  phone_internal_network_status_t *net = &networkInfo[simIdx]; //to shorten mprintf;
  mprintf("+%4d+%3d+%12s+%3d+%3d+%16s+%9d+\n", net->rssi, net->ber, net->networkOperator, net->simStatus, net->networkStatus, net->imei, net->bandClass);
}

void phoneNetworkStats(void)
{
  mprintf("+rssi+ber+--operator--+sim+net+-------imei-----+bandClass+\n");
  printNetworkStats(SIM1);
  printNetworkStats(SIM2);
  mprintf("+----+---+------------+---+---+----------------+---------+\n");
  mprintf(getModemPowerState() ? "modemPowerState = MODEM_OFF\n" : "modemPowerState = MODEM_ON\n");
  mprintf("last Command = |%s\n", getLastCommandToWhichModemReplied());
  mprintf("dBm sim1 = %d dBm\n", (int)(-113 + (2 * networkInfo[SIM1].rssi)));
  mprintf("dBm sim2 = %d dBm\n", (int)(-113 + (2 * networkInfo[SIM2].rssi)));
  mprintf("sim1 powerClass = %s\n", networkInfo[SIM1].powerClass);
  mprintf("sim1 band = %d\n", networkInfo[SIM1].band);

  mprintf("modemCmd Result = %d/%d\n", getCmdStats().modemSuccessCommandsCounter, getCmdStats().modemErrorCommandsCounter);
  mprintf("modemCmd Sent = %d/%d\n", getCmdStats().modemSentCommandsCounter, getCmdStats().modemSentErrorCommandsCounter);
}

#define NETWORK_UPDATE_ACTIVE_TIMEOUT 30
#define NETWORK_UPDATE_LOCKED_TIMEOUT (5*60)
static uint16_t getNetworkUpdatePriodForState(void)
{
  uint16_t period = NETWORK_UPDATE_ACTIVE_TIMEOUT;
  if (getCurrentState() == MANAGER_STATE_LOCK)
    {
      period = NETWORK_UPDATE_LOCKED_TIMEOUT;
    }
  return period;
}


#define MODEM_INACTIVE_PHONE_TASK_TIMEOUT  100
uint64_t phoneTaskTimeout = MODEM_INACTIVE_PHONE_TASK_TIMEOUT;

/**
 * @brief      Sets the phone task timeout.
 *
 * @param[in]  timeout  The timeout value
 */
void setPhoneTaskTimeout(uint32_t timeout)
{
  phoneTaskTimeout = timeout;
}

#define MODEM_SMS_PROMPT_TIMEOUT 5 // s
static void smsPromptCheck(void)
{
  if (modemPromptFlag == true)
    {
      if ((NS2S(getUptime()) - smsPromptTimestamp) >= MODEM_SMS_PROMPT_TIMEOUT)
        {
          sendCtrlZToModem();
          modemPromptFlag = false;
        }
    }
}

#define SIM_ABSENCE_CHECK_TIMEOUT 3 // s
static void simCardsAbsenceCheck(void)
{
  if ((NS2S(getUptime()) - simAbsenceTimestamp) >= SIM_ABSENCE_CHECK_TIMEOUT)
    {
      if (sendInfoToManagerAboutSim1Problem == true)
        {
          phone_internal_network_status_t *simErrorInfo = dataAlloc(sizeof(phone_internal_network_status_t));
          simErrorInfo->simIndx = SIM1;
          simErrorInfo->simStatus = networkInfo[SIM1].simStatus;
          simErrorInfo->networkStatus = networkInfo[SIM1].networkStatus;
          checkCommSend(MODULE_PHONE, MODULE_MANAGEMENT, CMD_PHONE_CONFIG, CMD_PHONE_CONFIG_SIM_ERROR, CMD_REQ_NO_REPLY, simErrorInfo, NULL, NULL, NULL, 5);
          sendInfoToManagerAboutSim1Problem = false;
        }

      if (sendInfoToManagerAboutSim2Problem == true)
        {
          phone_internal_network_status_t *simErrorInfo = dataAlloc(sizeof(phone_internal_network_status_t));
          simErrorInfo->simIndx = SIM2;
          simErrorInfo->simStatus = networkInfo[SIM2].simStatus;
          simErrorInfo->networkStatus = networkInfo[SIM2].networkStatus;
          checkCommSend(MODULE_PHONE, MODULE_MANAGEMENT, CMD_PHONE_CONFIG, CMD_PHONE_CONFIG_SIM_ERROR, CMD_REQ_NO_REPLY, simErrorInfo, NULL, NULL, NULL, 5);
          sendInfoToManagerAboutSim2Problem = false;
        }
      simAbsenceTimestamp = NS2S(getUptime());
    }
}

#define SEND_NETWORK_STATE_TO_MANAGER_TIMEOUT 3
#define SIM1_MASK (1<<SIM1)
#define SIM2_MASK (1<<SIM2)
static void checkNetworkStateAndCoverage(void)
{
  static uint64_t networkStateChangedTimestamp = 0;
  static uint8_t sentFlag = 0;
  static uint8_t changeFlag = 0;

  if (networkCoverageUpdateFlag)
    {
      networkStateChangedTimestamp = NS2S(getUptime());
      changeFlag = networkCoverageUpdateFlag || changeFlag;
      networkCoverageUpdateFlag = 0;
      sentFlag = 0;
    }

  if ((NS2S(getUptime()) - networkStateChangedTimestamp) >= SEND_NETWORK_STATE_TO_MANAGER_TIMEOUT && sentFlag == 0)
    {
      if (changeFlag & SIM1_MASK)
        sendUpdatedNetworkStatusToManager(SIM1);

      if (changeFlag & SIM2_MASK)
        sendUpdatedNetworkStatusToManager(SIM2);

      changeFlag = 0;
      sentFlag = 1;
    }

  return;
}

#define MODEM_VOICE_CALL_IN_TIMEOUT 5 // s
#define MODEM_VOICE_CALL_OUT_TIMEOUT 5 // s
#define MODEM_DIAL_IN_CHECK_STATE_TIMEOUT 10 // s
static void updateVoiceCallStatus(void)
{
  if (modemSendDialOutRequest == true)
    {
      if ((NS2S(getUptime()) - dialOutVoiceCallStateTimestamp) >= MODEM_VOICE_CALL_OUT_TIMEOUT)
        {
          dialOutVoiceCallStateTimestamp = NS2S(getUptime());
          listVoiceCallState(actualVoiceCallSim);
          updateSignalQualityAndNetworkStat(actualVoiceCallSim);
        }
    }
  else if (modemIsDuringVoiceCall == true)
    {
      if ((NS2S(getUptime()) - voiceCallStateUpdateTimestamp) >= MODEM_VOICE_CALL_IN_TIMEOUT)
        {
          voiceCallStateUpdateTimestamp = NS2S(getUptime());
          listVoiceCallState(actualVoiceCallSim);
          updateSignalQualityAndNetworkStat(actualVoiceCallSim);
        }
      modemIsDuringVoiceCall = true;
    }
  else if (modemWasInDialInState == true)
    {
      if ((NS2S(getUptime()) - voiceCallDialInStatekUpdateTimestamp) >= MODEM_DIAL_IN_CHECK_STATE_TIMEOUT)
        {
          voiceCallDialInStatekUpdateTimestamp = NS2S(getUptime());
          listVoiceCallState(actualVoiceCallSim);
          loudSpeakerModeOff();
          updateSignalQualityAndNetworkStat(actualVoiceCallSim);
          modemWasInDialInState = false;
        }
    }
}

static void ifThereIsNoSimTurnModemOff(void)
{
  if (getModemPowerState() == MODEM_ON)
    {
      if (networkInfo[SIM1].simStatus != SIM_INITIAL_STATE && networkInfo[SIM2].simStatus != SIM_INITIAL_STATE)
        {
          if ((networkInfo[SIM1].simStatus != SIM_OK_NO_PIN && networkInfo[SIM1].simStatus != WAIT_FOR_PIN) && \
              (networkInfo[SIM2].simStatus != SIM_OK_NO_PIN && networkInfo[SIM2].simStatus != WAIT_FOR_PIN))
            {
              // there are nos sim cards in phone, so we can turn modem off
              setModemInitializationStatus(CONF_STAGE_FINISHED_OK);
              modemPowerOff();
            }
        }
    }
}

#define MODEM_WAKE_PERIOD_DEFAULT 100 // ms
#define MODEM_WAKE_PERIOD_RING 5000 // ms
uint16_t modemKeepWakeTime = MODEM_WAKE_PERIOD_DEFAULT;
bool pinCheckedFlag = true;

/**
 * @brief      PhoneTask background function.
 *
 * @return     Always return 1.
 */
int phoneTaskBackgroud(void)
{
  if (getModemLastActivityTime() - NS2MS(getUptime()) < 5000)
    phoneTaskTimeout = MODEM_ACTIVE_PHONE_TASK_TIMEOUT;
  else
    phoneTaskTimeout = MODEM_INACTIVE_PHONE_TASK_TIMEOUT;

  readLpuartDmaBuffer();

  static uint64_t networkUpdateTimestamp = 0;
  uint16_t networkUpdatePeriod = getNetworkUpdatePriodForState();

  if (!networkUpdateTimestamp || getModemInitializationStatus() == CONF_STAGE_INITIAL_MODEM_STATE || modemWakeFromRing)
    {
      setModuleState(modBusyFast);
      modemWakeFromRing = false;
      modemKeepWakeTime = MODEM_WAKE_PERIOD_RING;
    }

  if (networkInfo[SIM1].simStatus == WAIT_FOR_PIN || networkInfo[SIM2].simStatus == WAIT_FOR_PIN)
    {
      // do nothing...
    }
  else
    {
      ifThereIsNoSimTurnModemOff();

      if (getModemInitializationStatus() != CONF_STAGE_FINISHED_OK)
        {
          continueFirstModemConfiguration();
        }
      else if ((getModemInitializationStatus() == CONF_STAGE_INITIAL_RESET) && (networkInfo[SIM1].simStatus == SIM_INITIAL_STATE))
        {
          if (pinCheckedFlag)
            {
              checkPinOrPukLockState(SIM1);
              pinCheckedFlag = false;
            }
        }
      else
        {
          updateVoiceCallStatus();
          smsPromptCheck();

          if (((NS2S(getUptime()) - networkUpdateTimestamp) >= networkUpdatePeriod) && (getModemPowerState() == MODEM_ON))
            {
              networkUpdateTimestamp = NS2S(getUptime());
              updateSignalQualityAndNetStatus(networkInfo[SIM1].simStatus, networkInfo[SIM2].simStatus);
            }
          if (checkAllSentCommands() == COMMAND_SUCCESS && ( NS2MS(getUptime()) - getModemLastActivityTime()) >= modemKeepWakeTime)
            {
              if (!modemIsDuringVoiceCall && !modemIsDuringUssdReceiving)
                {
                  modemSleep();
                  modemKeepWakeTime = MODEM_WAKE_PERIOD_DEFAULT;
                }
            }
        }
    }

  if (getModemInitializationStatus() == CONF_STAGE_MODEM_POWER_RESET && \
      (networkInfo[SIM1].networkStatus == REGISTERED_HOME_NETWORK || networkInfo[SIM1].networkStatus == REGISTERED_ROAMING) && networkInfo[SIM1].simStatus == SIM_OK_NO_PIN)
    {
      setModemInitializationStatus(CONF_STAGE_MODEM_CONFIG_AFTER_POWER_UP);
    }


  if (!((networkInfo[SIM1].simStatus == WAIT_FOR_PIN) || (networkInfo[SIM2].simStatus == WAIT_FOR_PIN) || (networkInfo[SIM1].simStatus == SIM_NOT_PRESENT))) // if modem waits for pin or no sim is present, do not ping it
    {
      checkModem();
    }
  processNextCommand();
  simCardsAbsenceCheck();

  checkNetworkStateAndCoverage();

  return 1;
}

static const commCallback_t cmdProcessPHONE[cmd_num] =
{
  [0 ... cmd_num - 1] = dummyCommCallback,
  [CMD_INTERNAL_INT] = receivedModemCommandCallback,
  [CMD_PHONE_PBOOK] = modemPhoneTaskPhoneBookCallback,
  [CMD_PHONE_CALL] = modemVoiceCallCallback,
  [CMD_PHONE_SMS] = modemSmsCallback,
  [CMD_PHONE_PASSWORD] = phoneTaskPinCallback,
  [CMD_PHONE_USSD] = modemUssdCallback,
  [CMD_PHONE_CONFIG] = phoneModemConfigurationCallback,
  [CMD_PHONE_NETWORK] = modemNetworkConfigurationCallback,
  [CMD_COMM_DEBUG] = phoneTaskDebug,
};

COMM_TASK_TEMPLATE(phoneTask, phoneTaskPrepare(), MODULE_PHONE, cmdProcessPHONE, 100, /* arg_funcOnMessage */, /*sendToModemOnTimeout()*/, /* arg_funcOnError */, phoneTaskBackgroud());
