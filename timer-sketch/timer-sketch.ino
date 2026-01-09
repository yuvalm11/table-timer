/*
 * Table Timer - Arduino-based work timer with OLED display
 * 
 * Features:
 * - 24-hour timer with half-hour/half-minute precision for smooth animations
 * - Rotary encoder for time adjustment
 * - Multiple animations based on time of day
 * - Motor vibration alerts during work hours
 * - Visual second indicator
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <string.h>

// ---------------- Hardware Pin Definitions ----------------
#define MOTOR D0

// ---------------- OLED Display Configuration ----------------
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ---------------- Rotary Encoder Pin Definitions ----------------
#define ENCODER_CLK D10
#define ENCODER_DT  D9
#define ENCODER_SW  D8

// ---------------- Animation Configuration ----------------
// Animation frames are stored in PROGMEM to save RAM
// Each animation has FRAME_COUNT frames, and there are NUM_ANIMATIONS total animations
// Frame index calculation: frame = (current_frame % FRAME_COUNT) + (animation_index * FRAME_COUNT)
#define FRAME_WIDTH (48)
#define FRAME_HEIGHT (48)
#define FRAME_COUNT 28
#define NUM_ANIMATIONS 6

// Animation state variables
int animation_index = 0;   // Current animation set (0-5)
int last_animation = 1;    // Last used animation for cycling
int frame = 0;             // Current frame index within all animations

// Include animation frames data
#include "animations.h"
#define SECONDS_PER_MINUTE 60
#define MINUTES_PER_HOUR 60
#define HALF_HOURS_PER_DAY 48  // 24 hours * 2 (for half-hour increments)
#define HALF_MINUTES_PER_HOUR 120  // 60 minutes * 2 (for half-minute increments)
#define MILLIS_PER_SECOND 1000
#define DISPLAY_UPDATE_INTERVAL_MS 100
#define MOTOR_BUZZ_DURATION_MS 70
#define MOTOR_PAUSE_DURATION_MS 70
#define MOTOR_CYCLE_PAUSE_MS 60
#define MOTOR_BUZZ_CYCLES 3
#define MOTOR_BUZZ_GROUPS 5

// ---------------- Time Variables ----------------
// Using half-hour and half-minute units for smoother animation updates
int hours_half = 24;  // Current hour in half-hour units (0-47, where 24 = 12:00)
int minutes_half = 0;  // Current minute in half-minute units (0-119)
int seconds = 0;

unsigned long lastMillis = 0;

// ---------------- Encoder State ----------------
int lastCLK;
bool setHours = false;  // true = adjust hours, false = adjust minutes

// ---------------- Button Debounce ----------------
unsigned long lastButtonTime = 0;
const unsigned long debounceDelay = 250;

void setup() {
  // Initialize motor pin
  pinMode(MOTOR, OUTPUT);
  digitalWrite(MOTOR, LOW);

  // Initialize rotary encoder pins
  pinMode(ENCODER_CLK, INPUT_PULLUP);
  pinMode(ENCODER_DT, INPUT_PULLUP);
  pinMode(ENCODER_SW, INPUT_PULLUP);
  lastCLK = digitalRead(ENCODER_CLK);

  // Initialize I2C for OLED
  Wire.begin();

  // Initialize OLED display
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    while (1); // Halt if OLED not found
  }

  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
}

void loop() {
  updateTime();      // Update time and handle hourly events
  readEncoder();     // Read encoder input for time adjustment
  updateDisplay();   // Refresh OLED display
}

void updateTime() {
  unsigned long now = millis();

  // Update every second
  if (now - lastMillis >= MILLIS_PER_SECOND) {
    lastMillis += MILLIS_PER_SECOND;
    seconds++;

    // Increment minutes every 60 seconds (in half-minute units)
    if (seconds >= SECONDS_PER_MINUTE) {
      seconds = 0;
      minutes_half += 2;  // Add 2 half-minutes = 1 full minute
    }

    // Increment hours every 60 minutes (in half-hour units)
    if (minutes_half >= HALF_MINUTES_PER_HOUR) {
      minutes_half = 0;
      hours_half = (hours_half + 2) % HALF_HOURS_PER_DAY;  // Add 2 half-hours = 1 full hour
      
      // Cycle to next animation when hour changes
      last_animation = (last_animation % NUM_ANIMATIONS) + 1;
      animation_index = last_animation;
      frame = (frame + 1) % FRAME_COUNT + animation_index * FRAME_COUNT;
      updateDisplay();
      
      // Motor buzz during work hours (9:00 - 19:00)
      int currentHour = hours_half / 2;
      if (currentHour >= 9 && currentHour <= 19) {
        triggerMotorBuzz();
      }
    }
    
    // Select animation based on time of day
    // Note: These are sequential if statements (not else-if) so later conditions can override earlier ones
    selectAnimationByTime();
  }
}

/**
 * Selects the appropriate animation based on the current time
 * Uses sequential if statements to allow priority-based selection
 */
