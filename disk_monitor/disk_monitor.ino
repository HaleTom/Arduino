/*

  The circuit:
  - https://photos.app.goo.gl/rDH59h3GBUiFratk6  however, change:
  - Fan signal wire attached to Pin 2 (built in pull-up)
  - AM2302 (DHT22) (http://akizukidenshi.com/download/ds/aosong/AM2302.pdf)
    attached to pin 0 with 10K pull up resistor
    Next time use more accurate Bosch BME280 or BME680
  - LED attached from pin 13 to ground (built-in)

  Digital IO threshold values:  LOW < 0.9V,  HIGH > 1.9V
   - My question: https://arduino.stackexchange.com/q/61447/53509
   - Forum answer: http://forum.arduino.cc/index.php?topic=149464.msg1122942#msg1122942

  Debounce: (Implemented in software due to component and time constraints)
  - Software debounce could be more accurate given cap discharge curve
  - Time to discharge calculator: http://ladyada.net/library/rccalc.html
  - Circuit references:
    http://www.labbookpages.co.uk/electronics/debounce.html (best circuit)
    http://www.ganssle.com/debouncing-pt2.htm 
  - Better to use additonal diode to have 0.7V drop over R2 and more equal rise / fall times
  - Also (more generally about switches): http://www.gammon.com.au/switches

  Fan:
    When tested, the fan data wire grounded for approx 15ms then opened for another ~15ms.
    Each cycle represents half a revolution and 60ms per rev is 1000RPM.
    Oscilloscope picture: https://photos.app.goo.gl/hVEmkFpTDJfzZD4g9

  Assumptions:
   * A revolution will take less than 71 minutes (UINT32_MAX is micros() overflow window)
   * There will be less than UINT32_MAX fan interrupts per period (and RPM won't overflow DBL_MAX)
*/


#include "DHT.h"  // https://github.com/markruys/arduino-DHT

// https://en.wikipedia.org/wiki/C_data_types

// Constants
const byte dhtPin = 2;
const byte fanPin = 0;
const byte ledPin = LED_BUILTIN;
const byte intervalsPerRev = 4;
// Ticks for extrapolating to partial revolutions. Minimum is 2 for one interval.
const byte minTicksForExtrapolation = 2;

const uint16_t ledBlinkDuration = 1;                 // MINIMUM Microseconds, but determined by loop duration
const uint16_t ticksPerBlink = 1 * intervalsPerRev;  // 1 tick per revolution

// Microseconds (excl included ISR call overheads) to ignore switch bounce.
// 60 was sufficient. Period is ~14500 @ 1000RPM
const uint32_t bouncePeriod = 2000;
const uint32_t interval = 1 * 1e6;   // Print interval, converted to microseconds

// Global variables
DHT dht;                     // Object for AM2302 (packaged DHT22)
uint32_t start;              // Start of interval of interest
uint32_t end;                // Interval end time, beginning of output

// Volatile variables may change without any action being taken by the surrounding code (eg ISR)
// https://barrgroup.com/Embedded-Systems/How-To/C-Volatile-Keyword
volatile uint32_t ISRTicks = 0;        // Number of transitions of fanPin per interval
volatile uint32_t firstTickDelay;      // Microseconds from start to first of fanTicks this period
volatile uint32_t lastTick;            // Time of last tick (us). First interval RPM will be slightly high as history is unavailable


void setup() {
  // Initialize the fan pin as a input with internal pull-up
  pinMode(fanPin, INPUT_PULLUP);

  // Initialise DHT22 pin. External pull-up required as library doesn't use _PULLUP
  dht.setup(dhtPin, DHT::DHT22);

  // Initialize the LED as an output:
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, HIGH); // Show we are alive

  // Initialize serial communication
  Serial.begin(2e6);  // 2M baud over USB
  while (!Serial);    // Wait until port is open (required for ATmega32U4)

//  Serial.println("Beginning...");

  // Interrupt handler for fanPin
  // Clear flag if interrupt has already occured causing handler to run immediately
  // http://www.gammon.com.au/interrupts
  EIFR = bit(digitalPinToInterrupt(fanPin));
  // https://www.arduino.cc/reference/en/language/functions/external-interrupts/attachinterrupt/
  attachInterrupt(digitalPinToInterrupt(fanPin), fanISR, CHANGE);

  lastTick = start = end = micros();  // Zero counters
}


