/*!
 \file SoC.cpp
 \brief SoC
 \author Màrius Montón
 \date Feb 2021
 */
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#include <semaphore.h>

#include "SoC.h"
#include "Memory.h"
#include "HAL.h"
#include "GUI.h"
#include "UART.h"

/** Timer clock frequency (16 MHz) */
#define TIMER_IN_FREQ (16000000)

/**
 * @brief BIT for PORT A IRQ in the NVIC register
 */
#define NVIC_PORTA_IRQ_BIT (1 << NVIC_PORTA_IRQ_NUM)

/**
 * @brief BIT for PORT B IRQ in the NVIC register
 */
#define NVIC_PORTB_IRQ_BIT (1 << NVIC_PORTB_IRQ_NUM)

/**
 * @brief BIT for PORT C IRQ in the NVIC register
 */
#define NVIC_PORTC_IRQ_BIT (1 << NVIC_PORTC_IRQ_NUM)

/**
 * @brief BIT for PORT D IRQ in the NVIC register
 */
#define NVIC_PORTD_IRQ_BIT (1 << NVIC_PORTD_IRQ_NUM)

/**
 * @brief BIT for RTC IRQ in the NVIC register
 */
#define NVIC_RTC_IRQ_BIT (1 << NVIC_RTC_IRQ_NUM)

/**
 * @brief BIT for DAC IRQ in the NVIC register
 */
#define NVIC_DAC_IRQ_BIT (1 << NVIC_DAC_IRQ_NUM)

/**
 * @brief BIT for UART IRQ in the NVIC register
 */
#define NVIC_UART_IRQ_BIT (1 << NVIC_UART_IRQ_NUM)

/**
 * @brief Semaphore to indicate that a GPIO IRQ is triggered
 */
sem_t mutex_gpio;

/**
 * @brief Semaphore to indicate UART new data in RX
 */
SemaphoreHandle_t UART_RX_IRQ;

/**
 * @brief Semaphore to indicate UART finished send TX data
 */
SemaphoreHandle_t UART_TX_IRQ;

/**
 * @brief Semaphore to indicat if Watchdog has been enabled
 */
SemaphoreHandle_t WDT_Enable;

/**
 * @brief Semaphore to indicat if Watchdog has been feed
 */
SemaphoreHandle_t WDT_Feed;

/**
 * @brief GPIO IRQ task handle, it depends on #mutex_gpio
 */
TaskHandle_t GPIO_IRQ_handle;

/**
 * @brief RTC IRQ task handle
 */
TaskHandle_t RTC_IRQ_handle;

/**
 * @brief DAC IRQ task handle
 */
TaskHandle_t DAC_IRQ_handle;

/**
 * @brief UART IRQ task handle
 */
TaskHandle_t UART_IRQ_handle;

/**
 * @brief ADC IRQ task handle
 */
TaskHandle_t ADC_IRQ_handle;

/**
 * @brief Watchdog task handle
 */
TaskHandle_t WDT_handle;

/* Forward declarations */

/**
 * @brief RTC thread
 */
[[noreturn]] void RTC_IRQ_thread(void *);

/**
 * @brief DAC thread
 */
[[noreturn]] void DAC_IRQ_thread(void *);

/**
 * @brief UART thread
 */
[[noreturn]] void UART_IRQ_thread(void *);

/**
 * @brief ADC thread
 */
[[noreturn]] void ADC_IRQ_thread(void *);

/**
 * @brief Watchdog thread
 */
[[noreturn]] void WDT_thread(void *);

/**
 * @brief  write callback function for WDT_CTRL register
 * @param val unused
 * @param param unused
 */
uint32_t WDT_cb(int val, int param);

/**
 * @brief write callback function for WDT_CMD register
 * @param val value to write
 * @param param unused
 */
uint32_t WDT_feed_cb(int val, int param);

/**
 * @brief read callback function for ADC data register
 * @param val unused
 * @param param unused
 */
uint32_t ADC_data_cb(int val, int param);

/**
 * @brief UART class
 */
UART *uart0;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief PORT A ISR must be defined by the user
 */
__attribute__((weak)) void PORT_A_ISR(void);

/**
 * @brief PORT B ISR must be defined by the user
 */
__attribute__((weak)) void PORT_B_ISR(void);

/**
 * @brief RTC ISR must be defined by the user
 */
__attribute__((weak)) void RTC_ISR(void);

/**
 * @brief DAC ISR must be defined by the user
 */
__attribute__((weak)) void DAC_ISR(void);

/**
 * @brief UART RX ISR must be defined by th user
 */
__attribute__((weak)) void UART_RX_ISR(void);

/**
 * @brief UART TX ISR must be defined by th user
 */
__attribute__((weak)) void UART_TX_ISR(void);

