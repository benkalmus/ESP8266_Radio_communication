/*	SI4463 Long range radio Info
	Product datasheet: https://www.elecrow.com/download/HC-12.pdf
	//pull SET low to enter command mode
	//Commands start with AT+
	//AT+Bxxxx serial port baud rate
	//AT+Cxxx channel 001-127, 400khz step
	//AT+FUx	transmit modes, 1-3
	//AT+Px		transmit power 1-8
	//AT+Rx		returns current config if u replace x with letter B,C,F,P
	//AT+SLEEP	to wake module, set pin to ON then OFF

*/
#include <i2c_BMP280.h>
#include "Wire.h"
#include <ESP8266WiFi.h>

#include "AudioFileSourceSPIFFS.h"
#include "AudioGeneratorMP3.h"
#include "AudioOutputI2S.h"
#include <ESP8266SAM.h>

#include <SoftwareSerial.h>
#include <FS.h>

#define AUDIODEVICE
#define SENSOR_BMP

#if defined(SENSOR_BMP)
	#define SDA_PIN	14
	#define SCL_PIN	13
#endif
#define SYS_VER 1.0f
#define COMMAND 0
#define TRANSMIT 1
#define DELIMITER '\n'
#define SERIAL_BUFFER_LEN	256

const byte HC12RxdPin = 5;                  // Recieve Pin on HC12
const byte HC12TxdPin = 4;                  // Transmit Pin on HC12
const byte SET = 12;                  // SET Pin on HC12

//barometer
uint32_t presUint;
int32_t tempInt;

//Serial and radio communication
char SerialIn, HC12In;
char HC12ReadBuffer[SERIAL_BUFFER_LEN];		//64 = 63 + \0 null terminator
char SerialReadBuffer[SERIAL_BUFFER_LEN];
uint16_t HC12len = 0;
uint16_t Seriallen = 0;
bool SerialEnd, HC12End;	//indicates buffer full
const uint8_t packetSize = 32;
const uint32_t timed_out = 1000;	//ms
//getoption
size_t currentOption = 0;

SoftwareSerial HC12(HC12TxdPin, HC12RxdPin); // TXD, RXD
#if defined(SENSOR_BMP)
BMP280 bmp;	//barometer
#endif
#ifdef AUDIODEVICE
//Audio
AudioGeneratorMP3* mp3;
AudioFileSourceSPIFFS* file;
AudioOutputI2S* out;
AudioOutputI2S* outSAM;
ESP8266SAM* sam;
char scriptToRead[256];
bool loopAudio = 0;
uint8_t phoneticMode = 0;
uint8_t singMode = 0;
char fileToLoop[256];
#endif

struct key_value
{
	char* key;
	uint8_t value;
};

uint8_t keyFromString(char* key, key_value table[],  uint8_t elementsInTable)
{
	for (uint8_t i = 0; i < elementsInTable; i++) {
		key_value* sym = &table[i];
		if (strcasecmp(sym->key, key) == 0)
			return sym->value;
	}
	return 255;
}

char* commandsList[] = { "help", "test", "say", "play", "sleep" };

#ifdef AUDIODEVICE
int playFile(const char* path)
{
	File test = SPIFFS.open(path, "r");
	if (!test) {
		//test.close();
		return 0;		//file does not exist
	}
	test.close();
	file = new AudioFileSourceSPIFFS(path);
	out = new AudioOutputI2S();
	mp3 = new AudioGeneratorMP3();
	mp3->begin(file, out);
	return 1;
}

void stopPlayback()
{
	if (mp3)
	{
		mp3->stop();
		delete mp3;
		mp3 = NULL;
	}
	if (file)
	{
		delete file;
		file = NULL;
	}
}

void beginSAM()
{
	if (!outSAM) {
		outSAM = new AudioOutputI2S();
		outSAM->begin();
	}
	if (!sam)
		sam = new ESP8266SAM();	
}

void endSAM()
{
	if (sam)
	{
		delete sam;
		sam = NULL;
	}
}
#endif 

