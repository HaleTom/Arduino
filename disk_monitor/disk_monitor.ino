// Ravi: Changed the Example to work with pull-up resistor on input pin

/*
  
  The circuit:
  - pushbutton attached to pin 2 from +5V
  - 10 kilohm resistor attached to pin 2 from ground
  - LED attached from pin 13 to ground (or use the built-in LED on most
    Arduino boards)

*/

// Constants
const int fanPin = 2;
const int ledPin = LED_BUILTIN;
const int interval = 1000; // milliseconds

// Volatile variables may change without any action being taken by the surrounding code
// https://barrgroup.com/Embedded-Systems/How-To/C-Volatile-Keyword
volatile long fan_edges = 0;

// Variables will change:
int buttonPushCounter = 0;   // counter for the number of button presses
int buttonState = 0;         // current state of the button
int lastButtonState = 0;     // previous state of the button

void setup() {
  // initialize the button pin as a input:
  pinMode(fanPin, INPUT_PULLUP);
  // initialize the LED as an output:
  pinMode(ledPin, OUTPUT);
  // initialize serial communication:
  Serial.begin(115200);
}

// Set the LED on for duration specified in milliseconds
void led_flash(unsigned long duration) {
  digitalWrite(ledPin, HIGH);
  delay(duration);
  digitalWrite(ledPin, LOW);
}


void loop() {
  // read the pushbutton input pin:
  buttonState = digitalRead(fanPin);

  // compare the buttonState to its previous state
  if (buttonState != lastButtonState) {
    // if the state has changed, increment the counter
    if (buttonState == LOW) {
      // if the current state is HIGH then the button went from off to on:
      buttonPushCounter++;
      Serial.println("on");
      Serial.print("number of button pushes: ");
      Serial.println(buttonPushCounter);
      if (buttonPushCounter % 4 == 0) {
        Serial.println("Blink LED");
        led_flash(5);
      } 
    } else {
      // if the current state is LOW then the button went from on to off:
      Serial.println("off");
    }
    
    // Delay a little bit to avoid bouncing
    delay(50);

  
  }
  // save the current state as the last state, for next time through the loop
  lastButtonState = buttonState;




}
