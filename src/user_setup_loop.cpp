/*
 * ESP32 Template
 * ==================
 * User specific function "user_loop" called in main loop
 * User specific funtion "user_setup" called in setup loop
 * Add stuff you want to run here
 */
#include "setup.h"

// Set up LED ring FastLED instance
CRGB LedRing[FL_RING_NUM_LEDS];

// Setup OneButton instance
OneButtonTiny Button(EMB_SW1, true);
ButtonActions ExecButtonActn = B_VOID; // no action when starting

// Setup ePaper display instance
SPIClass spi2(HSPI);
GxEPD2_3C<GxEPD2_213c, GxEPD2_213c::HEIGHT> Display(GxEPD2_213c(D_CS, D_DC, D_RST, D_BUSY)); // GDEW0213Z16 104x212, UC8151 (IL0373)
// Test Text for Display
char TextLine[D_CHARS_PER_LINE + 1] = "REMEMBRALL";

// Global string containing text to display
char taskTxtMsg[MQTT_MAX_MSG_SIZE];
char taskRemindrMsg[MQTT_MAX_MSG_SIZE];

// Variables that should be saved during DeepSleep
#ifdef KEEP_RTC_SLOWMEM
RTC_DATA_ATTR int SaveMe = 0;
#endif

/*
 * User Setup function
 * ========================================================================
 */
void user_setup()
{
  // Setup LedRing power supply and startup FastLED
  pinMode(EMB_PWS_U2, OUTPUT);
  digitalWrite(EMB_PWS_U2, LOW);
  FastLED.addLeds<FL_RING_LED_TYPE, FL_RING_DATA_PIN, FL_RING_RGB_ORDER>(LedRing, FL_RING_NUM_LEDS);
  FastLED.setBrightness(FL_GLOBAL_BRIGHTNESS);

  // Configure Button functions
  // link the myClickFunction function to be called on a click event.
  Button.attachClick(ButtonClickCB);
  // link the long-press function to be called on a long-press event.
  Button.attachLongPressStart(ButtonLongPressCB);
  // Double click
  Button.attachDoubleClick(ButtonDoubleClickCB);
  // set 80 msec. debouncing time. Default is 50 msec.
  Button.setDebounceMs(80);

  // Init Display w/o serial diag and custom SPI pinout
  spi2.begin(D_CLK, D_MISO, D_MOSI, D_CS);
  Display.epd2.selectSPI(spi2, SPISettings(4000000, MSBFIRST, SPI_MODE0));
  Display.init(0, true, 2, false);
}

/*
 * User Main Loop
 * ========================================================================
 */