void setup()
{
	WiFi.mode(WIFI_OFF);
	Serial.begin(9600);
#if defined(SENSOR_BMP)
	Wire.begin(SDA_PIN, SCL_PIN);
#endif
	delay(500);
	//setup HC12
	HC12.begin(9600);
	pinMode(SET, OUTPUT);                  // Output High for Transparent / Low for Command
	digitalWrite(SET, TRANSMIT);               // Enter Transparent mode
	Serial.println("Ready");
	/*		INIT SPIFFS  (File system) */
	if (SPIFFS.begin())
	{
		Serial.println("File system mounted.");
	} 
	else {
		Serial.println("Error mounting the file system");
	}
#if defined(SENSOR_BMP)
	if (bmp.initialize()) Serial.println("BMP Sensor found");
	else
	{
		Serial.println("BMP Sensor missing");
		while (1) {
			delay(0);
		}
	}
#endif
	/*		READING FROM SPIFFS*/

	/*char fileBuffer[64]; uint8_t p = 0;
	Serial.print(readTxt.fullName());
	Serial.print(" size: ");
	Serial.println(readTxt.size());
	uint32_t counter =0;
	while (readTxt.available() && counter != readTxt.size())
	{
		uint8_t b = readTxt.read();
		Serial.write(b);
		counter++;
	}
	fileBuffer[p] = '\0';
	Serial.println("End");*/
#ifdef AUDIODEVICE
	playFile("/startup.mp3");
#endif
}

void loop() 
{
#ifdef AUDIODEVICE
	if (mp3) {	//check if object still exists
		if (mp3->isRunning())
		{
			if (!mp3->loop())	//run mp3 loop. (fill buffer for audio playback)
			{
				mp3->stop();
				if (loopAudio)	//restart song
				{
					stopPlayback();
					playFile(fileToLoop);
				}
			}
		}
	}
#endif
	while (HC12.available()) 
	{
		char b = HC12.read();
		HC12ReadBuffer[HC12len] = b;		//concat to buffer
		if (char(b) == DELIMITER || HC12len == (SERIAL_BUFFER_LEN-1) && HC12len > 1)
		{
			HC12End = 1;		//indicate end of transmission
			HC12ReadBuffer[HC12len+1] = '\0';
		}
		else
		{
			HC12len++;
		}
	}
	if (HC12End)	//received transmission
	{

		//check if data is a command
		if (strncmp(HC12ReadBuffer, "AT+", 3) == 0)
		{
			Serial.print("Executing remote command: ");
			Serial.print(HC12ReadBuffer);
			processCommand(HC12ReadBuffer);
		}
		else
		{
			parseSerialBuffer(HC12ReadBuffer, HC12len + 1);
		}

		HC12End = 0; HC12len = 0;
		clearStr(HC12ReadBuffer, SERIAL_BUFFER_LEN);
	}

	while (Serial.available()) 
	{
		byte b = Serial.read();
		SerialReadBuffer[Seriallen] = b;		//concat to buffer
		if (char(b) == DELIMITER || Seriallen == (SERIAL_BUFFER_LEN-1))
		{
			SerialEnd = 1;
			SerialReadBuffer[Seriallen+1] = '\0';		//concat to buffer
		}
		else
		{
			Seriallen++;
		}
	}
	if (SerialEnd)	//send serial data to hc12
	{
		//check if data is a command
		if (strncmp(SerialReadBuffer, "AT+", 3) == 0)
		{
			Serial.println("Command detected, send Y to execute.");
			while (!Serial.available()) yield();	//wait until user responds
			char c = Serial.read();
			if (c == 'y' || c == 'Y')
			{
				//broadcast command to other radios
				HC12.print(SerialReadBuffer);
				delay(250);	//allow time
				processCommand(SerialReadBuffer);
			}
			else
			{
				Serial.println("Cancelled");
			}
		}
		else
		{
			//print transmitted message
			//HC12.write(SerialReadBuffer, Seriallen +1);
			Serial.print("Transmitting:");
			char* p = SerialReadBuffer;
			while (*p)
			{
				HC12.write(*p);
				Serial.write(*p);
				delay(66);
				p++;
			}
			//Serial.printf("\nsent %s\n", SerialReadBuffer);
		}
		SerialEnd = 0; Seriallen = 0;
		clearStr(SerialReadBuffer, SERIAL_BUFFER_LEN);
	}
}

