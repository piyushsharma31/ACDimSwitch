#ifndef ACDimmer_h
#define ACDimmer_h

#define HALF_CYCLE_MICROSECONDS			10000	//10ms is the pulse period (half cycle) for AC @50Hz
#define	STEPS							          128
short __ON = LOW;
short __OFF = HIGH;

class ACDimmer : public ESP8266Controller {

public:

	// store BULB PIN fade state; can be different from pinState when blink & fade both are ON
	// equals to current intensity
	short fadeState = capabilities[1]._value;

	// intensity increasing or decreasing step (+1 or -1)
	short fadeStep = 1;

	// store last time BULB was updated (blink or fade)
	unsigned long fadePreviousMicros = 0;
	unsigned long fadePreviousMillis = 0;
	unsigned long blinkPreviousMillis = 0;

public:
	ACDimmer(char* nam, uint8_t _pin, uint8_t capCount, int start_address):ESP8266Controller(nam, _pin, capCount, start_address)
	{
		pinMode(pin, OUTPUT);

		// On and Off switch
		strcpy(capabilities[0]._name, "ON/OFF");
		capabilities[0]._value_min = 0;
		capabilities[0]._value_max = 1;
		capabilities[0]._value  = 0;

		// Dimming level/intensity (0-128)  0 = ON, 128 = OFF
		strcpy(capabilities[1]._name, "INTENSITY");
		capabilities[1]._value_min = 0;
		capabilities[1]._value_max = STEPS;
		capabilities[1]._value  = 0;

		// Fade rate, reverse the intensity. 128 points/sec will dim from 0(low) to 128 (high) in 1 second. 64 points/sec, 32 p/s etc.
		strcpy(capabilities[2]._name, "FADE");
		capabilities[2]._value_min = 0;
		capabilities[2]._value_max = STEPS;
		capabilities[2]._value  = 0;

		// Toggle the swtich (on/off) at an interval (blinkDelay)
		strcpy(capabilities[3]._name, "BLINK");
		capabilities[3]._value_min = 0;
		capabilities[3]._value_max = 5000;
		capabilities[3]._value  = 0;

	}

public:
	virtual void loop();

};

