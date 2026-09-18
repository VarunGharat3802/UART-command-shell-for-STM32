# UART-command-shell-for-STM32
This project is a command shell for STM32G070RB over UART interface enabling FreeRTOS functionality of the chipset.

# Commands
1. help
2. status
3. led on
4. led off

# Pin Configuration
<img width="760" height="683" alt="image" src="https://github.com/user-attachments/assets/919a1b82-303a-4656-be96-e115d205b06f" />

# Architecture Overview
The application does two main tasks:
1. **Command Parser Task** — Reads incoming characters from USART, builds a command string, parses and dispatches commands.
2. **Logger Task** — Receives log messages from other tasks via a queue, serializes them out over USART (same or different peripheral).
A shared message queue connects them so logging never blocks the command parser.

# Hardware Mapping on STM32G070
From the datasheet you have, good USART choices:
1. **USART1** on PB6 (TX) / PB7 (RX) — Supports dual clock domain and wakeup from Stop, good for the command shell
2. **USART2** on PA2 (TX) / PA3 (RX) — Also supports auto baud rate detection, useful if your host terminal varies
Both support DMA, hardware flow control, and SPI emulation mode

# FreeRTOS Task Design
Log Message Queue

│                   Main Application                  │
|------------------------------------------------------|
│   CMD Parser Task    │      Logger Task              │
│   Priority: High     │      Priority: Low            │
│                      │                               │
│  USART1 RX (DMA or  │   Waits on xQueueReceive()   │
│  interrupt) → ring  │   Formats & sends via         │
│  buffer → parse     │   USART1/2 TX                 │
|------------------------------------------------------|
           │  xQueueSend()            │
           |--------------------------|
                  Log Message Queue
