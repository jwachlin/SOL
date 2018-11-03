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
 * @file SOL_V2.cpp
 * @author Jacob Wachlin
 * @date 26 Sept 2018
 * @brief SOL_V2 arduino compatible library for the ESP32
 */

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager
#include <Adafruit_ADS1015.h>
#include <mcp7940_sol.h>

#include "time.h"

#include "SOL_V2.h"

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 0;
const int   daylightOffset_sec = 0;

RTC_DATA_ATTR uint32_t sleepCount = 0;
RTC_DATA_ATTR uint32_t lastNTPTime = 0;

static uint16_t last_write_address;
static uint8_t ssid_length;
static char ssid[64];
static uint8_t pswd_length;
static char pswd[64];
static uint32_t device_ID;
static Adafruit_ADS1015 ads;

static adsGain_t ADC_gains[5] = {GAIN_ONE, GAIN_TWO, GAIN_FOUR, GAIN_EIGHT, GAIN_SIXTEEN};
static float ADC_max_v[5] = {4.096, 2.048, 1.024, 0.512, 0.256};

/**
 * @brief Handles touch sensor input
 *
 */
static void SOL_handletouch(void)
{
	#ifdef SOL_DEBUG
	Serial.println("Touch sensed");
	#endif
	uint8_t dummy = 0;
}

/**
 * @brief Performs initialization for SOL
 *
 * 	This function setups up GPIO, I2C, and touch sensor interrupt
 */
void SOL_begin()
{
	#ifdef SOL_DEBUG
	Serial.begin(115200);
	#endif

	Wire.begin(SDA_PIN,SCL_PIN,400000);

	//Set ID with MAC address
	device_ID = (uint32_t) ESP.getEfuseMac();

	#ifdef SOL_DEBUG
	// Start LED pin low
	pinMode(LED_PIN, OUTPUT);
	digitalWrite(LED_PIN, LOW);
	#endif

	// Set charge disable pin low
	pinMode(CHG_DISABLE_PIN, OUTPUT);
	digitalWrite(CHG_DISABLE_PIN, LOW);

	// Set up touch interrupts
	// TODO allow wakeup from deepsleep
	touchAttachInterrupt(TOUCH_PIN, SOL_handletouch, 40); // TODO set threshold intelligently?

	// Set up ADC
	ads.setGain(GAIN_ONE);  // 1x gain   +/- 4.096V  1 bit = 2mV
	ads.begin();

	// Set up RTC
	RTCSetup();

	#ifdef SOL_DEBUG
	Serial.println("Starting up");
	#endif
}

/**
 * @brief Manages SOL task upon wakeup, triggering data reading and uploading when necessary
 *
 */
void SOL_task()
{
	#ifdef SOL_DEBUG
	Serial.println("Running Task");
	#endif

	sleepCount = sleepCount + 1;

	esp_sleep_wakeup_cause_t wakeup_reason;
  	wakeup_reason = esp_sleep_get_wakeup_cause();
	if(wakeup_reason == ESP_SLEEP_WAKEUP_TOUCHPAD)
	{
		#ifdef SOL_DEBUG
		Serial.println("Starting provisioning");
		#endif
		SOL_startProvisioning();
	}

	// If not provisioned yet, don't save data
	else if( SOL_hasWiFiCredentials() )
	{
		// Run power sweep, save data
		SOL_generateDataPacket();

		// Determine if it is time to upload data
		uint16_t next_storage_address;
		SOL_readEEPROMNByte(EEPROM_ADDRESS_NEXT_STORAGE_ADDRESS, (uint8_t *)&next_storage_address, 2);

		uint16_t datapoints = (next_storage_address - EEPROM_ADDRESS_DATA_RANGE_START_ADDRESS) / sizeof(data_packet_t);

		#ifdef SOL_DEBUG
		Serial.print("Number of datapoints: ");
		Serial.println(datapoints);
		#endif

		if(datapoints >= SENSE_COUNT_TO_SEND)
		{
			// Connect with 10 second timeout and upload 
			if(SOL_connectToWiFi(10))
			{	
				#ifdef SOL_DEBUG
				Serial.println("Connected to wifi, now uploading");
				#endif
				SOL_upload();

				// Update time to reduce drift
				SOL_set_time_from_ntp();
				sleepCount = 0;
			}
		}
	}

	// Enter deep sleep
	SOL_deepsleep(SLEEP_TIME_SECONDS);
}

