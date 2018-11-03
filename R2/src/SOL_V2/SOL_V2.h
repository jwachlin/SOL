/*
MIT License

Copyright (c) 2018 by Jacob Wachlin

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

/**
 * @file SOL_V2.h
 * @author Jacob Wachlin
 * @date 26 Sept 2018
 * @brief SOL_V2 arduino compatible library for the ESP32
 */


#ifndef SOL_V2_h
#define SOL_V2_h

#define SOL_DEBUG

//Define I2C addresses
#define EEPROM_ADDRESS 									0x50				// I2C EEPROM address
#define ADC_ADDRESS										0x48

//Pins
#define LED_PIN											23					
#define CHG_DISABLE_PIN									26
#define SDA_PIN											21
#define SCL_PIN											22					
#define DAC_PIN              							25
#define TEMP_SENSE_PIN									A7
#define TOUCH_PIN										T0 //T1 is pin0, T0 is pin 4
#define DAC_RANGE            							255
#define ADC_RANGE            							4095
#define AD1015_RANGE            						2048
#define TEMP_SENSE_RANGE        						3.9					// ESP32 ADC max voltage
#define V_SENSE_RANGE        							3.3					
#define I_SENSE_RANGE        							3.3					
#define I_SENSE_AMPLIFICATION							101.0				// Amplification stage for current sensing
#define R_SENSE              							0.75				// Current sense resistance, Ohms
#define V_SENSE_AMPLIFICATION							(4.13333)

#define EEPROM_ADDRESS_WIFI_CREDENTIALS_AVAILABLE		0x0000				// Location for flag if WiFi credentials have been sent
#define EEPROM_ADDRESS_WIFI_SSID_START					0x0001				// Location of start of WiFi SSID
#define EEPROM_ADDRESS_WIFI_SSID_END					0x0041				// Location of end of WiFi SSID
#define EEPROM_ADDRESS_WIFI_PSWD_START					0x0042				// Location of start of WiFi password
#define EEPROM_ADDRESS_WIFI_PSWD_END					0x006A				// Location of end of WiFi password
#define EEPROM_ADDRESS_WIFI_SSID_LENGTH					0x006B				// Location of length of WiFi SSID length (# of chars)
#define EEPROM_ADDRESS_WIFI_PSWD_LENGTH					0x006C				// Location of length of WiFi PSWD length (# of chars)
#define EEPROM_ADDRESS_NEXT_STORAGE_ADDRESS				0x006D				// Location of information about where data was stored last (also takes 0x006E)
#define EEPROM_ADDRESS_DATA_RANGE_START_ADDRESS 		0x006F				// Location of start of data address
#define EEPROM_ADDRESS_DATA_RANGE_END_ADDRESS 			0x0FA0				// Last location available in 32kbit EEPROM

#define SLEEP_TIME_SECONDS								30 //600			// Amount of time to sleep between sensing
#define SENSE_COUNT_TO_SEND								4					// Number of sensing datapoints before upload
#define PROVISION_TIMEOUT								180					// WiFi provisioning timeout

// Only charge in certain temperature range
#define CHARGE_TEMP_MIN_CELSIUS							0
#define CHARGE_TEMP_MAX_CELSIUS							45
#define TEMP_SENSE_OFFSET_C								0.5		// V
#define TEMP_SENSE_COEFF								0.01 	// V/C

/**
 * @brief Data packet generated by SOL during each sensing cycle
 */
typedef struct data_packet_t
{
	uint32_t timestamp;
	float peak_power_mW;
	float peak_current_mA;
	float peak_voltage_V;
	float temp_celsius;
	float batt_v;
	uint32_t ID;
} data_packet_t;

/**
 * @brief Performs initialization for SOL
 *
 * 	This function setups up GPIO, I2C, and touch sensor interrupt
 */
void SOL_begin();

/**
 * @brief Manages SOL task upon wakeup, triggering data reading and uploading when necessary
 *
 */
void SOL_task();

/**
 * @brief Checks in EEPROM if  WiFi credentials are available and loads them if so
 *
 * @return 1 if credentials are available, otherwise 0
 */
uint8_t SOL_hasWiFiCredentials();

/**
 * @brief Attempts to connect to Wifi
 *
 * @param timeout The connection attempt timeout in seconds
 *
 * @return 1 if connected, otherwise 0
 */
uint8_t SOL_connectToWiFi(uint16_t timeout);

/**
 * @brief Handles provisioning to WiFi network by putting ESP32 into softAP mode
 */
void SOL_startProvisioning(void);

/**
 * @brief Uploads all available data from EEPROM
 *
 */
void SOL_upload(void);

/**
 * @brief Places system into deep sleep, enabling charging if temperature in valid range
 *
 * @param len The time to sleep in seconds
 *
 */
void SOL_deepsleep(int len);	

/**
 * @brief Generates a new data packet from sensors
 *
 */
void SOL_generateDataPacket(void);

/**
 * @brief Gets data packet stored in EEPROM
 *
 * @param start_address The starting location for the data packet struct
 *
 * @return The data packet
 *
 */
data_packet_t SOL_getDataPacket(uint16_t start_address);

/**
 * @brief Uploads an individual data packet
 *
 * @param data Pointer to data packet to upload
 *
 */
void SOL_uploadDataPacket(data_packet_t * data);

/**
 * @brief Writes a single byte to EEPROM
 *
 * @param address The address in EEPROM to write to
 * @param data The data to put into EEPROM
 *
 */
void SOL_writeEEPROMByte(uint16_t address, uint8_t data);

/**
 * @brief Abstract method for writing N bytes of data to EEPROM
 *
 * @param address The starting address in EEPROM to write to
 * @param data Pointer to the data
 * @param size The number of bytes to write
 *
 */
void SOL_writeEEPROMNByte(uint16_t address, uint8_t * data, uint16_t size);

/**
 * @brief Reads a single byte of data from EEPROM
 *
 * @param address The address in EEPROM of the byte to read
 *
 * @return The byte from EEPROM
 *
 */
uint8_t SOL_readEEPROMByte(uint16_t address);

/**
 * @brief Abstract method for reading N bytes of data from EEPROM
 *
 * @param address The starting address in EEPROM to read from
 * @param data Pointer to the data storage to read data into
 * @param size The number of bytes to read
 *
 */
void SOL_readEEPROMNByte(uint16_t address, uint8_t * data, uint16_t size);

/**
 * @brief Reads the current temperature
 *
 * @return The temperature in celsius
 *
 */
float get_temperature_C(void);

/**
 * @brief Reads the battery voltage
 *
 * @return The battery voltage in Volts
 *
 */
float get_battery_voltage(void);

/**
 * @brief Sets time from network time protocol server
 */
 void SOL_set_time_from_ntp(void);

#endif
