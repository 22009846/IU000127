#include "Adafruit_TCS34725.h"
#include <esp_now.h>
#include "ESP32S3Buzzer.h"
#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include <Arduino.h>
#include "WiFi.h"
#include <Wire.h>
#include <stdlib.h>

#define SCUI8T(x) static_cast<uint8_t>(x)
#define SCUI16T(x) static_cast<uint16_t>(x)
#define SCUI32T(x) static_cast<uint32_t>(x)

// Constants used for Micro SD card module support:
#define SD_MISO_PIN 19
#define SD_MOSI_PIN 23
#define SD_SCK_PIN 18
#define SD_CS_PIN 5

// Constants used for push button module support:
#define BUTTON_POWER_PIN 13
#define BUTTON_SIGNAL_PIN 12
#define ON true
#define OFF false

// Constants used for TCS34725 color sensor module support:
#define TCS_POWER_PIN 33
#define TCS_SCL_PIN 22
#define TCS_SDA_PIN 21
#define RED 0
#define GREEN 1
#define BLUE 2

// Constants used for passive buzzer module support (1/2):
#define BUZZER_POWER_PIN_1 14
#define BUZZER_SIGNAL_PIN_1 27

// Constants used for passive buzzer module support (2/2):
#define BUZZER_POWER_PIN_2 26
#define BUZZER_SIGNAL_PIN_2 25

// Constants used for passive buzzer module-related functions
#define SUCCESS true
#define FAILURE false

// MAC address of the LilyGO T-Display-S3 (34:85:18:71:CF:00)
const uint8_t broadcast_address[] = {SCUI8T(0x34), SCUI8T(0x85), SCUI8T(0x18), \
  SCUI8T(0x71), SCUI8T(0xCF), SCUI8T(0x00)};

// Container for the LilyGO T-Display-S3's identifying information
esp_now_peer_info_t peer_info;

// Number of signals received
uint8_t received = SCUI8T(0);

// True when the two microcontrollers have acknowledged each other
bool connection_initialized = false;

uint8_t high_score; // High score

// Number of signals sent
uint8_t sent = SCUI8T(0);

SPIClass sd_spi = SPIClass(VSPI); // Container for SPI data (used with the Micro SD card module)

Adafruit_TCS34725 *tcs; // Interface for the TCS34725 color sensor

ESP32S3Buzzer *buzzer_1; // Interface for one of the passive buzzer modules

ESP32S3Buzzer *buzzer_2; // Interface for the other passive buzzer module

// Last recorded button state
// (used to avoid interpreting multiple ON states as multiple clicks)
bool button_state = OFF;

// Last moment at which the push button was pressed
// (used to avoid "spam-clicking" issues)
unsigned long button_timeout = static_cast<unsigned long>(0);

// Current color being guessed
uint8_t current_color = SCUI8T(RED);

void postOutgoing(const uint8_t *mac_addr, esp_now_send_status_t status) {
  /*
   * Updates the `sent` variable
   */
  if (ESP_NOW_SEND_SUCCESS == 0)
    sent++;
  else
    Serial.println("Error: postOutgoing: failed to send data");
}

void writeHighScore(uint8_t score);
void playTones(bool success);

void processIncoming(const uint8_t *mac, const uint8_t *data, int len) {
  /*
   * Handles incoming WiFi signals
   */

  if (len == 2) {
    high_score = *data;
    writeHighScore(high_score);
    received++;
    return;
  }

  if (len > 2) {
    Serial.println("Error: processIncoming: invalid number of bytes received");
    return;
  }

  if (connection_initialized) {
    if (*data == SCUI8T(2)) { // (2: wrong answer, game NOT over)
      playTones(FAILURE);
    } else if (*data == SCUI8T(1)) { // (1: right answer)
      playTones(SUCCESS);
      switch (current_color) {
        case SCUI8T(RED):
          current_color = SCUI8T(GREEN);
          break;
        case SCUI8T(GREEN):
          current_color = SCUI8T(BLUE);
          break;
        case SCUI8T(BLUE):
          current_color = SCUI8T(RED);
          break;
        default:
          Serial.println("Error: processIncoming: unknown error");
      }
    } else if (*data == SCUI8T(0)) { // (0: wrong answer, game over)
      playTones(FAILURE);
      current_color = SCUI8T(RED);
    } else {
      Serial.println("Error: processIncoming: invalid byte received");
    }
  } else {
    connection_initialized = true;
  }

  received++;
}

uint8_t readHighScore(void) {
  fs::FS &fs = SD;
  File file = fs.open("/high_score.txt");

  if (!file) {
      Serial.println("Error: readHighScore: unable to read /high_score.txt");
      return SCUI8T(0);
  }

  // (using a C-style string due to complications with C++ String objects)
  char high_score[8];
  uint8_t count = 0;
  while(file.available())
    high_score[count++] = (char) (file.read());
  high_score[count] = '\0';

  file.close();

  return (uint8_t) (atoi(high_score));
}

inline bool getButtonState(void) {
  /*
   * Reads the state of the push button module (ON/OFF)
   */

  if (digitalRead(BUTTON_SIGNAL_PIN) == SCUI8T(HIGH))
    return ON;
  else
    return OFF;
}

uint8_t getRGBValue(uint8_t color) {
  /*
   * Returns either the red, green, or blue value being read by the
   * TCS34725 color sensor
   */

  uint16_t ret; // (will be normalized to uint8_t)
  int high, low; // High/low byte flag for tcs->read16

  switch (color) {
    case SCUI8T(RED):
      high = TCS34725_RDATAH;
      low = TCS34725_RDATAL;
      break;
    case SCUI8T(GREEN):
      high = TCS34725_GDATAH;
      low = TCS34725_GDATAL;
      break;
    case SCUI8T(BLUE):
      high = TCS34725_BDATAH;
      low = TCS34725_BDATAL;
      break;
    default:
      Serial.println("Error: getRGBValue: invalid color");
      return SCUI8T(0);
  }

  ret = tcs->read16(high); // Fetching the high byte...
  ret <<= 8; // Shifting the high byte to its correct place...
  ret | tcs->read16(low); // Setting the low byte...
  return (uint8_t) (ret / SCUI16T(257)); // Normalising the return value...
}

