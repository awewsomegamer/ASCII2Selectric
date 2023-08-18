#include <Arduino.h>
#include <cppQueue.h>

// These names are extracted from "IBM Selectric APM - Nov 1980.pdf"
// Page 58, PDF Page 60
#define TILT1_PIN          2
#define TILT2_PIN          3
#define ROTATE1_PIN        4
#define ROTATE2_PIN        5
#define ROTATE2A_PIN       6
#define NROTATE_PIN        7
#define SHIFT_PIN          8
#define SPACE_PIN          9
#define TAB_PIN            10
#define CARRIAGE_RET_PIN   11
#define CHAR_SEND_PIN      12
#define BELL_PIN           13

#define DEFUALT_CHAR_COUNT 44   // Number of characters per char_map
#define TIME_TO_PRINT_char 65   // Milliseconds
#define BREATHING_TIME     5    // Time between the characters
#define BAUD_RATE          9600
#define QUEUE_LENGTH       256  // 256 characters can be buffered before transmitter has to shut up
#define TERMINAL_BUFFER_SZ 30   // 30 characters to send to the terminal at a time (maximum)
#define ESCAPE             27   // ASCII character used to pause communication (27 = Escape key)

#define SOLENOID_PULL      LOW
#define SOLENOID_RELEASE   HIGH

#define XON                0x11
#define XOFF               0x13

char static_queue_data[QUEUE_LENGTH];
cppQueue character_queue(sizeof(char), QUEUE_LENGTH, FIFO, false, &static_queue_data, QUEUE_LENGTH * sizeof(char));
char send_to_terminal[TERMINAL_BUFFER_SZ];
int terminal_buffer_idx = 0;

int character_sent = 0;
int escaped = 0;
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
  pinMode(TILT1_PIN,               OUTPUT);
  pinMode(TILT2_PIN,               OUTPUT);
  pinMode(ROTATE1_PIN,             OUTPUT);
  pinMode(ROTATE2_PIN,             OUTPUT);
  pinMode(ROTATE2A_PIN,            OUTPUT);
  pinMode(NROTATE_PIN,             OUTPUT);
  pinMode(SHIFT_PIN,               OUTPUT);
  pinMode(SPACE_PIN,               OUTPUT);
  pinMode(TAB_PIN,                 OUTPUT);
  pinMode(CARRIAGE_RET_PIN,        OUTPUT);
  pinMode(CHAR_SEND_PIN,           OUTPUT);
  pinMode(BELL_PIN,                OUTPUT);
  
  Serial.begin(BAUD_RATE);
  while (!Serial) { }

  Serial.println("CONNECTED");
//  Serial.write(XOFF); // Drivers don't support this
}

void disable_all_pins() {
  digitalWrite(TILT1_PIN,               SOLENOID_RELEASE);
  digitalWrite(TILT2_PIN,               SOLENOID_RELEASE);
  digitalWrite(ROTATE1_PIN,             SOLENOID_RELEASE);
  digitalWrite(ROTATE2_PIN,             SOLENOID_RELEASE);
  digitalWrite(ROTATE2A_PIN,            SOLENOID_RELEASE);
  digitalWrite(NROTATE_PIN,             SOLENOID_RELEASE);
  digitalWrite(SHIFT_PIN,               SOLENOID_RELEASE);
  digitalWrite(SPACE_PIN,               SOLENOID_RELEASE);
  digitalWrite(TAB_PIN,                 SOLENOID_RELEASE);
  digitalWrite(CARRIAGE_RET_PIN,        SOLENOID_RELEASE);
  digitalWrite(CHAR_SEND_PIN,           SOLENOID_RELEASE);
  digitalWrite(BELL_PIN,                SOLENOID_PULL);
}

void special_character(char c) {
  switch (c) {
  case ' ': {
    digitalWrite(SPACE_PIN, SOLENOID_PULL);
    character_sent = 1;
    last_time = millis();
    break;
  }
  
  case '\t': {
    digitalWrite(TAB_PIN, SOLENOID_PULL);
    character_sent = 1;
    last_time = millis();
    break;
  }
  
  case '\r':
  case '\n': {
    digitalWrite(CARRIAGE_RET_PIN, SOLENOID_PULL);
    character_sent = 1;
    last_time = millis();
    break;
  }
  }
}

void send_character() {
  char c = -1;
  
  // Have we sent a character?
  if (character_sent == 0) {
    // If not, send one
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
      goto echo_back;
    }

    digitalWrite(SHIFT_PIN, shift ? SOLENOID_PULL : SOLENOID_RELEASE);

    int tilt = position % 4;
    int rotation = 0;

    if ((position / 4) >= 1 && (position / 4) <= 5) {
      rotation = (position / 4); // The character is found in the negative field
      digitalWrite(NROTATE_PIN, SOLENOID_PULL); // Tell hardware this is a negative rotation
    } else if ((position / 4) >= 6 && (position / 4) <= 10) {
      rotation = ((position / 4) - 5); // The character is found in the positive field
    }
    
    // Encode tilt and rotation variables into the pins
    digitalWrite(TILT1_PIN, (tilt & 1) ? SOLENOID_PULL : SOLENOID_RELEASE);
    digitalWrite(TILT2_PIN, ((tilt >> 1) & 1) ? SOLENOID_PULL : SOLENOID_RELEASE);

    digitalWrite(ROTATE1_PIN, (rotation & 1) ? SOLENOID_PULL : SOLENOID_RELEASE);
    digitalWrite(ROTATE2_PIN, ((rotation >> 1) & 1) ? SOLENOID_PULL : SOLENOID_RELEASE);
    digitalWrite(ROTATE2A_PIN, ((rotation >> 2) & 1) ? SOLENOID_PULL : SOLENOID_RELEASE);

    // Pull character send solenoid
    digitalWrite(CHAR_SEND_PIN, SOLENOID_PULL);
    
    // Set "character_sent" to 1
    character_sent = 1;
    // Keep track of the current time
    last_time = millis();
  }

  // See if character print cycle has elapsed
  if (millis() - last_time >= TIME_TO_PRINT_char && character_sent == 1)
    disable_all_pins();

  // Breathe
  if (millis() - last_time >= (TIME_TO_PRINT_char + BREATHING_TIME) && character_sent == 1)
    character_sent = 0;

  echo_back:
  
  // Echo the character back
  if (character_sent == 0)
    Serial.print(c);
 
  // Return back to main loop to buffer in more characters
}

// XON/XOFF Band-aid:
// Use CoolTerm
// Goto Options > Transmission and enable the
// "Wait for remote echo" option, the timeout
// can be lowered to be above 70 ms.
//
// Now large buffers can be transmitted through
// CoolTerm to the board without having the
// issue of the buffer moving on.

void loop() {
  char c = -1;
  
  // Are we nearing a full queue?
  if (character_queue.getCount() >= QUEUE_LENGTH - TERMINAL_BUFFER_SZ) {
    // If so:
    // Turn off communications
//    Serial.write(XOFF); // Drivers don't support this

    int count = Serial.available() > TERMINAL_BUFFER_SZ ? TERMINAL_BUFFER_SZ : Serial.available();
    for (int i = 0; i < count; i++)
      if ((c = (char)Serial.read()) && c != -1)
        character_queue.push(&c);

    while (character_queue.getCount() > 0)
      send_character();

    // Seems like by the time the above loop works its way
    // through the queue the transmission of bytes is already
    // done.

//    Serial.write(XON); // Drivers don't support this
    
    return;
  }
  
  c = (char)Serial.read();

  if (c != -1)
    character_queue.push(&c);

  send_character();
}