void user_loop()
{
  static bool LedRingEnabled = false;
  static uint32_t LastTxtMsgDecoded = 0;
  static uint32_t LastRemindrMsgDecoded = 0;
  static bool RunDisplayRefresh = false;
  static bool RunReminders = false;
  static taskInfoStruct LocalTaskInfo;
  static bool Cosy = true;

  // keep watching the push button:
  Button.tick();

  // Check if a new task messages arrived
  if (MqttSubscriptions[I_taskTxtSub].MsgRcvd > LastTxtMsgDecoded)
  {
    // New text message arrived, decode and update struct
    RunDisplayRefresh = DecodeDispTextMsg(taskTxtMsg, &LocalTaskInfo);
    LastTxtMsgDecoded = MqttSubscriptions[I_taskTxtSub].MsgRcvd;
  }
  if (MqttSubscriptions[I_taskRemindrSub].MsgRcvd > LastRemindrMsgDecoded)
  {
    // New text message arrived, decode and update struct
    RunReminders = DecodeReminderMsg(taskRemindrMsg, &LocalTaskInfo);
    LastRemindrMsgDecoded = MqttSubscriptions[I_taskRemindrSub].MsgRcvd;
    RunReminders = true;
  }

  if (RunDisplayRefresh)
  {
    // Display text lines according to LineCnt
    switch (LocalTaskInfo.LineCnt)
    {
    case 1:
      DisplayText(LocalTaskInfo.TextLines[0], LocalTaskInfo.LineCol[0]);
      break;
    case 2:
      DisplayText(LocalTaskInfo.TextLines[0], LocalTaskInfo.LineCol[0],
                  LocalTaskInfo.TextLines[1], LocalTaskInfo.LineCol[1]);
      break;
    case 3:
      DisplayText(LocalTaskInfo.TextLines[0], LocalTaskInfo.LineCol[0],
                  LocalTaskInfo.TextLines[1], LocalTaskInfo.LineCol[1],
                  LocalTaskInfo.TextLines[2], LocalTaskInfo.LineCol[2]);
      break;
    }
    RunDisplayRefresh = false;
  }

  // Run LED Ring Reminder
  if (RunReminders)
  {
    if (EpochTime > LocalTaskInfo.Deadline)
    {
      // it's too late..
      RunReminders = false;
      digitalWrite(EMB_PWS_U2, LOW); // Power down LED ring
      fill_solid(LedRing, FL_RING_NUM_LEDS, CRGB::Black);
      LedRingEnabled = false;
    }
    else if (EpochTime > LocalTaskInfo.CosyReminder && EpochTime < LocalTaskInfo.AgressiveReminder)
    {
      // Fire up cosy reminder
      digitalWrite(EMB_PWS_U2, HIGH); // Power up LED ring
      delay(20);
      LedRingEnabled = true;
      Cosy = true;
    }
    else
    {
      // Fire up agressive reminder
      digitalWrite(EMB_PWS_U2, HIGH); // Power up LED ring
      delay(20);
      LedRingEnabled = true;
      Cosy = false;
    }
  }

  // Execute Button action if requested
  switch (ExecButtonActn)
  {
  case B_VOID:
    break;
  case B_WIFI_TOGGLE:
    if (NetState != NET_DOWN)
    {
      wifi_down();
    }
    else
    {
      wifi_up();
    }
    ExecButtonActn = B_VOID;
    break;
  case B_LEDR_TOGGLE:
    if (LedRingEnabled)
    {
      LedRingEnabled = false;
      digitalWrite(EMB_PWS_U2, LOW);
      fill_solid(LedRing, FL_RING_NUM_LEDS, CRGB::Black);
    }
    else
    {
      LedRingEnabled = true;
      digitalWrite(EMB_PWS_U2, HIGH); // Power up LED ring
      delay(20);
    }
    ExecButtonActn = B_VOID;
    break;
  case B_DISP_REFRESH:
    DisplayText(TextLine, GxEPD_BLACK, TextLine, GxEPD_RED, TextLine, GxEPD_BLACK);
    ExecButtonActn = B_VOID;
    break;
  }

  if (LedRingEnabled)
  {
    // sinelon FastLED animation
    static int pos;
    fadeToBlackBy(LedRing, FL_RING_NUM_LEDS, 20);
    if (Cosy)
    {
      pos = beatsin16(FL_RING_BEATSIN_COSY, 0, FL_RING_NUM_LEDS - 1);
    }
    else
    {
      pos = beatsin16(FL_RING_BEATSIN_AGGRO, 0, FL_RING_NUM_LEDS - 1);
    }
    LedRing[pos] += CRGB(LocalTaskInfo.LedColor);
    // fill_rainbow_circular(LedRing, FL_RING_NUM_LEDS, millis() / 15);
    FastLED.show();
  }

#ifdef ONBOARD_LED
  // Toggle LED at each loop if WiFi is up
  if (NetState == NET_UP)
  {
    ToggleLed(LED, 1, 1);
  }
  else
  {
    digitalWrite(LED, LEDOFF);
  }
#endif
}

//
// Button callback functions
//
void ButtonClickCB()
{
  // Toggle LedRing on short press
  ExecButtonActn = B_LEDR_TOGGLE;
}

void ButtonLongPressCB()
{
  // Toggle WiFi on/off on long press
  ExecButtonActn = B_WIFI_TOGGLE;
}

void ButtonDoubleClickCB()
{
  // Update display on double click
  ExecButtonActn = B_DISP_REFRESH;
}

//
// Print text on Display
//
void DisplayText(char *Text, uint16_t Color)
{
  // 1 is an alias for red (to shorten data in JSON message)
  Color = (Color == 1) ? GxEPD_RED : Color;
  Display.setRotation(D_LANDSCAPE_ROT);
  Display.setFont(&FreeMonoBold18pt7b);
  Display.setTextColor(Color);
  int16_t tbx, tby;
  uint16_t tbw, tbh;
  Display.getTextBounds(Text, 0, 0, &tbx, &tby, &tbw, &tbh);
  // center the bounding box by transposition of the origin:
  uint16_t x = ((Display.width() - tbw) / 2) - tbx;
  uint16_t y = ((Display.height() - tbh) / 2) - tby;
  Display.setFullWindow();
  Display.firstPage();
  do
  {
    Display.fillScreen(GxEPD_WHITE);
    Display.setCursor(x, y);
    Display.print(Text);
  } while (Display.nextPage());
  Display.hibernate();
}

void DisplayText(char *Line1, uint16_t L1Color, char *Line2, uint16_t L2Color)
{
  // 1 is an alias for red (to shorten data in JSON message)
  L1Color = (L1Color == 1) ? GxEPD_RED : L1Color;
  L2Color = (L2Color == 1) ? GxEPD_RED : L2Color;

  int16_t L1Offset = D_Y_LINEHEIGTH / 2 - 4;
  int16_t L2Offset = D_Y_LINEHEIGTH / 2 + 4;
  Display.setRotation(D_LANDSCAPE_ROT);
  Display.setFont(&FreeMonoBold18pt7b);
  Display.setFullWindow();
  Display.firstPage();
  do
  {
    Display.fillScreen(GxEPD_WHITE);
    Display.setCursor(D_X_OFFSET, D_Y_OFFSET + L1Offset);
    Display.setTextColor(L1Color);
    Display.print(Line1);
    Display.setCursor(D_X_OFFSET, D_Y_OFFSET + D_Y_LINEHEIGTH + L2Offset);
    Display.setTextColor(L2Color);
    Display.print(Line2);
  } while (Display.nextPage());
  Display.hibernate();
}

