#include <Arduino.h>
#include <Wire.h>

#include "mcp7940_sol.h"

static void startRTCOscillator(void)
{
	Wire.beginTransmission(MCP7940_ADDRESS);
	Wire.write(byte(MCP7940_SECONDS));
	Wire.endTransmission();

	Wire.requestFrom(MCP7940_ADDRESS, 1);
	uint8_t seconds_register = Wire.read();

	seconds_register |= (1 << 7);

	Wire.beginTransmission(MCP7940_ADDRESS);
	Wire.write(byte(MCP7940_SECONDS));
	Wire.write(seconds_register);
	Wire.endTransmission();
}

static void stopRTCOscillator(void)
{
	Wire.beginTransmission(MCP7940_ADDRESS);
	Wire.write(byte(MCP7940_SECONDS));
	Wire.endTransmission(false);

	Wire.requestFrom(MCP7940_ADDRESS, 1);
	uint8_t seconds_register = Wire.read();

	seconds_register &= 0x7F;

	Wire.beginTransmission(MCP7940_ADDRESS);
	Wire.write(byte(MCP7940_SECONDS));
	Wire.write(seconds_register);
	Wire.endTransmission(false);
}

void RTCSetup(void)
{
	stopRTCOscillator();

	Wire.beginTransmission(MCP7940_ADDRESS);
  	Wire.write(byte(MCP7940_CALIBRATION));
  	Wire.write(0x00); // turn off calibration
  	Wire.endTransmission(false);

  	Wire.beginTransmission(MCP7940_ADDRESS);
  	Wire.write(byte(MCP7940_CONTROL_REG));
  	Wire.write(0x00);
  	Wire.endTransmission(false);

  	startRTCOscillator();
}

void setRTCTime(uint8_t seconds, uint8_t minutes, uint8_t hour, 
	uint8_t date, uint8_t month, uint16_t year)
{
	uint8_t raw_data[7];

	uint8_t ten_seconds = seconds / 10;
	raw_data[0] = ((ten_seconds & 0xF) << 4) + ((seconds - 10*ten_seconds) & 0xF);
	raw_data[0] |= (1 << 7);

	uint8_t ten_minutes = minutes / 10;
	raw_data[1] = ((ten_minutes & 0xF) << 4) + ((minutes - 10*ten_minutes) & 0xF);

	uint8_t ten_hour = hour / 10;
	raw_data[2] = ((ten_hour & 0x1) << 4) + ((hour - 10*ten_hour) & 0xF); // Set to 24 hour mode by bit 5 = 0

	raw_data[3] = 0; // TODO set day?

	uint8_t ten_date = date / 10;
	raw_data[4] = ((ten_date & 0x3) << 4) + ((date - 10*ten_date) & 0xF);

	uint8_t ten_month = month / 10;
	raw_data[5] = ((ten_month & 0x1) << 4) + ((month - 10*ten_month) & 0xF);

	uint8_t ten_year = year / 10;
	raw_data[6] = ((ten_year & 0xF) << 4) + ((year - 10*ten_year) & 0xF);

	//stopRTCOscillator();

	Wire.beginTransmission(MCP7940_ADDRESS);
	Wire.write(byte(MCP7940_SECONDS));
	for(int i = 0; i < 7; i++)
	{
		Wire.write(raw_data[i]);
	}
	Wire.endTransmission();

	startRTCOscillator();
}

uint32_t getRTCTime(void)
{
	startRTCOscillator();

	uint8_t raw_data[7];

	Wire.beginTransmission(MCP7940_ADDRESS);
	Wire.write(byte(0));
	Wire.endTransmission();

	Wire.requestFrom(MCP7940_ADDRESS, 7);
	for(int i = 0; i < 7; i++)
	{
		raw_data[i] = Wire.read();
	}

	// Parse raw data
	uint32_t seconds = (raw_data[0] & 0xF) + 10*((raw_data[0] & 0x70) >> 4);
	uint32_t minutes = (raw_data[1] & 0xF) + 10*((raw_data[1] & 0x70) >> 4);
	uint32_t hour = (raw_data[2] & 0xF) + 10*((raw_data[2] & 0x10) >> 4);
	uint32_t date = (raw_data[4] & 0xf)+ 10*((raw_data[4] & 0x30) >> 4);
	uint32_t month = (raw_data[5] & 0xF) + 10*((raw_data[5] & 0x10) >> 4);
	uint32_t year = (raw_data[6] & 0xF) + 10*((raw_data[6] & 0xF0) >> 4);

	Serial.println(seconds);
	Serial.print("Osc: ");
	Serial.println(raw_data[3] & (1 << 5));
	Serial.println(raw_data[0] & (1 << 7));

	//Serial.println(minutes);
	//Serial.println(hour);
	//Serial.println(date);
	//Serial.println(month);
	//Serial.println(year);

	// Time is seconds since January 1st, 1970
	uint32_t current_time = seconds;
	current_time += minutes * 60;
	current_time += hour * 3600;
	current_time += date * 86400;
	current_time += month * 2592000;
	current_time += year * 31104000; // TODO include leap year

	startRTCOscillator();

	return current_time;
}