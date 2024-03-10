/*
 *   ESP32 Rememberall Configuration
 *   =================================
 */
#ifndef USER_CONFIG_H
#define USER_CONFIG_H

#include "mqtt-ota-config.h"
#include <FastLED.h>
#include <OneButtonTiny.h>
#include <GxEPD2_3C.h>
#include <Fonts/FreeMonoBold18pt7b.h>

//
// Generic settings
//
#define WIFI_SLEEP_DURATION 1800 // seconds that Wifi will be off for power saving after receiving all required data

//
// ePaper Display Configuration (Type: WaveShare GDEW0213Z16, 3 color)
//
// Pinout
#define D_MOSI 35
#define D_CLK 36
#define D_MISO 37
#define D_CS 34
#define D_DC 33
#define D_RST 21
#define D_BUSY 18
#define D_LANDSCAPE_ROT 3   // landscape with cables pointing downwards
#define D_CHARS_PER_LINE 10 // characters per line (using fixed width font!)
#define D_X_OFFSET 1        // Pixel offset from the left display edge for first character
#define D_Y_OFFSET 30       // Pixel offset for the first line
#define D_Y_LINEHEIGTH 32   // Pixel heigth of each line

//
// FastLED Configuration
//
#define FL_RING_NUM_LEDS 32
#define FL_RING_DATA_PIN 13
#define FL_RING_LED_TYPE WS2812B
#define FL_RING_RGB_ORDER GRB    // usual color order for WS2812 chips
#define FL_GLOBAL_BRIGHTNESS 8   // LED ring powered with 3,3V, keep power usage low
#define FL_RING_BEATSIN_COSY 12  // Beatsin slow speed for cosy reminder
#define FL_RING_BEATSIN_AGGRO 32 // Beatsin fast speed for agressive reminder

//
// Button Configuration
//
#define BUTTON_GPIO 12 // other wire of the pushbutton needs to be wired to GND - shorting the button pulls GPIO LOW
#define BUT_SLEEP_DURATION 21600 // Sleep for 6hrs on button single click

// Globar char arrays for topics containing ePaper text and appointment infos
// larger MQTT_MAX_MSG_SIZE required
#define MQTT_MAX_MSG_SIZE 64
extern char eventTxtMsg[MQTT_MAX_MSG_SIZE];
extern char eventReminderMsg[MQTT_MAX_MSG_SIZE];
extern char StatusMsg[MQTT_MAX_MSG_SIZE];

//
// MQTT Topic tree prepended to all topics
// ATTN: Must end with "/"!
//
#define TOPTREE "HB7/Indoor/VZ/Rememberall/"

// MQTT Topic to receive event infos
// Message format for eventTxt: "LineCount|ColorLine1;TextLine1|ColorLine2;TextLine2|..."
#define eventTxt_topic TOPTREE "eventTxt"
// Message format for eventReminder: "EpochTimeStamp_EventDeadLine_in_hex|EpochTimeStamp_CosyReminder_in_hex|EpochTimeStamp_AgressiveReminder_in_hex|LedRingColor_in_0xRRGGBB"
#define eventReminder_topic TOPTREE "eventReminder"
#define Status_topic TOPTREE "Status" // Text message of what Rememberall is currently doing; set to "ack" if current reminder has been acknowledged by pressing the button
// Position in the MqttSubscriptions array (to be able to keep track on topic updates)
#define I_eventTxtSub 3
#define I_eventReminderSub 4
#define I_StatusSub 5

struct eventInfoStruct
{
    int LineCnt;                             // Number of lines to display
    char TextLines[3][D_CHARS_PER_LINE + 1]; // Text Lines
    uint16_t LineCol[3];                     // Color for each line
    time_t Deadline;                         // event deadline
    time_t CosyReminder;                     // cosy reminder
    time_t AgressiveReminder;                // agressive reminder
    uint32_t LedColor = 0xFF0000;            // Led reminder color (0xRRGGBB)
};

// Declare user setup and main loop functions
extern void user_setup();
extern void user_loop();

// Button callback function declarations
void ButtonClickCB();
void ButtonLongPressCB();
void ButtonDoubleClickCB();

// Display text drawing function with overloading up to 3 lines
void DisplayText(char *Text, uint16_t Color);
void DisplayText(char *Line1, uint16_t L1Color, char *Line2, uint16_t L2Color);
void DisplayText(char *Line1, uint16_t L1Color, char *Line2, uint16_t L2Color, char *Line3, uint16_t L3Color);

// Decoding functions for received MQTT messages
bool DecodeDispTextMsg(char *msg, eventInfoStruct *EventData);
bool DecodeReminderMsg(char *msg, eventInfoStruct *EventData);

// Button Actions
typedef enum
{
    B_VOID,        // no action
    B_WIFI_TOGGLE, // toggle WiFi (long button press)
    B_ACK_EVENT,   // acknowledge current event (short button press)
    B_SLEEP,
} ButtonActions;

//
// Use RTC RAM to store Variables that should survive DeepSleep
//
// ATTN: define KEEP_RTC_SLOWMEM or vars will be lost (PowerDomain disabled)
// #define KEEP_RTC_SLOWMEM

#ifdef KEEP_RTC_SLOWMEM
// Example
extern RTC_DATA_ATTR int SaveMe;
#endif

#endif // USER_CONFIG_H