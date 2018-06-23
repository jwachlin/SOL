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
 * @file SOL.cpp
 * @author Jacob Wachlin
 * @date 22 June 2018
 * @brief SOL arduino compatible library for the ESP32
 */

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>

#include "SOL.h"


// TODO this API is unofficial and unsupported!
// Returns the temperature in degrees F
extern "C" {
uint8_t temprature_sens_read();
}
uint8_t temprature_sens_read();

static volatile uint8_t touched = 0;
static uint16_t last_write_address;
static uint8_t ssid_length;
static char ssid[64];
static uint8_t pswd_length;
static char pswd[64];
static uint32_t device_ID;


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

	// TODO run faster?
	Wire.begin(SDA_PIN,SCL_PIN,100000);

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

	delay(10);
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

	// TODO better way to sense touch interrupt?
	delay(10);
	if(touched == 1)
	{
		SOL_startProvisioning();
		touched = 0;
	}

	// If not provisioned yet, don't save data
	if(SOL_hasWiFiCredentials())
	{
		// Run power sweep, save data
		SOL_generateDataPacket();

		// Determine if it is time to upload data
		uint16_t last_storage_address;
		SOL_readEEPROMNByte(EEPROM_ADDRESS_LAST_STORAGE_ADDRESS, (uint8_t *)&last_storage_address, 2);

		uint16_t datapoints = (last_storage_address - EEPROM_ADDRESS_DATA_RANGE_START_ADDRESS) / sizeof(data_packet_t);

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
	// TODO this is awful right now...and insecure
	#ifdef SOL_DEBUG
	Serial.println("Touch sensed, starting softAP");
	#endif


	String net_ssid, net_pswd;
	WiFiServer server(80);
	
	// Set up without password
	const char *AP_ssid = "SOL";
	WiFi.softAP(AP_ssid);
	delay(10);
	server.begin();

	delay(10);

	#ifdef SOL_DEBUG
	// Print the IP address
  	Serial.println("IP address: ");
  	Serial.println(WiFi.softAPIP());
	#endif

	long start_time = millis();
	uint8_t finished = 0;
	while( ( (millis() - start_time) < PROVISION_TIMEOUT * 1000) && !finished)
	{
		delay(1);

		WiFiClient client = server.available();   // listen for incoming clients

		if (client) {                             // if you get a client,
		    String current_line = "";                // make a String to hold incoming data from the client
		    net_ssid = "";
		    net_pswd = "";
		    uint8_t ssid_state = 0;
		    uint8_t pswd_state = 0;

		    // loop while the client's connected
		    while (client.connected() && ( (millis() - start_time) < PROVISION_TIMEOUT * 1000) && !finished) 
		    {   
		      	if (client.available()) 
		      	{             // if there's bytes to read from the client,
		        	char c = client.read();             // read a byte, then

		        	if (c == '\n') 
		        	{              

		          		// if the current line is blank, you got two newline characters in a row.
		          		// that's the end of the client HTTP request, so send a response:
		          		if (current_line.length() == 0) 
		          		{
		            		// HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
		            		// and a content-type so the client knows what's coming, then a blank line:
		            		client.println("HTTP/1.1 200 OK");
		            		client.println("Content-type:text/html");
		            		client.println();

		            		// the content of the HTTP response follows the header:
		            		client.print("<html><body><form action='' method='GET'>Please provide your WiFi SSID and password: <br>");
		            		client.print("SSID:<input type='text' name='SSID' placeholder='SSID (network name)'><br>");
		            		client.print("Password:<input type='password' name='PASSWORD' placeholder='password'><br>");
		            		client.print("<input type='submit' name='SUBMIT' value='Submit'></form>");

		            		// The HTTP response ends with another blank line:
				            client.println();
		            		// break out of the while loop:
		            		break;
		          		} else 
		          		{    
		          			// if you got a newline, then clear currentLine:
		            		current_line = "";
		          		}
		        	} else if (c != '\r') 
		        	{ 
		          		current_line += c;      // add it to the end of the currentLine
		        	}
		        
		        	// SSID assembly
		        	if(ssid_state == 1 && current_line.endsWith("&"))
		        	{
		          		// Stop assembling ssid
		          		ssid_state = 2;
		        	}
		        	if(ssid_state == 1){  net_ssid += c;}
		        	if(current_line.endsWith("SSID="))
		        	{
		          		// Start assembling SSID
		          		ssid_state = 1;
		        	}		        	

		        	// PASSWORD assembly     
		        	if(pswd_state == 1 && current_line.endsWith("&"))
		        	{
		          		// Stop assembling ssid
		          		pswd_state = 2;
		        	}   
		        	if(pswd_state == 1){  net_pswd += c;}
		        	if(current_line.endsWith("PASSWORD="))
		        	{
		          		// Start assembling password
		          		pswd_state = 1;
		        	}

		        	if(pswd_state == 2 && ssid_state == 2)
		        	{

		        		#ifdef SOL_DEBUG
		        		Serial.print("SSID: ");
          				Serial.println(net_ssid);
          				Serial.print("PSWD: ");
          				Serial.println(net_pswd);
		        		#endif

		        		// TODO test password and ssid first, request retry from user
		        		// TODO handle if inputs too long
		        		// Save password and ssid
		        		uint8_t len_ssid = net_ssid.length();
		        		uint8_t len_pswd = net_pswd.length();

		        		#ifdef SOL_DEBUG
		        		Serial.print("SSID length: ");
          				Serial.println(len_ssid);
          				Serial.print("PSWD length: ");
          				Serial.println(len_pswd);
		        		#endif

		        		SOL_writeEEPROMByte(EEPROM_ADDRESS_WIFI_PSWD_LENGTH, len_pswd);
		        		SOL_writeEEPROMByte(EEPROM_ADDRESS_WIFI_SSID_LENGTH, len_ssid);

		        		SOL_writeEEPROMNByte(EEPROM_ADDRESS_WIFI_PSWD_START, (uint8_t *) net_pswd.c_str(), len_pswd);
		        		SOL_writeEEPROMNByte(EEPROM_ADDRESS_WIFI_SSID_START, (uint8_t *) net_ssid.c_str(), len_ssid);

		        		// Indicate wifi credentials available
		        		SOL_writeEEPROMByte(EEPROM_ADDRESS_WIFI_CREDENTIALS_AVAILABLE, (uint8_t) 1);

		        		#ifdef SOL_DEBUG
		        		Serial.print("SSID length: ");
          				Serial.println(len_ssid);
          				Serial.print("PSWD length: ");
          				Serial.println(len_pswd);
		        		#endif

		        		// Exit
		        		client.stop();
		        		finished = 1;
		        	}
		      	}
		    }
		    // close the connection:
		    client.stop();
		}
	}

	WiFi.disconnect();
    WiFi.mode(WIFI_STA);

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
	uint16_t last_storage_address;
	SOL_readEEPROMNByte(EEPROM_ADDRESS_LAST_STORAGE_ADDRESS, (uint8_t *)&last_storage_address, 2);
	uint16_t first_storage_address = EEPROM_ADDRESS_DATA_RANGE_START_ADDRESS + sizeof(data_packet_t); // TODO fix indexing issue

	for(uint16_t dp = first_storage_address; dp <= last_storage_address+1; dp += sizeof(data_packet_t))
	{
		// Get data from EEPROM
		data_packet_t data = SOL_getDataPacket(dp-1); // TODO why the -1 needed?

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
		SOL_uploadDataPacket(&data);
		delay(100);
	}

	// Reset last storage address
	uint16_t last_storage = EEPROM_ADDRESS_DATA_RANGE_START_ADDRESS-1;
	SOL_writeEEPROMNByte(EEPROM_ADDRESS_LAST_STORAGE_ADDRESS, (uint8_t *) &last_storage, 2);

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
	float temp_C = (temprature_sens_read() - 32) / 1.8;

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

	for (uint16_t i = 0; i < DAC_RANGE; i++)
	{
	    // Sweep DAC
	    dacWrite(DAC_PIN, i);
	    delay(1);

	    // Read raw ADC values
	    int current_raw = analogRead(I_SENSE_PIN);
	    int voltage_raw = analogRead(V_SENSE_PIN);

	    // Convert to volts
	    float voltage = ((float) voltage_raw / (float) ADC_RANGE) * (float) V_SENSE_RANGE;
	    voltage = voltage * 3.0; // TODO use real gain

	    // Convert to amps
	    float current = ((float) current_raw / (float) ADC_RANGE) * (float) I_SENSE_RANGE; // Real volts measured
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

	// Get temperature
	float temp_C = (temprature_sens_read() - 32) / 1.8;

  	data_packet_t data;
  	data.timestamp = millis();	// TODO does this reset in deep sleep?
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
	Serial.print("ID: ");
	Serial.println(data.ID);
	#endif

	// Determine where to save data
	uint16_t new_storage_address;
	SOL_readEEPROMNByte(EEPROM_ADDRESS_LAST_STORAGE_ADDRESS, (uint8_t *) &new_storage_address, 2);
	new_storage_address += sizeof(data_packet_t);

	// Prevent attempt to write past available space, start overwriting oldest data
	// TODO this will confuse the upload function...
	if(new_storage_address + sizeof(data_packet_t) >= EEPROM_ADDRESS_DATA_RANGE_END_ADDRESS)
	{
		new_storage_address = EEPROM_ADDRESS_DATA_RANGE_START_ADDRESS;
	}

	#ifdef SOL_DEBUG
	Serial.print("New datapoint address: ");
	Serial.println(new_storage_address);
	#endif

	// Save data and location of it
	SOL_writeEEPROMNByte(new_storage_address, (uint8_t *) &data, sizeof(data_packet_t));
	SOL_writeEEPROMNByte(EEPROM_ADDRESS_LAST_STORAGE_ADDRESS, (uint8_t *) &new_storage_address, 2);
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
	const char* resource = "/trigger/sol_readings/with/key/ABCDEFGHIJKLMNOP";

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
 * @brief Handles touch sensor input by setting a flag
 *
 */
void SOL_handletouch(void)
{
	touched = 1;
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
  delay(5); // TODO is this necessary?
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