#include <Adafruit_NeoPixel.h>
#include <Arduino.h>

#define LED_PIN    14       // Pin connected to the data line of SK6812 LEDs
#define NUM_LEDS   19       // Number of SK6812 LEDs
#define BRIGHTNESS 50       // Set brightness (0-255)
#define THEREMIN_PIN 10     // GPIO pin where the Theremin output is connected
#define BUZZER_PIN 4        // GPIO pin for the buzzer
#define SW1_PIN    37       // GPIO pin for the start button (SW1)

Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

volatile unsigned long pulseCount = 0;       // Variable to store pulse count
unsigned long referenceFrequency = 0;        // Reference frequency after initialization
unsigned long currentFrequency = 0;          // Current frequency reading
bool systemActive = false;                   // System activation state
bool referenceFrequencySet = false;          // Flag to indicate reference frequency has been set

unsigned long lastSampleTime = 0;            // Timestamp for 10 ms sample intervals
unsigned long activationTime = 0;            // Timestamp for system activation
unsigned long lastBuzzerUpdateTime = 0;      // Timestamp for buzzer update interval

volatile int targetBuzzerFrequency = 0;      // Target frequency for smooth transition
volatile int currentBuzzerFrequency = 0;     // Current frequency to increment smoothly
const int buzzerStep = 5;                    // Frequency step size (5 Hz)
const int stepInterval = 1;                  // Interval between steps (1 ms)
hw_timer_t * timer = NULL;                   // Timer pointer for buzzer PWM control
volatile bool buzzerState = false;           // State to toggle the buzzer

// Parameters for sampling and averaging
const int sampleInterval = 10;               // Sample interval of 10 ms
const int numSamples = 20;                   // Number of samples for moving average
unsigned long samples[numSamples] = {0};     // Array to store samples for averaging
int sampleIndex = 0;                         // Current index in the samples array
unsigned long sum = 0;                       // Sum of samples for moving average

void IRAM_ATTR countPulse() {
  pulseCount++;  // Increment pulse count on each interrupt
}

// Function to get and reset the pulse count safely
unsigned long getPulseCount() {
  noInterrupts();              // Disable interrupts to read and reset safely
  unsigned long count = pulseCount;
  pulseCount = 0;              // Reset pulse count after reading
  interrupts();                // Re-enable interrupts
  return count;                // Return the pulse count value
}

// Timer ISR for toggling the buzzer pin
void IRAM_ATTR onTimer() {
  buzzerState = !buzzerState;           // Toggle buzzer state
  digitalWrite(BUZZER_PIN, buzzerState); // Output state to the buzzer pin
}

// Function to set the timer frequency based on the current frequency
void setBuzzerFrequency(int frequency) {
  if (frequency > 0) {
    int timerInterval = 1000000 / (2 * frequency); // Interval for half-period in microseconds
    timerAlarmWrite(timer, timerInterval, true);   // Set timer interval
    timerAlarmEnable(timer);                       // Enable the timer
  } else {
    timerAlarmDisable(timer);                      // Disable the timer to stop the buzzer
    digitalWrite(BUZZER_PIN, LOW);                 // Turn off buzzer output
  }
}

// Startup LED pattern function
void startupPattern() {
  Serial.println("Device is turning on...");
  int delayTime = 100;  // Time delay per step to complete in 2 seconds
  
  // Light up LEDs from center outwards
  for (int i = 0; i <= 9; i++) {
    int leftIndex = 9 - i;   // Left side LED index
    int rightIndex = 9 + i;  // Right side LED index
    if (leftIndex >= 0) strip.setPixelColor(leftIndex, strip.Color(255, 255, 255));  // Light left side LED
    if (rightIndex < NUM_LEDS) strip.setPixelColor(rightIndex, strip.Color(255, 255, 255));  // Light right side LED
    strip.show();
    delay(delayTime);
  }
  
  // Turn off LEDs from edges to center
  for (int i = 0; i <= 9; i++) {
    int leftIndex = i;       // Left side LED index
    int rightIndex = NUM_LEDS - 1 - i;  // Right side LED index
    strip.setPixelColor(leftIndex, strip.Color(0, 0, 0));  // Turn off left side LED
    strip.setPixelColor(rightIndex, strip.Color(0, 0, 0));  // Turn off right side LED
    strip.show();
    delay(delayTime);
  }
}

