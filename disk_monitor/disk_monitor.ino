/*

  The circuit:
  - Fan signal wire attached to Pin 2 via 10K pull-up resistor
  - LED attached from pin 13 to ground (built-in)
  - 400 nF capacitor to provide a 2.8ms delay (http://ladyada.net/library/rccalc.html)

  When tested, the fan data wire grounded for approx 15ms then opened for another ~15ms.
  Each cycle represents half a revolution and 60ms per rev is 1000RPM.
  Oscilloscope picture: https://photos.app.goo.gl/hVEmkFpTDJfzZD4g9

   Assumptions:
    * At least one revolution per print interval
    * A revolution will take less than 71 minutes (UINT32_MAX is micros() overflow window)
    * There will be less than UINT32_MAX fan interrupts per period (and RPM won't overflow DBL_MAX)
*/

// https://en.wikipedia.org/wiki/C_data_types

// Constants
const byte fanPin = 2;
const byte ledPin = LED_BUILTIN;
const uint32_t interval = 5 * 1e6;  // Print interval, converted to microseconds
const byte cyclesPerRev = 1;
// Required ticks to extrapolate to partial revolutions. Minimum is 2 for one interval.
const byte minTicksForExtrapolation = 2;

// Global variables
uint32_t end;           // Interval end time, beginning of output
uint32_t start;         // Start of previous interval

// Volatile variables may change without any action being taken by the surrounding code (eg ISR)
// https://barrgroup.com/Embedded-Systems/How-To/C-Volatile-Keyword
volatile uint32_t ISRTicks = 0;        // Number of transitions of fanPin per interval
volatile uint32_t firstTickDelay;      // Microseconds from start to first of fanTicks this period
volatile uint32_t lastTick;            // Time of last tick (us). First interval RPM will be slightly high as history is unavailable


void setup() {
  // initialize the button pin as a input:
  pinMode(fanPin, INPUT_PULLUP);
  // initialize the LED as an output:
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, HIGH);

  // Initialize serial communication
  Serial.begin(115200);
  while (!Serial); // Wait until port is open (required for ATmega32U4)

  // Interrupt handler for fanPin
  // Clear flag if interrupt has already occured causing handler to run immediately
  // http://www.gammon.com.au/interrupts
  EIFR = bit(digitalPinToInterrupt(fanPin));
  // https://www.arduino.cc/reference/en/language/functions/external-interrupts/attachinterrupt/
  attachInterrupt(digitalPinToInterrupt(fanPin), fanISR, CHANGE);

  Serial.println("Beginning...");
  
  lastTick = start = micros();  // Zero "previous" interval
}


void loop() {
  uint32_t ticks;                // fanTicks this interval
  uint32_t preTick;              // Time from interval start to first tick
  uint32_t postTick;             // Time from last tick to interval end
  uint32_t now;                  // Current time in microseconds

  double avgTickPeriod = 0;      // Average time period between ticks
  double cycles = 0;             // Extrapolated cycles incl. pre and post partial periods
  double RPM = 0;                // Revs per minute

  // Loop collecting interrupts if it's not yet time to start a new interval
  // Assign and compare for more precise interval end timing later
  while ( (now = micros()) - start < interval) {
    // Handle interrupts
  }

  // New time interval starts now, calculations are based on the past interval

  // Critical section - keep it short.  Copy / update all interrupt handler variables.
  noInterrupts();  // Elided interrupts will appear on reactivation
  end = now;

  ticks = ISRTicks;
  preTick  = firstTickDelay;
  postTick = end - lastTick;
  ISRTicks = 0;
  interrupts();

  if (!ticks) {
    // No ticks, so no pre or post period
    preTick  = 0;
    postTick = 0;
  }

  Serial.print("  Prex: ");
  Serial.print(preTick / 1000.0);

  Serial.print("  Postx: ");
  Serial.print(postTick / 1000.0);

  // Count fractional cycles if there are enough cycles
  if (ticks >= minTicksForExtrapolation) {
//    uint32_t measuredPeriod XXX
    avgTickPeriod = (end - start - preTick - postTick) / (ticks-1);  // -1 as two ticks define one period

    // Extrapolate over maximum one average period
    preTick  = preTick  > avgTickPeriod ? avgTickPeriod : preTick;
    postTick = postTick > avgTickPeriod ? avgTickPeriod : postTick;

    cycles = ticks-1 + (preTick + postTick) / avgTickPeriod;

  } else if (ticks >= 1) {
    cycles = ticks - 1;  // One tick does not a time period make
  } else {
    cycles = 0;
  }

  cycles /= cyclesPerRev;  // Allow for multiple ticks per revolution

  RPM = cycles * 60 * 1e6 / interval;

  Serial.print("Interval: ");
  Serial.print((end - start) / 1000.0);

  Serial.print("  Ticks: ");
  Serial.print(ticks);

  Serial.print("  Period: ");
  Serial.print(avgTickPeriod);

  Serial.print("  Cycles: ");
  Serial.print(cycles);

  Serial.print("  Pre: ");
  Serial.print(preTick / 1000.0);

  Serial.print("  Post: ");
  Serial.print(postTick / 1000.0);

  Serial.print("  RPM: ");
  Serial.println(RPM);

  start = end;  // Prepare to begin a new interval
}


// https://www.arduino.cc/reference/en/language/functions/external-interrupts/attachinterrupt/
// Inside the attached function, delay() won’t work and the value returned by millis() will not increment.
// Serial data received while in the function may be lost.
// You should declare as volatile any variables that you modify within the attached function
void fanISR() {
  uint32_t now = micros();  // micros() will behave erratically after 1-2ms inside ISR

  if (now - lastTick < 400 * 1000) return;   // XXX XXX Testing only

  if (!ISRTicks) {
    firstTickDelay = now - end;
  }
  ++ISRTicks;
  lastTick = now;
  /* delayMicroseconds(3000);  // Prevent bounce */
  /* ledFlash(5); */

  static uint32_t lastISR = 0;
  Serial.print("Time since last ISR: ");
  Serial.println(now - lastISR);
  lastISR = now;
}


// Set the LED on for duration specified in milliseconds, then off
void ledFlash(uint16_t duration) {
  digitalWrite(ledPin, HIGH);
  // Use delayMicroseconds() as it doesn't disable interrupts nor use any counter
  delayMicroseconds((uint32_t) duration * 1000);
  digitalWrite(ledPin, LOW);
}
