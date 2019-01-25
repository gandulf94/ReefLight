//#######################################################################################################
//#################################### Plugin 012: LCD ##################################################
//#######################################################################################################

// Sample templates
//  Temp: [DHT11#Temperature]   Hum:[DHT11#humidity]
//  DS Temp:[Dallas1#Temperature#R]
//  Lux:[Lux#Lux#R]
//  Baro:[Baro#Pressure#R]
//  Pump:[Pump#on#O] -> ON/OFF

#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C *lcd=NULL;

#define PLUGIN_012
#define PLUGIN_ID_012         12
#define PLUGIN_NAME_012       "Display - LCD2004"
#define PLUGIN_VALUENAME1_012 "LCD"

boolean Plugin_012(byte function, struct EventStruct *event, String& string)
{
  boolean success = false;
  static byte displayTimer = 0;

  switch (function)
  {

    case PLUGIN_DEVICE_ADD:
      {
        Device[++deviceCount].Number = PLUGIN_ID_012;
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
        string = F(PLUGIN_NAME_012);
        break;
      }

    case PLUGIN_GET_DEVICEVALUENAMES:
      {
        strcpy_P(ExtraTaskSettings.TaskDeviceValueNames[0], PSTR(PLUGIN_VALUENAME1_012));
        break;
      }

    case PLUGIN_WEBFORM_LOAD:
      {
        byte choice = Settings.TaskDevicePluginConfig[event->TaskIndex][0];
        //String options[16];
        int optionValues[16];
        for (byte x = 0; x < 16; x++)
        {
          if (x < 8)
            optionValues[x] = 0x20 + x;
          else
            optionValues[x] = 0x30 + x;
          //options[x] = F("0x");
          //options[x] += String(optionValues[x], HEX);
        }
        addFormSelectorI2C(string, F("plugin_012_adr"), 16, optionValues, choice);


        byte choice2 = Settings.TaskDevicePluginConfig[event->TaskIndex][1];
        String options2[2];
        options2[0] = F("2 x 16");
        options2[1] = F("4 x 20");
        int optionValues2[2] = { 1, 2 };
        addFormSelector(string, F("Display Size"), F("plugin_012_size"), 2, options2, optionValues2, choice2);


        char deviceTemplate[4][80];
        LoadCustomTaskSettings(event->TaskIndex, (byte*)&deviceTemplate, sizeof(deviceTemplate));
        for (byte varNr = 0; varNr < 4; varNr++)
        {
          string += F("<TR><TD>Line ");
          string += varNr + 1;
          string += F(":<TD><input type='text' size='80' maxlength='80' name='Plugin_012_template");
          string += varNr + 1;
          string += F("' value='");
          string += deviceTemplate[varNr];
          string += F("'>");
        }


        addRowLabel(string, "Display button");
        addPinSelect(false, string, "taskdevicepin3", Settings.TaskDevicePin3[event->TaskIndex]);


        char tmpString[128];
        sprintf_P(tmpString, PSTR("<TR><TD>Display Timeout:<TD><input type='text' name='plugin_12_timer' value='%u'>"), Settings.TaskDevicePluginConfig[event->TaskIndex][2]);
        string += tmpString;


        String options3[3];
        options3[0] = F("Continue to next line (as in v1.4)");
        options3[1] = F("Truncate exceeding message");
        options3[2] = F("Clear then truncate exceeding message");
        int optionValues3[3] = { 0,1,2 };
        addFormSelector(string, F("LCD command Mode"), F("plugin_012_mode"), 3, options3, optionValues3, Settings.TaskDevicePluginConfig[event->TaskIndex][3]);

        success = true;
        break;
      }

    case PLUGIN_WEBFORM_SAVE:
      {
        Settings.TaskDevicePluginConfig[event->TaskIndex][0] = getFormItemInt(F("plugin_012_adr"));
        Settings.TaskDevicePluginConfig[event->TaskIndex][1] = getFormItemInt(F("plugin_012_size"));
        Settings.TaskDevicePluginConfig[event->TaskIndex][2] = getFormItemInt(F("plugin_12_timer"));
        Settings.TaskDevicePluginConfig[event->TaskIndex][3] = getFormItemInt(F("plugin_012_mode"));

        char deviceTemplate[4][80];
        for (byte varNr = 0; varNr < 4; varNr++)
        {
          char argc[25];
          String arg = F("Plugin_012_template");
          arg += varNr + 1;
          arg.toCharArray(argc, 25);
          String tmpString = WebServer.arg(argc);
          strncpy(deviceTemplate[varNr], tmpString.c_str(), sizeof(deviceTemplate[varNr]));
        }

        SaveCustomTaskSettings(event->TaskIndex, (byte*)&deviceTemplate, sizeof(deviceTemplate));
        success = true;
        break;
      }

    case PLUGIN_INIT:
      {
        if (!lcd)
        {
          byte row = 2;
          byte col = 16;
          if (Settings.TaskDevicePluginConfig[event->TaskIndex][1] == 2)
          {
            row = 4;
            col = 20;
          }
          lcd = new LiquidCrystal_I2C(Settings.TaskDevicePluginConfig[event->TaskIndex][0], col, row);
        }
        // Setup LCD display
        lcd->init();                      // initialize the lcd
        lcd->backlight();
        lcd->print("ESP Easy");
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
            if (lcd) {
              lcd->backlight();
            }
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
          if (lcd && displayTimer == 0)
            lcd->noBacklight();
        }
        break;
      }

    case PLUGIN_READ:
      {
        char deviceTemplate[4][80];
        LoadCustomTaskSettings(event->TaskIndex, (byte*)&deviceTemplate, sizeof(deviceTemplate));

        byte row = 2;
        byte col = 16;
        if (Settings.TaskDevicePluginConfig[event->TaskIndex][1] == 2)
        {
          row = 4;
          col = 20;
        }

        for (byte x = 0; x < row; x++)
        {
          String tmpString = deviceTemplate[x];
          if (lcd && tmpString.length())
          {
            String newString = P012_parseTemplate(tmpString, col);
            lcd->setCursor(0, x);
            lcd->print(newString);
          }
        }
        success = false;
        break;
      }

    case PLUGIN_WRITE:
      {
        byte rows = 2;
        byte cols = 16;
        if (Settings.TaskDevicePluginConfig[event->TaskIndex][1] == 2){
          rows = 4;
          cols = 20;
        }

        String tmpString  = string;
        int argIndex = tmpString.indexOf(',');
        if (argIndex)
          tmpString = tmpString.substring(0, argIndex);

        if (lcd && tmpString.equalsIgnoreCase(F("LCDCMD")))
        {
          success = true;
          argIndex = string.lastIndexOf(',');
          tmpString = string.substring(argIndex + 1);
          if (tmpString.equalsIgnoreCase(F("Off"))){
              lcd->noBacklight();
          }
          else if (tmpString.equalsIgnoreCase(F("On"))){
              lcd->backlight();
          }
          else if (tmpString.equalsIgnoreCase(F("Clear"))){
              lcd->clear();
          }
        }
        else if (lcd && tmpString.equalsIgnoreCase(F("LCD")))
        {
          success = true;
          argIndex = string.lastIndexOf(',');
          tmpString = string.substring(argIndex + 1);

          int colPos = event->Par2 - 1;
          int rowPos = event->Par1 - 1;

          //clear line before writing new string
          if (Settings.TaskDevicePluginConfig[event->TaskIndex][3] == 2){
              lcd->setCursor(colPos, rowPos);
              for (byte i = colPos; i < cols; i++) {
                  lcd->print(F(" "));
              }
          }

          // truncate message exceeding cols
          lcd->setCursor(colPos, rowPos);
          if(Settings.TaskDevicePluginConfig[event->TaskIndex][3] == 1 || Settings.TaskDevicePluginConfig[event->TaskIndex][3] == 2){
              lcd->setCursor(colPos, rowPos);
              for (byte i = 0; i < cols - colPos; i++) {
                  if(tmpString[i]){
                     lcd->print(tmpString[i]);
                  }
              }
          }

          // message exceeding cols will continue to next line
          else{
              // Fix Weird (native) lcd display behaviour that split long string into row 1,3,2,4, instead of 1,2,3,4
              boolean stillProcessing = 1;
              byte charCount = 1;
              while(stillProcessing) {
                   if (++colPos > cols) {    // have we printed 20 characters yet (+1 for the logic)
                        rowPos += 1;
                        lcd->setCursor(0,rowPos);   // move cursor down
                        colPos = 1;
                   }

                   //dont print if "lower" than the lcd
                   if(rowPos < rows  ){
                       lcd->print(tmpString[charCount - 1]);
                   }

                   if (!tmpString[charCount]) {   // no more chars to process?
                        stillProcessing = 0;
                   }
                   charCount += 1;
              }
              //lcd->print(tmpString.c_str());
              // end fix
          }

        }
        break;
      }

  }
  return success;
}

// Perform some specific changes for LCD display
// https://www.letscontrolit.com/forum/viewtopic.php?t=2368
String P012_parseTemplate(String &tmpString, byte lineSize) {
  String result = parseTemplate(tmpString, lineSize);
  const char degree[3] = {0xc2, 0xb0, 0};  // Unicode degree symbol
  const char degree_lcd[2] = {0xdf, 0};  // P012_LCD degree symbol
  result.replace(degree, degree_lcd);
  return result;
}
