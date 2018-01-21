/*
 * BLF EE A6 firmware (special-edition group buy light)
 * This light uses a FET+1 style driver, with a FET on the main PWM channel
 * for the brightest high modes and a single 7135 chip on the secondary PWM
 * channel so we can get stable, efficient low / medium modes.  It also
 * includes a capacitor for measuring off time.
 *
 * Copyright (C) 2015 Selene Scriven
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * NANJG 105C Diagram
 *          ---
 *        -|   |- VCC
 *    OTC -|   |- Voltage ADC
 * Star 3 -|   |- PWM (FET)
 *    GND -|   |- PWM (1x7135)
 *          ---
 *
 * FUSES
 *	  I use these fuse settings
 *	  Low:  0x75  (4.8MHz CPU without 8x divider, 9.4kHz phase-correct PWM or 18.75kHz fast-PWM)
 *	  High: 0xfd  (to enable brownout detection)
 *
 *	  For more details on these settings, visit http://github.com/JCapSolutions/blf-firmware/wiki/PWM-Frequency
 *
 * VOLTAGE
 *	  Resistor values for voltage divider (reference BLF-VLD README for more info)
 *	  Reference voltage can be anywhere from 1.0 to 1.2, so this cannot be all that accurate
 *
 *		   VCC
 *			|
 *		   Vd (~.25 v drop from protection diode)
 *			|
 *		  1912 (R1 19,100 ohms)
 *			|
 *			|---- PB2 from MCU
 *			|
 *		  4701 (R2 4,700 ohms)
 *			|
 *		   GND
 *
 *   To find out what values to use, flash the driver with battcheck.hex
 *   and hook the light up to each voltage you need a value for.  This is
 *   much more reliable than attempting to calculate the values from a
 *   theoretical formula.
 *
 *   Same for off-time capacitor values.  Measure, don't guess.
 */

#include "driver.h"
#include "default_modes.h"

// counter for entering config mode
// (needs to be remembered while off, but only for up to half a second)
uint8_t fast_presses __attribute__ ((section (".noinit")));
// LOCK_MODE variable
uint8_t locked_in  __attribute__ ((section (".noinit")));

const uint8_t voltage_blinks[] = {
	ADC_0,	// 1 blink  for 0%-25%
	ADC_25,   // 2 blinks for 25%-50%
	ADC_50,   // 3 blinks for 50%-75%
	ADC_75,   // 4 blinks for 75%-100%
	ADC_100,  // 5 blinks for >100%
	255
};

// EEPROM_read/write taken from the datasheet
void EEPROM_write(uint8_t Address, uint8_t Data) {
	// Set Programming mode
	EECR = (0<<EEPM1)|(0>>EEPM0);

	// Set up address and data registers
	EEARL = Address;
	EEDR = Data;

	// Write logical one to EEMPE
	EECR |= (1<<EEMPE);

	// Start eeprom write by setting EEPE
	EECR |= (1<<EEPE);
	
	// Wait for completion of write
	while(EECR & (1<<EEPE));
}

inline uint8_t EEPROM_read(uint8_t Address) {
	// Set up address register
	EEARL = Address;
	// Start eeprom read by writing EERE
	EECR |= (1<<EERE);
	// Return data from data register
	return EEDR;
}

// Write mode index to EEPROM (with wear leveling)
uint8_t save_mode_idx(uint8_t mode_idx, uint8_t config, uint8_t eepos) {  
	// Reverse the index again if we're reversed
	if ((config & MODE_DIR) && (mode_idx < NUM_MODES)) {
		mode_idx = (NUM_MODES - 1 - mode_idx);
	}	
	EEPROM_write(eepos, 0xff);         // erase old state
	eepos = ((eepos+1) & EEPMODE); // wear leveling, use next cell
	EEPROM_write(eepos, ~mode_idx);         // save current index, flipped
	return eepos;
}