/**
 * @brief Checks in EEPROM if  WiFi credentials are available and loads them if so
 *
 * @return 1 if credentials are available, otherwise 0
 */
uint8_t SOL_hasWiFiCredentials()
{
	uint8_t hasCred = SOL_readEEPROMByte(EEPROM_ADDRESS_WIFI_CREDENTIALS_AVAILABLE);

	if(hasCred == 1)
	{
		// Get credentials
		ssid_length = SOL_readEEPROMByte(EEPROM_ADDRESS_WIFI_SSID_LENGTH);
		pswd_length = SOL_readEEPROMByte(EEPROM_ADDRESS_WIFI_PSWD_LENGTH);

		#ifdef SOL_DEBUG
		Serial.print("SSID length: ");
		Serial.println(ssid_length);
		Serial.print("PSWD length: ");
		Serial.println(pswd_length);
		#endif

		// Guard against too long
		if(ssid_length > 64) {ssid_length = 64;}
		if(pswd_length > 64) {pswd_length = 64;}

		
		for(uint8_t i = 0; i < ssid_length; i++)
		{
			ssid[i] = SOL_readEEPROMByte(EEPROM_ADDRESS_WIFI_SSID_START+i);
		}

		for(uint8_t i = 0; i < pswd_length; i++)
		{
			pswd[i] = SOL_readEEPROMByte(EEPROM_ADDRESS_WIFI_PSWD_START+i);
		}

		#ifdef SOL_DEBUG
		String str_ssid(ssid);
		Serial.print("SSID: ");
		Serial.println(str_ssid);
		String str_pswd(pswd);
		Serial.print("PSWD: ");
		Serial.println(str_pswd);
		#endif
		
	}
	else
	{
		hasCred = 0;
	}

	#ifdef SOL_DEBUG
	Serial.print("Cred: ");
	Serial.println(hasCred);
	#endif

	return hasCred;
}

/**
 * @brief Attempts to connect to Wifi
 *
 * @param timeout The connection attempt timeout in seconds
 *
 * @return 1 if connected, otherwise 0
 */
uint8_t SOL_connectToWiFi(uint16_t timeout)
{
	#ifdef SOL_DEBUG
	Serial.println("Attempting to connect to WiFi");
	Serial.println(ssid_length);
	Serial.println(pswd_length);
	#endif

	// Get correct parts of ssid and pswd TODO clean up?
	char ssid_part[ssid_length+1];
	char pswd_part[pswd_length+1];
	for(uint8_t i = 0; i < ssid_length; i++) ssid_part[i] = ssid[i];
	for(uint8_t i = 0; i < pswd_length; i++) pswd_part[i] = pswd[i];

	// Null terminate
	ssid_part[ssid_length] = '\0';
	pswd_part[pswd_length] = '\0';

	WiFi.begin(ssid_part,pswd_part);

	#ifdef SOL_DEBUG
	String str_ssid(ssid_part);
	String str_pswd(pswd_part);
	Serial.print("SSID: ");
	Serial.println(str_ssid);
	Serial.print("PSWD: ");
	Serial.println(str_pswd);

	Serial.println("Connecting");
	#endif

	uint8_t success = 1;
	long start_time = millis();
	while (WiFi.status() != WL_CONNECTED) //not connected
	{
		delay(50);
		#ifdef SOL_DEBUG
		Serial.print(".");
		#endif
		if((millis() - start_time) > (timeout*1000))
		{
			#ifdef SOL_DEBUG
			Serial.println("Could not connect. Timeout");
			#endif

			success = 0;
			break;
		}
	}
	return success;
}

/**
 * @brief Handles provisioning to WiFi network by putting ESP32 into softAP mode
 */
