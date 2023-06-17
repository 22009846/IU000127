#include "TFT_eSPI.h"
#include <esp_now.h>
#include <string>
#include "Arduino.h"
#include "pin_config.h"
#include "WiFi.h"

#define SCUI8T(x) static_cast<uint8_t>(x)
#define SCUI16T(x) static_cast<uint16_t>(x)
#define SCUI32T(x) static_cast<uint32_t>(x)

#define PADDING static_cast<int32_t>(10)

#define ATTEMPT 0
#define SCORE 1
#define COLOR 2
#define HIGH_SCORE 3

// Range of color to be guessed
typedef struct {
    uint8_t low, high;
} range_t;

// Cartesian coordinates of text
struct textpos_t {
    int32_t x, y;
} positions[4];

TFT_eSPI tft = TFT_eSPI(); // Interface for the display

// MAC address of the NodeMCU-ESP32 ESP32 DEVKITV1 (A0:B7:65:4A:2A:A4)
const uint8_t broadcast_address[] = {SCUI8T(0xA0), SCUI8T(0xB7), SCUI8T(0x65), \
    SCUI8T(0x4A), SCUI8T(0x2A), SCUI8T(0xA4)};

// Container for the NodeMCU-ESP32 ESP32 DEVKITV1's identifying information
esp_now_peer_info_t peer_info;

uint8_t sent = SCUI8T(0); // Number of signals sent

// Number of signals received
uint8_t received = SCUI8T(0);

uint8_t high_score; // High score

String strings[4]; // Strings to be displayed

range_t current_range = {SCUI8T(0), SCUI8T(0)}; // Current range

uint16_t current_color; // Current color being guessed

void initDisplay(void) {
    /*
     * Initializes the display
     */

    pinMode(PIN_POWER_ON, OUTPUT);
    digitalWrite(PIN_POWER_ON, HIGH);

    tft.begin();
    tft.setRotation(3);

    ledcSetup(0, 2000, 8);
    ledcAttachPin(PIN_LCD_BL, 0);
    ledcWrite(0, 255);
}

void postOutgoing(const uint8_t *mac_addr, esp_now_send_status_t status) {
    /*
     * Updates the `sent` variable
     */

    if (ESP_NOW_SEND_SUCCESS == 0)
        sent++;
    else
        Serial.println("Error: postOutgoing: failed to send data"); // ?
}

void kill(void) {
    /*
     * Analogous to C/C++'s exit()
     */

    while (true);
}

void processIncoming(const uint8_t *mac, const uint8_t *data, int len) {
    /*
     * Processes incoming WiFi signals from the NodeMCU-ESP32 ESP32 DEVKITV1
     */

    if (len != 1) {
        Serial.println("Error: processIncoming: invalid number of bytes received"); // ?
        return;
    }

    if (received == SCUI8T(0)) {
        high_score = *data;
        received++;
        return;
    }

    // If the data (guessed color) falls outside of the current range:
    if (*data < current_range.low || *data > current_range.high) {
        if (strings[ATTEMPT][8] != '3') {
            updateText(SCUI8T(2));
            // Signaling the NodeMCU-ESP32 ESP32 DEVKITV1 to play the
            // "failure" sound effect...
            uint8_t signal = SCUI8T(2);
            esp_now_send(broadcast_address, &signal, 1);
        } else {
            updateText(SCUI8T(0));
            // Signaling the NodeMCU-ESP32 ESP32 DEVKITV1 to play the
            // "failure" sound effect and reset its current_color variable...
            uint8_t signal = SCUI8T(0);
            esp_now_send(broadcast_address, &signal, 1);
        }
    }
    // Otherwise:
    else {
        updateText(SCUI8T(1));
        // Signaling the NodeMCU-ESP32 ESP32 DEVKITV1 to play the
        // "success" sound effect and update its current_color variable...
        uint8_t signal = SCUI8T(1);
        esp_now_send(broadcast_address, &signal, 1);
    }

    received++;
}

