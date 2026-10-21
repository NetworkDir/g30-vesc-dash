#ifndef MOCK_NRF_GPIO_H_
#define MOCK_NRF_GPIO_H_
#include <stdint.h>
typedef enum { NRF_GPIO_PIN_NOPULL, NRF_GPIO_PIN_PULLDOWN, NRF_GPIO_PIN_PULLUP } nrf_gpio_pin_pull_t;
static inline void nrf_gpio_pin_set(uint32_t p) { (void)p; }
static inline void nrf_gpio_pin_clear(uint32_t p) { (void)p; }
static inline void nrf_gpio_cfg_output(uint32_t p) { (void)p; }
static inline void nrf_gpio_cfg_input(uint32_t p, nrf_gpio_pin_pull_t u) { (void)p; (void)u; }
static inline void nrf_gpio_cfg_default(uint32_t p) { (void)p; }
static inline uint32_t nrf_gpio_pin_read(uint32_t p) { (void)p; return 1; }
#endif
