/*
 * ESP32 Rememberall
 * ==================
 */
#include "setup.h"

// Set up LED ring FastLED instance
CRGB LedRing[FL_RING_NUM_LEDS];

// Setup OneButton instance
OneButtonTiny Button(BUTTON_GPIO, true, true); // setup active low button with internal pullup enabled
ButtonActions ExecButtonActn = B_VOID;         // no action when starting

// Setup ePaper display instance
SPIClass spi2(HSPI);
GxEPD2_3C<GxEPD2_213c, GxEPD2_213c::HEIGHT> Display(GxEPD2_213c(D_CS, D_DC, D_RST, D_BUSY)); // GDEW0213Z16 104x212, UC8151 (IL0373)

// Global string containing text to display
char eventTxtMsg[MQTT_MAX_MSG_SIZE];
char eventReminderMsg[MQTT_MAX_MSG_SIZE];
char StatusMsg[MQTT_MAX_MSG_SIZE];

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
  // Double click - unused
  Button.attachDoubleClick(ButtonDoubleClickCB);
  // set 50ms debouncing time
  Button.setDebounceMs(50);

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
  static bool EventAcknowledged = false;
  static bool ButtonActionEventAck = false;
  static uint32_t LastTxtMsgDecoded = 0;
  static uint32_t LastReminderMsgDecoded = 0;
  static uint32_t LastStatusMsgDecoded = 0;
  static bool RunDisplayRefresh = false;
  static bool RunReminders = false;
  static eventInfoStruct LocalEventInfo;
  static bool Cosy = true;
  static time_t NextWiFiStart = 0;

  // keep watching the push button:
  Button.tick();

  // Execute Button action if requested
  switch (ExecButtonActn)
  {
  case B_VOID:
    break;
  case B_WIFI_TOGGLE:
    if (NetState != NET_DOWN)
    {
      wifi_down();
      NextWiFiStart = EpochTime + (time_t)WIFI_SLEEP_DURATION;
    }
    else
    {
      // Force WiFi restart
      NextWiFiStart = 0;
    }
    ExecButtonActn = B_VOID;
    break;
  case B_ACK_EVENT:
    ButtonActionEventAck = true;
    EventAcknowledged = true;
    if (NetState == NET_UP)
    {
      // Send event confirmation to broker (and make sure it's been sent)
      while (!mqttClt.publish(Status_topic, String("ack").c_str(), true))
      {
        MqttDelay(250);
      }
    }
    else
    {
      // Force immediate WiFi restart
      NextWiFiStart = 0;
    }
    ExecButtonActn = B_VOID;
    break;
  case B_SLEEP:
    esp_deep_sleep((uint64_t)BUT_SLEEP_DURATION * 1000000ULL);
    ExecButtonActn = B_VOID;
    break;
  }

  // check Status message
  if (MqttSubscriptions[I_StatusSub].MsgRcvd > LastStatusMsgDecoded)
  {
    if (strcmp(StatusMsg, "ack") == 0)
    {
      // Status of current event has already been acknowledged (in a previous activeReminderPeriod)
      EventAcknowledged = true;
    }
    else
    {
      EventAcknowledged = false;
    }
    LastStatusMsgDecoded = MqttSubscriptions[I_StatusSub].MsgRcvd;
  }

  // Check if a new event messages arrived only if we have valid local time
  if (MqttSubscriptions[I_eventReminderSub].MsgRcvd > LastReminderMsgDecoded && NTPSyncCounter > 0)
  {
    // New text message arrived, decode and update struct
    RunReminders = DecodeReminderMsg(eventReminderMsg, &LocalEventInfo);
    LastReminderMsgDecoded = MqttSubscriptions[I_eventReminderSub].MsgRcvd;
  }
  if (MqttSubscriptions[I_eventTxtSub].MsgRcvd > LastTxtMsgDecoded && NTPSyncCounter > 0)
  {
    // New text message arrived, decode and update struct
    RunDisplayRefresh = DecodeDispTextMsg(eventTxtMsg, &LocalEventInfo);
    LastTxtMsgDecoded = MqttSubscriptions[I_eventTxtSub].MsgRcvd;
  }

  // Run LED Ring Reminder
  if (RunReminders && NTPSyncCounter > 0)
  {
    if (EpochTime > LocalEventInfo.Deadline || EventAcknowledged)
    {
      // it's too late.. or event acknowledged by user
      RunReminders = false;
      digitalWrite(EMB_PWS_U2, LOW); // Power down LED ring
      fill_solid(LedRing, FL_RING_NUM_LEDS, CRGB::Black);
      LedRingEnabled = false;
      // Initiate display refresh (clear)
      RunDisplayRefresh = true;
    }
    else if (EpochTime > LocalEventInfo.CosyReminder && EpochTime < LocalEventInfo.AgressiveReminder)
    {
      // Fire up cosy reminder
      if (!LedRingEnabled)
      {
        digitalWrite(EMB_PWS_U2, HIGH); // Power up LED ring
        delay(20);
        LedRingEnabled = true;
      }
      Cosy = true;
    }
    else
    {
      // Fire up agressive reminder
      if (!LedRingEnabled)
      {
        digitalWrite(EMB_PWS_U2, HIGH); // Power up LED ring
        delay(20);
        LedRingEnabled = true;
      }
      Cosy = false;
    }
  }

  if (RunDisplayRefresh && NTPSyncCounter > 0 && LastReminderMsgDecoded > 0)
  {
    if (EpochTime > LocalEventInfo.Deadline || EventAcknowledged)
    {
      // Event started in the past or has been acknowledged by the user, clear screen
      Display.clearScreen();
      Display.hibernate();
    }
    else
    {
      // Display text lines according to LineCnt
      switch (LocalEventInfo.LineCnt)
      {
      case 1:
        DisplayText(LocalEventInfo.TextLines[0], LocalEventInfo.LineCol[0]);
        break;
      case 2:
        DisplayText(LocalEventInfo.TextLines[0], LocalEventInfo.LineCol[0],
                    LocalEventInfo.TextLines[1], LocalEventInfo.LineCol[1]);
        break;
      case 3:
        DisplayText(LocalEventInfo.TextLines[0], LocalEventInfo.LineCol[0],
                    LocalEventInfo.TextLines[1], LocalEventInfo.LineCol[1],
                    LocalEventInfo.TextLines[2], LocalEventInfo.LineCol[2]);
        break;
      }
    }
    RunDisplayRefresh = false;
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
    LedRing[pos] += CRGB(LocalEventInfo.LedColor);
    // fill_rainbow_circular(LedRing, FL_RING_NUM_LEDS, millis() / 15);
    FastLED.show();
  }

  // Handle WiFi
  // WiFi currently off, start it at scheduled NextWiFiStart
  if (EpochTime > NextWiFiStart && NetState == NET_DOWN)
  {
    wifi_up();
    LastReminderMsgDecoded = 0;
    LastTxtMsgDecoded = 0;
    LastStatusMsgDecoded = 0;
    if (ButtonActionEventAck)
    {
      // Send event confirmation to broker (and make sure it's been sent) when button was double-clicked
      ButtonActionEventAck = false;
      delay(200);
      while (!mqttClt.publish(Status_topic, String("ack").c_str(), true))
      {
        MqttDelay(250);
      }
      // Add some more delay just to make sure..
      MqttDelay(300);
      // ..and sleep for a while
      esp_deep_sleep((uint64_t)WIFI_SLEEP_DURATION * 1000000ULL);
    }
  }
  // In case all network traffic has been handled, WiFi can be disabled for WIFI_SLEEP_DURATION
  else if (LastStatusMsgDecoded > 0 && LastReminderMsgDecoded > 0 && LastTxtMsgDecoded > 0 && NTPSyncCounter > 0 && NetState != NET_DOWN)
  {
    wifi_down();
    NextWiFiStart = EpochTime + (time_t)WIFI_SLEEP_DURATION;
    // If requested, ESP may go to sleep at the end of this main loop
    DelayDeepSleep = false;
  }

  // If Infos are missing, add some delay for WiFi background tasks
  if (LastStatusMsgDecoded == 0 || LastReminderMsgDecoded == 0 || LastTxtMsgDecoded == 0)
  {
    // Delay DeepSleep until everything has been received
    DelayDeepSleep = true;
#ifdef ONBOARD_LED
    ToggleLed(LED, 50, 2);
#else
    delay(100);
#endif
  }