#ifdef __cplusplus
}
#endif

/**
 * @brief Thread to manage GPIO IRQs
 * @param parameters unused
 */
[[noreturn]] void GPIO_IRQ_thread(void *parameters) {
    (void) parameters;
    while (true) {
        if (sem_wait(&mutex_gpio) == 0) {
            uint32_t pending_irq = memory[ADDR_NVIC_IRQ];

            if (pending_irq & NVIC_PORTA_IRQ_BIT) {
                PORT_A_ISR();
                sem_post(&mutex_gpio);
                vTaskDelay(10);
            }

            if (pending_irq & NVIC_PORTB_IRQ_BIT) {
                PORT_B_ISR();
                sem_post(&mutex_gpio);
                vTaskDelay(10);
            }
        }
    }
}

/**
 * @brief CB function to trigger (if necessary) corresponding GPIO IRQ
 * @param val PIN that has a change
 * @param param PORT that has a change
 */
uint32_t GPIO_in_cb(int val, int param) {

    uint32_t addr;
    uint32_t bit;

    switch (param) {
        case 1:
            addr = ADDR_PORTA_INT;
            bit = NVIC_PORTA_IRQ_BIT;
            break;
        case 2:
            addr = ADDR_PORTB_INT;
            bit = NVIC_PORTB_IRQ_BIT;
            break;
        case 3:
            addr = ADDR_PORTC_INT;
            bit = NVIC_PORTC_IRQ_BIT;
            break;
        case 4:
            addr = ADDR_PORTD_INT;
            bit = NVIC_PORTD_IRQ_BIT;
            break;
        default:
            addr = 0;
            bit = 0;
            break;
    }

    if ((memory[addr] & val) != 0) {
        uint32_t aux = memory[ADDR_NVIC_IRQ];
        aux |= bit;
        memory[ADDR_NVIC_IRQ] = aux;
        sem_post(&mutex_gpio);
    }

    return 0;
}

/**
 * @brief CB function to be called when TRACE register is updated
 * @param val character written
 * @param param unused
 */
uint32_t Trace_cb(int val, int param) {
    (void) param;
    gui_add_trace((char) (val & 0x00FF));
    return 0;
}

uint32_t send_to_uart(int value, int uart);

void SoC_Init() {

    xTaskCreate(GPIO_IRQ_thread, "IRQ1", 10000, nullptr, 1, &GPIO_IRQ_handle);
    xTaskCreate(RTC_IRQ_thread, "RTC", 10000, nullptr, 1, &RTC_IRQ_handle);
    xTaskCreate(DAC_IRQ_thread, "DAC", 10000, nullptr, 1, &DAC_IRQ_handle);
    xTaskCreate(UART_IRQ_thread, "UART", 10000, nullptr, 1, &UART_IRQ_handle);
    //xTaskCreate(ADC_IRQ_thread, "ADC", 10000, nullptr, 1, &ADC_IRQ_handle);
    xTaskCreate(WDT_thread, "WDT", 1000, nullptr, 1, &WDT_handle);


    memory[ADDR_PORTA_IN].register_wr_cb(GPIO_in_cb, 1);
    memory[ADDR_PORTB_IN].register_wr_cb(GPIO_in_cb, 2);
    memory[ADDR_PORTC_IN].register_wr_cb(GPIO_in_cb, 3);
    memory[ADDR_PORTD_IN].register_wr_cb(GPIO_in_cb, 4);

    memory[ADDR_TRACE].register_wr_cb(Trace_cb, 0);

    memory[ADDR_WDOG_CTRL].register_wr_cb(WDT_cb, 0);
    memory[ADDR_WDOG_CMD].register_wr_cb(WDT_feed_cb, 5);

    memory[ADDR_ADC_DATA].register_rd_cb(ADC_data_cb, 0);

    sem_init(&mutex_gpio, 0, 0);

    UART_RX_IRQ = xSemaphoreCreateBinary();
    WDT_Feed = xSemaphoreCreateBinary();
    WDT_Enable = xSemaphoreCreateBinary();

    uart0 = new UART(9600);

    memory[ADDR_UART_TXDATA].register_wr_cb(send_to_uart, 0);
}

uint32_t send_to_uart(int value, int uart) {
    (void) uart;
    uart0->send(value);
    return 0;
}

void I2CSlaveSet(int dev, int val) {
    (void) dev;
    (void) val;
}