// Function to update LEDs based on frequency increase
void updateLEDsBasedOnFrequencyIncrease(int frequencyIncrease) {
  int numLEDs = constrain(frequencyIncrease / 2, 1, NUM_LEDS);  // Determine number of LEDs to light up
  strip.clear();  // Clear the strip before setting LEDs
  for (int i = 0; i < numLEDs; i++) {
    strip.setPixelColor(i, strip.Color(255, 0, 0));  // Set LED to red
  }
  strip.show();
}

void setup() {
  Serial.begin(115200);

  // Initialize LED strip
  strip.begin();
  strip.show();
  strip.setBrightness(BRIGHTNESS);

  // Initialize buzzer pin as output
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  // Set up the timer for the buzzer with a base frequency
  timer = timerBegin(0, 80, true);             // Timer 0, prescaler 80 (for 1 µs tick)
  timerAttachInterrupt(timer, &onTimer, true); // Attach ISR to timer

  // Initialize Theremin input and interrupts
  pinMode(THEREMIN_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(THEREMIN_PIN), countPulse, RISING);

  // Initialize button for activation
  pinMode(SW1_PIN, INPUT_PULLUP);
}

void loop() {
  unsigned long currentTime = millis();

  // Check for system activation
  if (!systemActive && digitalRead(SW1_PIN) == LOW) {
    delay(3000);                      
    if (digitalRead(SW1_PIN) == LOW) {
      systemActive = true;            
      activationTime = millis();       // Record activation time
      Serial.println("System Activated. Showing startup pattern...");
      startupPattern();                // Display the startup pattern
      Serial.println("Startup pattern complete.");
      delay(1000);                     // Wait an additional 1 second before starting readings
    }
    return;
  }

  // Collect samples and update moving average every 10 ms
  if (systemActive) {
    if (currentTime - lastSampleTime >= sampleInterval) {
      lastSampleTime = currentTime;

      // Update moving average
      unsigned long newSample = getPulseCount();
      sum -= samples[sampleIndex];                // Subtract the oldest sample
      sum += newSample;                           // Add the new sample
      samples[sampleIndex] = newSample;           // Update sample array
      sampleIndex = (sampleIndex + 1) % numSamples;

      // Update current frequency
      currentFrequency = sum / numSamples;

      Serial.print("Updated Current Frequency: ");
      Serial.println(currentFrequency);

      // Calculate frequency increase
      if (referenceFrequencySet) {
        int frequencyIncrease = abs((int)currentFrequency - (int)referenceFrequency);

        if (frequencyIncrease >= 2 && frequencyIncrease <= 46) {
          updateLEDsBasedOnFrequencyIncrease(frequencyIncrease);
          targetBuzzerFrequency = 212 + ((frequencyIncrease - 2) * 154);
        } else if (frequencyIncrease > 46) {
          updateLEDsBasedOnFrequencyIncrease(46);
          targetBuzzerFrequency = 7028;
        } else {
          targetBuzzerFrequency = 0;
          strip.clear();
          strip.show();
        }
      }
    }

    // Smoothly adjust buzzer frequency
    if (currentTime - lastBuzzerUpdateTime >= stepInterval) {
      lastBuzzerUpdateTime = currentTime;

      if (currentBuzzerFrequency < targetBuzzerFrequency) {
        currentBuzzerFrequency += buzzerStep;
        if (currentBuzzerFrequency > targetBuzzerFrequency) {
          currentBuzzerFrequency = targetBuzzerFrequency;
        }
        setBuzzerFrequency(currentBuzzerFrequency);
      } else if (currentBuzzerFrequency > targetBuzzerFrequency) {
        currentBuzzerFrequency -= buzzerStep;
        if (currentBuzzerFrequency < targetBuzzerFrequency) {
          currentBuzzerFrequency = targetBuzzerFrequency;
        }
        setBuzzerFrequency(currentBuzzerFrequency);
      }
    }
  }
}
