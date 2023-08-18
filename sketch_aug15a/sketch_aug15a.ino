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
#define char_SEND_PIN      12
#define BELL_PIN           13

#define DEFUALT_CHAR_COUNT 44   // Number of characters per char_map
#define TIME_TO_PRINT_char 65   // Milliseconds
#define BREATHING_TIME     5    // Time between the characters
#define BAUD_RATE          9600 // Slowest baudrate Arduino serial monitor
                                // will work with
#define QUEUE_LENGTH       1024  // 256 characters can be buffered before transmitter has to shut up
#define TERMINAL_BUFFER_SZ 512   // 30 characters to send to the terminal at a time (maximum)
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
  pinMode(char_SEND_PIN,           OUTPUT);
  pinMode(BELL_PIN,                OUTPUT);
  
  Serial.begin(BAUD_RATE);
  while (!Serial) { }

  Serial.println("CONNECTED");
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
  digitalWrite(char_SEND_PIN,           SOLENOID_RELEASE);
  digitalWrite(BELL_PIN,                SOLENOID_PULL);
}

void send(char c) {
  send_to_terminal[terminal_buffer_idx++] = c;
}

void term_print() {
  for (int i = 0; i < terminal_buffer_idx; i++) {
    if (send_to_terminal[i] != -1)
      Serial.print(send_to_terminal[i]);
      
    send_to_terminal[i] = -1;
  }

  terminal_buffer_idx = 0;
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
    send('\n');
  case '\n': {
    send('\r');
    digitalWrite(CARRIAGE_RET_PIN, SOLENOID_PULL);
    character_sent = 1;
    last_time = millis();
    break;
  }
  }
}

void send_character(int count) {
  for (int i = 0; i < count; i++) {
    // Have we sent a character?
    if (character_sent == 0) {
      // If not, send one
      char c = 0;
      if (!character_queue.pop(&c))
        return;
  
      send(c);
      
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
      digitalWrite(char_SEND_PIN, SOLENOID_PULL);
      
      // Set "character_sent" to 1
      character_sent = 1;
      // Keep track of the current time
      last_time = millis();
    }
  
    // See if character print cycle has elapsed
    if (millis() - last_time >= TIME_TO_PRINT_char && character_sent == 1)
      disable_all_pins();
  
    // Breath
    if (millis() - last_time >= (TIME_TO_PRINT_char + BREATHING_TIME) && character_sent == 1)
      character_sent = 0;
  }
 
  // Return back to main loop to buffer in more characters
}

void loop() {
  // Place characters into a buffer
  Serial.write(XON);
  
  char c = -1;

  if (Serial.available() > 0)
    c = (char)Serial.read();

  // Seems like turning of serial communications causes de-synchrnoization
  // I have been investigating:
  // Bi-directional communication - maybe there is corruption on the line?
  // Package control on the terminal side - maybe the terminal emulator only checks after X
  // bytes to see if XOFF was called?
  // Synchronization - Maybe we miss a character when turning off the communications?
  // I do not know what is going wrong here, there is just a point where the
  // data echoed back to the terminal simply misses a few characters no matter
  // what I do. Maybe I should just switch over to pure C instead of bothering
  // with Arduino C++?
  
  if (character_queue.getCount() > QUEUE_LENGTH - TERMINAL_BUFFER_SZ) {
    // Turn off communications
    Serial.write(XOFF);
    
    character_queue.push(&c);
    
    // Read in any remaining characters
    for (int i = 0; i < Serial.available() % TERMINAL_BUFFER_SZ; i++)
      if ((c = (char)Serial.read()) && c != -1)
        character_queue.push(&c);
    
    // Process characters
    send_character(QUEUE_LENGTH);

    // Echo to terminal
    term_print();

    character_queue.flush();
  
    return;
  }
  
  if (c == ESCAPE)
    escaped = !escaped;

  if (escaped) {
    disable_all_pins();
    return;
  }
  
  if (c != -1)
    character_queue.push(&c);
  
  // Send the next character in the queue
  send_character(1);

  term_print();
}