void SoC_Button1Pressed() {
#if 0
    if (memory[ADDR_PORTA_INT] & (1 << BUTTON_1_PIN)) {
        //memory[ADDR_NVIC_IRQ] |= NVIC_PORTA_IRQ_BIT;
        uint32_t aux = memory[ADDR_NVIC_IRQ];
        aux  |= NVIC_PORTA_IRQ_BIT;
        memory[ADDR_NVIC_IRQ] = aux;
        xSemaphoreGive(GUI_GPIO_IRQ);
    }
#else
    uint32_t aux = memory[ADDR_PORTA_IN];
    aux |= (1 << BUTTON_1_PIN);
    memory[ADDR_PORTA_IN] = aux;
#endif
}

void SoC_Button1Released() {
    uint32_t aux = memory[ADDR_PORTA_IN];
    aux &= ~(1 << BUTTON_1_PIN);
    memory[ADDR_PORTA_IN] = aux;
}

void SoC_Button2Pressed() {
#if 0
    if (memory[ADDR_PORTB_INT] & (1 << BUTTON_2_PIN)) {
        //memory[ADDR_NVIC_IRQ] |= NVIC_PORTB_IRQ_BIT;
        uint32_t aux = memory[ADDR_NVIC_IRQ];
        aux  |= NVIC_PORTB_IRQ_BIT;
        memory[ADDR_NVIC_IRQ] = aux;
        xSemaphoreGive(GUI_GPIO_IRQ);
    }
#else
    uint32_t aux = memory[ADDR_PORTB_IN];
    aux |= (1 << BUTTON_2_PIN);
    memory[ADDR_PORTB_IN] = aux;
#endif
}

void SoC_Button2Released() {
    uint32_t aux = memory[ADDR_PORTB_IN];
    aux &= ~(1 << BUTTON_2_PIN);
    memory[ADDR_PORTB_IN] = aux;
}

bool SoC_LED1On() {
    if (memory[ADDR_PORTC_CTRL] & (1 << LED_1_PIN)) {
        if (memory[ADDR_PORTC_OUT] & (1 << LED_1_PIN)) {
            return true;
        }
    }

    return false;
}

bool SoC_LED2On() {

    if (memory[ADDR_PORTD_CTRL] & (1 << LED_2_PIN)) {
        if (memory[ADDR_PORTD_OUT] & (1 << LED_2_PIN)) {
            return true;
        }
    }
    return false;
}

/******************** PWM *******************/

unsigned int PWMDutyGet() {
    unsigned int duty;

    if (memory[ADDR_TIMER_TOP] != 0) {
        duty = 100U * memory[ADDR_TIMER_CMP] / memory[ADDR_TIMER_TOP];
    } else {
        duty = 0;
    }

    return duty;
}

unsigned int PWMFreqGet() {
    unsigned int freq;

    if (memory[ADDR_TIMER_TOP] != 0) {
        freq = TimerFreqGet() / memory[ADDR_TIMER_TOP];
    } else {
        freq = 0;
    }

    return freq;
}

unsigned int TimerFreqGet() {
    unsigned int prescaler;
    unsigned int freq = 0;

    prescaler = TIMER_PrescalerGet();
    if (prescaler != 0) {
        freq = TIMER_IN_FREQ / prescaler;
    }
    return freq;
}

/******************** RTC *******************/

[[noreturn]] void RTC_IRQ_thread(void *parameters) {
    (void) parameters;
    TickType_t pxPreviousWakeTime;

    pxPreviousWakeTime = xTaskGetTickCount();
    uint32_t now = 0;

    while (true) {

        if (memory[ADDR_RTC_CTRL] & 0x01) {
            memory[ADDR_RTC_CNT] = now;
        }

        if (memory[ADDR_RTC_CTRL] & 0x00000080) {
            if (memory[ADDR_RTC_CNT] == memory[ADDR_RTC_CMP]) {
#if 1
                memory[ADDR_NVIC_IRQ] |= NVIC_RTC_IRQ_BIT;
#else
                uint32_t aux = memory[ADDR_NVIC_IRQ];
                aux |= NVIC_RTC_IRQ_BIT;
                memory[ADDR_NVIC_IRQ] = aux;
#endif
            }
        }

        if (memory[ADDR_NVIC_IRQ] & NVIC_RTC_IRQ_BIT) {
            RTC_ISR();
        }

        /* Check every 1 s. */
        now++;
        xTaskDelayUntil(&pxPreviousWakeTime, 1000);
    }
}

/************************ DAC ***********************/

static int wr_idx = 0;

/**
 * @brief Buffer to store DAC output values
 */
float DACValues[DAC_TOTAL_VALUES] = {0.0};

/**
 * @brief Insert new data to DAC output buffer for GUI
 * @param data DAC sample
 * @param idx buffer index
 */
void set_DACVal(float data, int idx) {
    DACValues[idx] = data;
}

/**
 * @brief Insert new data to DAC output buffer for GUI
 * @param data DAC sample
 */