void processCommand(const char * command)
{
	// enter command mode
	digitalWrite(SET, COMMAND);
	delay(100);	//allow time
	HC12.print(command);  //print command to this HC12
	//wait for tx buffer to empty
	//while (!HC12.availableForWrite())	//>always returns 1, bug in library
	//{	
		delay(500);	//allow time for command to be sent, as serial buffer is a background interrupt process
	//}
	digitalWrite(SET, TRANSMIT);	//back to transmit mode
}

bool sendFileBuffered(const char* filename)
{
	File file = SPIFFS.open(filename, "r");
	if (!file)
	{
		Serial.println("Error opening file.");
		return false;
	}
	size_t size = file.size();
	size_t counter = 0;
	uint8_t buffer[64];
	clearStr((char*)buffer, 64);
	HC12.println("FILE");
	HC12.println(file.name());
	HC12.println(file.size());
	Serial.print("name "); Serial.println(file.name());
	Serial.print("size "); Serial.println(size);
	while (file.available() && counter != size)
	{
		yield();
		//case when last bit of bytes doesn't quite fit packetsize
		if ((counter + packetSize) > size)		//last packet to be sent
		{
			size_t bytes = (size - counter);
			for (size_t i = 0; i < bytes; i++)
			{
				buffer[i] = file.read();
			}
			buffer[bytes] = checksum(buffer, bytes);
			HC12.write(buffer, bytes + 1);
			bool receivedAck = 0;
			while (!receivedAck)
			{
				yield();
				if (HC12.available())
				{
					if (HC12.read() == 'a')
					{
						HC12.flush();
						receivedAck = 1;	//acknowledged, proceed
						Serial.println("ACK");
					}
					else
					{
						HC12.flush();
						HC12.write(buffer, bytes);	//resend
						Serial.print(" resend ");  Serial.println(counter);
						Serial.write(buffer, 64);
					}
				}
			}
			break;
		}

		uint8_t b = file.read();
		buffer[counter % packetSize] = b;
		counter++;
		if (counter % packetSize == 0)
		{
			uint8_t chk = checksum(buffer, packetSize);
			buffer[packetSize] = chk;
			HC12.write(buffer, packetSize+1);
			Serial.print("sent\n");
			Serial.write(buffer, packetSize);
			Serial.println(counter);
			bool receivedAck = 0;
			while (!receivedAck)
			{
				yield();
				if (HC12.available())
				{
					if (HC12.read() == 'a') 
					{
						HC12.flush();
						receivedAck = 1;	//acknowledged, proceed
						Serial.println("ACK");
					} else 
					{
						HC12.flush();
						HC12.write(buffer, packetSize+1);	//resend
						Serial.print(" resend ");  Serial.println(counter);
						Serial.write(buffer, packetSize);
					}
				}

			}

			Serial.write(buffer, packetSize);
			float progress = float(counter) / float(size) * 100;
			Serial.print("\nprog: "); Serial.println(progress); Serial.println();
		}
		
	}
	HC12.println("END");
	return true;
}

uint8_t checksum(const uint8_t * arr, size_t len)
{
	size_t sum=0;
	for (size_t i = 0; i < len; i++)
	{
		sum += (uint8_t)arr[i];
	}
	return (uint8_t)sum;
}