void selectAnimationByTime() {
  int currentHour = hours_half / 2;
  int currentMinute = minutes_half / 2;
  
  // Default: Work animation after first 5 minutes of each hour (minutes_half >= 10 = 5+ minutes)
  if (minutes_half >= 10) { animation_index = 0; }
  
  // Lunch time (12:00) - overrides work animation
  if (currentHour == 12) { animation_index = 6; }
  
  // Afternoon break (16:00 - 16:15) - overrides work animation
  if (currentHour == 16 && currentMinute <= 15) { animation_index = 7; }
  
  // Evening/Night (19:00 - 07:59) - overrides work animation
  if (currentHour >= 19 || currentHour < 8) { animation_index = 8; }
  
  // Morning (8:00 - 9:00) - overrides work animation
  if (currentHour >= 8 && currentHour <= 9) { animation_index = 9; }
}

/**
 * Triggers motor vibration pattern for hourly notification
 */
void triggerMotorBuzz() {
  for (int group = 0; group < MOTOR_BUZZ_GROUPS; group++) {
    for (int cycle = 0; cycle < MOTOR_BUZZ_CYCLES; cycle++) {
      digitalWrite(MOTOR, HIGH);
      updateDisplay();
      delay(MOTOR_BUZZ_DURATION_MS);
      updateDisplay();
      delay(MOTOR_BUZZ_DURATION_MS);
      digitalWrite(MOTOR, LOW);
      updateDisplay();
      delay(MOTOR_PAUSE_DURATION_MS);
    }
    updateDisplay();
    delay(MOTOR_CYCLE_PAUSE_MS);
  }
}

/**
 * Reads rotary encoder input and handles time adjustment
 */
void readEncoder() {
  int currentCLK = digitalRead(ENCODER_CLK);

  // Detect encoder rotation
  if (currentCLK != lastCLK) {
    // Determine rotation direction based on DT pin state
    if (digitalRead(ENCODER_DT) != currentCLK) { adjustTime(1); } // Clockwise
    else { adjustTime(-1); }  // Counter-clockwise
  }
  lastCLK = currentCLK;

  // Handle button press (toggle between hours/minutes adjustment)
  if (digitalRead(ENCODER_SW) == LOW) {
    if (millis() - lastButtonTime > debounceDelay) {
      seconds = 0;  // Reset seconds when switching modes
      setHours = !setHours;  // Toggle between hours and minutes
      lastButtonTime = millis();
    }
  }
}

/**
 * Adjusts time based on encoder rotation
 * @param delta +1 for clockwise, -1 for counter-clockwise
 */
void adjustTime(int delta) {
  if (setHours) {
    // Adjust hours (wrap around 24-hour cycle)
    hours_half = (hours_half + delta + HALF_HOURS_PER_DAY) % HALF_HOURS_PER_DAY;
  } else {
    // Adjust minutes (wrap around 60-minute cycle)
    minutes_half += delta;
    if (minutes_half < 0) minutes_half = HALF_MINUTES_PER_HOUR - 1;
    if (minutes_half >= HALF_MINUTES_PER_HOUR) minutes_half = 0;
  }
}

/**
 * Updates the OLED display with current time and animation
 */
void updateDisplay() {
  static unsigned long lastDraw = 0;
  
  // Limit redraw rate to prevent flickering
  if (millis() - lastDraw < DISPLAY_UPDATE_INTERVAL_MS) return;
  lastDraw = millis();

  // Format time string (convert half-units to regular units)
  char timeStr[9];
  sprintf(timeStr, "%02d:%02d", hours_half / 2, minutes_half / 2);

  // Calculate text position for center alignment
  int16_t x1, y1;
  uint16_t w, h;
  display.setTextSize(2);
  display.getTextBounds(timeStr, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2 + 30, 30);

  // Clear and redraw display
  display.clearDisplay();
  
  // Draw animation bitmap
  display.drawBitmap(5, 12, frames[frame], FRAME_WIDTH, FRAME_HEIGHT, 1);
  
  // Draw time text
  display.print(timeStr);

  // Draw second indicator dots
  display.setTextSize(1);
  for (int i = 0; i < seconds; i++) {
    display.setCursor(i * 2 + 5, 55);
    display.print('.');
  }

  display.display();
  
  // Advance to next animation frame
  frame = (frame + 1) % FRAME_COUNT + animation_index * FRAME_COUNT;
}