inline uint8_t restore_mode_idx() {
	uint8_t eep;
	uint8_t eepos;
	// Find the config data
	for(eepos=0; eepos<=EEPMODE; eepos++) {
		eep = ~(EEPROM_read(eepos));
		if (eep) {
			break;
		}
	}
	return eep;
}

#ifdef TEMP_CAL_MODE
void save_maxtemp(uint8_t maxtemp){
	// Save both the max temperature and config
	EEPROM_write((EEPLEN - 1), maxtemp);
}

inline uint8_t restore_maxtemp(){
	return EEPROM_read((EEPLEN - 1));
}
#endif

void save_config(uint8_t config) {
	EEPROM_write(EEPLEN, ~config);
}

inline uint8_t restore_config() {
	return ~EEPROM_read(EEPLEN);
}

inline void ADC_on(uint8_t dpin, uint8_t channel) {
	// disable digital input on ADC pin to reduce power consumption
	DIDR0 |= (1 << dpin);
	// 1.1v reference, left-adjust, ADC1/PB2
	ADMUX  = (1 << V_REF) | (1 << ADLAR) | channel; 
	// enable, start, prescale
	ADCSRA = (1 << ADEN ) | (1 << ADSC ) | ADC_PRSCL;   
	// Toss out the garbage first result
	while (ADCSRA & (1 << ADSC));
}

uint8_t get_voltage() {
	// Get the voltage for later
	ADCSRA |= (1 << ADSC);
	// Wait for completion
	while (ADCSRA & (1 << ADSC));
	return ADCH;
}

inline void set_output(uint8_t pwm1, uint8_t pwm2) {
	PWM_LVL = pwm1;
	ALT_PWM_LVL = pwm2;
}
#ifdef DEBUG
// Blink out the contents of a byte
void debug_byte(uint8_t byte) {
	uint8_t x=0;
	for (; x <= 7; x++ ) {
		set_output(0,0);
		_delay_10_ms(50);
		if (byte & (1 << (7 - x))) {
			set_output(0,200);
		} else {
			set_output(0,10);
		}
		_delay_10_ms(10);
	}
	set_output(0,0);
	_delay_s();
}
#endif
void blink(uint8_t val, uint8_t speed, uint8_t brightness) {
	ALT_PWM_LVL = 0;
	for (; val; val--) {
		PWM_LVL = brightness;
		_delay_10_ms(speed);
		PWM_LVL = 0;
		_delay_10_ms(speed); _delay_10_ms(speed);
	}
}

void emergency_shutdown(){
	// Shut down, voltage is too low.
	// Power down as many components as possible
	set_sleep_mode(SLEEP_MODE_PWR_DOWN);
	set_output(0,0);
	sleep_mode();
}
#ifdef TEMP_CAL_MODE
uint8_t get_temperature() {
	// Configure the ADC for temperature readings
	ADC_on(ADC_DIDR, TEMP_CHANNEL);
	// average a few values; temperature is noisy
	uint16_t temp = 0;
	uint8_t i;
	for(i=0; i<16; i++) {
		temp += get_voltage();
	}
	temp >>= 4;
	return temp;
}
#endif

inline void set_lock(uint8_t config) {
	if (config & LOCK_MODE) {
		_delay_10_ms(255);
		locked_in = 1;
	}
}

inline void configure_output() {
	// Set PWM pin to output
	DDRB |= (1 << PWM_PIN);	    // enable main channel
	DDRB |= (1 << ALT_PWM_PIN); // enable second channel
	TCCR0A = PHASE;             // Set timer to do PWM 
	TCCR0B = 1;              // pre-scaler for timer

	// Charge up the capacitor by setting CAP_PIN to output
	DDRB  |= (1 << CAP_PIN);	// Output
	PORTB |= (1 << CAP_PIN);	// High
}

inline uint8_t get_cap() {
	// Start up ADC for capacitor pin
	ADC_on(CAP_DIDR, CAP_CHANNEL);	
	return get_voltage();
}