void initText(void) {
    /*
     * Initializes the `positions` and `strings` variables
     * (used to display text correctly)
     */

    int32_t viewport_width = tft.getViewportWidth();
    int32_t viewport_height = tft.getViewportHeight();

    // Processing data for the main body of text...

    // Gathering data for the top row of text...
    tft.setTextSize(SCUI8T(2));
    strings[ATTEMPT] = "Attempt 1/3";
    int32_t attempt_text_width = (int32_t) tft.textWidth(strings[ATTEMPT], 1);
    strings[SCORE] = "Score 0";
    int32_t score_text_width = (int32_t) tft.textWidth(strings[SCORE], 1);
    int32_t attempt_text_height, score_text_height;
    attempt_text_height = score_text_height = (int32_t) tft.fontHeight(static_cast<int16_t>(1));

    // Gathering data for the bottom row of text...
    tft.setTextSize(SCUI8T(5));
    int32_t color_text_width = (int32_t) tft.textWidth("RED 00-22", 1); // (arbitrary string)
    int32_t color_text_height = (int32_t) tft.fontHeight(static_cast<int16_t>(1));

    // Gathering data about the entire block of text...
    int32_t text_block_height = attempt_text_height + PADDING + color_text_height;
                             // score_text_height + ...
    // text_block_width == color_text_width

    positions[COLOR].x = (viewport_width - color_text_width) / static_cast<int32_t>(2);
    positions[ATTEMPT].x = positions[COLOR].x;
    positions[SCORE].x = positions[COLOR].x + color_text_width - score_text_width;

    positions[ATTEMPT].y = positions[SCORE].y = (viewport_height - text_block_height) / static_cast<int32_t>(2);
    positions[COLOR].y = positions[ATTEMPT].y + attempt_text_height + PADDING;
                      // positions[SCORE].y + ...

    current_color = getNextColor(true);
    current_range = generateRange();
    
    if (current_range.low != SCUI8T(0))
        strings[COLOR] = "RED " + String((unsigned char) current_range.low, (unsigned char) HEX) + "-" + \
            String((unsigned char) current_range.high, (unsigned char) HEX);
    else
        strings[COLOR] = "RED 00-" + String((unsigned char) current_range.high, (unsigned char) HEX);
    strings[COLOR].toUpperCase(); // Ensuring correct formatting (e.g., "AA" instead of "aa")...

    // Processing data for the high score text...

    tft.setTextSize(SCUI8T(2));
    strings[HIGH_SCORE] = "High Score " + String((unsigned char) high_score);
    int32_t high_score_text_width = (int32_t) tft.textWidth(strings[HIGH_SCORE], 1);
    positions[HIGH_SCORE].x = (viewport_width - high_score_text_width) / SCUI32T(2);
    positions[HIGH_SCORE].y = viewport_height - attempt_text_height - PADDING;
    // attempt_text_height == score_text_height == high_score_text_height
}

range_t generateRange(void) {
    /*
     * Generates a range following the NN-MM format
     * where NN and MM are repeating hexadecimal digits
     * and NN-MM cover 20% of the 0-255 range
     */

    range_t ret = {SCUI8T(0), SCUI8T(0)};

    uint8_t temp = esp_random() % 13;

    ret.low = temp;
    ret.low <<= 4;
    ret.low |= temp;

    temp += 3;

    ret.high = temp;
    ret.high <<= 4;
    ret.high |= temp;

    return ret;
}

uint16_t getNextColor(bool reset) {
    /*
     * Returns the next color in the red-green-blue sequence
     * (or red by default if the game has been reset after a loss)
     */

    static uint16_t colors[3] = {SCUI16T(TFT_RED), SCUI16T(TFT_GREEN), SCUI16T(TFT_BLUE)};
    static int index = 0;

    if (reset) {
        index = 0;
        return SCUI16T(TFT_RED);
    }

    index = (index + 1) % 3;
    return colors[index];
}

void drawText(uint16_t color_text_color) {
    /*
     * Draws the text on the display using the `strings` and `positions` variables
     */

    // Clearing the screen...
    tft.fillScreen(TFT_BLACK);

    // Drawing the main body of text...

    // Drawing the top row of text...
    tft.setTextSize(SCUI8T(2));
    tft.setTextColor(SCUI16T(TFT_WHITE));
    tft.drawString(strings[ATTEMPT], positions[ATTEMPT].x, positions[ATTEMPT].y, 1);
    tft.drawString(strings[SCORE], positions[SCORE].x, positions[SCORE].y, 1);

    // Drawing the bottom row of text...
    tft.setTextSize(SCUI8T(5));
    tft.setTextColor(color_text_color); // !
    tft.drawString(strings[COLOR], positions[COLOR].x, positions[COLOR].y, 1);

    // Drawing the high score text...

    tft.setTextSize(SCUI8T(2));
    tft.setTextColor(SCUI16T(TFT_WHITE));
    tft.drawString(strings[HIGH_SCORE], positions[HIGH_SCORE].x, positions[HIGH_SCORE].y, 1);
}

inline int updateTextHelper(String &score_string) {
    /*
     * Helper function for updateText
     *
     * Modifies score_string to contain the current score as a string
     * Returns the current score as an int
     */
    int space_pos = (int) strings[SCORE].indexOf(' ');
    score_string = strings[SCORE].substring(space_pos + 1);
    return score_string.toInt();
}

