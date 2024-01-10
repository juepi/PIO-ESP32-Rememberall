/*
 *   ESP32 Template
 *   User specific defines and Function declarations
 */
#ifndef USER_CONFIG_H
#define USER_CONFIG_H

#include "mqtt-ota-config.h"
#include <FastLED.h>
#include <OneButtonTiny.h>
#include <GxEPD2_3C.h>
#include <Fonts/FreeMonoBold18pt7b.h>

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
#define FL_GLOBAL_BRIGHTNESS 5   // LED ring powered with 3,3V, keep power usage low
#define FL_RING_BEATSIN_COSY 13  // Beatsin slow speed for cosy reminder
#define FL_RING_BEATSIN_AGGRO 26 // Beatsin fast speed for agressive reminder

// Globar char array for JSON string containing ePaper text and appointment infos
// larger MQTT_MAX_MSG_SIZE required
#define MQTT_MAX_MSG_SIZE 64
extern char taskTxtMsg[MQTT_MAX_MSG_SIZE];
extern char taskRemindrMsg[MQTT_MAX_MSG_SIZE];

// MQTT Topic to receive task infos
// Message format for taskTxt: "LineCount|ColorLine1;TextLine1|ColorLine2;TextLine2|..."
#define taskTxt_topic TOPTREE "taskTxt"
// Message format for taskReminder: "EpochTimeStamp_TaskDeadLine_in_hex|EpochTimeStamp_CosyReminder_in_hex|EpochTimeStamp_AgressiveReminder_in_hex|LedRingColor_in_0xRRGGBB"
#define taskReminder_topic TOPTREE "taskReminder"
// Position in the MqttSubscriptions array (to be able to keep track on topic updates)
#define I_taskTxtSub 3
#define I_taskRemindrSub 4

struct taskInfoStruct
{
    int LineCnt;                             // Number of lines to display
    char TextLines[3][D_CHARS_PER_LINE + 1]; // Text Lines
    uint16_t LineCol[3];                     // Color for each line
    time_t Deadline;                         // task deadline
    time_t CosyReminder;                     // cosy reminder
    time_t AgressiveReminder;                // agressive reminder
    uint32_t LedColor = 0xFF0000;            // Led reminder color (0xRRGGBB)
};

// Declare user setup and main loop functions
extern void user_setup();
extern void user_loop();

// Button callback function declarations
void ButtonClickCB();
void ButtonDoubleClickCB();
void ButtonLongPressCB();

// Display text drawing function with overloading up to 3 lines
void DisplayText(char *Text, uint16_t Color);
void DisplayText(char *Line1, uint16_t L1Color, char *Line2, uint16_t L2Color);
void DisplayText(char *Line1, uint16_t L1Color, char *Line2, uint16_t L2Color, char *Line3, uint16_t L3Color);

// Decoding functions for received MQTT messages
bool DecodeDispTextMsg(char *msg, taskInfoStruct *TaskData);
bool DecodeReminderMsg(char *msg, taskInfoStruct *TaskData);

// Button Actions
typedef enum
{
    B_VOID,        // no action
    B_WIFI_TOGGLE, // toggle WiFi
    B_LEDR_TOGGLE, // toggle LED Ring
    B_DISP_REFRESH // Refresh ePaper display
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