void SOL_startProvisioning(void)
{
	// Turn on LED
	digitalWrite(LED_PIN, HIGH);

	WiFiManager wifiManager;

	// Create SSID with ID
	String provision_ssid = "SOL " + String(device_ID);

	// Set a timeout
	wifiManager.setTimeout(120);
	if (wifiManager.startConfigPortal(provision_ssid.c_str())) {

		// Get the new network information and save it
		String connected_ssid = wifiManager.getSSID();
		String connected_pswd = wifiManager.getPassword();

		uint8_t len_ssid = connected_ssid.length();
		uint8_t len_pswd = connected_pswd.length();

		SOL_writeEEPROMByte(EEPROM_ADDRESS_WIFI_PSWD_LENGTH, len_pswd);
		SOL_writeEEPROMByte(EEPROM_ADDRESS_WIFI_SSID_LENGTH, len_ssid);

		SOL_writeEEPROMNByte(EEPROM_ADDRESS_WIFI_PSWD_START, (uint8_t *) connected_pswd.c_str(), len_pswd);
		SOL_writeEEPROMNByte(EEPROM_ADDRESS_WIFI_SSID_START, (uint8_t *) connected_ssid.c_str(), len_ssid);

		// Indicate wifi credentials available
		SOL_writeEEPROMByte(EEPROM_ADDRESS_WIFI_CREDENTIALS_AVAILABLE, (uint8_t) 1);

		// Reset next storage address
		uint16_t next_storage = EEPROM_ADDRESS_DATA_RANGE_START_ADDRESS;
		SOL_writeEEPROMNByte(EEPROM_ADDRESS_NEXT_STORAGE_ADDRESS, (uint8_t *) &next_storage, 2);

		SOL_set_time_from_ntp();
	}

	// Turn off LED
	digitalWrite(LED_PIN, LOW);

	#ifdef SOL_DEBUG
	Serial.println("Provisioned");
	#endif
}

/**
 * @brief Uploads all available data from EEPROM
 *
 */
void SOL_upload(void)
{
	#ifdef SOL_DEBUG
	// Turn on LED
	digitalWrite(LED_PIN, HIGH);
	#endif

	// Check where was written last
	uint16_t next_storage_address;
	SOL_readEEPROMNByte(EEPROM_ADDRESS_NEXT_STORAGE_ADDRESS, (uint8_t *)&next_storage_address, 2);
	uint16_t first_storage_address = EEPROM_ADDRESS_DATA_RANGE_START_ADDRESS;

	for(uint16_t dp = first_storage_address; dp < next_storage_address; dp += sizeof(data_packet_t))
	{
		// Get data from EEPROM
		data_packet_t data = SOL_getDataPacket(dp);

		// Fix ID
		data.ID = device_ID;

		#ifdef SOL_DEBUG
		Serial.print("Address: ");
		Serial.println(dp);
		Serial.print("Time: ");
		Serial.println(data.timestamp);
		Serial.print("Power: ");
		Serial.println(data.peak_power_mW);
		Serial.print("Voltage: ");
		Serial.println(data.peak_voltage_V);
		Serial.print("Current: ");
		Serial.println(data.peak_current_mA);
		Serial.print("ID: ");
		Serial.println(data.ID);
		#endif

		// Upload
		//SOL_uploadDataPacket(&data);
	}

	// Reset next storage address
	uint16_t next_storage = EEPROM_ADDRESS_DATA_RANGE_START_ADDRESS;
	SOL_writeEEPROMNByte(EEPROM_ADDRESS_NEXT_STORAGE_ADDRESS, (uint8_t *) &next_storage, 2);

	#ifdef SOL_DEBUG
	// Turn off LED
	digitalWrite(LED_PIN, LOW);
	#endif
}

/**
 * @brief Places system into deep sleep, enabling charging if temperature in valid range
 *
 * @param len The time to sleep in seconds
 *
 */
void SOL_deepsleep(int len)
{

	#ifdef SOL_DEBUG
	Serial.println("Feeling sleepy...");
	#endif

	// Check temperature
	float temp_C = get_temperature_C();

	#ifdef SOL_DEBUG
	Serial.print("Temp: ");
	Serial.print(temp_C);
	Serial.println("C");
	#endif

	// enable charging if in OK temperature range
	if(temp_C < CHARGE_TEMP_MAX_CELSIUS && temp_C > CHARGE_TEMP_MIN_CELSIUS)
	{
		digitalWrite(CHG_DISABLE_PIN, HIGH);
	}

	// enable timer deep sleep
    esp_sleep_enable_timer_wakeup(len * 1000000);
    esp_sleep_enable_touchpad_wakeup();
    esp_deep_sleep_start();
}


/**
 * @brief Generates a new data packet from sensors
 *
 */