void insert_DACVal(float data) {
    set_DACVal(data, wr_idx);
    wr_idx = wr_idx + 1;

    if (wr_idx >= DAC_TOTAL_VALUES) {
        wr_idx = 0;
    }
}

float get_DACVal(void *data, int idx) {
    (void) data;
    return DACValues[idx];
}

[[noreturn]] void DAC_IRQ_thread(void *parameters) {
    (void) parameters;
    TickType_t pxPreviousWakeTime;

    pxPreviousWakeTime = xTaskGetTickCount();
    while (true) {

        if (memory[ADDR_DAC_CTRL] & 0x01) {
            uint32_t dac_data = memory[ADDR_DAC_DATA];
            dac_data = dac_data & 0x00000FFF;   // DAC uses only 12 bits
            insert_DACVal((float) dac_data);

            if (memory[ADDR_DAC_CTRL] & 0x00000080) {
                uint32_t aux = memory[ADDR_NVIC_IRQ];
                aux |= NVIC_DAC_IRQ_BIT;
                memory[ADDR_NVIC_IRQ] = aux;
            }
        }

        if (memory[ADDR_NVIC_IRQ] & NVIC_DAC_IRQ_BIT) {
            DAC_ISR();
        }

        xTaskDelayUntil(&pxPreviousWakeTime, 200);
    }
}

/******************** UART **********************/

uint16_t UART_GetBaudRate() {
    return uart0->getBaudrate();
}

void UART_NotifyRxData() {
    xSemaphoreGive(UART_RX_IRQ);
}

const char *getUART_Path() {
    return uart0->getDevicename().c_str();
}


[[noreturn]] void UART_IRQ_thread(void *parameters) {
    (void) parameters;

    while (true) {

        if (xSemaphoreTake(UART_RX_IRQ, portMAX_DELAY)) {

            if (memory[ADDR_UART_CTRL] & 0x00000080) {
                uint32_t aux = memory[ADDR_NVIC_IRQ];
                aux |= NVIC_UART_IRQ_BIT;
                memory[ADDR_NVIC_IRQ] = aux;
            }
        }

        if (memory[ADDR_NVIC_IRQ] & NVIC_UART_IRQ_BIT) {
            UART_RX_ISR();
        }
    }
}

/******************** ADC **********************/

uint16_t ADC_values[2] = {0};

[[noreturn]] void ADC_IRQ_thread(void *parameters) {
    (void) parameters;
    while (true) {

    }
}

void ADCSetValue(int ch, uint16_t value) {
    if (ch < 2) {
        ADC_values[ch] = value;
    }
}

uint32_t ADC_data_cb(int val, int param) {
    (void) param;
    uint32_t mode;
    uint32_t aux;
    uint32_t ch;

    mode = ( memory[ADDR_ADC_CTRL] >> 1 ) & 0x0000000F;

    aux = memory[ADDR_ADC_ADMUX];
    ch = (aux >> 2);

    if (mode == SINGLE) {
        switch (ch) {
            case 1:
                val = ADC_values[0];
                break;
            case 2:
                val = ADC_values[1];
                break;
            default:
                val = 0xFFFF;
                break;
        }
    } else {
        val = ADC_values[0] - ADC_values[1];
    }
    std::cout << "ADC val " << val << '\n';
    return val;
}

/******************** WDT **********************/

/**
 * @brief Watchdog task thread
 * @param parameters unused
 */
[[noreturn]] void WDT_thread(void *parameters) {
    (void) parameters;

    while (true) {

        xSemaphoreTake(WDT_Enable, portMAX_DELAY);
        bool enabled;
        do {
            /* if enabled, start count-down */
            enabled = memory[ADDR_WDOG_CTRL] & 0x00000001;
            if (enabled) {
                unsigned int wdt_time = (memory[ADDR_WDOG_CTRL] >> WDT_CTRL_PRESCALER_SHIFT) & 0x0000000F;
                wdt_time = (1 << wdt_time) * 16;
                if (xSemaphoreTake (WDT_Feed, wdt_time / portTICK_PERIOD_MS) == pdFALSE) {
                    /* Time-out, nobody has feed us, we are angry and bit the bone */
                    std::cout << "********************* WDT RESET !!! ************************\n" << std::endl;
                    vTaskEndScheduler();
                } else {
                    /* Somebody feed the watchdog, nothings happens */
                }
            }
        } while (enabled);

    }
}

uint32_t WDT_cb(int val, int param) {
    (void) val;
    (void) param;

    if (memory[ADDR_WDOG_CTRL] & 0x00000001) {
        xSemaphoreGive(WDT_Enable);
    }

    return 0;
}

uint32_t WDT_feed_cb(int val, int param) {
    (void) val;
    (void) param;
    if (val == 0x00505345) {
        xSemaphoreGive(WDT_Feed);
    }

    return 0;
}