#ifdef ONBOARD_LED
  if (NetState == NET_UP)
  {
    digitalWrite(LED, LEDON);
  }
  else
  {
    digitalWrite(LED, LEDOFF);
  }
#endif
}

// ======================================================================================================
// =========================== User Functions ===========================================================
// ======================================================================================================
//
// Button callback functions
//
void ButtonClickCB()
{
  // Skip reminding and sleep for a while
  ExecButtonActn = B_SLEEP;
}

void ButtonLongPressCB()
{
  // Toggle WiFi on/off on long press
  ExecButtonActn = B_WIFI_TOGGLE;
}

void ButtonDoubleClickCB()
{
  // Toggle WiFi on/off on long press
  ExecButtonActn = B_ACK_EVENT;
}

//
// Print text on Display
//
void DisplayText(char *SingleLine, uint16_t Color)
{
  // 1 is an alias for red (to shorten data in MQTT message)
  Color = (Color == 1) ? GxEPD_RED : Color;
  Display.setRotation(D_LANDSCAPE_ROT);
  Display.setFont(&FreeMonoBold18pt7b);
  Display.setTextColor(Color);
  int16_t tbx, tby;
  uint16_t tbw, tbh;
  Display.getTextBounds(SingleLine, 0, 0, &tbx, &tby, &tbw, &tbh);
  // center the bounding box by transposition of the origin:
  uint16_t x = ((Display.width() - tbw) / 2) - tbx;
  uint16_t y = ((Display.height() - tbh) / 2) - tby;
  Display.setFullWindow();
  Display.firstPage();
  do
  {
    Display.fillScreen(GxEPD_WHITE);
    Display.setCursor(x, y);
    Display.print(SingleLine);
  } while (Display.nextPage());
  Display.hibernate();
}

void DisplayText(char *Line1, uint16_t L1Color, char *Line2, uint16_t L2Color)
{
  // 1 is an alias for red (to shorten data in MQTT message)
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
  // 1 is an alias for red (to shorten data in MQTT message)
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

bool DecodeDispTextMsg(char *msg, eventInfoStruct *EventData)
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
    // Fill our eventInfoStruct with the available data
    EventData->LineCnt = LineCount;
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
        EventData->LineCol[i] = (uint16_t)atoi(tok2[0]);
        // Second token is the text itself
        strcpy(EventData->TextLines[i], tok2[1]);
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

bool DecodeReminderMsg(char *msg, eventInfoStruct *EventData)
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
    EventData->Deadline = (time_t)strtol(String(tokens[0]).c_str(), NULL, 16);
    EventData->CosyReminder = (time_t)strtol(String(tokens[1]).c_str(), NULL, 16);
    EventData->AgressiveReminder = (time_t)strtol(String(tokens[2]).c_str(), NULL, 16);
    EventData->LedColor = (uint32_t)strtol(String(tokens[3]).c_str(), NULL, 16);
  }
  return true;
}