void receiveFileBuffered()
{
	uint8_t fileIndicator = 0;
	char fileName[64];
	uint32_t fileSize = 0;
	uint32_t receivedSize = 0;
	//intercept all serialreads
	while (fileIndicator != 3)
	{
		if (!HC12.available())
		{
			yield();		//if no data received, yield to prevent blocking code
			continue;
		}
		uint8_t b = HC12.read();
		if (char(b) == '\n' && fileIndicator == 0)	//expecting file name (needed to create file)
		{	//no need to append \n
			HC12ReadBuffer[HC12len] = '\0';
			strcpy(fileName, HC12ReadBuffer);	//name
			HC12len = 0;
			clearStr(HC12ReadBuffer, 64);
			fileIndicator = 1;		//now expect file size
			Serial.print("Receiving "); Serial.println(fileName);
		}
		else if (char(b) == '\n' && fileIndicator == 1) //expecting file size
		{
			HC12ReadBuffer[HC12len] = '\0';
			fileSize = strtoul(HC12ReadBuffer, NULL, 0);	//conver to int, base 10
			HC12len = 0;
			clearStr(HC12ReadBuffer, 64);
			fileIndicator = 2;
			Serial.print("Size: "); Serial.println(fileSize);
			if (fileSize == 0) return;
		}
		else if (fileIndicator == 2)
		{
			//check if file exists
			uint8_t inBuffer[64];
			clearStr((char*)inBuffer, 64);
			char reply;
			File file = SPIFFS.open(fileName, "w");
			if (!file) {
				Serial.println("error opening file, new name is temp.mp3 ");
				file.close();
				file = SPIFFS.open("/temp.mp3", "w");
			}
			file.write(b);
			int32_t counter = 0;
			uint32_t lastReceived = millis();
			inBuffer[counter] = b;
			counter++;
			while (true)
			{
				if (!HC12.available())
				{
					reply = 'n';
					if (millis() - lastReceived >= timed_out)
					{
						HC12.print(reply);
						lastReceived = millis();
					}
					yield();		//if no data received, yield to prevent blocking code
					continue;
				}
				uint8_t chr = HC12.read();
				inBuffer[counter % (packetSize+1)] = chr;
				lastReceived = millis();
				if (counter % (packetSize+1) == packetSize) {	//check sum once 61 bytes have been received (counting 0)
					uint8_t chk = (uint8_t)inBuffer[packetSize];
					if (checksum(inBuffer, packetSize) == chk)
					{
						file.write(inBuffer, packetSize);
						receivedSize += packetSize;
						Serial.write(inBuffer, packetSize);
						float progress = float(receivedSize) / float(fileSize) * 100;
						Serial.print("\nprog: "); Serial.println(receivedSize); Serial.println();
						reply = 'a';
						HC12.flush();
						HC12.print(reply);	//send ACK several times for robustness
					}
					else
					{	//need to resend
						Serial.print(receivedSize);
						Serial.print("BAD\n");
						Serial.write(inBuffer, 64);
						HC12.flush();
						reply = 'n';
						HC12.print(reply);	//send NACK
						clearStr((char*)inBuffer, 64);
						counter -= packetSize;
						continue;
						//counter -= (packetSize+1);
					}
					lastReceived = millis();
				}

				counter++;
				if (receivedSize >= fileSize)
				{
					Serial.println("Received and expected size match.");
					break;
				}
			}
			fileIndicator = 3;
			file.close();
			clearStr(HC12ReadBuffer, 64);
			HC12len = 0;
			Serial.println("File received.");
		}
		else
		{
			HC12ReadBuffer[HC12len++] = b;		//write  to buffer
			//HC12len++;
		}
	}
}

//clear any line ending characters
void clearStr(char str[], size_t len)
{
	for (size_t i = 0; i < len; i++) str[i] = '\0';
	//str[0] = '\0';
}

