/*
 * m365-eload <-> G30 dashboard protocol.
 *
 * Single wire half duplex bus, 115200 8N1, VESC packet framing:
 *   0x02 len payload[len] crc16_hi crc16_lo 0x03   (crc16 = CRC-CCITT/XMODEM)
 * The dashboard is the bus master: it sends a request every ~30 ms and the
 * ESC answers each request with one status frame.
 *
 * Keep this file identical in both repositories.
 */
#ifndef ELOAD_PROTO_H
#define ELOAD_PROTO_H

#define EP_CMD_REQUEST      0x70    /* dash -> ESC */
#define EP_CMD_STATUS       0x71    /* ESC -> dash */

/* Request payload:
 *   [0]     EP_CMD_REQUEST
 *   [1..2]  potentiometer, 0..1000 (big endian), EP_POT_INVALID = sensor fault
 *   [3]     flags (EP_REQ_*)
 *   [4..]   terminal input text from Bluetooth (0..EP_TEXT_IN_MAX bytes)
 */
#define EP_POT_INVALID      0xFFFF
#define EP_REQ_BLE          0x01    /* phone connected: send terminal text */
#define EP_TEXT_IN_MAX      40

/* Status payload:
 *   [0]       EP_CMD_STATUS
 *   [1]       state (eload_state_t)
 *   [2]       fault (eload_fault_t), 0 = none
 *   [3]       flags (EP_ST_*)
 *   [4]       button counter, +1 for every short press of the ESC button
 *   [5..6]    bus voltage, 0.01 V
 *   [7..8]    DC current, 0.01 A (signed)
 *   [9..10]   current setpoint, 0.1 A
 *   [11..12]  maximum current (pot full scale), 0.1 A
 *   [13..14]  MOSFET temperature, 0.1 C (signed)
 *   [15..16]  power, W
 *   [17..]    terminal output text (0..EP_TEXT_OUT_MAX bytes)
 * All multi byte values big endian.
 */
#define EP_ST_ARMED         0x01
#define EP_ST_FAN           0x02
#define EP_ST_DERATING      0x04
#define EP_ST_RUNNING       0x08
#define EP_STATUS_LEN       17
#define EP_TEXT_OUT_MAX     100

#endif