void playTones(bool success) {
  /*
   * Adds one of two sound effects to the buzzer queue
   */

  if (success) {
    buzzer_1->tone(SCUI32T(146.83), SCUI32T(150), SCUI32T(0)); // Buzzer 1 plays D3
    buzzer_2->tone(SCUI32T(0), SCUI32T(150), SCUI32T(0)); // Buzzer 2 waits silently
    buzzer_2->tone(SCUI32T(196.00), SCUI32T(500), SCUI32T(0)); // Buzzer 2 plays G3
  } else {
    buzzer_1->tone(SCUI32T(196.00), SCUI32T(150), SCUI32T(0)); // Buzzer 1 plays G3
    buzzer_2->tone(SCUI32T(0), SCUI32T(150), SCUI32T(0)); // Buzzer 2 waits silently
    buzzer_2->tone(SCUI32T(146.83), SCUI32T(500), SCUI32T(0)); // Buzzer 2 plays D3
  }
}

void writeHighScore(uint8_t score){
  fs::FS &fs = SD;
  File file = fs.open("/high_score.txt", FILE_WRITE);

  if (!file) {
      Serial.println("Error: writeHighScore: unable to write to /high_score.txt");
      return;
  }

  if (file.print(String(score)))
      Serial.println("writeHighScore: operation successful");
  else
      Serial.println("Error: writeHighScore: unable to write to /high_score.txt");

  file.close();
}

void setup() {
  Serial.begin(9600);

  // Establishing a connection to the LilyGO T-Display-S3...
  WiFi.mode(WIFI_MODE_STA);
  esp_now_init();
  esp_now_register_send_cb(postOutgoing);
  memcpy(peer_info.peer_addr, broadcast_address, static_cast<size_t>(6));
  peer_info.channel = SCUI8T(0);
  peer_info.encrypt = false;
  esp_now_add_peer(&peer_info);
  esp_now_register_recv_cb(processIncoming);

  Serial.print("Waiting for a connection");
  while (received == 0) // Waiting for a "hello" signal...
    Serial.print('.');
  Serial.print("\n\n");

  // (sharing the high score is part of the initial "handshake")
  sd_spi.begin(SCUI8T(SD_SCK_PIN), SCUI8T(SD_MISO_PIN), SCUI8T(SD_MOSI_PIN), SCUI8T(SD_CS_PIN));
  while (!SD.begin(SCUI8T(SD_CS_PIN), sd_spi)) { // (loop added due to recurring issues while testing)
    Serial.println("SD.begin() failed; trying again...");
    delay(1000);
  }

  high_score = readHighScore();
  esp_now_send(broadcast_address, &high_score, 1); // Sending the high score to finalize the "handshake."

  // Handling permanent state pins...
  pinMode(SCUI8T(BUTTON_POWER_PIN), SCUI8T(OUTPUT));
  digitalWrite(SCUI8T(BUTTON_POWER_PIN), SCUI8T(HIGH));

  pinMode(SCUI8T(TCS_POWER_PIN), SCUI8T(OUTPUT));
  digitalWrite(SCUI8T(TCS_POWER_PIN), SCUI8T(HIGH));

  pinMode(SCUI8T(BUZZER_POWER_PIN_1), SCUI8T(OUTPUT));
  pinMode(SCUI8T(BUZZER_POWER_PIN_2), SCUI8T(OUTPUT));
  digitalWrite(SCUI8T(BUZZER_POWER_PIN_1), SCUI8T(HIGH));
  digitalWrite(SCUI8T(BUZZER_POWER_PIN_2), SCUI8T(HIGH));

  // Handling variable state pins...
  pinMode(SCUI8T(BUTTON_SIGNAL_PIN), SCUI8T(INPUT));

  pinMode(SCUI8T(BUZZER_SIGNAL_PIN_1), SCUI8T(OUTPUT));
  pinMode(SCUI8T(BUZZER_SIGNAL_PIN_2), SCUI8T(OUTPUT));

  // Initializing variables & performing configurations...
  Wire.begin(TCS_SDA_PIN, TCS_SCL_PIN);
  tcs = new Adafruit_TCS34725(TCS34725_INTEGRATIONTIME_154MS, TCS34725_GAIN_60X); // !
  tcs->begin();

  buzzer_1 = new ESP32S3Buzzer(SCUI8T(BUZZER_SIGNAL_PIN_1), SCUI8T(0));
  buzzer_2 = new ESP32S3Buzzer(SCUI8T(BUZZER_SIGNAL_PIN_2), SCUI8T(1));
  buzzer_1->begin();
  buzzer_2->begin();

  Serial.println();
}

void loop() {
  if (getButtonState() == ON) { // Checking if the button is pressed...
    if (button_state == OFF) { // Ensuring the state detected is not a "duplicate"...
      // Ensuring the button timeout has expired...
      if (millis() - button_timeout > static_cast<unsigned long>(1000)) {
        uint8_t color = getRGBValue(current_color);
        // Sending the guessed color to the LilyGO T-Display-S3...
        esp_now_send(broadcast_address, &color, 1);
        button_state = ON;
        button_timeout = millis();
      }
    }
  } else {
    if (button_state == ON) {
      button_state = OFF;
    }
  }

  buzzer_1->update();
  buzzer_2->update();
}