void DisplayText(char *Line1, uint16_t L1Color, char *Line2, uint16_t L2Color, char *Line3, uint16_t L3Color)
{
  // 1 is an alias for red (to shorten data in JSON message)
  L1Color = (L1Color == 1) ? GxEPD_RED : L1Color;
  L2Color = (L2Color == 1) ? GxEPD_RED : L2Color;
  L3Color = (L3Color == 1) ? GxEPD_RED : L3Color;

  Display.setRotation(D_LANDSCAPE_ROT);
  Display.setFont(&FreeMonoBold18pt7b);
  Display.setFullWindow();
  Display.firstPage();
  do
  {
    Display.fillScreen(GxEPD_WHITE);
    Display.setCursor(D_X_OFFSET, D_Y_OFFSET);
    Display.setTextColor(L1Color);
    Display.print(Line1);
    Display.setCursor(D_X_OFFSET, D_Y_OFFSET + D_Y_LINEHEIGTH);
    Display.setTextColor(L2Color);
    Display.print(Line2);
    Display.setCursor(D_X_OFFSET, D_Y_OFFSET + 2 * D_Y_LINEHEIGTH);
    Display.setTextColor(L3Color);
    Display.print(Line3);
  } while (Display.nextPage());
  Display.hibernate();
}

bool DecodeDispTextMsg(char *msg, taskInfoStruct *TaskData)
{
  char *tokens[6]; // maximum number of allowed tokens (2 more than expected)
  char *ptr = NULL;
  int index = 0;
  ptr = strtok(msg, "|"); // strtok removes the delimiter from the char array!
  while (ptr != NULL)
  {
    tokens[index] = ptr; // copy pointer to each split token of the original array
    index++;
    ptr = strtok(NULL, "|"); // no idea what this line does..
  }
  // handle the tokens
  // First token is a line counter: 1-3 lines allowed
  int LineCount = atoi(tokens[0]);
  if (LineCount > 0 && LineCount < 4 && LineCount == (index - 1))
  {
    // LineCount is OK and appropiate amount of tokens available
    // Fill our taskInfoStruct with the available data
    TaskData->LineCnt = LineCount;
    for (int i = 0; i < LineCount; i++)
    {
      // Split text color and text (ColorNum;Text)
      char *tok2[2];
      char *ptr2 = NULL;
      int idx2 = 0;
      ptr2 = strtok(tokens[i + 1], ";");
      while (ptr2 != NULL)
      {
        tok2[idx2] = ptr2; // copy pointer to each split token of the original array
        idx2++;
        ptr2 = strtok(NULL, ";"); // no idea what this line does..
      }
      // 2 tokens must be available
      if (idx2 == 2)
      {
        // First token is text color for this line
        TaskData->LineCol[i] = (uint16_t)atoi(tok2[0]);
        // Second token is the text itself
        strcpy(TaskData->TextLines[i], tok2[1]);
      }
      else
      {
        DEBUG_PRINTLN("Decode TXT Msg failed: No Line color or text for line " + String(i + 1));
        return false;
      }
    }
  }
  else
  {
    DEBUG_PRINTLN("Decode TXT Msg failed: LineCounter (" + String(LineCount) + ") does not match lines in data (" + String(index) + ")");
    return false;
  }
  return true;
}

bool DecodeReminderMsg(char *msg, taskInfoStruct *TaskData)
{
  char *tokens[6]; // maximum number of allowed tokens (2 more than expected)
  char *ptr = NULL;
  int index = 0;
  ptr = strtok(msg, "|"); // strtok removes the delimiter from the char array!
  while (ptr != NULL)
  {
    tokens[index] = ptr; // create pointer to each split token of the original array
    index++;
    ptr = strtok(NULL, "|"); // no idea what this line does..
  }
  // Expecting 4 tokens
  if (index != 4)
  {
    DEBUG_PRINTLN("Decode Reminder Msg failed: unable to extract 4 tokens from message");
    return false;
  }
  else
  {
    TaskData->Deadline = (time_t)strtol(String(tokens[0]).c_str(), NULL, 16);
    TaskData->CosyReminder = (time_t)strtol(String(tokens[1]).c_str(), NULL, 16);
    TaskData->AgressiveReminder = (time_t)strtol(String(tokens[2]).c_str(), NULL, 16);
    TaskData->LedColor = (uint32_t)strtol(String(tokens[3]).c_str(), NULL, 16);
  }
  return true;
}