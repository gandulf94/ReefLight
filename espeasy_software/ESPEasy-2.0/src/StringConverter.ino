/********************************************************************************************\
  Convert a char string to integer
  \*********************************************************************************************/
//FIXME: change original code so it uses String and String.toInt()
unsigned long str2int(char *string)
{
  unsigned long temp = atof(string);
  return temp;
}

/********************************************************************************************\
  Check if valid float and convert string to float.
  \*********************************************************************************************/
bool string2float(const String& string, float& floatvalue) {
  if (!isFloat(string)) return false;
  floatvalue = atof(string.c_str());
  return true;
}


/********************************************************************************************\
  Convert a char string to IP byte array
  \*********************************************************************************************/
boolean str2ip(const String& string, byte* IP) {
  return str2ip(string.c_str(), IP);
}

boolean str2ip(const char *string, byte* IP)
{
  IPAddress tmpip; // Default constructor => set to 0.0.0.0
  if (*string == 0 || tmpip.fromString(string)) {
    // Eiher empty string or a valid IP addres, so copy value.
    for (byte i = 0; i < 4; ++i)
      IP[i] = tmpip[i];
    return true;
  }
  return false;
}

// Call this by first declaring a char array of size 20, like:
//  char strIP[20];
//  formatIP(ip, strIP);
void formatIP(const IPAddress& ip, char (&strIP)[20]) {
  sprintf_P(strIP, PSTR("%u.%u.%u.%u"), ip[0], ip[1], ip[2], ip[3]);
}

String formatIP(const IPAddress& ip) {
  char strIP[20];
  formatIP(ip, strIP);
  return String(strIP);
}

