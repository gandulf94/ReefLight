//#######################################################################################################
//#################################### Plugin 023: OLED SSD1306 display #################################
//#######################################################################################################

// Sample templates
//  Temp: [DHT11#Temperature]   Hum:[DHT11#humidity]
//  DS Temp:[Dallas1#Temperature#R]
//  Lux:[Lux#Lux#R]
//  Baro:[Baro#Pressure#R]

#define PLUGIN_023
#define PLUGIN_ID_023         23
#define PLUGIN_NAME_023       "Display - OLED SSD1306"
#define PLUGIN_VALUENAME1_023 "OLED"

byte Plugin_023_OLED_address = 0x3c;
byte Plugin_023_OLED_type = 0;

enum
{
  OLED_64x48   = 0x01,
  OLED_rotated = 0x02
};

boolean Plugin_023(byte function, struct EventStruct *event, String& string)
{
  boolean success = false;
  static byte displayTimer = 0;

  switch (function)
  {

    case PLUGIN_DEVICE_ADD:
      {
        Device[++deviceCount].Number = PLUGIN_ID_023;
        Device[deviceCount].Type = DEVICE_TYPE_I2C;
        Device[deviceCount].VType = SENSOR_TYPE_SINGLE;
        Device[deviceCount].Ports = 0;
        Device[deviceCount].PullUpOption = false;
        Device[deviceCount].InverseLogicOption = false;
        Device[deviceCount].FormulaOption = false;
        Device[deviceCount].ValueCount = 0;
        Device[deviceCount].SendDataOption = false;
        Device[deviceCount].TimerOption = true;
        break;
      }

    case PLUGIN_GET_DEVICENAME:
      {
        string = F(PLUGIN_NAME_023);
        break;
      }

    case PLUGIN_GET_DEVICEVALUENAMES:
      {
        strcpy_P(ExtraTaskSettings.TaskDeviceValueNames[0], PSTR(PLUGIN_VALUENAME1_023));
        break;
      }

    case PLUGIN_WEBFORM_LOAD:
      {
        byte choice = Settings.TaskDevicePluginConfig[event->TaskIndex][0];
        /*String options[2] = { F("3C"), F("3D") };*/
        int optionValues[2] = { 0x3C, 0x3D };
        addFormSelectorI2C(string, F("plugin_023_adr"), 2, optionValues, choice);

        byte choice2 = Settings.TaskDevicePluginConfig[event->TaskIndex][1];
        String options2[2] = { F("Normal"), F("Rotated") };
        int optionValues2[2] = { 1, 2 };
        addFormSelector(string, F("Rotation"), F("plugin_023_rotate"), 2, options2, optionValues2, choice2);

        byte choice3 = Settings.TaskDevicePluginConfig[event->TaskIndex][3];
        String options3[2] = { F("128x64"), F("64x48") };
        int optionValues3[2] = { 1, 2 };
        addFormSelector(string, F("Display Size"), F("plugin_023_size"), 2, options3, optionValues3, choice3);

        char deviceTemplate[8][64];
        LoadCustomTaskSettings(event->TaskIndex, (byte*)&deviceTemplate, sizeof(deviceTemplate));
        for (byte varNr = 0; varNr < 8; varNr++)
        {
        	addFormTextBox(string, String(F("Line ")) + (varNr + 1), String(F("Plugin_023_template")) + (varNr + 1), deviceTemplate[varNr], 64);
        }

        addFormPinSelect(string, F("Display button"), F("taskdevicepin3"), Settings.TaskDevicePin3[event->TaskIndex]);

        addFormNumericBox(string, F("Display Timeout"), F("plugin_23_timer"), Settings.TaskDevicePluginConfig[event->TaskIndex][2]);

        success = true;
        break;
      }

    case PLUGIN_WEBFORM_SAVE:
      {
        Settings.TaskDevicePluginConfig[event->TaskIndex][0] = getFormItemInt(F("plugin_023_adr"));
        Settings.TaskDevicePluginConfig[event->TaskIndex][1] = getFormItemInt(F("plugin_023_rotate"));
        Settings.TaskDevicePluginConfig[event->TaskIndex][2] = getFormItemInt(F("plugin_23_timer"));
        Settings.TaskDevicePluginConfig[event->TaskIndex][3] = getFormItemInt(F("plugin_023_size"));

        char deviceTemplate[8][64];
        for (byte varNr = 0; varNr < 8; varNr++)
        {
          String arg = F("Plugin_023_template");
          arg += varNr + 1;
          String tmpString = WebServer.arg(arg);
          strncpy(deviceTemplate[varNr], tmpString.c_str(), sizeof(deviceTemplate[varNr])-1);
          deviceTemplate[varNr][63]=0;
        }

        SaveCustomTaskSettings(event->TaskIndex, (byte*)&deviceTemplate, sizeof(deviceTemplate));
        success = true;
        break;
      }

    case PLUGIN_INIT:
      {
        Plugin_023_OLED_type = 0;
        Plugin_023_OLED_address = Settings.TaskDevicePluginConfig[event->TaskIndex][0];
        Plugin_023_StartUp_OLED();
        Plugin_023_clear_display();
        if (Settings.TaskDevicePluginConfig[event->TaskIndex][1] == 2)
        {
          Plugin_023_OLED_type |= OLED_rotated;
          Plugin_023_sendcommand(0xA0 | 0x1);      //SEGREMAP   //Rotate screen 180 deg
          Plugin_023_sendcommand(0xC8);            //COMSCANDEC  Rotate screen 180 Deg
        }
        if (Settings.TaskDevicePluginConfig[event->TaskIndex][3] == 2)
        {
          Plugin_023_OLED_type |= OLED_64x48;
        }

        Plugin_023_sendStrXY("ESP Easy ", 0, 0);
        displayTimer = Settings.TaskDevicePluginConfig[event->TaskIndex][2];
        if (Settings.TaskDevicePin3[event->TaskIndex] != -1)
          pinMode(Settings.TaskDevicePin3[event->TaskIndex], INPUT_PULLUP);
        success = true;
        break;
      }

    case PLUGIN_TEN_PER_SECOND:
      {
        if (Settings.TaskDevicePin3[event->TaskIndex] != -1)
        {
          if (!digitalRead(Settings.TaskDevicePin3[event->TaskIndex]))
          {
            Plugin_023_displayOn();
            displayTimer = Settings.TaskDevicePluginConfig[event->TaskIndex][2];
          }
        }
        break;
      }

    case PLUGIN_ONCE_A_SECOND:
      {
        if ( displayTimer > 0)
        {
          displayTimer--;
          if (displayTimer == 0)
            Plugin_023_displayOff();
        }
        break;
      }

    case PLUGIN_READ:
      {
        char deviceTemplate[8][64];
        LoadCustomTaskSettings(event->TaskIndex, (byte*)&deviceTemplate, sizeof(deviceTemplate));

        for (byte x = 0; x < 8; x++)
        {
          String tmpString = deviceTemplate[x];
          if (tmpString.length())
          {
            String newString = P023_parseTemplate(tmpString, 16);
            Plugin_023_sendStrXY(newString.c_str(), x, 0);
          }
        }
        success = false;
        break;
      }

    case PLUGIN_WRITE:
      {
        String tmpString  = string;
        int argIndex = tmpString.indexOf(',');
        if (argIndex)
          tmpString = tmpString.substring(0, argIndex);
        if (tmpString.equalsIgnoreCase(F("OLEDCMD")))
        {
          success = true;
          argIndex = string.lastIndexOf(',');
          tmpString = string.substring(argIndex + 1);
          if (tmpString.equalsIgnoreCase(F("Off")))
            Plugin_023_displayOff();
          else if (tmpString.equalsIgnoreCase(F("On")))
            Plugin_023_displayOn();
          else if (tmpString.equalsIgnoreCase(F("Clear")))
            Plugin_023_clear_display();
        }
        else if (tmpString.equalsIgnoreCase(F("OLED")))
        {
          success = true;
          argIndex = string.lastIndexOf(',');
          tmpString = string.substring(argIndex + 1);
          String newString = P023_parseTemplate(tmpString, 16);
          Plugin_023_sendStrXY(newString.c_str(), event->Par1 - 1, event->Par2 - 1);
        }
        break;
      }
  }
  return success;
}