void ACDimmer::loop() {

	// check to see if it's time to change the state of the BULB
	unsigned long currentMillis = millis();
	unsigned long currentMicros = micros();

	if ((	capabilities[0]._value  == 0/*onOff==OFF*/ || capabilities[1]._value  == 0)/*intensity==0*/	&& pinState == __ON ) {

		pinState = __OFF;//BULB should be ALWAYS OFF in these conditions
		digitalWrite(pin, pinState);
		DEBUG_PRINT(currentMillis/1000);DEBUG_PRINT(" pinState ");DEBUG_PRINT(pinState);DEBUG_PRINT(" ");toString();

	} else if (	capabilities[0]._value  == 1/*onOff==ON*/ && capabilities[1]._value > 0/*intensity==ANY*/) {

		if(	capabilities[3]._value  == 0/*blinking==OFF*/	&& capabilities[2]._value  == 0 /*fading==OFF*/) {

			if(pinState ==__OFF) {

				pinState = __ON;

				if(capabilities[1]._value == STEPS) {

					digitalWrite(pin, pinState);//BULB should be ALWAYS ON in these conditions
					DEBUG_PRINT(currentMillis/1000);DEBUG_PRINT(" pinState ");DEBUG_PRINT(pinState);DEBUG_PRINT(" ");toString();
				}

			} else if(fadeState != capabilities[1]._value) {

				fadeState = capabilities[1]._value;
				pinState = __ON;

				if(capabilities[1]._value == STEPS) {

					digitalWrite(pin, pinState);//BULB should be ALWAYS ON in these conditions
					DEBUG_PRINT(currentMillis/1000);DEBUG_PRINT(" pinState ");DEBUG_PRINT(pinState);DEBUG_PRINT(" ");toString();
				}
			}

		} else if (capabilities[3]._value  > 0 /*blinking==ON*/) {

			if(currentMillis - blinkPreviousMillis >= capabilities[3]._value) {/*blink delay over*/

				if (pinState == __OFF) {

					pinState = __ON;

					if(capabilities[2]._value  > 0) {/*fading==ON*/

						// reverse the direction of the fading at the ends of the fade:
						if (fadeState <= 1) {
							fadeStep = 1;
							//fadeStep = ((currentMicros - fadePreviousMicros) * capabilities[2]._value)/HALF_CYCLE_MICROSECONDS;
						} else if (fadeState >= capabilities[1]._value ) {
							fadeStep = -1;
							//fadeStep = -(((currentMicros - fadePreviousMicros) * capabilities[2]._value)/HALF_CYCLE_MICROSECONDS);
						}

						fadeState = fadeState + fadeStep;
						//DEBUG_PRINT(currentMillis/1000);DEBUG_PRINT(" fadState ");DEBUG_PRINT(fadeState);DEBUG_PRINT(" ");toString();
						fadePreviousMicros = currentMicros;

					} else {

						if(fadeState != capabilities[1]._value) {
							fadeState = capabilities[1]._value;
						}

						if(capabilities[1]._value == STEPS) {
							digitalWrite(pin, pinState);
							DEBUG_PRINT(currentMillis/1000);DEBUG_PRINT(" pinState ");DEBUG_PRINT(pinState);DEBUG_PRINT(" ");toString();
						}
					}
				} else if (pinState == __ON) {

					pinState = __OFF;
					digitalWrite(pin, pinState);
					DEBUG_PRINT(currentMillis/1000);DEBUG_PRINT(" pinState ");DEBUG_PRINT(pinState);DEBUG_PRINT(" ");toString();
				}

				blinkPreviousMillis = currentMillis;

			}

		} else if (	capabilities[2]._value > 0 ) {/*fading==ON*/

			if(currentMillis - fadePreviousMillis >= ((1000/capabilities[2]._value)))	{	//fade rate duration over, STEPS@128/sec
			//if(currentMicros - fadePreviousMicros >= (HALF_CYCLE_MICROSECONDS/capabilities[2]._value)) { //fade rate duration over, STEPS@10000/sec

				pinState = __ON;

				// BULB should fade @fadeRate e.g. 128 points/s completes fading in 1 sec, 64 points/s completes fading in 2 seconds, 32 point/s in 4 sec, etc.)
				// 10000 points ---------- in 1 sec (1000ms=1000000us) at every 1000000/10000=100us completes in 1 sec
				// 5000 points ----------- in 1 sec (1000ms=1000000us) at every 1000000/5000=200us completes in 2sec
				// 1000 points ----------- in 1 sec (1000ms=1000000us) at every 1000000/1000=1000us completes in 10sec

				// reverse the direction of the fading at the ends of the fade:
				if (fadeState <= 1) {// never goto zero
					fadeStep = 1;
				} else if (fadeState >= capabilities[1]._value ) {
					fadeStep = -1;
				}

				fadeState = fadeState + fadeStep;

				fadePreviousMicros = currentMicros;
				fadePreviousMillis = currentMillis;
				//DEBUG_PRINT(currentMillis/1000);DEBUG_PRINT(" fadeState ");DEBUG_PRINT(fadeState);DEBUG_PRINT(" ");toString();
			}
		}
	}

	// update EEPROM if capabilities changed
	if (currentMillis - lastEepromUpdate > eeprom_update_interval) {
		//DEBUG_PRINTLN();

		lastEepromUpdate = millis();
		//DEBUG_PRINT("[MAIN] Free heap: ");
		//Serial.println(ESP.getFreeHeap(), DEC);

		// save RGB LED status every one minute if required
		if(eepromUpdatePending == true) {

			DEBUG_PRINT("saveCapabilities pin ");DEBUG_PRINTLN(pin);
			saveCapabilities();
		}
	}
}

#endif