void formatMAC(const uint8_t* mac, char (&strMAC)[20]) {
  sprintf_P(strMAC, PSTR("%02X:%02X:%02X:%02X:%02X:%02X"), mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/*********************************************************************************************\
   Workaround for removing trailing white space when String() converts a float with 0 decimals
  \*********************************************************************************************/
String toString(float value, byte decimals)
{
  String sValue = String(value, decimals);
  sValue.trim();
  return sValue;
}

/*********************************************************************************************\
   Format a value to the set number of decimals
  \*********************************************************************************************/
String formatUserVar(struct EventStruct *event, byte rel_index)
{
  float f(UserVar[event->BaseVarIndex + rel_index]);
  if (!isValidFloat(f)) {
    String log = F("Invalid float value for TaskIndex: ");
    log += event->TaskIndex;
    log += F(" varnumber: ");
    log += rel_index;
    addLog(LOG_LEVEL_DEBUG, log);
    f = 0;
  }
  return toString(f, ExtraTaskSettings.TaskDeviceValueDecimals[rel_index]);
}

/*********************************************************************************************\
   Wrap a string with given pre- and postfix string.
  \*********************************************************************************************/
String wrap_String(const String& string, const String& wrap) {
  String result;
  result.reserve(string.length() + 2* wrap.length());
  result = wrap;
  result += string;
  result += wrap;
  return result;
}

/*********************************************************************************************\
   Format an object value pair for use in JSON.
  \*********************************************************************************************/
String to_json_object_value(const String& object, const String& value) {
  String result;
  result.reserve(object.length() + value.length() + 6);
  result = wrap_String(object, F("\""));
  result += F(":");
  if (value.length() == 0 || !isFloat(value)) {
    result += wrap_String(value, F("\""));
  } else {
    result += value;
  }
  return result;
}


/*********************************************************************************************\
   Parse a string and get the xth command or parameter
  \*********************************************************************************************/
String parseString(String& string, byte indexFind)
{
  String tmpString = string;
  tmpString += ",";
  tmpString.replace(" ", ",");
  String locateString = "";
  byte count = 0;
  int index = tmpString.indexOf(',');
  while (index > 0)
  {
    count++;
    locateString = tmpString.substring(0, index);
    tmpString = tmpString.substring(index + 1);
    index = tmpString.indexOf(',');
    if (count == indexFind)
    {
      locateString.toLowerCase();
      return locateString;
    }
  }
  return "";
}


/*********************************************************************************************\
   Parse a string and get the xth command or parameter
  \*********************************************************************************************/
int getParamStartPos(String& string, byte indexFind)
{
  String tmpString = string;
  byte count = 0;
  tmpString.replace(" ", ",");
  for (unsigned int x = 0; x < tmpString.length(); x++)
  {
    if (tmpString.charAt(x) == ',')
    {
      count++;
      if (count == (indexFind - 1))
        return x + 1;
    }
  }
  return -1;
}

//escapes special characters in strings for use in html-forms
void htmlEscape(String & html)
{
  html.replace("&",  "&amp;");
  html.replace("\"", "&quot;");
  html.replace("'",  "&#039;");
  html.replace("<",  "&lt;");
  html.replace(">",  "&gt;");
}

/********************************************************************************************\
  replace other system variables like %sysname%, %systime%, %ip%
  \*********************************************************************************************/
void parseControllerVariables(String& s, struct EventStruct *event, boolean useURLencode) {
  parseSystemVariables(s, useURLencode);
  parseEventVariables(s, event, useURLencode);
  parseStandardConversions(s, useURLencode);
}


void repl(const String& key, const String& val, String& s, boolean useURLencode)
{
  if (useURLencode) {
    s.replace(key, URLEncode(val.c_str()));
  } else {
    s.replace(key, val);
  }
}

void parseSpecialCharacters(String& s, boolean useURLencode)
{
  bool no_accolades = s.indexOf('{') == -1 || s.indexOf('}') == -1;
  bool no_html_entity = s.indexOf('&') == -1 || s.indexOf(';') == -1;
  if (no_accolades && no_html_entity)
    return; // Nothing to replace

  {
    // Degree
    const char degree[3] = {0xc2, 0xb0, 0};  // Unicode degree symbol
    const char degreeC[4] = {0xe2, 0x84, 0x83, 0};  // Unicode degreeC symbol
    const char degree_C[4] = {0xc2, 0xb0, 'C', 0};  // Unicode degree symbol + captial C
    repl(F("{D}"), degree, s, useURLencode);
    repl(F("&deg;"), degree, s, useURLencode);
    repl(degreeC, degree_C, s, useURLencode);
  }
  {
    // Angle quotes
    const char laquo[3]  = {0xc2, 0xab, 0}; // Unicode left angle quotes symbol
    const char raquo[3]  = {0xc2, 0xbb, 0}; // Unicode right angle quotes symbol
    repl(F("{<<}"), laquo, s, useURLencode);
    repl(F("&laquo;"), laquo, s, useURLencode);
    repl(F("{>>}"), raquo, s, useURLencode);
    repl(F("&raquo;"), raquo, s, useURLencode);
  }
  {
    // Greek letter Mu
    const char mu[3]  = {0xc2, 0xb5, 0}; // Unicode greek letter mu
    repl(F("{u}"), mu, s, useURLencode);
    repl(F("&micro;"), mu, s, useURLencode);
  }
  {
    // Currency
    const char euro[4] = {0xe2, 0x82, 0xac, 0}; // Unicode euro symbol
    const char yen[3]   = {0xc2, 0xa5, 0}; // Unicode yen symbol
    const char pound[3] = {0xc2, 0xa3, 0}; // Unicode pound symbol
    const char cent[3]  = {0xc2, 0xa2, 0}; // Unicode cent symbol
    repl(F("{E}"), euro, s, useURLencode);
    repl(F("&euro;"), euro, s, useURLencode);
    repl(F("{Y}"), yen, s, useURLencode);
    repl(F("&yen;"), yen, s, useURLencode);
    repl(F("{P}"), pound, s, useURLencode);
    repl(F("&pound;"), pound, s, useURLencode);
    repl(F("{c}"), cent, s, useURLencode);
    repl(F("&cent;"), cent, s, useURLencode);
  }
  {
    // Math symbols
    const char sup1[3]  = {0xc2, 0xb9, 0}; // Unicode sup1 symbol
    const char sup2[3]  = {0xc2, 0xb2, 0}; // Unicode sup2 symbol
    const char sup3[3]  = {0xc2, 0xb3, 0}; // Unicode sup3 symbol
    const char frac14[3]  = {0xc2, 0xbc, 0}; // Unicode frac14 symbol
    const char frac12[3]  = {0xc2, 0xbd, 0}; // Unicode frac12 symbol
    const char frac34[3]  = {0xc2, 0xbe, 0}; // Unicode frac34 symbol
    const char plusmn[3]  = {0xc2, 0xb1, 0}; // Unicode plusmn symbol
    const char times[3]   = {0xc3, 0x97, 0}; // Unicode times symbol
    const char divide[3]  = {0xc3, 0xb7, 0}; // Unicode divide symbol
    repl(F("{^1}"), sup1, s, useURLencode);
    repl(F("&sup1;"), sup1, s, useURLencode);
    repl(F("{^2}"), sup2, s, useURLencode);
    repl(F("&sup2;"), sup2, s, useURLencode);
    repl(F("{^3}"), sup3, s, useURLencode);
    repl(F("&sup3;"), sup3, s, useURLencode);
    repl(F("{1_4}"), frac14, s, useURLencode);
    repl(F("&frac14;"), frac14, s, useURLencode);
    repl(F("{1_2}"), frac12, s, useURLencode);
    repl(F("&frac12;"), frac12, s, useURLencode);
    repl(F("{3_4}"), frac34, s, useURLencode);
    repl(F("&frac34;"), frac34, s, useURLencode);
    repl(F("{+-}"), plusmn, s, useURLencode);
    repl(F("&plusmn;"), plusmn, s, useURLencode);
    repl(F("{x}"), times, s, useURLencode);
    repl(F("&times;"), times, s, useURLencode);
    repl(F("{..}"), divide, s, useURLencode);
    repl(F("&divide;"), divide, s, useURLencode);
  }
}

// Simple macro to create the replacement string only when needed.
#define SMART_REPL(T,S) if (s.indexOf(T) != -1) { repl((T), (S), s, useURLencode);}
void parseSystemVariables(String& s, boolean useURLencode)
{
  parseSpecialCharacters(s, useURLencode);
  if (s.indexOf('%') == -1)
    return; // Nothing to replace

  #if FEATURE_ADC_VCC
    repl(F("%vcc%"), String(vcc), s, useURLencode);
  #endif
  repl(F("%CR%"), F("\r"), s, useURLencode);
  repl(F("%LF%"), F("\n"), s, useURLencode);
  SMART_REPL(F("%ip%"),WiFi.localIP().toString())
  SMART_REPL(F("%rssi%"), String((WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0))
  SMART_REPL(F("%ssid%"), (WiFi.status() == WL_CONNECTED) ? WiFi.SSID() : F("--"))
  SMART_REPL(F("%unit%"), String(Settings.Unit))
  SMART_REPL(F("%mac%"), String(WiFi.macAddress()))
  #if defined(ESP8266)
    SMART_REPL(F("%mac_int%"), String(ESP.getChipId()))  // Last 24 bit of MAC address as integer, to be used in rules.
  #endif

  if (s.indexOf(F("%sys")) != -1) {
    SMART_REPL(F("%sysload%"), String(100 - (100 * loopCounterLast / loopCounterMax)))
    SMART_REPL(F("%systm_hm%"), getTimeString(':', false))
    SMART_REPL(F("%systm_hm_am%"), getTimeString_ampm(':', false))
    SMART_REPL(F("%systime%"), getTimeString(':'))
    SMART_REPL(F("%systime_am%"), getTimeString_ampm(':'))
    repl(F("%sysname%"), Settings.Name, s, useURLencode);

    // valueString is being used by the macro.
    char valueString[5];
    #define SMART_REPL_TIME(T,F,V) if (s.indexOf(T) != -1) { sprintf_P(valueString, (F), (V)); repl((T),valueString, s, useURLencode);}
    SMART_REPL_TIME(F("%syshour%"), PSTR("%02d"), hour())
    SMART_REPL_TIME(F("%sysmin%"), PSTR("%02d"), minute())
    SMART_REPL_TIME(F("%syssec%"),PSTR("%02d"), second())
    SMART_REPL_TIME(F("%sysday%"), PSTR("%02d"), day())
    SMART_REPL_TIME(F("%sysmonth%"),PSTR("%02d"), month())
    SMART_REPL_TIME(F("%sysyear%"), PSTR("%04d"), year())
    SMART_REPL_TIME(F("%sysyears%"),PSTR("%02d"), year()%100)
    SMART_REPL(F("%sysweekday%"), String(weekday()))
    SMART_REPL(F("%sysweekday_s%"), weekday_str())
    #undef SMART_REPL_TIME
  }
  SMART_REPL(F("%lcltime%"), getDateTimeString('-',':',' '))
  SMART_REPL(F("%lcltime_am%"), getDateTimeString_ampm('-',':',' '))
  SMART_REPL(F("%uptime%"), String(wdcounter / 2))

  repl(F("%tskname%"), ExtraTaskSettings.TaskDeviceName, s, useURLencode);
  if (s.indexOf("%vname") != -1) {
    repl(F("%vname1%"), ExtraTaskSettings.TaskDeviceValueNames[0], s, useURLencode);
    repl(F("%vname2%"), ExtraTaskSettings.TaskDeviceValueNames[1], s, useURLencode);
    repl(F("%vname3%"), ExtraTaskSettings.TaskDeviceValueNames[2], s, useURLencode);
    repl(F("%vname4%"), ExtraTaskSettings.TaskDeviceValueNames[3], s, useURLencode);
  }
}

void parseEventVariables(String& s, struct EventStruct *event, boolean useURLencode)
{
  SMART_REPL(F("%id%"), String(event->idx))
  if (s.indexOf("%val") != -1) {
    if (event->sensorType == SENSOR_TYPE_LONG) {
      SMART_REPL(F("%val1%"), String((unsigned long)UserVar[event->BaseVarIndex] + ((unsigned long)UserVar[event->BaseVarIndex + 1] << 16)))
    } else {
      SMART_REPL(F("%val1%"), formatUserVar(event, 0))
      SMART_REPL(F("%val2%"), formatUserVar(event, 1))
      SMART_REPL(F("%val3%"), formatUserVar(event, 2))
      SMART_REPL(F("%val4%"), formatUserVar(event, 3))
    }
  }
}
#undef SMART_REPL

bool getConvertArgument(const String& marker, const String& s, float& argument, int& startIndex, int& endIndex) {
  startIndex = s.indexOf(marker);
  if (startIndex == -1) return false;

  int startIndexArgument = startIndex + marker.length();
  if (s.charAt(startIndexArgument) != '(') {
    return false;
  }
  ++startIndexArgument;
  endIndex = s.indexOf(')', startIndexArgument);
  if (endIndex == -1) return false;

  String argumentString = s.substring(startIndexArgument, endIndex);
  if (argumentString.length() == 0 || !isFloat(argumentString)) return false;

  argument = argumentString.toFloat();
  ++endIndex; // Must also strip ')' from the original string.
  return true;
}

// Parse conversions marked with "%conv_marker%(float)"
// Must be called last, since all sensor values must be converted, processed, etc.
void parseStandardConversions(String& s, boolean useURLencode) {
  if (s.indexOf(F("%c_")) == -1)
    return; // Nothing to replace

  float arg = 0.0;
  int startIndex = 0;
  int endIndex = 0;
  // These replacements should be done in a while loop per marker,
  // since they also replace the numerical parameter.
  // The marker may occur more than once per string, but with different parameters.
  #define SMART_CONV(T,FUN) while (getConvertArgument((T), s, arg, startIndex, endIndex)) { repl(s.substring(startIndex, endIndex), (FUN), s, useURLencode); }
  SMART_CONV(F("%c_w_dir%"),  getBearing(arg))
  SMART_CONV(F("%c_c2f%"),    toString(CelsiusToFahrenheit(arg), 1))
  SMART_CONV(F("%c_ms2Bft%"), String(m_secToBeaufort(arg)))
  SMART_CONV(F("%c_cm2imp%"), centimeterToImperialLength(arg))
  SMART_CONV(F("%c_mm2imp%"), millimeterToImperialLength(arg))
  SMART_CONV(F("%c_m2day%"),  toString(minutesToDay(arg), 2))
  SMART_CONV(F("%c_m2dh%"),   minutesToDayHour(arg))
  SMART_CONV(F("%c_m2dhm%"),  minutesToDayHourMinute(arg))
  SMART_CONV(F("%c_s2dhms%"), secondsToDayHourMinuteSecond(arg))
  #undef SMART_CONV
}