// Blink LED every TicksPerBlink while waiting for the end of the _already_started_ interval
// Copy the fan ISR statistics at interval completion;  start a new interval
// Calculate RPM based on full and partial tick periods
// Read temperature and humidity; print output
void loop() {
  static uint32_t ledTicks = 0;    // Doesn't reset to 0 on new time interval
  static uint32_t ledOffTime = 0;  // Time at which LED should be off

  uint32_t ticks = 0;              // fanTicks this interval
  uint32_t preTick;                // Time from interval start to first tick
  uint32_t postTick;               // Time from last tick to interval end
  uint32_t now;                    // Current time in microseconds

  double avgTickPeriod = 0;        // Average time period between ticks
  double cycles = 0;               // Extrapolated cycles incl. pre and post partial periods
  double RPM = 0;                  // Revs per minute


  // Loop collecting interrupts if it's not yet time to start a new interval
  // Blink LED on each interrupt while waiting for the next output time
  // Assign and compare for more precise interval end timing
  while ( (now = micros()) - start < interval) {
    // Turn off LED if it shouldn't be on
    if (now - ledOffTime >= 0) {
      digitalWrite(ledPin, LOW);
    }

    // Check to see if another tick has occurred
    noInterrupts(); // Elided interrupts will appear on reactivation
    if (ticks != ISRTicks) {  // New tick occurred
      ledTicks += 1;
    }
    ticks = ISRTicks;
    interrupts();

    // Turn on LED every TicksPerBlink
    if (ledTicks % ticksPerBlink == 0) {
      digitalWrite(ledPin, HIGH);
      ledOffTime = now + ledBlinkDuration;
      ledTicks = 0; // Avoid overflow
    }
  }

  // Close off the completed interval and copy its statistics
  noInterrupts();
  end = now;
  ticks = ISRTicks;
  preTick  = firstTickDelay;
  postTick = end - lastTick;
  ISRTicks = 0;  // Zero ISR tick counter to start a new interval
  interrupts();

  // ISR will continue with new interval accounting in the background

  if (!ticks) {  // No ticks, so no pre or post period
    preTick  = 0;
    postTick = 0;
  }

  // Count fractional cycles if there are enough cycles
  if (ticks >= minTicksForExtrapolation) {
    avgTickPeriod = (end - start - preTick - postTick) / (double) (ticks-1);  // -1 as two ticks define one period

    // Extrapolate over maximum one average period.  TODO allow a fudge factor here?
    preTick  = preTick  > avgTickPeriod ? avgTickPeriod : preTick;
    postTick = postTick > avgTickPeriod ? avgTickPeriod : postTick;

    cycles = ticks-1 + (preTick + postTick) / avgTickPeriod;
  } else if (ticks >= 1) {
    cycles = ticks - 1;  // One tick does not a time period make
  } else {  // Catch-all
    cycles = 0;
  }

  cycles /= intervalsPerRev;  // Allow for multiple ticks per revolution
  RPM = cycles * 60 * 1e6 / interval;  // 1e6 is a double literal, so no type cast needed

  // Reading DHT22 must be done with interrupts enabled as it uses millis() internally??
  // https://arduino.stackexchange.com/questions/61567/what-functions-are-disabled-with-nointerrupts
  // Readings may be up to 2000ms old
  float humidity = dht.getHumidity();
  float temperature = dht.getTemperature();
  char *dhtStatus = dht.getStatusString();

  // Output
  const char floatStrLen = 10;       // Length of a formatted float. 9 characters and '\0'
  char floatStr[floatStrLen];     // For formatting floats for output

  // Format float as string  https://arduino.stackexchange.com/a/53719/53509
  dtostrf(RPM, 6, 1, floatStr); //  Format: 6 output chars, 1 decimal place, right align
  // Remove trailing spaces.  https://arduino.stackexchange.com/a/53719/53509
  //  char *space = strchr(floatStr, ' ');
  //  if (space != NULL) *space = '\0';

  Serial.print("RPM: ");
//  dtostrf(RPM, 6, 1, floatString);
//  Serial.print();
  Serial.print(floatStr);

  Serial.print("  Temp: ");
  Serial.print(temperature, 1);
  Serial.print("  Humid: ");
  Serial.print(humidity, 1);
  Serial.print("  DHT22: ");
  Serial.print(dhtStatus);

  Serial.print("  Interval: ");
  Serial.print(end - start);

  Serial.print("  Ticks: ");
  Serial.print(ticks);

  Serial.print("  Pre: ");
  Serial.print(preTick);

  Serial.print("  Post: ");
  Serial.print(postTick);

  Serial.print("  Period: ");
  Serial.print(avgTickPeriod);

  Serial.print("  Cycles: ");
  Serial.println(cycles);

  start = end;  // Loop begins in an already started interval
}


// https://www.arduino.cc/reference/en/language/functions/external-interrupts/attachinterrupt/
// Inside the attached function, delay() won’t work and the value returned by millis() will not increment.
// Serial data received while in the function may be lost.
// You should declare as volatile any variables that you modify within the attached function
void fanISR() {
  uint32_t now = micros();  // micros() will behave erratically after 1-2ms inside ISR says attachInterrupt() doco

  if (now - lastTick < bouncePeriod) {
//    if (now - lastTick > 60) {
//      Serial.print("Bounce: ");
//      Serial.println(now - lastTick);
//    }
    return;  // Switch has bounced
  }

  if (!ISRTicks) {
    firstTickDelay = now - end;
  }

  ++ISRTicks;
  lastTick = now;
  }
