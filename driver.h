

// Choose your MCU here, or in the build script
//#define ATTINY 13
//#define ATTINY 25
//#define ATTINY 85

// set some hardware-specific values...
// (while configuring this firmware, skip this section)
#if (ATTINY == 13)
#define F_CPU 4800000UL
#define EEPLEN 64
#elif (ATTINY == 25)
#define F_CPU 8000000UL
#define EEPLEN 128
#elif (ATTINY == 85)
#define F_CPU 8000000UL
#define EEPLEN 512
#else
Hey, you need to define ATTINY.
#endif

/*
 * =========================================================================
 * Settings to modify per driver
 */
//#define DEBUG

//#define FAST 0x23           // fast PWM channel 1 only
//#define PHASE 0x21          // phase-correct PWM channel 1 only
#define FAST 0xA3           // fast PWM both channels
#define PHASE 0xA1          // phase-correct PWM both channels

#define OWN_DELAY           // Should we use the built-in delay or our own?
// Adjust the timing per-driver, since the hardware has high variance
// Higher values will run slower, lower values run faster.
#if (ATTINY == 13)
#define DELAY_TWEAK         950
#elif (ATTINY == 25 || ATTINY == 85)
#define DELAY_TWEAK         2000
#endif

// WARNING: You can only have a maximum of 16 modes TOTAL
// That means NUM_MODES1 + NUM_MODES2 + NUM_HIDDEN MUST be <= 16
// Mode group 1
#define NUM_MODES1          7
// PWM levels for the big circuit (FET or Nx7135)
#define MODESNx1            0,0,0,7,56,137,255
// PWM levels for the small circuit (1x7135)
#define MODES1x1            3,20,110,255,255,255,0
// My sample:     6=0..6,  7=2..11,  8=8..21(15..32)
// Krono sample:  6=5..21, 7=17..32, 8=33..96(50..78)
// Manker2:       2=21, 3=39, 4=47, ... 6?=68
// PWM speed for each mode
//#define MODES_PWM1          PHASE,FAST,FAST,FAST,FAST,FAST,PHASE
// Mode group 2
#define NUM_MODES2          4
#define MODESNx2            0,0,90,255
#define MODES1x2            3,110,255,0
//#define MODES_PWM2          PHASE,FAST,FAST,PHASE

// Hidden modes are *before* the lowest (moon) mode
#define NUM_HIDDEN          4
#define HIDDENMODES         TURBO,STROBE,BATTCHECK,BIKING_STROBE
//#define HIDDENMODES_PWM     PHASE,PHASE,PHASE,PHASE
#define HIDDENMODES_ALT     0,0,0,0   // Zeroes, same length as NUM_HIDDEN

#define TURBO     255       // Convenience code for turbo mode
#define BATTCHECK 254       // Convenience code for battery check mode
// Uncomment to enable tactical strobe mode
#define STROBE    253       // Convenience code for strobe mode
// Uncomment to unable a 2-level stutter beacon instead of a tactical strobe
#define BIKING_STROBE 252   // Convenience code for biking strobe mode

// How many timer ticks before before dropping down.
// Each timer tick is 1s, so "30" would be a 30-second stepdown.
// Max value of 255 unless you change "ticks"
#define TURBO_TIMEOUT       60

// These values were measured using wight's "A17HYBRID-S" driver built by DBCstm.
// Your mileage may vary.
#define ADC_100         170 // the ADC value for 100% full (4.2V resting)
#define ADC_75          162 // the ADC value for 75% full (4.0V resting)
#define ADC_50          154 // the ADC value for 50% full (3.8V resting)
#define ADC_25          141 // the ADC value for 25% full (3.5V resting)
#define ADC_0           121 // the ADC value for 0% full (3.0V resting)
#define ADC_LOW         113 // When do we start ramping down (2.8V)
#define ADC_CRIT        109 // When do we shut the light off (2.7V)

// the BLF EE A6 driver may have different offtime cap values than most other drivers
// Values are between 1 and 255, and can be measured with offtime-cap.c
// These #defines are the edge boundaries, not the center of the target.
#define CAP_SHORT           230  // Anything higher than this is a short press
#define CAP_MED             160  // Between CAP_MED and CAP_SHORT is a medium press
                                 // Below CAP_MED is a long press

#define CAP_PIN     PB3
#define CAP_CHANNEL 0x03    // MUX 03 corresponds with PB3 (Star 4)
#define CAP_DIDR    ADC3D   // Digital input disable bit corresponding with PB3
#define PWM_PIN     PB1
#define ALT_PWM_PIN PB0
#define VOLTAGE_PIN PB2
#define ADC_CHANNEL 0x01    // MUX 01 corresponds with PB2
#define ADC_DIDR    ADC1D   // Digital input disable bit corresponding with PB2
#define ADC_PRSCL   0x06    // clk/64
#define PWM_LVL     OCR0B   // OCR0B is the output compare register for PB1
#define ALT_PWM_LVL OCR0A   // OCR0A is the output compare register for PB0

/*
 * =========================================================================
 */


// Required libraries 
//#include <avr/pgmspace.h>
//#include <avr/io.h>
//#include <avr/interrupt.h>
#include <avr/eeprom.h>
#include <avr/sleep.h>
//#include <avr/power.h>

#include <util/delay_basic.h>
void _delay_ms(uint16_t n)
{
    // TODO: make this take tenths of a ms instead of ms,
    // for more precise timing?
    while(n-- > 0) _delay_loop_2(DELAY_TWEAK);
}
void _delay_s()  // because it saves a bit of ROM space to do it this way
{
    _delay_ms(1000);
}

// Some driver-specific globals

#if ( ATTINY == 13 || ATTINY == 25 )
uint8_t eepos = 0;
#elif ( ATTINY == 85 )
uint16_t eepos = 0;
#endif