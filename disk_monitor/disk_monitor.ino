/*

  The circuit:
  - https://photos.app.goo.gl/rDH59h3GBUiFratk6  however, change:
  - Fan signal wire attached to Pin 1 via 300K pull-up resistor XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
  - LED attached from pin 13 to ground (built-in)

  Digital IO threshold values:  LOW < 0.9V,  HIGH > 1.9V
   - My question: https://arduino.stackexchange.com/q/61447/53509
   - Forum answer: http://forum.arduino.cc/index.php?topic=149464.msg1122942#msg1122942

  Debounce: (Implemented in software due to component and time constraints)
  - Software debounce could be more accurate given cap discharge curve
  - Time to discharge calculator: http://ladyada.net/library/rccalc.html
  - Circuit: http://www.ganssle.com/debouncing-pt2.htm Figure 2:
    18 nF capacitor, R1=300K + R2=10K, gives 1.1ms to drop to 0.9V on switch close
  - Unsure of R2 time on switch open: 10K to 1.9V = 0.086?
  - Better to use additonal diode to have 0.7V drop over R2 and more equal rise / fall times
  - Also (more generally about switches): http://www.gammon.com.au/switches

  - Capacitor discharges to 2.5v through 10k resistor in 0.12ms

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
const byte fanPin = 1;
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
uint32_t start;              // Start of interval of interest
uint32_t end;                // Interval end time, beginning of output

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

// Wait for completion of, then print the stats for one time interval
// Blink LED every TicksPerBlink while waiting for time interval completion
// Start new interval then print out stats for the completed one
// End after the stats for one interval have been printed out
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
      ledTicks = 0; // Prevent weirdness at overflow
    }
  }

  // New time interval starts now, calculations are based on the completed interval
  // Get a copy of stats for completed interval period; Zero ISR tick counter to begin new interval.
  noInterrupts();
  end = now;

  ticks = ISRTicks;
  preTick  = firstTickDelay;
  postTick = end - lastTick;
  ISRTicks = 0;
  interrupts();

  // ISR will now continue doing new accounting in the background

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

  Serial.print("RPM: ");
  Serial.print(RPM);

  Serial.print("  Interval: ");
  Serial.print(end - start);

  Serial.print("  Ticks: ");
  Serial.print(ticks);

  Serial.print("  Period: ");
  Serial.print(avgTickPeriod);

  Serial.print("  Cycles: ");
  Serial.print(cycles);

  Serial.print("  Pre: ");
  Serial.print(preTick / 1000.0);

  Serial.print("  Post: ");
  Serial.println(postTick / 1000.0);

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
