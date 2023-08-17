#include <Arduino.h>
#include <cppQueue.h>

// These names are extracted from "IBM Selectric APM - Nov 1980.pdf"
// Page 58, PDF Page 60
#define TILT1_PIN          5
#define TILT2_PIN          6
#define ROTATE1_PIN        7
#define ROTATE2_PIN        8
#define ROTATE2A_PIN       9
#define NROTATE_PIN        10
#define SHIFT_PIN          11
#define SPACE_PIN          12
#define CHAR_SEND_PIN      13
#define DEFUALT_CHAR_COUNT 44
#define TIME_TO_PRINT_CHAR 1000 // Place holder, this is the time in
                                // milliseconds that it takes for the
                                // Selectric takes to print one character
#define BAUD_RATE          1200 // Slowest baudrate Arduino serial monitor
                                // will work with
#define QUEUE_LENGTH       512  // 512 characters can be buffered, try to get
                                // a lower baudrate

cppQueue character_queue(sizeof(char), QUEUE_LENGTH, FIFO, true);

int character_sent = 0;
unsigned long last_time = 0;

// Designed for Standard U.S. 7XX type elements
// My machine: < = 1/2 lowercase, > = 1/4 upercase
                  // Tilt:  0    1    2    3
char char_map_lower[]  = { // Home
                           'z', 't', '<', 'j',  // Rotate 0
                            // Negative
                           '4', 'l', 'o', '/',  // Rotate -1
                           '8', 'c', 'a', ',',  // Rotate -2
                           '7', 'd', 'r', ';',  // Rotate -3
                           '3', 'u', 'v', 'f',  // Rotate -4
                           '1', 'x', 'm', 'g',  // Rotate -5
                            // Positive
                           '2', 'n', '.', '=',  // Rotate 1
                           '5', 'e', '\'', 'p', // Rotate 2               
                           '6', 'k', 'i', 'q',  // Rotate 3
                           '0', 'h', 's', 'y',  // Rotate 4
                           '9', 'b', 'w', '-',  // Rotate 5
                                          };

char char_map_upper[]  = { // Home
                          'Z', 'T', '>', 'J',  // Rotate 0
                          // Negative
                          '$', 'L', 'O', '?',  // Rotate -1
                          '*', 'C', 'A', ',',  // Rotate -2
                          '&', 'D', 'R', ':',  // Rotate -3
                          '#', 'U', 'V', 'F',  // Rotate -4
                          '!', 'X', 'M', 'G',  // Rotate -5
                          // Positive
                          '@', 'N', '.', '+',  // Rotate 1
                          '%', 'E', '"', 'P',  // Rotate 2               
                          '^', 'K', 'I', 'Q',  // Rotate 3
                          ')', 'H', 'S', 'Y',  // Rotate 4
                          '(', 'B', 'W', '_',  // Rotate 5
                                          };

void setup() {
  pinMode(TILT1_PIN,        OUTPUT);
  pinMode(TILT2_PIN,        OUTPUT);
  pinMode(ROTATE1_PIN,      OUTPUT);
  pinMode(ROTATE2_PIN,      OUTPUT);
  pinMode(ROTATE2A_PIN,     OUTPUT);
  pinMode(NROTATE_PIN,      OUTPUT);
  pinMode(SHIFT_PIN,        OUTPUT);
  pinMode(SPACE_PIN,        OUTPUT);
  pinMode(CHAR_SEND_PIN,    OUTPUT);
  
  Serial.begin(BAUD_RATE);
  while (!Serial) { }

  Serial.println("CONNECTED");
}

void disable_all_pins() {
  digitalWrite(TILT1_PIN,        LOW);
  digitalWrite(TILT2_PIN,        LOW);
  digitalWrite(ROTATE1_PIN,      LOW);
  digitalWrite(ROTATE2_PIN,      LOW);
  digitalWrite(ROTATE2A_PIN,     LOW);
  digitalWrite(NROTATE_PIN,      LOW);
  digitalWrite(SHIFT_PIN,        LOW);
  digitalWrite(SPACE_PIN,        LOW);
  digitalWrite(CHAR_SEND_PIN,    LOW);
}

void special_character(char c) {
  switch (c) {
  case ' ': {
    digitalWrite(SPACE_PIN, HIGH);
    character_sent = 1;
    last_time = millis();
    break;
  }
  }
}

void send_character() {
  // Have we sent a character?
  if (character_sent == 0) {
    // If not, send one
    char c = 0;
    if (!character_queue.pop(&c))
      return;
  
    int shift = 0;
    int position = -1;
    for (int i = 0; i < DEFUALT_CHAR_COUNT; i++) {
      if (c == char_map_lower[i]) {
        position = i;
        break;
      }
    }

    // Have we found the lowercase position?
    if (position == -1)
      shift = 1; // No, so let's write that down
      
    for (int i = 0; i < DEFUALT_CHAR_COUNT; i++) {
      if (c == char_map_upper[i]) {
        position = i;
        break;
      }
    }    
    
    // Have we truly found the character?
    if (position == -1) {
      special_character(c); // Apparently not, try a special character
      return;
    }

    digitalWrite(SHIFT_PIN, shift);

    int tilt = position % 4;
    int rotation = 0;

    if ((position / 4) >= 1 && (position / 4) <= 5) {
      rotation = (position / 4); // The character is found in the negative field
      digitalWrite(NROTATE_PIN, HIGH); // Tell hardware this is a negative rotation
    } else if ((position / 4) >= 6 && (position / 4) <= 10) {
      rotation = ((position / 4) - 5); // The character is found in the positive field
    }
    
    // Encode tilt and rotation variables into the pins
    digitalWrite(TILT1_PIN, (tilt & 1));
    digitalWrite(TILT2_PIN, ((tilt >> 1) & 1));

    digitalWrite(ROTATE1_PIN, (rotation & 1));
    digitalWrite(ROTATE2_PIN, ((rotation >> 1) & 1));
    digitalWrite(ROTATE2A_PIN, ((rotation >> 2) & 1));

    // Pull character send solenoid
    digitalWrite(CHAR_SEND_PIN, HIGH);
    
    // Set "character_sent" to 1
    character_sent = 1;
    // Keep track of the current time
    last_time = millis();
  }

  // Add if statement here to see if enough time has elapsed, and in which case
  // "character_sent" will be set to 0
  // "enough" = typewriter has most likely printed the character
  if (millis() - last_time >= TIME_TO_PRINT_CHAR && character_sent == 1) {
    character_sent = 0;
    disable_all_pins();
  }
  // Return back to main loop to buffer in more characters
}

void loop() {
  // Place characters into a buffer
  char c = -1;
  if (Serial.available() && (c = (char)Serial.read()) != -1)
    character_queue.push(&c);
  
  // Send the next character in the queue
  send_character();
}
