/*

AC Dim Switch with 2 switches and 1 dimmer

Inspired from:

AC Voltage dimmer with Zero cross detection
Author: Charith Fernanado http://www.inmojo.com
License: Creative Commons Attribution Share-Alike 3.0 License.

Attach the Zero cross pin of the module to Arduino External Interrupt pin
Select the correct Interrupt # from the below table:
(the Pin numbers are digital pins, NOT physical pins:
digital pin 2 [INT0]=physical pin 4
and digital pin 3 [INT1]= physical pin 5)

Pin    |  Interrupt # | Arduino Platform
---------------------------------------
2      |  0            |  All
3      |  1            |  All
18     |  5            |  Arduino Mega Only
19     |  4            |  Arduino Mega Only
20     |  3            |  Arduino Mega Only
21     |  2            |  Arduino Mega Only

In the program pin 2 is chosen

*/

#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <EepromUtil.h>
#include <ESPConfig.h>
#include <ESP8266Controller.h>
#include "ACDimmer.h"
#include "ACSwitch.h"

/*

Firing angle calculation : 1 full 50Hz wave =1/50=20ms
Every zero crossing thus: (50Hz)-> 10ms (1/2 Cycle). For 60Hz => 8.33ms (10.000/120)
Half wave duration (50Hz): 10ms = 10,000us

STEPS for dimming:
1. Wait for the zero crossing
2. Wait a specific time between 0 and 10,000 microseconds (based on dimming level)
3. Switch ON TRIAC
4. Wait for about 2-10us (that is the time you need to make sure the TRIAC is ON)
5. Switch OFF TRIAC (TRIAC will stay ON till the next zero crossing)

Setup timer1 to trigger ISR every dimming period (0-10,000)
TIM_DIV1 = 0,   @80MHz (80 ticks/us - 104857.588 us max)
TIM_DIV16 = 1,  @5MHz (5 ticks/us - 1677721.4 us max)
TIM_DIV256 = 3  @312.5Khz (1 tick = 3.2us - 26843542.4 us max)
TIM_SINGLE, TIM_LOOP

*/

#define ZERO_CROSS_PIN	3
#define AC_SWITCH_PIN	2
#define AC_DIMMER_PIN	0

#define DEBOUNCE_MICROSECONDS           9800	//9.8ms - 10ms is the pulse period for AC @50Hz. Increasing this value towards 10ms introduced flicker
#define TRIAC_TRANSITION_MICROSECONDS   3		//3us - minimum time to turn the triac ON before turning OFF
#define TICKS_PER_MICROSECONDS          5		//CPU ticks per microseconds for TIM_DIV16

ESPConfig configuration(/*controller name*/	"AC DIM SWITCH", /*location*/ "Unknown", /*firmware version*/ "acds.200920.bin", /*router SSID*/ "onion", /*router SSID key*/ "242374666");
WiFiUDP Udp;

byte packetBuffer[255 * 3];
byte replyBuffer[255 * 3];
short replyBufferSize = 0;

// variables used both inside and outside an ISR should be volatile
static uint32_t lastPulse = 0;
static uint16_t stepp    = 0;				//defines the dimming level, max value = STEPS
static uint32_t multiplier = HALF_CYCLE_MICROSECONDS/STEPS;// (stepp * multiplier) = TRIAC OFF period in microseconds

ACSwitch sswitch("SWITCH", AC_SWITCH_PIN, 2/*Capabilities*/, configuration.sizeOfEEPROM());
ACDimmer dimmer("DIMMER", AC_DIMMER_PIN, 4/*Capabilities*/, configuration.sizeOfEEPROM() + sswitch.sizeOfEEPROM());

void ICACHE_RAM_ATTR onTimerISR() {
	if (digitalRead(dimmer.pin) == __ON) {
		digitalWrite(dimmer.pin, __OFF);
	} else {
		digitalWrite(dimmer.pin, __ON);
		timer1_write(TRIAC_TRANSITION_MICROSECONDS * TICKS_PER_MICROSECONDS);
	}
}

void ICACHE_RAM_ATTR onPinISR() {

	uint32_t now = micros();

	if (now - lastPulse < DEBOUNCE_MICROSECONDS) {
		return;
	}

	lastPulse = now;

	//digitalWrite(dimmer.pin, __OFF);
	if (dimmer.capabilities[0]._value  == 1/*OnOff==On*/ && dimmer.capabilities[1]._value < STEPS && dimmer.capabilities[1]._value > 0 && dimmer.pinState == __ON) {

		/*if(dimmer.capabilities[2]._value > 0) {
			// if fading, do not cut boundaries (-+10%)
			stepp = STEPS - dimmer.fadeState;

		} else {*/
		// if NOT fading, cut boundaries (-+10% from left/right of the sine wave to avoid flickering at ends)
		if((STEPS - dimmer.fadeState) < STEPS/10) {
			stepp = STEPS/10;
		} /*else if((STEPS - dimmer.fadeState) > STEPS - STEPS/10) {
				stepp = STEPS - (STEPS/10);
			}*/ else {
			stepp = STEPS - dimmer.fadeState;
		}
		//}

		timer1_write(stepp * multiplier * TICKS_PER_MICROSECONDS);
	}
}