uint8_t get_bat() {
	// Start up ADC for Battery pin
	ADC_on(ADC_DIDR, ADC_CHANNEL);
	return get_voltage();
}

inline uint8_t med_press(uint8_t mode_idx, uint8_t config, uint8_t i) {
	if (mode_idx >= MODE_CNT) {
		// Loop back if we've hit the end of hidden modes
		mode_idx = 0;
	} else if (mode_idx == 0) {
		// If we're at mode_idx 0, go to hidden modes
		mode_idx = NUM_MODES;
	} else if (mode_idx < NUM_MODES) {
		// Walk backwards if we're in normal modes
		mode_idx -= i;
	} else if (mode_idx >= NUM_MODES) {
		// Walk forward if we're in hidden modes
		mode_idx += 1;
	}
	return mode_idx;
}

inline uint8_t next(uint8_t mode_idx, uint8_t config, uint8_t i) {
	mode_idx += i;
	if (mode_idx >= NUM_MODES) {
		mode_idx = 0;
	}
	// Handle mode order reversal
	if (config & MODE_DIR) {
		// subtract 1 since mode_idx starts at 0
		mode_idx = (NUM_MODES - 1 - mode_idx);
	}
	return mode_idx;
}

inline uint8_t low_batt_stepdown(uint8_t mode_idx) {
	// Start off by dropping out of hidden modes to
	// TURBO_STEP_DOWN
	if (mode_idx > TURBO_STEP_DOWN) {
		mode_idx = TURBO_STEP_DOWN;
	} else {
		mode_idx--;
	}
	
	if (mode_idx == 0) {
		// If we're already at 0, save state at low and turn off
		emergency_shutdown();
	}
	return mode_idx;
}

