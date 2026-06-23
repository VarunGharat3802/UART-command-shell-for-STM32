/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : app_freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "stm32g070xx.h"
#include "stm32g0xx_hal.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"
#include "cmsis_os2.h"
#include "portable.h"
#include "stm32g0xx_hal_dma.h"
#include "stm32g0xx_hal_gpio.h"
#include "stm32g0xx_hal_uart.h"
#include "stm32g0xx_hal_uart_ex.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
#include "usart.h" /*Gives access to huart2 handle without this, HAL_UART_Transmit() won't know what huart2 is*/
#include "gpio.h" /*Gives access to GPIOA, GPIO_PIN_5 without this, HAL_GPIO_WritePin() won't compile*/
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define CMD_BUF_SIZE 128 /*Maximum length of a command the user can type 128 bytes is more than enough. Used to size the DMA buffer and command buffer*/
#define LOG_MSG_SIZE 64 /*Maximum length of a log message string 64 bytes fits "[LOG] LED turned ON\r\n" comfortably used to size the log message buffers*/
#define UART_TIMEOUT 100 /*How long HAL_UART_Transmit() waits in milliseconds before giving up if UART is stuck 100ms is generous for short strings at 115200 baud*/
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
volatile uint8_t dataReady = 0; /*A flag that acts as a signal between two contexts DMA callback (ISR context) sets it to 1 cmdTask checks it and sets it back to 0. Volatile tells the compiler "this can change at any time from outside normal code flow — never cache it in a register" without volatile the compiler might optimise away the check*/
volatile uint16_t dataLen = 0; /*Stores how many bytes DMA actually received the DMA callback passes this number to us we need it to know how much of dmaBuf to read. Also volatile for the same reason as dataReady*/
volatile uint8_t ledToggle = 0;
/* USER CODE END Variables */
/* Definitions for cmdTask */
osThreadId_t cmdTaskHandle;
const osThreadAttr_t cmdTask_attributes = {
  .name = "cmdTask",
  .priority = (osPriority_t) osPriorityNormal,
  .stack_size = 256 * 4
};
/* Definitions for logTask */
osThreadId_t logTaskHandle;
const osThreadAttr_t logTask_attributes = {
  .name = "logTask",
  .priority = (osPriority_t) osPriorityLow,
  .stack_size = 256 * 4
};
/* Definitions for rxCharQueue */
osMessageQueueId_t rxCharQueueHandle;
const osMessageQueueAttr_t rxCharQueue_attributes = {
  .name = "rxCharQueue"
};
/* Definitions for logQueue */
osMessageQueueId_t logQueueHandle;
const osMessageQueueAttr_t logQueue_attributes = {
  .name = "logQueue"
};
/* Definitions for uartMutex */
osMutexId_t uartMutexHandle;
const osMutexAttr_t uartMutex_attributes = {
  .name = "uartMutex"
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
static void uart_print(const char *str); /*sends a string to UART safely*/
static void log_send(const char *msg); /*sends a message to logTask via queue*/
static void parse_command(const char *cmd); /*decides what to do with a received command*/
/* USER CODE END FunctionPrototypes */

void cmdTask_funct(void *argument); 
void logTask_funct(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/* Hook prototypes */
void vApplicationStackOverflowHook(xTaskHandle xTask, signed char *pcTaskName);
void vApplicationMallocFailedHook(void);

/* USER CODE BEGIN 4 */
void vApplicationStackOverflowHook(xTaskHandle xTask, signed char *pcTaskName)
{
   /* Run time stack overflow checking is performed if
   configCHECK_FOR_STACK_OVERFLOW is defined to 1 or 2. This hook function is
   called if a stack overflow is detected. */
  __disable_irq(); //Turns off all interrupts. Stops the system completely
  while(1); //Infinite loop, System halts here, debigger will catch it at this line and "pcTaskName" tells you which task is Overflowed
}
/* USER CODE END 4 */

/* USER CODE BEGIN 5 */
void vApplicationMallocFailedHook(void)
{
   /* vApplicationMallocFailedHook() will only be called if
   configUSE_MALLOC_FAILED_HOOK is set to 1 in FreeRTOSConfig.h. It is a hook
   function that will get called if a call to pvPortMalloc() fails.
   pvPortMalloc() is called internally by the kernel whenever a task, queue,
   timer or semaphore is created. It is also called by various parts of the
   demo application. If heap_1.c or heap_2.c are used, then the size of the
   heap available to pvPortMalloc() is defined by configTOTAL_HEAP_SIZE in
   FreeRTOSConfig.h, and the xPortGetFreeHeapSize() API function can be used
   to query the size of free heap space that remains (although it does not
   provide information on how the remaining heap might be fragmented). */
  __disable_irq();
  while(1);
}
/* USER CODE END 5 */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */
  /* Create the mutex(es) */
  /* creation of uartMutex */
  uartMutexHandle = osMutexNew(&uartMutex_attributes);

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* Create the queue(s) */
  /* creation of rxCharQueue */
  rxCharQueueHandle = osMessageQueueNew (64, 1, &rxCharQueue_attributes);

  /* creation of logQueue */
  logQueueHandle = osMessageQueueNew (10, 64, &logQueue_attributes);

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of cmdTask */
  cmdTaskHandle = osThreadNew(cmdTask_funct, NULL, &cmdTask_attributes);

  /* creation of logTask */
  logTaskHandle = osThreadNew(logTask_funct, NULL, &logTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}


/* USER CODE BEGIN Header_cmdTask_funct */
/**
  * @brief  Function implementing the cmdTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_cmdTask_funct */
void cmdTask_funct(void *argument)
{
  /* USER CODE BEGIN cmdTask_funct */
  char cmdBuf[CMD_BUF_SIZE];
  uint16_t cmdLen = 0;
  uint8_t rxByte = 0;
  memset(cmdBuf, 0, sizeof(cmdBuf));

  uint32_t lastToggleTime = 0;

  /*Start DMA reception one bit at a time
    Callback fires automatically when idle line detected*/
  /*Infinite loop */
  HAL_UARTEx_ReceiveToIdle_DMA(&huart2, &rxByte, 1);

  /*Disable half-transfer interrupt. DMA would normally fire a callback when buffer is half full too we do not want that.
    We only want full reception callback
  */
  __HAL_DMA_DISABLE_IT((&huart2)->hdmarx, DMA_IT_HT);
  
  /*Print Welcome Banner*/
  uart_print("\033[2J\033[H");
  uart_print("========================================\r\n");
  uart_print("   STM32G070 UART Command Shell\r\n");
  uart_print("   FreeRTOS 10.3.1  |  64 MHz\r\n");
  uart_print("========================================\r\n\r\n");
  log_send("System initialized");
  log_send("cmdTask Started");

  for(;;){
    if(dataReady){
      if(ledToggle){
        uint32_t now = osKernelGetTickCount();
        if((now - lastToggleTime) >= 10){
          HAL_GPIO_TogglePin(GPIOA, USER_LED_Pin);
          lastToggleTime = now;
        }
      }
      dataReady = 0;
      char c = (char)rxByte; //Process the single received byte
      if(c == '\r' || c == '\n'){
        uart_print("\r\n"); //Enter pressed. Process command
        if(cmdLen > 0){
          cmdBuf[cmdLen] = '\0';
          parse_command(cmdBuf);
          memset(cmdBuf, 0, sizeof(cmdBuf)); //Clear buffer for next command
          cmdLen = 0;
        }
        uart_print("\r\n> ");
      } else if(c == 0x88 || c == 0x7F){
        if(cmdLen > 0){
          cmdLen--;
          cmdBuf[cmdLen] = '\0';
          uart_print("\b \b"); //Erase character on the Terminal
        }
      } else{
        if(cmdLen < CMD_BUF_SIZE - 1){
          cmdBuf[cmdLen ++] = c;
          char echo[2] = {c, '\0'};
          uart_print(echo);
        }
      }
      HAL_UARTEx_ReceiveToIdle_DMA(&huart2, &rxByte, 1);
      __HAL_DMA_DISABLE_IT((&huart2)->hdmarx, DMA_IT_HT);
    }
    osDelay(10);
  }
  /* USER CODE END cmdTask_funct */
}

/* USER CODE BEGIN Header_logTask_funct */
/**
* @brief Function implementing the logTask thread. Waits for log messages on logQueue and prints them to UART
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_logTask_funct */
void logTask_funct(void *argument)
{
  /* USER CODE BEGIN logTask_funct */
  char msg[LOG_MSG_SIZE];
  osDelay(200);
  uart_print("[LOG] logTask started\r\n\r\n> ");
  /* Infinite loop */
  for(;;){
    if(osMessageQueueGet(logQueueHandle, msg, NULL, osWaitForever) == osOK){ // block here indefinitely until a message arrives
      uart_print(msg);
      uart_print("> "); //reprint the prompt so terminal looks clean after a log message appears mid-typing
    }  
  }
  /* USER CODE END logTask_funct */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/**
 * @brief  Transmit a string over UART2, protected by mutex
 */
static void uart_print(const char *str){
  osMutexAcquire(uartMutexHandle, osWaitForever); /*Lock the UART. Another is printing, this task sleeps until the other finishes and waits as long as needed.*/
  HAL_UART_Transmit(&huart2, (uint8_t*) str, (uint16_t) strlen(str), UART_TIMEOUT); /*Sends bytes*/
  osMutexRelease(uartMutexHandle); /*Unlock UART. Now another task can use it*/
}

/**
 * @brief  Send a formatted log message to logTask queue
 */
static void log_send(const char *msg){
  char buf[LOG_MSG_SIZE]; /*Temporary buffer to build the full log string*/
  snprintf(buf, sizeof(buf), "[LOG] %s\r\n", msg); /*safely builds the string with [LOG] prefix "[LOG] %s\r\n" is the format %s gets replaced with the msg parameter result example: "[LOG] LED turned ON\r\n" sizeof(buf) prevents buffer overflow*/
  osMessageQueuePut(logQueueHandle, buf, 0,0); /*Puts the built string into logQueue. logTask is sleeping waiting on this queue. It will wake up and print it last two 0s mean: "priority 0" = normal priority message "timeout 0" = do not wait if queue is full, just drop the message*/
}

/**
 * @brief  Parse received command string and execute action
 */
static void parse_command(const char *cmd){
  char response[LOG_MSG_SIZE];
  char logmsg[LOG_MSG_SIZE];

  if(strcmp(cmd, "help") == 0){
    uart_print("\r\nAvailable Commands:: \r\n");
    uart_print("  help     - show this message\r\n");
    uart_print("  status   - show system status\r\n");
    uart_print("  led on   - turn on user LED\r\n");
    uart_print("  led off  - turn off user LED\r\n");
    uart_print("  clear    - clear terminal screen\r\n");
    snprintf(logmsg, sizeof(logmsg), "Help Executed");
  } else if(strcmp(cmd, "led on") == 0){
    HAL_GPIO_WritePin(GPIOA, USER_LED_Pin, GPIO_PIN_SET);
    uart_print("OK - LED ON\r\n");
    snprintf(logmsg, sizeof(logmsg), "LED ON");
  } else if(strcmp(cmd, "led off") == 0){
    HAL_GPIO_WritePin(GPIOA, USER_LED_Pin, GPIO_PIN_RESET);
    uart_print("OK - LED OFF\r\n");
    snprintf(logmsg, sizeof(logmsg), "LED OFF");
  } else if(strcmp(cmd, "status") == 0){
    char statusBuf[128];
    uint32_t heap_free = xPortGetFreeHeapSize();
    uint32_t uptime_s = osKernelGetTickCount() / 1000;
    uint32_t minutes = uptime_s / 60;
    uint32_t seconds = uptime_s % 60;
    snprintf(statusBuf, sizeof(statusBuf), 
            "\r\nSystem Status: \r\n"
            "CPU Clock : 64 MHz\r\n"
            "RTOS Tick : 1000 Hz\r\n"
            "Heap Free : %lu bytes\r\n"
            "Uptime : %02lu:%02lu\r\n", heap_free, minutes, seconds);
    uart_print(statusBuf);
    snprintf(logmsg, sizeof(logmsg), "Status Executed");
  } else if(strcmp(cmd, "clear") == 0){
    uart_print("\033[2J\33[H");
    snprintf(logmsg, sizeof(logmsg), "Screen Cleared");
  } else if(strcmp(cmd, "led toggle") == 0){
    ledToggle = 1;
    uart_print("OK - LED Toggle Starts\r\n");
    snprintf(logmsg, sizeof(logmsg), "LED Toggle Start");
  } else if(strcmp(cmd, "led stop") == 0){
    ledToggle = 0;
    HAL_GPIO_WritePin(GPIOA, USER_LED_Pin, GPIO_PIN_RESET);
    uart_print("LED Toggle Stopped");
    snprintf(logmsg, sizeof(logmsg), "LED Toggle Stopped");
  }else{
    snprintf(response, sizeof(response), 
              "[ERR] Unknown : '%s'\r\n"
              "Type 'help'\r\n", cmd);
    uart_print(response);
    snprintf(logmsg, sizeof(logmsg), "Unknown Command : %s", cmd);
  }
  log_send(logmsg);
}
/* USER CODE END Application */