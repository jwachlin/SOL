

#ifndef MCP7940_SOL_H
#define MCP7940_SOL_H

#include <stdint.h>

#define MCP7940_ADDRESS			0x6F

#define MCP7940_SECONDS			0x00

#define MCP7940_MINUTES			0x01

#define MCP7940_HOURS			0x02

#define MCP7940_OSCON_VBAT_DAY  0x03

#define MCP7940_DATE			0x04

#define MCP7940_MONTH			0x05 

#define MCP7940_YEAR			0x06 

#define MCP7940_CONTROL_REG		0x07 

#define MCP7940_CALIBRATION		0x08

void RTCSetup(void);

void setRTCTime(uint8_t seconds, uint8_t minutes, uint8_t hour, uint8_t date, uint8_t month, uint16_t year);

uint32_t getRTCTime(void);


#endif