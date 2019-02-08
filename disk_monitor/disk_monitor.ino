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
const uint32_t interval = 5 * 1e6;  // Print interval, in microseconds
const byte ticksPerRev = 2;

// Global variables
uint32_t end;           // Interval end time, beginning of output
uint32_t prevStart;     // Start of previous interval

// Volatile variables may change without any action being taken by the surrounding code (eg ISR)
// https://barrgroup.com/Embedded-Systems/How-To/C-Volatile-Keyword
volatile uint32_t ISRTicks = 0;        // Number of transitions to fanPin LOW per interval  XXXXXXXXXXXXX
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

  lastTick = prevStart = micros();  // Zero "previous" interval
}


void loop() {
  double RPM;                    // Revs per minute of last time period
  double ticks;                  // fanTicks this interval
  uint32_t prePartial;           // Time from interval start to first tick
  uint32_t postPartial;          // Time from last tick to interval end
  uint32_t now;                  // Current time in microseconds

  Serial.print("Start: ");
  Serial.print(prevStart);

  // Loop collecting interrupts if it's not yet time to start a new interval
  // Assign and compare for more precise interval end timing later
  while ( (now = micros()) - prevStart < interval) {
    // Handle interrupts
  }

  // New time interval and interrupt counting starts now, calculations are based on the completed interval
  noInterrupts();  // Elided interrupts will appear on reactivation
  end = now;

  // Copy / update all interrupt handler variables
  ticks = ISRTicks;
  if (ticks) {
    prePartial  = firstTickDelay;
    postPartial = end - lastTick;
  } else {
    prePartial  = 0;
    postPartial = 0;
  }
  ISRTicks = 0;
  interrupts();

  Serial.print("  Ticks: ");
  Serial.print(ticks);

  Serial.print("  Pre interval: ");
  Serial.print(prePartial / 1000.0);

  Serial.print("  Post interval: ");
  Serial.print(postPartial / 1000.0);

  if (ticks > 1) {
    // We have an interval to extrapolate from
    double avgTickPeriod = (end - prevStart - prePartial - postPartial) / ticks;
    ticks += (prePartial + postPartial) / avgTickPeriod;
  }

  ticks /= ticksPerRev;  // Allow for multiple ticks per revolution

  Serial.print("  RPM: ");
  RPM = ticks * 60 * 1e6 / interval;
  Serial.println(RPM);
  prevStart = end;  // Prepare to begin a new interval
}


// https://www.arduino.cc/reference/en/language/functions/external-interrupts/attachinterrupt/
// Inside the attached function, delay() won’t work and the value returned by millis() will not increment.
// Serial data received while in the function may be lost.
// You should declare as volatile any variables that you modify within the attached function
void fanISR() {
  uint32_t now = micros();  // micros() will behave erratically after 1-2ms inside ISR

  if (now - lastTick < 200 * 1000) return;   // XXX XXX Testing only

  if (!ISRTicks) {
    firstTickDelay = now - end;
  }
  ++ISRTicks;
  lastTick = now;
  /* delayMicroseconds(3000);  // Prevent bounce */
  /* ledFlash(5); */
  /* Serial.println("ISR done"); */
}


// Set the LED on for duration specified in milliseconds, then off
void ledFlash(uint16_t duration) {
  digitalWrite(ledPin, HIGH);
  // Use delayMicroseconds() as it doesn't disable interrupts nor use any counter
  delayMicroseconds((uint32_t) duration * 1000);
  digitalWrite(ledPin, LOW);
}