// Perform some specific changes for OLED display
String P023_parseTemplate(String &tmpString, byte lineSize) {
  String result = parseTemplate(tmpString, lineSize);
  const char degree[3] = {0xc2, 0xb0, 0};  // Unicode degree symbol
  const char degree_oled[2] = {0x7F, 0};  // P023_OLED degree symbol
  result.replace(degree, degree_oled);
  return result;
}



const char Plugin_023_myFont[][8] PROGMEM = {
  {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
  {0x00, 0x00, 0x5F, 0x00, 0x00, 0x00, 0x00, 0x00},
  {0x00, 0x00, 0x07, 0x00, 0x07, 0x00, 0x00, 0x00},
  {0x00, 0x14, 0x7F, 0x14, 0x7F, 0x14, 0x00, 0x00},
  {0x00, 0x24, 0x2A, 0x7F, 0x2A, 0x12, 0x00, 0x00},
  {0x00, 0x23, 0x13, 0x08, 0x64, 0x62, 0x00, 0x00},
  {0x00, 0x36, 0x49, 0x55, 0x22, 0x50, 0x00, 0x00},
  {0x00, 0x00, 0x05, 0x03, 0x00, 0x00, 0x00, 0x00},
  {0x00, 0x1C, 0x22, 0x41, 0x00, 0x00, 0x00, 0x00},
  {0x00, 0x41, 0x22, 0x1C, 0x00, 0x00, 0x00, 0x00},
  {0x00, 0x08, 0x2A, 0x1C, 0x2A, 0x08, 0x00, 0x00},
  {0x00, 0x08, 0x08, 0x3E, 0x08, 0x08, 0x00, 0x00},
  {0x00, 0xA0, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00},
  {0x00, 0x08, 0x08, 0x08, 0x08, 0x08, 0x00, 0x00},
  {0x00, 0x60, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00},
  {0x00, 0x20, 0x10, 0x08, 0x04, 0x02, 0x00, 0x00},
  {0x00, 0x3E, 0x51, 0x49, 0x45, 0x3E, 0x00, 0x00},
  {0x00, 0x00, 0x42, 0x7F, 0x40, 0x00, 0x00, 0x00},
  {0x00, 0x62, 0x51, 0x49, 0x49, 0x46, 0x00, 0x00},
  {0x00, 0x22, 0x41, 0x49, 0x49, 0x36, 0x00, 0x00},
  {0x00, 0x18, 0x14, 0x12, 0x7F, 0x10, 0x00, 0x00},
  {0x00, 0x27, 0x45, 0x45, 0x45, 0x39, 0x00, 0x00},
  {0x00, 0x3C, 0x4A, 0x49, 0x49, 0x30, 0x00, 0x00},
  {0x00, 0x01, 0x71, 0x09, 0x05, 0x03, 0x00, 0x00},
  {0x00, 0x36, 0x49, 0x49, 0x49, 0x36, 0x00, 0x00},
  {0x00, 0x06, 0x49, 0x49, 0x29, 0x1E, 0x00, 0x00},
  {0x00, 0x00, 0x36, 0x36, 0x00, 0x00, 0x00, 0x00},
  {0x00, 0x00, 0xAC, 0x6C, 0x00, 0x00, 0x00, 0x00},
  {0x00, 0x08, 0x14, 0x22, 0x41, 0x00, 0x00, 0x00},
  {0x00, 0x14, 0x14, 0x14, 0x14, 0x14, 0x00, 0x00},
  {0x00, 0x41, 0x22, 0x14, 0x08, 0x00, 0x00, 0x00},
  {0x00, 0x02, 0x01, 0x51, 0x09, 0x06, 0x00, 0x00},
  {0x00, 0x32, 0x49, 0x79, 0x41, 0x3E, 0x00, 0x00},
  {0x00, 0x7E, 0x09, 0x09, 0x09, 0x7E, 0x00, 0x00},
  {0x00, 0x7F, 0x49, 0x49, 0x49, 0x36, 0x00, 0x00},
  {0x00, 0x3E, 0x41, 0x41, 0x41, 0x22, 0x00, 0x00},
  {0x00, 0x7F, 0x41, 0x41, 0x22, 0x1C, 0x00, 0x00},
  {0x00, 0x7F, 0x49, 0x49, 0x49, 0x41, 0x00, 0x00},
  {0x00, 0x7F, 0x09, 0x09, 0x09, 0x01, 0x00, 0x00},
  {0x00, 0x3E, 0x41, 0x41, 0x51, 0x72, 0x00, 0x00},
  {0x00, 0x7F, 0x08, 0x08, 0x08, 0x7F, 0x00, 0x00},
  {0x00, 0x41, 0x7F, 0x41, 0x00, 0x00, 0x00, 0x00},
  {0x00, 0x20, 0x40, 0x41, 0x3F, 0x01, 0x00, 0x00},
  {0x00, 0x7F, 0x08, 0x14, 0x22, 0x41, 0x00, 0x00},
  {0x00, 0x7F, 0x40, 0x40, 0x40, 0x40, 0x00, 0x00},
  {0x00, 0x7F, 0x02, 0x0C, 0x02, 0x7F, 0x00, 0x00},
  {0x00, 0x7F, 0x04, 0x08, 0x10, 0x7F, 0x00, 0x00},
  {0x00, 0x3E, 0x41, 0x41, 0x41, 0x3E, 0x00, 0x00},
  {0x00, 0x7F, 0x09, 0x09, 0x09, 0x06, 0x00, 0x00},
  {0x00, 0x3E, 0x41, 0x51, 0x21, 0x5E, 0x00, 0x00},
  {0x00, 0x7F, 0x09, 0x19, 0x29, 0x46, 0x00, 0x00},
  {0x00, 0x26, 0x49, 0x49, 0x49, 0x32, 0x00, 0x00},
  {0x00, 0x01, 0x01, 0x7F, 0x01, 0x01, 0x00, 0x00},
  {0x00, 0x3F, 0x40, 0x40, 0x40, 0x3F, 0x00, 0x00},
  {0x00, 0x1F, 0x20, 0x40, 0x20, 0x1F, 0x00, 0x00},
  {0x00, 0x3F, 0x40, 0x38, 0x40, 0x3F, 0x00, 0x00},
  {0x00, 0x63, 0x14, 0x08, 0x14, 0x63, 0x00, 0x00},
  {0x00, 0x03, 0x04, 0x78, 0x04, 0x03, 0x00, 0x00},
  {0x00, 0x61, 0x51, 0x49, 0x45, 0x43, 0x00, 0x00},
  {0x00, 0x7F, 0x41, 0x41, 0x00, 0x00, 0x00, 0x00},
  {0x00, 0x02, 0x04, 0x08, 0x10, 0x20, 0x00, 0x00},
  {0x00, 0x41, 0x41, 0x7F, 0x00, 0x00, 0x00, 0x00},
  {0x00, 0x04, 0x02, 0x01, 0x02, 0x04, 0x00, 0x00},
  {0x00, 0x80, 0x80, 0x80, 0x80, 0x80, 0x00, 0x00},
  {0x00, 0x01, 0x02, 0x04, 0x00, 0x00, 0x00, 0x00},
  {0x00, 0x20, 0x54, 0x54, 0x54, 0x78, 0x00, 0x00},
  {0x00, 0x7F, 0x48, 0x44, 0x44, 0x38, 0x00, 0x00},
  {0x00, 0x38, 0x44, 0x44, 0x28, 0x00, 0x00, 0x00},
  {0x00, 0x38, 0x44, 0x44, 0x48, 0x7F, 0x00, 0x00},
  {0x00, 0x38, 0x54, 0x54, 0x54, 0x18, 0x00, 0x00},
  {0x00, 0x08, 0x7E, 0x09, 0x02, 0x00, 0x00, 0x00},
  {0x00, 0x18, 0xA4, 0xA4, 0xA4, 0x7C, 0x00, 0x00},
  {0x00, 0x7F, 0x08, 0x04, 0x04, 0x78, 0x00, 0x00},
  {0x00, 0x00, 0x7D, 0x00, 0x00, 0x00, 0x00, 0x00},
  {0x00, 0x80, 0x84, 0x7D, 0x00, 0x00, 0x00, 0x00},
  {0x00, 0x7F, 0x10, 0x28, 0x44, 0x00, 0x00, 0x00},
  {0x00, 0x41, 0x7F, 0x40, 0x00, 0x00, 0x00, 0x00},
  {0x00, 0x7C, 0x04, 0x18, 0x04, 0x78, 0x00, 0x00},
  {0x00, 0x7C, 0x08, 0x04, 0x7C, 0x00, 0x00, 0x00},
  {0x00, 0x38, 0x44, 0x44, 0x38, 0x00, 0x00, 0x00},
  {0x00, 0xFC, 0x24, 0x24, 0x18, 0x00, 0x00, 0x00},
  {0x00, 0x18, 0x24, 0x24, 0xFC, 0x00, 0x00, 0x00},
  {0x00, 0x00, 0x7C, 0x08, 0x04, 0x00, 0x00, 0x00},
  {0x00, 0x48, 0x54, 0x54, 0x24, 0x00, 0x00, 0x00},
  {0x00, 0x04, 0x7F, 0x44, 0x00, 0x00, 0x00, 0x00},
  {0x00, 0x3C, 0x40, 0x40, 0x7C, 0x00, 0x00, 0x00},
  {0x00, 0x1C, 0x20, 0x40, 0x20, 0x1C, 0x00, 0x00},
  {0x00, 0x3C, 0x40, 0x30, 0x40, 0x3C, 0x00, 0x00},
  {0x00, 0x44, 0x28, 0x10, 0x28, 0x44, 0x00, 0x00},
  {0x00, 0x1C, 0xA0, 0xA0, 0x7C, 0x00, 0x00, 0x00},
  {0x00, 0x44, 0x64, 0x54, 0x4C, 0x44, 0x00, 0x00},
  {0x00, 0x08, 0x36, 0x41, 0x00, 0x00, 0x00, 0x00},
  {0x00, 0x00, 0x7F, 0x00, 0x00, 0x00, 0x00, 0x00},
  {0x00, 0x41, 0x36, 0x08, 0x00, 0x00, 0x00, 0x00},
  {0x00, 0x02, 0x01, 0x01, 0x02, 0x01, 0x00, 0x00},
  {0x00, 0x02, 0x05, 0x05, 0x02, 0x00, 0x00, 0x00}
};

static void Plugin_023_reset_display(void)
{
  Plugin_023_displayOff();
  Plugin_023_clear_display();
  Plugin_023_displayOn();
}


void Plugin_023_StartUp_OLED()
{
  Plugin_023_init_OLED();
  Plugin_023_reset_display();
  Plugin_023_displayOff();
  Plugin_023_setXY(0, 0);
  Plugin_023_clear_display();
  Plugin_023_displayOn();
}


void Plugin_023_displayOn(void)
{
  Plugin_023_sendcommand(0xaf);        //display on
}


void Plugin_023_displayOff(void)
{
  Plugin_023_sendcommand(0xae);    //display off
}


static void Plugin_023_clear_display(void)
{
  unsigned char i, k;
  for (k = 0; k < 8; k++)
  {
    Plugin_023_setXY(k, 0);
    {
      for (i = 0; i < 128; i++) //clear all COL
      {
        Plugin_023_SendChar(0);         //clear all COL
      }
    }
  }
}


// Actually this sends a byte, not a char to draw in the display.
static void Plugin_023_SendChar(unsigned char data)
{
  Wire.beginTransmission(Plugin_023_OLED_address);  // begin transmitting
  Wire.write(0x40);                      //data mode
  Wire.write(data);
  Wire.endTransmission();              // stop transmitting
}


// Prints a display char (not just a byte) in coordinates X Y,
//currently unused:
// static void Plugin_023_sendCharXY(unsigned char data, int X, int Y)
// {
//   //if (interrupt && !doing_menu) return; // Stop printing only if interrupt is call but not in button functions
//   Plugin_023_setXY(X, Y);
//   Wire.beginTransmission(Plugin_023_OLED_address); // begin transmitting
//   Wire.write(0x40);//data mode
//
//   for (int i = 0; i < 8; i++)
//     Wire.write(pgm_read_byte(Plugin_023_myFont[data - 0x20] + i));
//
//   Wire.endTransmission();    // stop transmitting
// }


static void Plugin_023_sendcommand(unsigned char com)
{
  Wire.beginTransmission(Plugin_023_OLED_address);     //begin transmitting
  Wire.write(0x80);                          //command mode
  Wire.write(com);
  Wire.endTransmission();                    // stop transmitting
}


// Set the cursor position in a 16 COL * 8 ROW map (128x64 pixels)
// or 8 COL * 5 ROW map (64x48 pixels)
static void Plugin_023_setXY(unsigned char row, unsigned char col)
{
  switch (Plugin_023_OLED_type)
  {
    case OLED_64x48:
      col += 4;
      break;
    case OLED_64x48 | OLED_rotated:
      col += 4;
      row += 2;
  }

  Plugin_023_sendcommand(0xb0 + row);              //set page address
  Plugin_023_sendcommand(0x00 + (8 * col & 0x0f)); //set low col address
  Plugin_023_sendcommand(0x10 + ((8 * col >> 4) & 0x0f)); //set high col address
}


// Prints a string regardless the cursor position.
// unused:
// static void Plugin_023_sendStr(unsigned char *string)
// {
//   unsigned char i = 0;
//   while (*string)
//   {
//     for (i = 0; i < 8; i++)
//     {
//       Plugin_023_SendChar(pgm_read_byte(Plugin_023_myFont[*string - 0x20] + i));
//     }
//     string++;
//   }
// }


// Prints a string in coordinates X Y, being multiples of 8.
// This means we have 16 COLS (0-15) and 8 ROWS (0-7).
static void Plugin_023_sendStrXY(const char *string, int X, int Y)
{
  Plugin_023_setXY(X, Y);
  unsigned char i = 0;
  while (*string)
  {
    for (i = 0; i < 8; i++)
    {
      Plugin_023_SendChar(pgm_read_byte(Plugin_023_myFont[*string - 0x20] + i));
    }
    string++;
  }
}


static void Plugin_023_init_OLED(void)
{
  Plugin_023_sendcommand(0xae);                //display off
  Plugin_023_sendcommand(0xa6);                //Set Normal Display (default)
  Plugin_023_sendcommand(0xAE);              //DISPLAYOFF
  Plugin_023_sendcommand(0xD5);              //SETDISPLAYCLOCKDIV
  Plugin_023_sendcommand(0x80);              // the suggested ratio 0x80
  Plugin_023_sendcommand(0xA8);              //SSD1306_SETMULTIPLEX
  Plugin_023_sendcommand(0x3F);
  Plugin_023_sendcommand(0xD3);              //SETDISPLAYOFFSET
  Plugin_023_sendcommand(0x0);               //no offset
  Plugin_023_sendcommand(0x40 | 0x0);        //SETSTARTLINE
  Plugin_023_sendcommand(0x8D);              //CHARGEPUMP
  Plugin_023_sendcommand(0x14);
  Plugin_023_sendcommand(0x20);              //MEMORYMODE
  Plugin_023_sendcommand(0x00);              //0x0 act like ks0108
  Plugin_023_sendcommand(0xA0);
  Plugin_023_sendcommand(0xC0);
  Plugin_023_sendcommand(0xDA);              //0xDA
  Plugin_023_sendcommand(0x12);              //COMSCANDEC
  Plugin_023_sendcommand(0x81);              //SETCONTRAS
  Plugin_023_sendcommand(0xCF);
  Plugin_023_sendcommand(0xd9);              //SETPRECHARGE
  Plugin_023_sendcommand(0xF1);
  Plugin_023_sendcommand(0xDB);              //SETVCOMDETECT
  Plugin_023_sendcommand(0x40);
  Plugin_023_sendcommand(0xA4);              //DISPLAYALLON_RESUME
  Plugin_023_sendcommand(0xA6);              //NORMALDISPLAY

  Plugin_023_clear_display();
  Plugin_023_sendcommand(0x2e);            // stop scroll
  Plugin_023_sendcommand(0x20);            //Set Memory Addressing Mode
  Plugin_023_sendcommand(0x00);            //Set Memory Addressing Mode ab Horizontal addressing mode
}
