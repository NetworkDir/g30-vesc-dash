/* Host mock of the few nRF51 registers the link layer touches. */
#ifndef MOCK_NRF_H_
#define MOCK_NRF_H_

#include <stdint.h>

typedef struct {
	volatile uint32_t TASKS_STARTRX, TASKS_STOPRX, TASKS_STARTTX, TASKS_STOPTX;
	volatile uint32_t EVENTS_RXDRDY, EVENTS_TXDRDY, EVENTS_ERROR;
	volatile uint32_t ERRORSRC, ENABLE, PSELRTS, PSELTXD, PSELCTS, PSELRXD;
	volatile uint32_t RXD, TXD, BAUDRATE, CONFIG, INTENSET, INTENCLR;
} NRF_UART_Type;

extern NRF_UART_Type mock_uart0;
#define NRF_UART0 (&mock_uart0)

#define UART_ENABLE_ENABLE_Disabled         0
#define UART_ENABLE_ENABLE_Enabled          4
#define UART_BAUDRATE_BAUDRATE_Pos          0
#define UART_BAUDRATE_BAUDRATE_Baud115200   0x01D7E000UL
#define UART_INTENSET_RXDRDY_Msk            (1u << 2)
#define UART_INTENSET_TXDRDY_Msk            (1u << 7)
#define UART_INTENSET_ERROR_Msk             (1u << 9)
#define UART0_IRQn                          2

static inline void NVIC_ClearPendingIRQ(int n) { (void)n; }
static inline void NVIC_SetPriority(int n, int p) { (void)n; (void)p; }
static inline void NVIC_EnableIRQ(int n) { (void)n; }

#define ADC_CONFIG_PSEL_AnalogInput0        1
#define ADC_CONFIG_PSEL_AnalogInput1        2

#endif