void updateText(uint8_t flag) {
    String score_string = "";
    int score_int;
    uint8_t update_color_text = SCUI8T(0); // (0: leave the color text as is)

    switch (flag) {
        case SCUI8T(2): // (2: update the attempt text after a failed guess)
            switch (strings[ATTEMPT][8]) { // Attempt _/3
                                           //         ^
                case '1':
                    strings[ATTEMPT] = "Attempt 2/3";
                    break;
                case '2':
                    strings[ATTEMPT] = "Attempt 3/3";
                    break;
                default:
                    Serial.println("Error: updateText: unknown error");
            }
            break;
        case SCUI8T(1): // (1: update all the text after a successful guess)
            score_int = updateTextHelper(score_string);
            score_int++;
            score_string = "Score " + String(score_int);
            
            strings[ATTEMPT] = "Attempt 1/3";

            update_color_text = SCUI8T(1); // (1: cycle to the next color text)

            break;
        case SCUI8T(0): // (0: game over--reset all the text)
            // Updating the high score if necessary...
            score_int = updateTextHelper(score_string);
            if ((uint8_t) score_int > high_score) {
                high_score = score_int;
                strings[HIGH_SCORE] = "High Score " + String((unsigned char) high_score);
                uint8_t *signal = (uint8_t *) malloc(2);
                *signal = high_score;
                esp_now_send(broadcast_address, signal, 2);
                free(signal);
            }

            strings[ATTEMPT] = "Attempt 1/3";
            score_string = "Score 0";
            update_color_text = SCUI8T(2); // (2: reset the color text)
            break;
        default:
            Serial.println("Error: updateText: unknown error");
    }

    // Updating the score text if applicable...
    if (score_string != "") {
        tft.setTextSize(SCUI8T(2));
        int32_t curr_score_text_width = (int32_t) tft.textWidth(strings[SCORE], 1);
        int32_t new_score_text_width = (int32_t) tft.textWidth(score_string, 1);
        if (curr_score_text_width < new_score_text_width)
            positions[SCORE].x -= (new_score_text_width - curr_score_text_width);
        else if (curr_score_text_width > new_score_text_width)
            positions[SCORE].x += (curr_score_text_width - new_score_text_width);
        strings[SCORE] = score_string;
    }

    // Updating the color text if applicable...
    uint16_t new_color = current_color;
    switch (update_color_text) {
        case SCUI8T(0):
            break;
        case SCUI8T(1): // (1: cycle to the next color text)
            new_color = getNextColor(false);
            current_range = generateRange(); // (new range)

            switch (new_color) {
                case SCUI16T(TFT_RED):
                    strings[COLOR] = "RED ";
                    break;
                case SCUI16T(TFT_GREEN):
                    strings[COLOR] = "GRN ";
                    break;
                case SCUI16T(TFT_BLUE):
                    strings[COLOR] = "BLU ";
                    break;
                default:
                    Serial.println("Error: updateText: invalid color");
            }

            if (current_range.low != SCUI8T(0))
                strings[COLOR] = strings[COLOR] + String((unsigned char) current_range.low, HEX) + \
                    "-" + String((unsigned char) current_range.high, HEX);
            else
                strings[COLOR] = strings[COLOR] + "00-" + String((unsigned char) current_range.high, HEX);
            strings[COLOR].toUpperCase(); // Ensuring correct formatting (e.g., "AA" instead of "aa")...
            
            break;
        case SCUI8T(2): // (2: reset to red and generate a new range)
            new_color = getNextColor(true);
            current_range = generateRange(); // (new range)

            if (current_range.low != SCUI8T(0))
                strings[COLOR] = "RED " + String((unsigned char) current_range.low, HEX) + \
                    "-" + String((unsigned char) current_range.high, HEX);
            else
                strings[COLOR] = "RED 00-" + String((unsigned char) current_range.high, HEX);
            strings[COLOR].toUpperCase(); // ...

            break;
        default:
            Serial.println("Error: updateText: invalid update_color_text value");
    }

    current_color = new_color;
    drawText(current_color);
}

void setup(void) {
    Serial.begin(9600);

    // Initializing the display...
    initDisplay();

    // Showing the loading screen...
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(SCUI8T(1));
    tft.setTextColor(TFT_WHITE);
    tft.drawString("Loading...", 0, 0, 1);

    // Establishing a connection with the NodeMCU-ESP32 ESP32 DEVKITV1...
    WiFi.mode(WIFI_MODE_STA);
    esp_now_init();
    esp_now_register_send_cb(postOutgoing);
    memcpy(peer_info.peer_addr, broadcast_address, static_cast<size_t>(6)); // ?
    peer_info.channel = SCUI8T(0);
    peer_info.encrypt = false;
    esp_now_add_peer(&peer_info);
    esp_now_register_recv_cb(processIncoming);

    delay(1000); // Waiting for the NodeMCU-ESP32 ESP32 DEVKITV1 to boot...

    uint8_t hello = SCUI8T(0);
    esp_now_send(broadcast_address, &hello, 1); // Sending a "hello" signal...

    unsigned long then = millis();
    while (received == 0) { // Waiting for a "hello" back...
        if (millis() - then >= static_cast<unsigned long>(5000)) {
            tft.fillScreen(TFT_BLACK);
            tft.drawString("Failed to connect. Reboot and try again.", 0, 0, 1);
            kill(); // Aborting...
        }
    }

    // Initializing and drawing the text...
    initText();
    drawText(current_color);
}

void loop(void) {

}