void setup() {

	//delay(2000);
	Serial.begin(115200);//,SERIAL_8N1,SERIAL_TX_ONLY);

	// set zero cross pin
	pinMode(ZERO_CROSS_PIN, INPUT_PULLUP);

	digitalWrite(dimmer.pin, __OFF);
	digitalWrite(sswitch.pin, __OFF);

	DEBUG_PRINT("digitalPinToInterrupt(ZERO_CROSS_PIN) "); DEBUG_PRINTLN(digitalPinToInterrupt(ZERO_CROSS_PIN));

	configuration.init(dimmer.pin);

	// load controller capabilities (values) from EEPROM
	sswitch.loadCapabilities();
	dimmer.loadCapabilities();

	Udp.begin(port);

	timer1_isr_init();
	timer1_attachInterrupt(onTimerISR);
	timer1_enable(TIM_DIV16, TIM_EDGE, TIM_SINGLE);

	attachInterrupt(digitalPinToInterrupt(ZERO_CROSS_PIN), onPinISR, RISING);

}

unsigned long last = 0;
int packetSize = 0;
_udp_packet udpPacket;
int heap_print_interval = 10000;

void loop() {

	/*if (millis() - last > heap_print_interval) {
		DEBUG_PRINTLN();

		last = millis();
		DEBUG_PRINT("[MAIN] Free heap: ");
		Serial.println(ESP.getFreeHeap(), DEC);

	}*/

	packetSize = Udp.parsePacket();

	if (packetSize) {

		DEBUG_PRINTLN();
		DEBUG_PRINT("Received packet of size "); DEBUG_PRINT(packetSize); DEBUG_PRINT(" from "); DEBUG_PRINT(Udp.remoteIP()); DEBUG_PRINT(", port "); DEBUG_PRINTLN(Udp.remotePort());

		// read the packet into packetBuffer
		//int packetLength = Udp.read(packetBuffer, packetSize);
		Udp.read(packetBuffer, packetSize);
		//if (packetLength > 0) {
		packetBuffer[packetSize] = 0;
		//}

		// initialize the replyBuffer
		memcpy(replyBuffer, packetBuffer, 3);

		// prepare the UDP header from buffer
		//short _size = packetBuffer[1] << 8 | packetBuffer[0];

		udpPacket._size = packetBuffer[1] << 8 | packetBuffer[0];
		//udpPacket._size = _size;
		udpPacket._command = packetBuffer[2];
		udpPacket._payload = (char*)packetBuffer + 3;

		if (udpPacket._command == DEVICE_COMMAND_DISCOVER) {

			replyBufferSize = 3 + configuration.discover(replyBuffer+3);

		} else if (udpPacket._command == DEVICE_COMMAND_SET_CONFIGURATION) {

			replyBufferSize = 3 + configuration.set(replyBuffer+3, (byte*)udpPacket._payload);

		} else if (udpPacket._command == DEVICE_COMMAND_GET_CONTROLLER) {

			byte _pin = udpPacket._payload[0];

			if (_pin == sswitch.pin) {

				memcpy(replyBuffer + 3, sswitch.toByteArray(), sswitch.sizeOfUDPPayload());
				replyBufferSize = 3 + sswitch.sizeOfUDPPayload();

			} else if (_pin == dimmer.pin) {

				memcpy(replyBuffer + 3, dimmer.toByteArray(), dimmer.sizeOfUDPPayload());
				replyBufferSize = 3 + dimmer.sizeOfUDPPayload();

			}

		} else if (udpPacket._command == DEVICE_COMMAND_SET_CONTROLLER) {

			byte _pin = udpPacket._payload[0];

			// (OVERRIDE) send 3 bytes (size, command) as reply to client
			replyBufferSize = 3;

			if (_pin == sswitch.pin) {

				sswitch.fromByteArray((byte*)udpPacket._payload);

			} else if (_pin == dimmer.pin) {

				dimmer.fromByteArray((byte*)udpPacket._payload);

			}

		} else if (udpPacket._command == DEVICE_COMMAND_GETALL_CONTROLLER) {

			memcpy(replyBuffer + 3, sswitch.toByteArray(), sswitch.sizeOfUDPPayload());
			memcpy(replyBuffer + 3 + sswitch.sizeOfUDPPayload(), dimmer.toByteArray(), dimmer.sizeOfUDPPayload());

			replyBufferSize = 3 + sswitch.sizeOfUDPPayload() + dimmer.sizeOfUDPPayload();

		} else if (udpPacket._command == DEVICE_COMMAND_SETALL_CONTROLLER) {

			int i = 0;

			// update the LED variables with new values
			for (int count = 0; count < 3; count++) {

				if (udpPacket._payload[i] == sswitch.pin) {

					sswitch.fromByteArray((byte*)udpPacket._payload + i);

					i = i + sswitch.sizeOfEEPROM();

				} else if (udpPacket._payload[i] == dimmer.pin) {

					dimmer.fromByteArray((byte*)udpPacket._payload + i);

					i = i + dimmer.sizeOfEEPROM();

				}
			}

			// (OVERRIDE) send 3 bytes (size, command) as reply to client
			replyBufferSize = 3;

		}

		// update the size of replyBuffer in packet bytes
		replyBuffer[0] = lowByte(replyBufferSize);
		replyBuffer[1] = highByte(replyBufferSize);

		// send a reply, to the IP address and port that sent us the packet we received
		Udp.beginPacket(Udp.remoteIP(), Udp.remotePort());
		Udp.write(replyBuffer, replyBufferSize);
		Udp.endPacket();
	}

	sswitch.loop();
	dimmer.loop();

	yield();
}
