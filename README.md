

This sofware was written in Visual Studio however it can be ran from the Arduino IDE.
All the code is contained within "esp12_si4463.ino" file.
One can open this file in the Arduino IDE however I strongly recommend using a text editor like Sublime
and making sure the language is set to C++.
There is a camera recording of the project under "ESP Radio Project.MTS" showcasing the features of the software.

Designed and compiled to use with ESP8266. 

Library dependencies:
i2c_BMP280	pressure and temp sensor
ESP8266Audio	audio
ESP8266Spiram	reading spi flash memory
ESP8266SAM 	text to speech
SoftwareSerial 	connecting to radio module SI4463 (HC12)

Key features:
	Long range radio communication between ESPs using SI4463 (HC12) module.
	Data collection of available sensors, this version was compiled for use with BMP280 Pressure and Temperature Sensor. 
	Remote audio playback of mp3s in the file system of the ESP. Although the DAC audio quality is not great.
	Remote speech synthesis of text transmitted via radio.
	Read out sensor values using Speech-to-Text. 
	Execute remote AT commands on the ESP, such as put radio in SLEEP mode or Frequency hopping. Switch every ESP's frequency/transmitting power on the network with one command. 