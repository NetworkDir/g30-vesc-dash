/*
 * nRF51822 with 16 kB RAM (QFAA/QFAB, also fine on 32 kB parts) and the
 * S130 2.0.1 SoftDevice. One peripheral link.
 */

SEARCH_DIR(.)
GROUP(-lgcc -lc -lnosys)

MEMORY
{
  FLASH (rx) : ORIGIN = 0x1b000, LENGTH = 0x25000
  RAM (rwx)  : ORIGIN = 0x20001fe8, LENGTH = 0x2018
}

SECTIONS
{
  .fs_data :
  {
    PROVIDE(__start_fs_data = .);
    KEEP(*(.fs_data))
    PROVIDE(__stop_fs_data = .);
  } > RAM
} INSERT AFTER .data;

INCLUDE "nrf5x_common.ld"