void SOL_generateDataPacket(void)
{
	// Perform power sweep
	float max_power = 0.0;
	float max_current = 0.0;
	float max_voltage = 0.0;

	for(uint16_t gain_idx = 0; gain_idx < 3; gain_idx++)
	{
		ads.setGain(ADC_gains[gain_idx]);

		for (uint16_t i = 0; i < DAC_RANGE; i++)
		{
		    // Sweep DAC
		    dacWrite(DAC_PIN, i);

		    // Read raw ADC values
		    int16_t current_raw = ads.readADC_SingleEnded(0);
		    int16_t voltage_raw = ads.readADC_SingleEnded(1);

		    // Convert to volts
		    float voltage = ((float) voltage_raw / (float) AD1015_RANGE) * ADC_max_v[gain_idx];
		    voltage = voltage * V_SENSE_AMPLIFICATION;

		    // Convert to amps
		    float current = ((float) current_raw / (float) AD1015_RANGE) * ADC_max_v[gain_idx]; // Real volts measured
		    current = current / R_SENSE ; // Ohms law I = V/R
		    current = current / I_SENSE_AMPLIFICATION;

		    float power = current * voltage;

		    if (power > max_power)
		    {
		      max_power = power;
		    }

		    if (current > max_current)
		    {
		      max_current = current;
		    }

		    if (voltage > max_voltage)
		    {
		      max_voltage = voltage;
		    }
	  	}
	  	// TODO if any ADCs are maxed out, they will should read lower than their previous max, so we can just loop
	  	// TODO we can not loop further for efficiency if we are confident the value is reasonable
  	}

	// Get temperature
	float temp_C = get_temperature_C();

  	data_packet_t data;

  	// Get battery voltage
	data.batt_v = get_battery_voltage();
  	data.timestamp = lastNTPTime + (sleepCount * SLEEP_TIME_SECONDS);
  	data.peak_power_mW = max_power * 1000.0;
  	data.peak_current_mA = max_current * 1000.0;
  	data.peak_voltage_V = max_voltage;
  	data.temp_celsius = temp_C;
  	data.ID = device_ID;

  	#ifdef SOL_DEBUG
  	Serial.println("New datapoint:");
	Serial.print("Time: ");
	Serial.println(data.timestamp);
	Serial.print("Power: ");
	Serial.println(data.peak_power_mW);
	Serial.print("Voltage: ");
	Serial.println(data.peak_voltage_V);
	Serial.print("Current: ");
	Serial.println(data.peak_current_mA);
	Serial.print("Temp, C: ");
	Serial.println(data.temp_celsius);
	Serial.print("Battery voltage: ");
	Serial.println(data.batt_v);
	Serial.print("ID: ");
	Serial.println(data.ID);
	#endif

	// Determine where to save data
	uint16_t next_storage_address;
	SOL_readEEPROMNByte(EEPROM_ADDRESS_NEXT_STORAGE_ADDRESS, (uint8_t *) &next_storage_address, 2);

	// Prevent attempt to write past available space, start overwriting oldest data
	// TODO this will confuse the upload function...
	if(next_storage_address + sizeof(data_packet_t) >= EEPROM_ADDRESS_DATA_RANGE_END_ADDRESS)
	{
		next_storage_address = EEPROM_ADDRESS_DATA_RANGE_START_ADDRESS;
	}

	#ifdef SOL_DEBUG
	Serial.print("Next datapoint address: ");
	Serial.println(next_storage_address);
	#endif

	// Save data and location of it
	SOL_writeEEPROMNByte(next_storage_address, (uint8_t *) &data, sizeof(data_packet_t));
	next_storage_address += sizeof(data_packet_t);
	SOL_writeEEPROMNByte(EEPROM_ADDRESS_NEXT_STORAGE_ADDRESS, (uint8_t *) &next_storage_address, 2);
}

/**
 * @brief Gets data packet stored in EEPROM
 *
 * @param start_address The starting location for the data packet struct
 *
 * @return The data packet
 *
 */
data_packet_t SOL_getDataPacket(uint16_t start_address)
{

	data_packet_t data;
	SOL_readEEPROMNByte(start_address, (uint8_t *) &data, sizeof(data_packet_t));

	return data;
}

/**
 * @brief Uploads an individual data packet
 *
 * @param data Pointer to data packet to upload
 *
 */