//extract command, arv, argc from serial buffer /0 terminated
void parseSerialBuffer(const char* buffer, size_t length)
{
	static key_value lookuptable[] = {
		{ "help", 0 }, { "version", 1 }, { "say", 2 }, { "play", 3 }, { "sleep", 4 }, { "sensor", 5 }, {"wake", 6}, {"t", 7}
	};
	uint8_t elementsTable = (sizeof(lookuptable) / sizeof(key_value));
	size_t argc = 0;
	char** ap, * argv[32], * ptrString;
	char command[10];
	ptrString = strdup(buffer);	//duplicate str
	/*explanation:
		ap = pointer to current argument
		argv = array of argv
		ptrString = duplicate of buffer string because strsep is destructive (replaces delimiter with /0)
	for(ap points to next argv; argv is set to token from strsep (via *ap which points to argv) if not null; no increment needed)
		check argument is not null, then increment pointer to next argv while ensuring not out of bounds.
		argument counter increment		*/
	for (ap = argv; (*ap = strsep(&ptrString, " \n")) != NULL;)
	{
		if (**ap != '\0')
		{
			++argc;
			if (++ap >= &argv[32])
				break;
		}
	}
	//extract command from first argument then remove from array.
	strcpy(command, argv[0]);
	char* match = strstr(argv[0], command);		//returns pointer to char where string comparison matched
	*match = '\0';
	/*for (int i = 1; i < argc; i++)
	{
		Serial.printf("argv[%d]=%s\n",i,argv[i]);
	}*/
	//string = > command. from look up table
	if (strncmp(command, "OK+", 3) == 0)		//ignore commands from the HC12.
	{
		Serial.println(command);
		free(ptrString);
		return;
	}
	switch (keyFromString(command, lookuptable, elementsTable))
	{
	case 0:	//help
		Serial.printf("\n\nList of available commands:\n");
		HC12.printf("t List of commands:\n");
		delay(100);
		for (int i = 0; i < elementsTable; i++)
		{
			HC12.printf("t %s\n", lookuptable[i].key);
			Serial.printf("%s\n", lookuptable[i].key);
			delay(100);
		}
		break;
	case 1:			//version
		Serial.printf("Version %f\n", SYS_VER);
		HC12.printf("t Version %f\n", SYS_VER);
		break;
#ifdef AUDIODEVICE
	case 2:		//say
	{
		endSAM();
		int opt;
		char* optionString = "sSPpmtvV";
		opt = getOption(argc, argv, optionString);
		while (opt != -1)
		{
			switch (opt)
			{
			case 'S':	//sing
				beginSAM();
				sam->SetSingMode(1);
				break;
			case 'P':	//phonetic
				beginSAM();
				sam->SetPhonetic(1);
				break;
			case 'p':
				if ((currentOption + 1) < argc)
				{
					beginSAM();
					sam->SetPitch((uint8_t)strtol(argv[currentOption + 1], NULL, 10));
				}
				else
				{
					HC12.printf("t Required argument not provided for pitch\n");
					Serial.printf("Required argument not provided for pitch\n"); break;
				}
				break;
			case 'm':
				if ((currentOption + 1) < argc)
				{
					beginSAM();
					sam->SetMouth((uint8_t)strtol(argv[currentOption + 1], NULL, 10));
				}
				else
				{
					HC12.printf("t Required argument not provided for mouth\n");
					Serial.printf("Required argument not provided for mouth\n"); break;
				}
				break;
			case 't':
				if ((currentOption + 1) < argc)
				{
					beginSAM();
					sam->SetThroat((uint8_t)strtol(argv[currentOption + 1], NULL, 10));
				}
				else
				{
					HC12.printf("t Required argument not provided for throat\n");
					Serial.printf("Required argument not provided for throat\n"); break;
				}
				break;
			case 'v':
				if ((currentOption + 1) < argc)
				{
					beginSAM();
					HC12.printf("t Speed set to %d\n", strtol(argv[currentOption + 1], NULL, 10));
					Serial.printf("Speed set to %d\n", strtol(argv[currentOption + 1], NULL, 10));
					delay(500);
					sam->SetSpeed((uint8_t)strtol(argv[currentOption + 1], NULL, 10));
				}
				else
				{
					HC12.printf("t Required argument not provided for speed\n");
					Serial.printf("Required argument not provided for speed\n"); break;
				}
				break;
			case 's':
			{
				clearStr(scriptToRead, 256);
				if ((currentOption + 1) < argc)
					strcpy(scriptToRead, argv[currentOption + 1]);		//first word
				else {
					HC12.printf("t No script provided\n");
					Serial.printf("No script provided\n");
				}
				size_t index = currentOption + 2;		//search for following script
				while (index < argc)
				{
					if (argv[index][0] == '-')	//found an option argument, stop searching for script
						break;
					strcat(scriptToRead, " ");	//cat whitespace (as whitespaces were removed by strsep())
					strcat(scriptToRead, argv[index]);	//concat next word in script 
					index++;
				}
				strcat(fileToLoop, "\0");
				HC12.printf("t script: %s\n", scriptToRead);
				Serial.printf("script: %s\n", scriptToRead);
			}
			break;
			case 'V':
			{
				if ((currentOption + 1) < argc)
				{
					uint8_t VOICE = (uint8_t)strtol(argv[currentOption + 1], NULL, 10);
					if (VOICE > 5) {			//5 voice settings
						Serial.println("VOICE setting out of range [0:5]");
						HC12.println("t VOICE setting out of range [0:5]");
						break;
					}
					beginSAM();
					ESP8266SAM::SAMVoice v = (ESP8266SAM::SAMVoice)VOICE;
					HC12.printf("t Voice set to %d\n", v);
					Serial.printf("Voice set to %d\n", v);
					sam->SetVoice(v);
				}
			}
			break;
			case '?':
				//"SPTpmtv"
				HC12.printf("t Unknown argument '%s'\nt List of possible commands for 'say':\nt -s script <string>\nt -V: Voice setting uint8_t [0:5]\nt -S: Sing mode enable\nt -P: Phonetic mode enable\nt -p: pitch <uint8_t>\nt -m: mouth <uint8_t>\nt -t: throat <uint8_t>\nt -v: speaking speed <uint8_t>\n", argv[currentOption]);
				Serial.printf("Unknown argument '%s'\nList of possible commands for 'say':\n-s script <string>\n-V: Voice setting uint8_t [0:5]\n-S: Sing mode enable\n-P: Phonetic mode enable\n-p: pitch <uint8_t>\n-m: mouth <uint8_t>\n-t: throat <uint8_t>\n-v: speaking speed <uint8_t>\n", argv[currentOption]);
				delay(300);
				playFile("/notify.mp3");
				break;
			}
			opt = getOption(argc, argv, optionString);
		}
		if (scriptToRead[0])
		{
			//combine non option arguments to form a string = script
			beginSAM();
			sam->Say(outSAM, scriptToRead);
		}
		else {
			HC12.println("t Cannot speak out an empty script. Try 'say -h' for list of commands.");
			Serial.println("Cannot speak out an empty script");
			playFile("/notify.mp3");
		}
	}
	break;
	case 3:		//play audio
	{
		int opt;
		opt = getOption(argc, argv, "slf");
		while (opt != -1)
		{
			switch (opt)
			{
			case 'l':	//loop
				loopAudio = 1;
				break;
			case 'f':	//file
				clearStr(fileToLoop, 255);
				if ((currentOption + 1) < argc)
				{
					strcpy(fileToLoop, argv[currentOption + 1]);
					size_t index = currentOption + 2;
					while (index < argc)		//stitch file name since strsep seperated words due to whitespaces
					{
						if (argv[index][0] == '-') break;		//exit loop (due to start of command)
						strcat(fileToLoop, " ");
						strcat(fileToLoop, argv[index]);
						index++;
					}
					strcat(fileToLoop, "\0");
				}
				break;
			case 's':
				loopAudio = 0;
				stopPlayback();
				clearStr(fileToLoop, 255);
				HC12.print("t Stopping playback.\n");
				Serial.println("Stopping playback.");
				delay(300);
				break;

			case '?':
				HC12.printf("t Unknown argument '%s'\nt List of possible commands for 'play':\nt -l: loop\nt -f: file path required\nt -s: stop playback\n", argv[currentOption]);
				Serial.printf("Unknown argument '%s'\nList of possible commands for 'play':\n-l: loop\n-f: file path required\n-s: stop playback\n", argv[currentOption]);
				clearStr(fileToLoop, 255);
				delay(300);
				playFile("/notify.mp3");
				break;
			}
			opt = getOption(argc, argv, "slf");
		}
		if (fileToLoop[0] == '\0') break;
		endSAM();
		stopPlayback();
		if (!playFile(fileToLoop)) 
		{
			HC12.printf("t File '%s' does not exist, have you forgotten root '/' ?\n", fileToLoop); 
			Serial.printf("File '%s' does not exist, have you forgotten root '/' ?\n", fileToLoop); 
		}
		else
		{
			Serial.printf("Playing '%s' audio file.\n", fileToLoop);
		}
	}
	break;
#endif
	case 4:		//sleep
	{
		//broadcast sleep command
		Serial.println("Broadcasting sleep mode.");
		char sleepCommand[] = "AT+SLEEP";
		HC12.print(sleepCommand);
		HC12.print('\n');
		Serial.println("Entering sleep mode.");
		//then enter sleep mode
		processCommand(sleepCommand);
	}
		break;
	case 6:	//wake module
		Serial.println("Waking up from sleep mode.");
		//switch  mode to command
		digitalWrite(SET, COMMAND);
		delay(50);	//wait for wakeup
		//switch back to normal operation mode
		digitalWrite(SET, TRANSMIT);
		delay(250);	//wait for initalization
		Serial.println("Module is ready.");
		HC12.println("t Module is ready.");
		break;
	case 5:		//sensor
	{
		bool readT = false, readP = false, synthesize = false;
		float pressure;
		float tempCelius;
		//prepare sensors
#if defined(SENSOR_BMP)
		bmp.getPressure(pressure);
		bmp.getTemperature(tempCelius);
		char *optionString = "stpal";
#else
		char* optionString = "e";
#endif
		int opt;
		opt = getOption(argc, argv, optionString);
		while (opt != -1)
		{
			switch (opt)
			{
			case 't':	//return temperature in celsius
				HC12.printf("t Temperature: %.3f C\n", tempCelius);
				Serial.printf("Temperature: %.3f C\n", tempCelius);
				readT = true;
				break;
			case 'p':	//return pressure 
				HC12.printf("t Pressure: %.2f Pascals\n", pressure);
				Serial.printf("Pressure: %.2f Pascals\n", pressure);
				readP = true;
				break;
			case 'a':	//return all sensor values
				HC12.printf("t Temperature: %.3f C\n", tempCelius);
				HC12.printf("t Pressure: %.3f Pascals\n", pressure);
				Serial.printf("Temperature: %.3f C\n", tempCelius);
				Serial.printf("Pressure: %.3f Pascals\n", pressure);
				break;
			case 'l':	//return list of sensors.
				HC12.printf("t List of sensors:\nt -t Temperature (Celsius)\nt -p Pressure (Pascals)\n");
				Serial.printf("List of sensors:\n-t Temperature (Celsius)\n-p Pressure (Pascals)\n");
				delay(300);
				break;
#if defined(AUDIODEVICE)
			case 's':	//get synthesizer to read out sensors
				endSAM();
				synthesize = true;
				break;
#endif
			case '?':
				HC12.printf("t Unknown argument '%s'. List of possible commands for 'sensor':\nt -t: Temperature\nt -p: Pressure\nt -a: Returns all installed sensor values\nt -s: Use synthesizer (if avail) to read out selected sensor values\nt -l: List of sensors available.\n", argv[currentOption]);
				Serial.printf("Unknown argument '%s'\nList of possible commands for 'sensor':\n-t: Temperature\n-p: Pressure\n-a: Returns all installed sensor values\n-s: Use synthesizer (if avail) to read out selected sensor values\n-l: List of sensors available.\n", argv[currentOption]);
				delay(300);
#if defined(AUDIODEVICE)
				playFile("/notify.mp3");
#endif
				break;
			}
			opt = getOption(argc, argv, optionString);
		}
#if defined(AUDIODEVICE)
		if (synthesize)
		{
			beginSAM();
			char readSensors[80];
			if (readT) snprintf(readSensors, 40, "Temperature is %.1f degrees Celsius. ", tempCelius);
			if (readP)
			{
				char buffer[40];
				snprintf(buffer, 40, "Pressure is %.1f pascals. ", pressure);
				strcat(readSensors, buffer);
			}
			if (!readT && !readP)
			{
				strcpy(readSensors, "t Please select a sensor to read values from.");
				HC12.println(readSensors);
				Serial.println(readSensors);
			}
			sam->Say(outSAM, readSensors);
		}
#endif
	}
	break;
	case 7:	//displays text
		//remove the -t command and print out the received string.
		memmove(HC12ReadBuffer, HC12ReadBuffer + 2, strlen(HC12ReadBuffer)-2);
		Serial.println(HC12ReadBuffer);
		break;
	case 255:
		HC12.printf("t Unrecognized command '%s', use help for list of commands.\n", command);
		Serial.printf("Unrecognized command '%s', use help for list of commands.\n", command);
	//default:
	//	//return error
	//	HC12.printf("t Unrecognized command '%s', use help for list of commands.\n", command);
	//	Serial.printf("Unrecognized command '%s', use help for list of commands.\n", command);
	}

	free(ptrString);		//duplicate input buffer needs to be freed.
}

int getOption(size_t argCounter, char ** arguments, char * options)
{
	static int index = 0;
	
	//reached end 
	if (index >= argCounter) {
		index = 0;		//reset counters
		currentOption = 0;
		return -1;
	}                                                                                                              

	if (arguments[index][0] == '-')
	{
		char* p = options;
		while (*p)
		{
			if (*p == arguments[index][1])	//check if option exists in string 
			{
				currentOption = (size_t)index;
				index++;
				return (char)*p;
			}
			*p++;
			delay(0);
		}
		currentOption = (size_t)index;
		index++;
		return '?';	//argument not recognised
	}

	currentOption = (size_t)index;
	index++;	//carry on with next argument
	return 0;	//not an argument
}