int main(void) {
	// Read the off-time cap *first* to get the most accurate reading
	uint8_t cap_val = get_cap(); // save this for later

	// Set up output pins and charge up capacitor
	configure_output();
	
	// Get the battery voltage
	uint8_t voltage = get_bat();
	
	// If the battery is getting low, flash thrice when turning on or changing brightness
	if (voltage < ADC_0) {
		blink(3, 5, 30);
	}

	// Protect the battery if we're just starting and the voltage is too low.
	if (voltage < ADC_LOW){
		emergency_shutdown();
	}

#ifdef TEMP_CAL_MODE
	uint8_t maxtemp = restore_maxtemp();
	if (maxtemp < 79) { maxtemp = 79; }
#endif
	
	// Keep track of the eeprom position
	uint8_t eepos = 0;
	// Read config values
	uint8_t config = restore_config();
	// Wipe the config if option 6 is 1
	// or config is empty (fresh flash)
	if ((config & CONFIG_RESET) || !config) {
		config = CONFIG_DEFAULT;
		save_config(config);
	}

	// First, get the "mode group" (increment value)
	uint8_t i=1; // This is reused a lot, it's set to 2 for mode group 2 step augmentation
	if (config & MODE_GROUP) {
		i=2;
	}

	// Read saved index
	// mode_idx is the position in the mode arrays to set the output to
	uint8_t mode_idx = restore_mode_idx();

	// Manipulate index depending on config options
	if (cap_val < CAP_MED || (cap_val < CAP_SHORT && !(config & MED_PRESS))) {
		// Long press, clear fast_presses
		fast_presses = 0;
		// Reset to the first mode if memory isn't set on
		if (!(config & MEMORY)) {
			mode_idx = 0;
		}
		locked_in = 0;
	} else if (locked_in && (config & LOCK_MODE)) {
		// Do nothing
	} else if (cap_val < CAP_SHORT) {
		// User did a medium press
		mode_idx = med_press(mode_idx, config, i);
	} else {
		// We don't care what the value is as long as it's over 15
		fast_presses = (fast_presses+1) & 0x1f;
		// Indicates they did a short press, go to the next mode
		mode_idx = next(mode_idx, config, i);
	}

	// Save resultant index
	eepos = save_mode_idx(mode_idx, config, eepos);

	// Main running loop
	uint8_t ticks = 0;
	uint8_t lowbatt_overheat_cnt = 0;

	while(1) {
		voltage = get_bat();
#ifdef TEMP_CAL_MODE
		uint8_t temp = get_temperature();
		if (voltage < ADC_LOW || temp >= maxtemp) {
#else
		if (voltage < ADC_LOW) {
#endif
			lowbatt_overheat_cnt ++;
		} else {
			lowbatt_overheat_cnt = 0;
		}

		// See if the battery has been low for a while
		// or the temperature has been high for a while
		// and step down if so.
		if (lowbatt_overheat_cnt >= 8) {
			// Reset the counter
			lowbatt_overheat_cnt = 0;
			mode_idx = low_batt_stepdown(mode_idx);
			// Save the index so we don't jump back to high when
			// the user fast presses again
			eepos = save_mode_idx(mode_idx, config, eepos);
		}
		
		// Config mode
		if (fast_presses > 0x0f) {  
			_delay_s();	      // wait for user to stop fast-pressing button
			fast_presses = 0; // exit this mode after one use
			mode_idx = 0;     // Always exit at lowest mode index
			
			// Loop through each config option, toggle, blink the mode number,
			// buzz a second for user to confirm, toggle back.
			//
			// Config items:
			//
			// 1 = Mode Group
			// 2 = Mode Memory
			// 4 = Reverse Mode Order
			// 8 = Medium Press Disable
			// 16 = Mode Locking
			//
			// Each toggle's blink count will be
			// linear, so 1 blink for Mode Group,
			// 3 blinks for Reverse Mode Order,
			// 4 blinks for Medium Press.
			
			uint8_t blinks=1;
			for (i=1; i<=CONFIG_RESET; i<<=1, blinks++) {
				blink(blinks, 12, 30);
				_delay_10_ms(5);
				save_config(config ^= i);
				blink(48, 1, 20);
				save_config(config ^= i);
				_delay_s();
			}

#ifdef TEMP_CAL_MODE
			// Enter Temperature Calibration Mode
			blink(7, 12, 30);
			maxtemp = 255;
			save_maxtemp(maxtemp);
			_delay_10_ms(200);
			while (1) {
				set_output(255,0);
				maxtemp = get_temperature();
				save_maxtemp(maxtemp);
				_delay_s();
				// Blink twice every second to indicate calibration mode
				blink(2, 12, 255);
			}
#endif
		}

		uint8_t output = modesNx[mode_idx];
		switch (output) {
			case SOS:
				blink(3,10,255);
				_delay_10_ms(20);
				blink(3,20,255);
				blink(3,10,255);
				_delay_s();
				break;

			case BATTCHECK:
				// figure out how many times to blink
				for (i=0; voltage > voltage_blinks[i]; i++) {}
				// blink zero to five times to show voltage
				// (~0%, ~25%, ~50%, ~75%, ~100%, >100%)
				blink(i, 12, 30);
				// wait between readouts
				_delay_s();
				break;

			case STROBE:
			case BIKING_STROBE:
				// 10Hz strobe
				blink(4, 2, 255);
				// Break here for strobe
				if (output == STROBE) {
					break;
				}
			default:
				// Do some magic here to handle turbo step-down
				if ((output == TURBO) && (ticks > TURBO_TIMEOUT)) {
					// step down to second-highest mode
					mode_idx = TURBO_STEP_DOWN; 
					eepos = save_mode_idx(mode_idx, config, eepos);
				}
				// Regular non-hidden solid mode
				set_output(modesNx[mode_idx], modes1x[mode_idx]);
				set_lock(config);
				_delay_s();
				break;
		}
		// If we got this far, the user has stopped fast-pressing.
		// So, don't enter config mode.
		ticks++;
		fast_presses = 0;
	}
}