void SOL_uploadDataPacket(data_packet_t * data)
{
	/*
	*  See tutorial here: https://randomnerdtutorials.com/esp32-esp8266-publish-sensor-readings-to-google-sheets/
	*
	*	This function for uploading data is based heavily on that tutorial, obviously with different data
	*/

	// TODO do differently in future
	// IFTTT URL resource
	// NOTE: Put your own key here
	const char* resource = "/trigger/your_key";

	// Maker Webhooks IFTTT
	const char* server = "maker.ifttt.com";

	WiFiClient client;
  	int retries = 5;
  	while (!!!client.connect(server, 80) && (retries-- > 0)) {
    	delay(100);
  	}

  	// Assemble data
  	String jsonObject = String("{\"value1\":\"") + data->peak_power_mW + "\",\"value2\":\"" + data->peak_current_mA
                      + "\",\"value3\":\"" + data->peak_voltage_V + "\"}";


  	client.println(String("POST ") + resource + " HTTP/1.1");
  	client.println(String("Host: ") + server);
  	client.println("Connection: close\r\nContent-Type: application/json");
  	client.print("Content-Length: ");
  	client.println(jsonObject.length());
  	client.println();
  	client.println(jsonObject);

  	int timeout = 5 * 10; // 5 seconds
  	while (!client.available() && (timeout-- > 0)) {
    	delay(100);
  	}

  	while (client.available()) {
    	Serial.write(client.read());
  	}
  	client.stop();
}

/**
 * @brief Writes a single byte to EEPROM
 *
 * @param address The address in EEPROM to write to
 * @param data The data to put into EEPROM
 *
 */
void SOL_writeEEPROMByte(uint16_t address, uint8_t data)
{
	Wire.beginTransmission(EEPROM_ADDRESS);
  	Wire.write((address >> 8)); // MSB
  	Wire.write((address & 0xFF)); // LSB
  	Wire.write(data);
  	Wire.endTransmission();
  	delay(5); // Takes 5 milliseconds to write page
}

/**
 * @brief Abstract method for writing N bytes of data to EEPROM
 *
 * @param address The starting address in EEPROM to write to
 * @param data Pointer to the data
 * @param size The number of bytes to write
 *
 */
void SOL_writeEEPROMNByte(uint16_t address, uint8_t * data, uint16_t size)
{
	for(uint16_t i = 0; i < size; i++)
	{
		SOL_writeEEPROMByte(address+i, *(data+i));
	}
}

/**
 * @brief Reads a single byte of data from EEPROM
 *
 * @param address The address in EEPROM of the byte to read
 *
 * @return The byte from EEPROM
 *
 */
uint8_t SOL_readEEPROMByte(uint16_t address)
{
  Wire.beginTransmission(EEPROM_ADDRESS);
  Wire.write((address >> 8)); // MSB
  Wire.write((address & 0xFF)); // LSB
  Wire.endTransmission();
  Wire.requestFrom(EEPROM_ADDRESS, 1);
  return Wire.read();
}


/**
 * @brief Abstract method for reading N bytes of data from EEPROM
 *
 * @param address The starting address in EEPROM to read from
 * @param data Pointer to the data storage to read data into
 * @param size The number of bytes to read
 *
 */
void SOL_readEEPROMNByte(uint16_t address, uint8_t * data, uint16_t size)
{
	// TODO make faster version by request consecutive data

	for(uint16_t i = 0; i < size; i++)
	{
		data[i] = SOL_readEEPROMByte(address+i);
	}
}

/**
 * @brief Reads the current temperature
 *
 * @return The temperature in celsius
 *
 */
float get_temperature_C(void)
{
	float v_meas = analogRead(TEMP_SENSE_PIN) * (TEMP_SENSE_RANGE/ADC_RANGE);

	return (v_meas - TEMP_SENSE_OFFSET_C) / TEMP_SENSE_COEFF;
}

/**
 * @brief Reads the battery voltage
 *
 * @return The battery voltage in Volts
 *
 */
float get_battery_voltage(void)
{
	ads.setGain(GAIN_ONE);

	int16_t raw = ads.readADC_SingleEnded(2);

	float v_meas = ads.readADC_SingleEnded(2) * (4.096 / (float) AD1015_RANGE);

	return v_meas * 2.0;
}

/**
 * @brief Sets time from network time protocol server
 */
 void SOL_set_time_from_ntp(void)
 {
 	if(SOL_hasWiFiCredentials() && SOL_connectToWiFi(10))
 	{
 		configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
 		struct tm timeinfo;
		if(getLocalTime(&timeinfo))
		{
			#ifdef SOL_DEBUG
		    Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
		    #endif

		    /*lastNTPTime = timeinfo.tm_sec;
		    lastNTPTime += timeinfo.tm_min * 60;
			lastNTPTime += timeinfo.tm_hour * 3600;
			lastNTPTime += timeinfo.tm_mday * 86400;
			lastNTPTime += timeinfo.tm_mon * 2592000;
			lastNTPTime += (timeinfo.tm_year - 1970) * 31104000;*/

			time_t now;
			time(&now);
			lastNTPTime = (uint32_t) now;

  		}
 	}
 }
