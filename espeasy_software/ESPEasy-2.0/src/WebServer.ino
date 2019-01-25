//********************************************************************************
// Allowed IP range check
//********************************************************************************
#define ALL_ALLOWED            0
#define LOCAL_SUBNET_ALLOWED   1
#define ONLY_IP_RANGE_ALLOWED  2
#define _HEAD false
#define _TAIL true
#define CHUNKED_BUFFER_SIZE          400

void sendContentBlocking(String& data);
void sendHeaderBlocking(bool json);

class StreamingBuffer {
private:
  bool lowMemorySkip;

public:
  uint32_t initialRam;
  uint32_t beforeTXRam;
  uint32_t duringTXRam;
  uint32_t finalRam;
  uint32_t maxCoreUsage;
  uint32_t maxServerUsage;
  unsigned int sentBytes;
  String buf;

  StreamingBuffer(void) : lowMemorySkip(false),
    initialRam(0), beforeTXRam(0), duringTXRam(0), finalRam(0), maxCoreUsage(0),
    maxServerUsage(0), sentBytes(0)
  {
    buf.reserve(CHUNKED_BUFFER_SIZE + 50);
    buf = "";
  }
  StreamingBuffer operator= (String& a)                 { flush(); return addString(a); }
  StreamingBuffer operator= (const String& a)           { flush(); return addString(a); }
  StreamingBuffer operator+= (long unsigned int  a)     { return addString(String(a)); }
  StreamingBuffer operator+= (float a)                  { return addString(String(a)); }
  StreamingBuffer operator+= (int a)                    { return addString(String(a)); }
  StreamingBuffer operator+= (uint32_t a)               { return addString(String(a)); }
  StreamingBuffer operator+=(const String& a)           { return addString(a); }

  StreamingBuffer addString(const String& a) {
    if (lowMemorySkip) return *this;
    int flush_step = CHUNKED_BUFFER_SIZE - this->buf.length();
    if (flush_step < 1) flush_step = 0;
    int pos = 0;
    const int length = a.length();
    while (pos < length) {
      if (flush_step == 0) {
        sendContentBlocking(this->buf);
        flush_step = CHUNKED_BUFFER_SIZE;
      }
      this->buf += a[pos];
      ++pos;
      --flush_step;
    }
    checkFull();
    return *this;
  }

  void flush() {
    if (lowMemorySkip) {
      this->buf = "";
    } else {
      sendContentBlocking(this->buf);
    }
  }

  void checkFull(void) {
    if (lowMemorySkip) this->buf = "";
    if (this->buf.length() > CHUNKED_BUFFER_SIZE) {
      trackTotalMem();
      sendContentBlocking(this->buf);
    }
  }

  void startStream() {
    startStream(false);
  }

  void startJsonStream() {
    startStream(true);
  }

private:
  void startStream(bool json) {
    maxCoreUsage = maxServerUsage = 0;
    initialRam = ESP.getFreeHeap();
    beforeTXRam = initialRam;
    sentBytes = 0;
    buf = "";
    if (beforeTXRam < 3000) {
      lowMemorySkip = true;
      WebServer.send(200, "text/plain", "Low memory. Cannot display webpage :-(");
      tcpCleanup();
      return;
    } else
      sendHeaderBlocking(json);
  }

  void trackTotalMem() {
    beforeTXRam = ESP.getFreeHeap();
    if ((initialRam - beforeTXRam) > maxServerUsage)
      maxServerUsage = initialRam - beforeTXRam;
  }

public:

  void trackCoreMem() {
    duringTXRam = ESP.getFreeHeap();
    if ((initialRam - duringTXRam) > maxCoreUsage)
      maxCoreUsage = (initialRam - duringTXRam);
  }

  void endStream(void) {
    if (!lowMemorySkip) {
      if (buf.length() > 0) sendContentBlocking(buf);
      buf = "";
      sendContentBlocking(buf);
      finalRam = ESP.getFreeHeap();
      String log = String("Ram usage: Webserver only: ") + maxServerUsage +
                   " including Core: " + maxCoreUsage;
      addLog(LOG_LEVEL_DEBUG, log);
    } else {
      String log = String("Webpage skipped: low memory: ") + finalRam;
      addLog(LOG_LEVEL_DEBUG, log);
      lowMemorySkip = false;
    }
  }
} TXBuffer;

void sendContentBlocking(String& data) {
  checkRAM(F("sendContentBlocking"));
  uint32_t freeBeforeSend = ESP.getFreeHeap();
  const uint32_t length = data.length();
  String log = String("sendcontent free: ") + freeBeforeSend + " chunk size:" + length;
  addLog(LOG_LEVEL_DEBUG_DEV, log);
  freeBeforeSend = ESP.getFreeHeap();
  if (TXBuffer.beforeTXRam > freeBeforeSend)
    TXBuffer.beforeTXRam = freeBeforeSend;
  TXBuffer.duringTXRam = freeBeforeSend;
#if defined(ESP8266) && defined(ARDUINO_ESP8266_RELEASE_2_3_0)
  String size = String(length, HEX) + "\r\n";
  // do chunked transfer encoding ourselves (WebServer doesn't support it)
  WebServer.sendContent(size);
  if (length > 0) WebServer.sendContent(data);
  WebServer.sendContent("\r\n");
#else  // ESP8266 2.4.0rc2 and higher and the ESP32 webserver supports chunked http transfer
  unsigned int timeout = 0;
  if (freeBeforeSend < 5000) timeout = 100;
  if (freeBeforeSend < 4000) timeout = 1000;
  const uint32_t beginWait = millis();
  WebServer.sendContent(data);
  while ((ESP.getFreeHeap() < freeBeforeSend) &&
         !timeOutReached(beginWait + timeout)) {
    if (ESP.getFreeHeap() < TXBuffer.duringTXRam)
      TXBuffer.duringTXRam = ESP.getFreeHeap();
    ;
    TXBuffer.trackCoreMem();
    checkRAM(F("duringDataTX"));
    delay(1);
  }
#endif

  TXBuffer.sentBytes += length;
  data = "";
}

void sendHeaderBlocking(bool json) {
  checkRAM(F("sendHeaderBlocking"));
#if defined(ESP8266) && defined(ARDUINO_ESP8266_RELEASE_2_3_0)
  WebServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
  WebServer.sendHeader(F("Content-Type"), json ? F("application/json") : F("text/html"), true);
  WebServer.sendHeader(F("Accept-Ranges"), F("none"));
  WebServer.sendHeader(F("Cache-Control"), F("no-cache"));
  WebServer.sendHeader(F("Transfer-Encoding"), F("chunked"));
  WebServer.send(200);
#else
  unsigned int timeout = 0;
  uint32_t freeBeforeSend = ESP.getFreeHeap();
  if (freeBeforeSend < 5000) timeout = 100;
  if (freeBeforeSend < 4000) timeout = 1000;
  const uint32_t beginWait = millis();
  WebServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
  WebServer.sendHeader(F("Content-Type"), json ? F("application/json") : F("text/html"), true);
  WebServer.sendHeader(F("Cache-Control"), F("no-cache"));
  WebServer.send(200);
  // dont wait on 2.3.0. Memory returns just too slow.
  while ((ESP.getFreeHeap() < freeBeforeSend) &&
         !timeOutReached(beginWait + timeout)) {
    checkRAM(F("duringHeaderTX"));
    delay(1);
  }
#endif
}

void sendHeadandTail(const String& tmplName, boolean Tail = false) {
  String pageTemplate = "";
  int indexStart, indexEnd;
  String varName;  //, varValue;
  String fileName = tmplName;
  fileName += F(".htm");
  fs::File f = SPIFFS.open(fileName, "r+");

  if (f) {
    pageTemplate.reserve(f.size());
    while (f.available()) pageTemplate += (char)f.read();
    f.close();
  } else {
    getWebPageTemplateDefault(tmplName, pageTemplate);
  }
  checkRAM(F("sendWebPage"));
  // web activity timer
  lastWeb = millis();

  if (Tail) {
    TXBuffer += pageTemplate.substring(
        11 + // Size of "{{content}}"
        pageTemplate.indexOf("{{content}}"));  // advance beyond content key
  } else {
    while ((indexStart = pageTemplate.indexOf("{{")) >= 0) {
      TXBuffer += pageTemplate.substring(0, indexStart);
      pageTemplate = pageTemplate.substring(indexStart);
      if ((indexEnd = pageTemplate.indexOf("}}")) > 0) {
        varName = pageTemplate.substring(2, indexEnd);
        pageTemplate = pageTemplate.substring(indexEnd + 2);
        varName.toLowerCase();

        if (varName == F("content")) {  // is var == page content?
          break;  // send first part of result only
        } else if (varName == F("error")) {
          String errors(getErrorNotifications());
          if (errors.length() > 0) TXBuffer += (errors);
        } else {
          getWebPageTemplateVar(varName);
          TXBuffer.checkFull();
        }
      } else {  // no closing "}}"
        pageTemplate = pageTemplate.substring(2);  // eat "{{"
      }
    }
  }
  if (shouldReboot) {
    //we only add this here as a seperate chucnk to prevent using too much memory at once
    TXBuffer += F(
      "<script>"
        "i=document.getElementById('rbtmsg');"
        "i.innerHTML=\"Please reboot: <input id='reboot' class='button link' value='Reboot' type='submit' onclick='r()'>\";"
        "var x = new XMLHttpRequest();"

        //done
        "function d(){"
          "i.innerHTML='';"
          "clearTimeout(t);"
        "}"


        //keep requesting mainpage until no more errors
        "function c(){"
          "i.innerHTML+='.';"
          "x.onload=d;"
          "x.open('GET', window.location.origin);"
          "x.send();"
        "}"

        //rebooting
        "function b(){"
          "i.innerHTML='Rebooting..';"
          "t=setInterval(c,2000);"
        "}"


        //request reboot
        "function r(){"
          "i.innerHTML+=' (requesting)';"
          "x.onload=b;"
          "x.open('GET', window.location.origin+'/?cmd=reboot');"
          "x.send();"
        "}"

      "</script>"
      );
  }
}

//********************************************************************************
// Web Interface init
//********************************************************************************
#include "core_version.h"
#define HTML_SYMBOL_WARNING "&#9888;"

#define TASKS_PER_PAGE 12

static const char pgDefaultCSS[] PROGMEM = {
    //color scheme: #07D #D50 #DB0 #A0D
    "* {font-family: sans-serif; font-size: 12pt; margin: 0px; padding: 0px; box-sizing: border-box; }"
    "h1 {font-size: 16pt; color: #07D; margin: 8px 0; font-weight: bold; }"
    "h2 {font-size: 12pt; margin: 0 -4px; padding: 6px; background-color: #444; color: #FFF; font-weight: bold; }"
    "h3 {font-size: 12pt; margin: 16px -4px 0 -4px; padding: 4px; background-color: #EEE; color: #444; font-weight: bold; }"
    "h6 {font-size: 10pt; color: #07D; }"
    // buttons
    ".button {margin: 4px; padding: 4px 16px; background-color: #07D; color: #FFF; text-decoration: none; border-radius: 4px; }"
    ".button.link {}"
    ".button.help {padding: 2px 4px; border: solid 1px #FFF; border-radius: 50%; }"
    ".button:hover {background: #369; }"
    // tables
    "th {padding: 6px; background-color: #444; color: #FFF; border-color: #888; font-weight: bold; }"
    "td {padding: 4px; }"
    "tr {padding: 4px; }"
    "table {color: #000; width: 100%; min-width: 420px; }"
    // inside a form
    ".note {color: #444; font-style: italic; }"
    //header with title and menu
    ".headermenu {position: fixed; top: 0; left: 0; right: 0; height: 90px; padding: 8px 12px; background-color: #F8F8F8; border-bottom: 1px solid #DDD; }"
    ".apheader {padding: 8px 12px; background-color: #F8F8F8;}"
    ".bodymenu {margin-top: 96px; }"
    // menu
    ".menubar {position: inherit; top: 55px; }"
    ".menu {float: left; padding: 4px 16px 8px 16px; color: #444; white-space: nowrap; border: solid transparent; border-width: 4px 1px 1px; border-radius: 4px 4px 0 0; text-decoration: none; }"
    ".menu.active {color: #000; background-color: #FFF; border-color: #07D #DDD #FFF; }"
    ".menu:hover {color: #000; background: #DEF; }"
    // symbols for enabled
    ".on {color: green; }"
    ".off {color: red; }"
    // others
    ".div_l {float: left; }"
    ".div_r {float: right; margin: 2px; padding: 1px 10px; border-radius: 4px; background-color: #080; color: white; }"
    ".div_br {clear: both; }"
    // The alert message box
    ".alert {padding: 20px; background-color: #f44336; color: white; margin-bottom: 15px; }"
    // The close button
    ".closebtn {margin-left: 15px; color: white; font-weight: bold; float: right; font-size: 22px; line-height: 20px; cursor: pointer; transition: 0.3s; }"
    // When moving the mouse over the close button
    ".closebtn:hover {color: black; }"
    "section{overflow-x: auto; width: 100%; }"
    // For screens with width less than 960 pixels
    "@media screen and (max-width: 960px) {"
      ".bodymenu{  margin-top: 0px; }"
      ".headermenu{  position: relative;   height: auto;   float: left;   width: 100%;   padding: 0px; }"
      ".headermenu h1{  padding: 8px 12px; }"
      ".menubar{  top: 0px;   position: relative;   float: left;   width: 100%; }"
      ".headermenu a{  width: 100%;   padding:7px 10px;   display: block;   height: auto;   border: 0px;   border-radius:0px; }; }"
    "\0"
};

#define PGMT( pgm_ptr ) ( reinterpret_cast< const __FlashStringHelper * >( pgm_ptr ) )

//if there is an error-string, add it to the html code with correct formatting
void  addHtmlError( String error){
  String t;
   addHtmlError(t,  error);
  TXBuffer+=t;
}
void addHtmlError(String & str, String error)
{
    if (error.length()>0)
    {
      str += F("<div class=\"alert\"><span class=\"closebtn\" onclick=\"this.parentElement.style.display='none';\">&times;</span>");
      str += error;
      str += F("</div>");
    }
}

void WebServerInit()
{
  // Prepare webserver pages
  WebServer.on("/", handle_root);
  WebServer.on("/config", handle_config);
  WebServer.on("/controllers", handle_controllers);
  WebServer.on("/hardware", handle_hardware);
  WebServer.on("/devices", handle_devices);
  WebServer.on("/notifications", handle_notifications);
  WebServer.on("/log", handle_log);
  WebServer.on("/tools", handle_tools);
  WebServer.on("/i2cscanner", handle_i2cscanner);
  WebServer.on("/wifiscanner", handle_wifiscanner);
  WebServer.on("/login", handle_login);
  WebServer.on("/control", handle_control);
  WebServer.on("/download", handle_download);
  WebServer.on("/upload", HTTP_GET, handle_upload);
  WebServer.on("/upload", HTTP_POST, handle_upload_post, handleFileUpload);
  WebServer.onNotFound(handleNotFound);
  WebServer.on("/filelist", handle_filelist);
#ifdef FEATURE_SD
  WebServer.on("/SDfilelist", handle_SDfilelist);
#endif
  WebServer.on("/advanced", handle_advanced);
  WebServer.on("/setup", handle_setup);
  WebServer.on("/json", handle_json);
  WebServer.on("/rules", handle_rules);
  WebServer.on("/sysinfo", handle_sysinfo);
  WebServer.on("/pinstates", handle_pinstates);
  WebServer.on("/favicon.ico", handle_favicon);

  #if defined(ESP8266)
    if (ESP.getFlashChipRealSize() > 524288)
      httpUpdater.setup(&WebServer);
  #endif

  #if defined(ESP8266)
  if (Settings.UseSSDP)
  {
    WebServer.on("/ssdp.xml", HTTP_GET, []() {
      WiFiClient client(WebServer.client());
      SSDP_schema(client);
    });
    SSDP_begin();
  }
  #endif

  WebServer.begin();
}




void getWebPageTemplateDefault(const String& tmplName, String& tmpl)
{
  if (tmplName == F("TmplAP"))
  {
    tmpl += F(
              "<!DOCTYPE html><html lang='en'>"
              "<head>"
              "<meta charset='utf-8'/>"
              "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
              "<title>{{name}}</title>"
              "{{css}}"
              "</head>"
              "<body>"
              "<header class='apheader'>"
              "<h1>Welcome to ESP Easy Mega AP</h1>"
              "</header>"
              "<section>"
              "<span class='message error'>"
              "{{error}}"
              "</span>"
              "{{content}}"
              "</section>"
              "<footer>"
              "<h6>Powered by www.letscontrolit.com</h6>"
              "</footer>"
              "</body>"            );
  }
  else if (tmplName == F("TmplMsg"))
  {
    tmpl += F(
              "<!DOCTYPE html><html lang='en'>"
              "<head>"
              "<meta charset='utf-8'/>"
              "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
              "<title>{{name}}</title>"
              "{{css}}"
              "</head>"
              "<body>"
              "<header class='headermenu'>"
              "<h1>ESP Easy Mega: {{name}}</h1>"
              "</header>"
              "<section>"
              "<span class='message error'>"
              "{{error}}"
              "</span>"
              "{{content}}"
              "</section>"
              "<footer>"
              "<h6>Powered by www.letscontrolit.com</h6>"
              "</footer>"
              "</body>"
            );
  }
  else   //all other template names e.g. TmplStd
  {
    tmpl += F(
      "<!DOCTYPE html><html lang='en'>"
      "<head>"
        "<meta charset='utf-8'/>"
        "<title>{{name}}</title>"
        "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
        "{{js}}"
        "{{css}}"
      "</head>"
      "<body class='bodymenu'>"
        "<span class='message' id='rbtmsg'></span>"
        "<header class='headermenu'>"
          "<h1>ESP Easy Mega: {{name}} {{logo}}</h1>"
          "{{menu}}"
        "</header>"
        "<section>"
        "<span class='message error'>"
        "{{error}}"
        "</span>"
        "{{content}}"
        "</section>"
        "<footer>"
          "<h6>Powered by www.letscontrolit.com</h6>"
        "</footer>"
      "</body></html>"
            );
  }
}



String getErrorNotifications() {
  String errors;
  // Check number of MQTT controllers active.
  int nrMQTTenabled = 0;
  for (byte x = 0; x < CONTROLLER_MAX; x++) {
    if (Settings.Protocol[x] != 0) {
      byte ProtocolIndex = getProtocolIndex(Settings.Protocol[x]);
      if (Settings.ControllerEnabled[x] && Protocol[ProtocolIndex].usesMQTT) {
        ++nrMQTTenabled;
      }
    }
  }
  if (nrMQTTenabled > 1) {
    // Add warning, only one MQTT protocol should be used.
    addHtmlError(errors, F("Only one MQTT controller should be active."));
}
  // Check checksum of stored settings.


  return errors;
}


static byte navMenuIndex = 0;

void getWebPageTemplateVar(const String& varName )
{
 // Serial.print(varName); Serial.print(" : free: "); Serial.print(ESP.getFreeHeap());   Serial.print("var len before:  "); Serial.print (varValue.length()) ;Serial.print("after:  ");
 //varValue = F("");

  if (varName == F("name"))
  {
    TXBuffer += Settings.Name;
  }

  else if (varName == F("unit"))
  {
    TXBuffer += String(Settings.Unit);
  }

  else if (varName == F("menu"))
  {
    static const __FlashStringHelper* gpMenu[8][2] = {
      F("Main"), F("."),                      //0
      F("Config"), F("config"),               //1
      F("Controllers"), F("controllers"),     //2
      F("Hardware"), F("hardware"),           //3
      F("Devices"), F("devices"),             //4
      F("Rules"), F("rules"),                 //5
      F("Notifications"), F("notifications"), //6
      F("Tools"), F("tools"),                 //7
    };

    TXBuffer += F("<div class='menubar'>");

    for (byte i = 0; i < 8; i++)
    {
      if (i == 5 && !Settings.UseRules)   //hide rules menu item
        continue;

      TXBuffer += F("<a class='menu");
      if (i == navMenuIndex)
        TXBuffer += F(" active");
      TXBuffer += F("' href='");
      TXBuffer += gpMenu[i][1];
      TXBuffer += F("'>");
      TXBuffer += gpMenu[i][0];
      TXBuffer += F("</a>");
    }

    TXBuffer += F("</div>");
  }

  else if (varName == F("logo"))
  {
    if (SPIFFS.exists("esp.png"))
    {
      TXBuffer = F("<img src=\"esp.png\" width=48 height=48 align=right>");
    }
  }

  else if (varName == F("css"))
  {
    if (SPIFFS.exists("esp.css"))   //now css is written in writeDefaultCSS() to SPIFFS and always present
    //if (0) //TODO
    {
      TXBuffer = F("<link rel=\"stylesheet\" type=\"text/css\" href=\"esp.css\">");
    }
   else
    {
      TXBuffer += F("<style>");
      //TXBuffer += PGMT(pgDefaultCSS);
      // Send CSS per chunk to avoid sending either too short or too large strings.
      String tmpString;
      tmpString.reserve(64);
      uint16_t tmpStringPos = 0;
      for (unsigned int i = 0; i < strlen(pgDefaultCSS); i++)
      {
        tmpString += (char)pgm_read_byte(&pgDefaultCSS[i]);
        ++tmpStringPos;
        if (tmpStringPos == 64) {
          TXBuffer += tmpString;
          tmpString = "";
          tmpStringPos = 0;
        }
      } // saves 1k of ram
      if (tmpString.length() > 0) {
        // Flush left over part.
        TXBuffer += tmpString;
      }
      TXBuffer += F("</style>");
    }
  }


  else if (varName == F("js"))
  {
    TXBuffer += F(
                  "<script><!--\n"
                  "function dept_onchange(frmselect) {frmselect.submit();}"
                  "\n//--></script>");
  }

  else if (varName == F("error"))
  {
    //print last error - not implemented yet
  }

  else if (varName == F("debug"))
  {
    //print debug messages - not implemented yet
  }

  else
  {
    String log = F("Templ: Unknown Var : ");
    log += varName;
    addLog(LOG_LEVEL_ERROR, log);
    //no return string - eat var name
  }

 }


void writeDefaultCSS(void)
{
  return; //TODO

  if (!SPIFFS.exists("esp.css"))
  {
    String defaultCSS;

    fs::File f = SPIFFS.open("esp.css", "w");
    if (f)
    {
      String log = F("CSS  : Writing default CSS file to SPIFFS (");
      log += defaultCSS.length();
      log += F(" bytes)");
      addLog(LOG_LEVEL_INFO, log);
      defaultCSS= PGMT(pgDefaultCSS);
      f.write((const unsigned char*)defaultCSS.c_str(), defaultCSS.length());   //note: content must be in RAM - a write of F("XXX") does not work
      f.close();
    }

  }
}


//********************************************************************************
// Add top menu
//********************************************************************************
void addHeader(boolean showMenu, String& str)
{
  //not longer used - now part of template
}


//********************************************************************************
// Add footer to web page
//********************************************************************************
void addFooter(String& str)
{
  //not longer used - now part of template
}


//********************************************************************************
// Web Interface root page
//********************************************************************************
void handle_root() {
 Serial.println("handleroot");
  // if Wifi setup, launch setup wizard
  if (wifiSetup)
  {
    WebServer.send(200, "text/html", F("<meta HTTP-EQUIV='REFRESH' content='0; url=/setup'>"));
    return;
  }
   if (!isLoggedIn()) return;
   navMenuIndex = 0;
   TXBuffer.startStream();
   sendHeadandTail(F("TmplStd"),_HEAD);

  int freeMem = ESP.getFreeHeap();
  String sCommand = WebServer.arg(F("cmd"));

  if ((strcasecmp_P(sCommand.c_str(), PSTR("wifidisconnect")) != 0) && (strcasecmp_P(sCommand.c_str(), PSTR("reboot")) != 0)&& (strcasecmp_P(sCommand.c_str(), PSTR("reset")) != 0))
  {
    if (timerAPoff)
      timerAPoff = millis() + 2000L;  //user has reached the main page - AP can be switched off in 2..3 sec



    printToWeb = true;
    printWebString = "";
    if (sCommand.length() > 0) {
      ExecuteCommand(VALUE_SOURCE_HTTP, sCommand.c_str());
    }

    IPAddress ip = WiFi.localIP();
    // IPAddress gw = WiFi.gatewayIP();

    TXBuffer += printWebString;
    TXBuffer += F("<form>");
    TXBuffer += F("<table><TR><TH>System Info<TH>Value<TH><TH>System Info<TH>Value<TH>");

    TXBuffer += F("<TR><TD>Unit:<TD>");
    TXBuffer += String(Settings.Unit);

    TXBuffer += F("<TD><TD>GIT version:<TD>");
    TXBuffer += BUILD_GIT;

    TXBuffer += F("<TR><TD>Local Time:<TD>");
    if (Settings.UseNTP)
    {
      TXBuffer += getDateTimeString('-', ':', ' ');
    }
    else
      TXBuffer += F("NTP disabled");

    TXBuffer += F("<TD><TD>Uptime:<TD>");
    char strUpTime[40];
    int minutes = wdcounter / 2;
    int days = minutes / 1440;
    minutes = minutes % 1440;
    int hrs = minutes / 60;
    minutes = minutes % 60;
    sprintf_P(strUpTime, PSTR("%d days %d hours %d minutes"), days, hrs, minutes);
    TXBuffer += strUpTime;

    TXBuffer += F("<TR><TD>Load:<TD>");
    if (wdcounter > 0)
    {
      TXBuffer += String(100 - (100 * loopCounterLast / loopCounterMax));
      TXBuffer += F("% (LC=");
      TXBuffer += String(int(loopCounterLast / 30));
      TXBuffer += F(")");
    }

    TXBuffer += F("<TD><TD>Free Mem:<TD>");
    TXBuffer += String(freeMem);
    TXBuffer += F(" (");
    TXBuffer += String(lowestRAM);
    TXBuffer += F(" - ");
    TXBuffer += String(lowestRAMfunction);
    TXBuffer += F(")");

    TXBuffer += F("<TR><TD>IP:<TD>");
    TXBuffer += formatIP(ip);

    TXBuffer += F("<TD><TD>Wifi RSSI:<TD>");
    if (WiFi.status() == WL_CONNECTED)
    {
      TXBuffer += String(WiFi.RSSI());
      TXBuffer += F(" dB");
    }

    #ifdef FEATURE_MDNS
      TXBuffer += F("<TR><TD>mDNS:<TD><a href='http://");
      TXBuffer += WifiGetHostname();
      TXBuffer += F(".local'>");
      TXBuffer += WifiGetHostname();
      TXBuffer += F(".local</a><TD><TD><TD>");
    #endif


    TXBuffer += F("<TR><TH>Node List:<TH>Name<TH>Build<TH>Type<TH>IP<TH>Age<TR><TD><TD>");
    for (byte x = 0; x < UNIT_MAX; x++)
    {
      if (Nodes[x].ip[0] != 0)
      {
        char url[80];
        sprintf_P(url, PSTR("<a class='button link' href='http://%u.%u.%u.%u'>%u.%u.%u.%u</a>"), Nodes[x].ip[0], Nodes[x].ip[1], Nodes[x].ip[2], Nodes[x].ip[3], Nodes[x].ip[0], Nodes[x].ip[1], Nodes[x].ip[2], Nodes[x].ip[3]);
        TXBuffer += F("<TR><TD>Unit ");
        TXBuffer += String(x);
        TXBuffer += F("<TD>");
        if (x != Settings.Unit)
          TXBuffer += Nodes[x].nodeName;
        else
          TXBuffer += Settings.Name;
        TXBuffer += F("<TD>");
        if (Nodes[x].build)
          TXBuffer += String(Nodes[x].build);
        TXBuffer += F("<TD>");
        if (Nodes[x].nodeType)
          switch (Nodes[x].nodeType)
          {
            case NODE_TYPE_ID_ESP_EASY_STD:
              TXBuffer += F("ESP Easy");
              break;
            case NODE_TYPE_ID_ESP_EASYM_STD:
              TXBuffer += F("ESP Easy Mega");
              break;
            case NODE_TYPE_ID_ESP_EASY32_STD:
              TXBuffer += F("ESP Easy 32");
              break;
            case NODE_TYPE_ID_ARDUINO_EASY_STD:
              TXBuffer += F("Arduino Easy");
              break;
            case NODE_TYPE_ID_NANO_EASY_STD:
              TXBuffer += F("Nano Easy");
              break;
          }
        TXBuffer += F("<TD>");
        TXBuffer += url;
        TXBuffer += F("<TD>");
        TXBuffer += String( Nodes[x].age);
      }
    }

    TXBuffer += F("</table></form>");

    printWebString = "";
    printToWeb = false;
    sendHeadandTail(F("TmplStd"),_TAIL);
    TXBuffer.endStream();
      Serial.println("done.");

  }
  else
  {
    //TODO: move this to handle_tools, from where it is actually called?

    // have to disconnect or reboot from within the main loop
    // because the webconnection is still active at this point
    // disconnect here could result into a crash/reboot...
    if (strcasecmp_P(sCommand.c_str(), PSTR("wifidisconnect")) == 0)
    {
      String log = F("WIFI : Disconnecting...");
      addLog(LOG_LEVEL_INFO, log);
      cmd_within_mainloop = CMD_WIFI_DISCONNECT;
    }

    if (strcasecmp_P(sCommand.c_str(), PSTR("reboot")) == 0)
    {
      String log = F("     : Rebooting...");
      addLog(LOG_LEVEL_INFO, log);
      cmd_within_mainloop = CMD_REBOOT;
    }
   if (strcasecmp_P(sCommand.c_str(), PSTR("reset")) == 0)
    {
      String log = F("     : factory reset...");
      addLog(LOG_LEVEL_INFO, log);
      cmd_within_mainloop = CMD_REBOOT;
      TXBuffer+= F("OK. Please wait > 1 min and connect to Acces point. PW=configesp, URL=192.168.4.1");
      TXBuffer.endStream();
      ExecuteCommand(VALUE_SOURCE_HTTP, sCommand.c_str());
    }

    TXBuffer+= "OK";
    TXBuffer.endStream();

  }
}


//********************************************************************************
// Web Interface config page
//********************************************************************************
void handle_config() {

 if (!isLoggedIn()) return;
   navMenuIndex = 1;
   TXBuffer.startStream();
   sendHeadandTail(F("TmplStd"),_HEAD);

  if (timerAPoff)
    timerAPoff = millis() + 2000L;  //user has reached the main page - AP can be switched off in 2..3 sec


  String name = WebServer.arg(F("name"));
  //String password = WebServer.arg(F("password"));
  String ssid = WebServer.arg(F("ssid"));
  //String key = WebServer.arg(F("key"));
  String ssid2 = WebServer.arg(F("ssid2"));
  //String key2 = WebServer.arg(F("key2"));
  String iprangelow = WebServer.arg(F("iprangelow"));
  String iprangehigh = WebServer.arg(F("iprangehigh"));

  String sensordelay = WebServer.arg(F("delay"));
  String deepsleep = WebServer.arg(F("deepsleep"));
  String deepsleeponfail = WebServer.arg(F("deepsleeponfail"));
  String espip = WebServer.arg(F("espip"));
  String espgateway = WebServer.arg(F("espgateway"));
  String espsubnet = WebServer.arg(F("espsubnet"));
  String espdns = WebServer.arg(F("espdns"));
  String unit = WebServer.arg(F("unit"));
  //String apkey = WebServer.arg(F("apkey"));


  if (ssid[0] != 0)
  {
    if (strcmp(Settings.Name, name.c_str()) != 0) {
      addLog(LOG_LEVEL_INFO, F("Unit Name changed."));
      MQTTclient_should_reconnect = true;
    }
    strncpy(Settings.Name, name.c_str(), sizeof(Settings.Name));
    //strncpy(SecuritySettings.Password, password.c_str(), sizeof(SecuritySettings.Password));
    copyFormPassword(F("password"), SecuritySettings.Password, sizeof(SecuritySettings.Password));
    strncpy(SecuritySettings.WifiSSID, ssid.c_str(), sizeof(SecuritySettings.WifiSSID));
    //strncpy(SecuritySettings.WifiKey, key.c_str(), sizeof(SecuritySettings.WifiKey));
    copyFormPassword(F("key"), SecuritySettings.WifiKey, sizeof(SecuritySettings.WifiKey));
    strncpy(SecuritySettings.WifiSSID2, ssid2.c_str(), sizeof(SecuritySettings.WifiSSID2));
    //strncpy(SecuritySettings.WifiKey2, key2.c_str(), sizeof(SecuritySettings.WifiKey2));
    copyFormPassword(F("key2"), SecuritySettings.WifiKey2, sizeof(SecuritySettings.WifiKey2));
    //strncpy(SecuritySettings.WifiAPKey, apkey.c_str(), sizeof(SecuritySettings.WifiAPKey));
    copyFormPassword(F("apkey"), SecuritySettings.WifiAPKey, sizeof(SecuritySettings.WifiAPKey));

    Settings.Delay = sensordelay.toInt();
    Settings.deepSleep = deepsleep.toInt();
    Settings.deepSleepOnFail = (deepsleeponfail == "on");
    str2ip(espip, Settings.IP);
    str2ip(espgateway, Settings.Gateway);
    str2ip(espsubnet, Settings.Subnet);
    str2ip(espdns, Settings.DNS);
    Settings.Unit = unit.toInt();
    addHtmlError(  SaveSettings());
  }

  TXBuffer += F("<form name='frmselect' method='post'><table>");

  addFormHeader(TXBuffer.buf,  F("Main Settings"));

  Settings.Name[25] = 0;
  SecuritySettings.Password[25] = 0;
  addFormTextBox(TXBuffer.buf,  F("Unit Name"), F("name"), Settings.Name, 25);
  addFormNumericBox(TXBuffer.buf,  F("Unit Number"), F("unit"), Settings.Unit, 0, 9999);
  addFormPasswordBox( TXBuffer.buf, F("Admin Password"), F("password"), SecuritySettings.Password, 25);

  addFormSubHeader(TXBuffer.buf,  F("Wifi Settings"));

  addFormTextBox(TXBuffer.buf,  F("SSID"), F("ssid"), SecuritySettings.WifiSSID, 31);
  addFormPasswordBox( TXBuffer.buf, F("WPA Key"), F("key"), SecuritySettings.WifiKey, 63);
  addFormTextBox(TXBuffer.buf,  F("Fallback SSID"), F("ssid2"), SecuritySettings.WifiSSID2, 31);
  addFormPasswordBox(TXBuffer.buf,  F("Fallback WPA Key"), F("key2"), SecuritySettings.WifiKey2, 63);
  addFormSeparator(TXBuffer.buf  );
  addFormPasswordBox( TXBuffer.buf, F("WPA AP Mode Key"), F("apkey"), SecuritySettings.WifiAPKey, 63);

  addFormSubHeader( TXBuffer.buf, F("IP Settings"));

  addFormIPBox(TXBuffer.buf,  F("ESP IP"), F("espip"), Settings.IP);
  addFormIPBox( TXBuffer.buf, F("ESP GW"), F("espgateway"), Settings.Gateway);
  addFormIPBox(TXBuffer.buf,  F("ESP Subnet"), F("espsubnet"), Settings.Subnet);
  addFormIPBox(TXBuffer.buf,  F("ESP DNS"), F("espdns"), Settings.DNS);
  addFormNote(TXBuffer.buf,  F("Leave empty for DHCP"));


  addFormSubHeader(TXBuffer.buf,  F("Sleep Mode"));

  addFormNumericBox(TXBuffer.buf,  F("Sleep awake time"), F("deepsleep"), Settings.deepSleep, 0, 255);
  addUnit(TXBuffer.buf,  F("sec"));
  addFormNote(TXBuffer.buf,  F("0 = Sleep Disabled, else time awake from sleep"));

  addHelpButton(TXBuffer.buf,  F("SleepMode"));
  addFormNumericBox(TXBuffer.buf,  F("Sleep Delay"), F("delay"), Settings.Delay, 0, 4294);   //limited by hardware to ~1.2h
  addUnit(TXBuffer.buf,  F("sec"));

  addFormCheckBox( TXBuffer.buf, F("Sleep on connection failure"), F("deepsleeponfail"), Settings.deepSleepOnFail);

  addFormSeparator( TXBuffer.buf);

  TXBuffer += F("<TR><TD><TD>");
  addSubmitButton(TXBuffer.buf );
  TXBuffer += F("</table></form>");

  sendHeadandTail(F("TmplStd"),_TAIL);
  TXBuffer.endStream();
}


//********************************************************************************
// Web Interface controller page
//********************************************************************************
void handle_controllers() {
  if (!isLoggedIn()) return;
  navMenuIndex = 2;
  TXBuffer.startStream();
  sendHeadandTail(F("TmplStd"),_HEAD);

  struct EventStruct TempEvent;

  byte controllerindex = WebServer.arg(F("index")).toInt();
  boolean controllerNotSet = controllerindex == 0;
  --controllerindex;

  String usedns = WebServer.arg(F("usedns"));
  String controllerip = WebServer.arg(F("controllerip"));
  String controllerhostname = WebServer.arg(F("controllerhostname"));
  String controllerport = WebServer.arg(F("controllerport"));
  String protocol = WebServer.arg(F("protocol"));
  String controlleruser = WebServer.arg(F("controlleruser"));
  String controllerpassword = WebServer.arg(F("controllerpassword"));
  String controllersubscribe = WebServer.arg(F("controllersubscribe"));
  String controllerpublish = WebServer.arg(F("controllerpublish"));
  String controllerenabled = WebServer.arg(F("controllerenabled"));



  //submitted data
  if (protocol.length() != 0 && !controllerNotSet)
  {
    ControllerSettingsStruct ControllerSettings;
    //submitted changed protocol
    if (Settings.Protocol[controllerindex] != protocol.toInt())
    {

      Settings.Protocol[controllerindex] = protocol.toInt();

      //there is a protocol selected?
      if (Settings.Protocol[controllerindex]!=0)
      {
        //reset (some) default-settings
        byte ProtocolIndex = getProtocolIndex(Settings.Protocol[controllerindex]);
        ControllerSettings.Port = Protocol[ProtocolIndex].defaultPort;
        if (Protocol[ProtocolIndex].usesTemplate)
          CPlugin_ptr[ProtocolIndex](CPLUGIN_PROTOCOL_TEMPLATE, &TempEvent, dummyString);
        strncpy(ControllerSettings.Subscribe, TempEvent.String1.c_str(), sizeof(ControllerSettings.Subscribe));
        strncpy(ControllerSettings.Publish, TempEvent.String2.c_str(), sizeof(ControllerSettings.Publish));
        TempEvent.String1 = "";
        TempEvent.String2 = "";
        //NOTE: do not enable controller by default, give user a change to enter sensible values first

        //not resetted to default (for convenience)
        //SecuritySettings.ControllerUser[controllerindex]
        //SecuritySettings.ControllerPassword[controllerindex]

        ClearCustomControllerSettings(controllerindex);
      }

    }

    //subitted same protocol
    else
    {
      //there is a protocol selected
      if (Settings.Protocol != 0)
      {
        //copy all settings to conroller settings struct
        byte ProtocolIndex = getProtocolIndex(Settings.Protocol[controllerindex]);
        TempEvent.ControllerIndex = controllerindex;
        TempEvent.ProtocolIndex = ProtocolIndex;
        CPlugin_ptr[ProtocolIndex](CPLUGIN_WEBFORM_SAVE, &TempEvent, dummyString);
        ControllerSettings.UseDNS = usedns.toInt();
        if (ControllerSettings.UseDNS)
        {
          ControllerSettings.setHostname(controllerhostname);
        }
        //no protocol selected
        else
        {
          str2ip(controllerip, ControllerSettings.IP);
        }
        //copy settings to struct
        Settings.ControllerEnabled[controllerindex] = (controllerenabled == "on");
        ControllerSettings.Port = controllerport.toInt();
        strncpy(SecuritySettings.ControllerUser[controllerindex], controlleruser.c_str(), sizeof(SecuritySettings.ControllerUser[0]));
        //strncpy(SecuritySettings.ControllerPassword[controllerindex], controllerpassword.c_str(), sizeof(SecuritySettings.ControllerPassword[0]));
        copyFormPassword(F("controllerpassword"), SecuritySettings.ControllerPassword[controllerindex], sizeof(SecuritySettings.ControllerPassword[0]));
        strncpy(ControllerSettings.Subscribe, controllersubscribe.c_str(), sizeof(ControllerSettings.Subscribe));
        strncpy(ControllerSettings.Publish, controllerpublish.c_str(), sizeof(ControllerSettings.Publish));
        CPlugin_ptr[ProtocolIndex](CPLUGIN_INIT, &TempEvent, dummyString);
      }
    }
    addHtmlError( TXBuffer.buf, SaveControllerSettings(controllerindex, (byte*)&ControllerSettings, sizeof(ControllerSettings)));
    addHtmlError( TXBuffer.buf,  SaveSettings());
  }

  TXBuffer += F("<form name='frmselect' method='post'>");

  if (controllerNotSet)
  {
    TXBuffer += F("<table border=1px frame='box' rules='all'><TR><TH>");
    TXBuffer += F("<TH>Nr<TH>Enabled<TH>Protocol<TH>Host<TH>Port");

    ControllerSettingsStruct ControllerSettings;
    for (byte x = 0; x < CONTROLLER_MAX; x++)
    {
      LoadControllerSettings(x, (byte*)&ControllerSettings, sizeof(ControllerSettings));
      TXBuffer += F("<TR><TD>");
      TXBuffer += F("<a class='button link' href=\"controllers?index=");
      TXBuffer +=  x + 1;
      TXBuffer += F("\">Edit</a>");
      TXBuffer += F("<TD>");
      TXBuffer +=  getControllerSymbol(x);
      TXBuffer += F("<TD>");
      if (Settings.Protocol[x] != 0)
      {
        addEnabled( TXBuffer.buf, Settings.ControllerEnabled[x]);

        TXBuffer += F("<TD>");
        byte ProtocolIndex = getProtocolIndex(Settings.Protocol[x]);
        String ProtocolName = "";
        CPlugin_ptr[ProtocolIndex](CPLUGIN_GET_DEVICENAME, 0, ProtocolName);
        TXBuffer +=  ProtocolName;

        TXBuffer += F("<TD>");
        TXBuffer +=  ControllerSettings.getHost();
        TXBuffer += F("<TD>");
        TXBuffer +=  ControllerSettings.Port;
      }
      else
        TXBuffer += F("<TD><TD><TD>");
    }
    TXBuffer += F("</table></form>");
  }
  else
  {
    TXBuffer += F("<table><TR><TH>Controller Settings<TH>");
    TXBuffer += F("<TR><TD>Protocol:");
    byte choice = Settings.Protocol[controllerindex];
    TXBuffer += F("<TD>");
    addSelector_Head(TXBuffer.buf,  F("protocol"), true);
    addSelector_Item(TXBuffer.buf,  F("- Standalone -"), 0, false, false, F(""));
    for (byte x = 0; x <= protocolCount; x++)
    {
      String ProtocolName = "";
      CPlugin_ptr[x](CPLUGIN_GET_DEVICENAME, 0, ProtocolName);
      boolean disabled = false;// !((controllerindex == 0) || !Protocol[x].usesMQTT);
      addSelector_Item( TXBuffer.buf,
                       ProtocolName,
                       Protocol[x].Number,
                       choice == Protocol[x].Number,
                       disabled,
                       F(""));
    }
    addSelector_Foot( TXBuffer.buf);

    addHelpButton(TXBuffer.buf,  F("EasyProtocols"));
      // char str[20];

    if (Settings.Protocol[controllerindex])
    {
      ControllerSettingsStruct ControllerSettings;
      LoadControllerSettings(controllerindex, (byte*)&ControllerSettings, sizeof(ControllerSettings));
      byte choice = ControllerSettings.UseDNS;
      String options[2];
      options[0] = F("Use IP address");
      options[1] = F("Use Hostname");

      byte ProtocolIndex = getProtocolIndex(Settings.Protocol[controllerindex]);
      if (!Protocol[ProtocolIndex].Custom){

        addFormSelector(TXBuffer.buf,  F("Locate Controller"), F("usedns"), 2, options, NULL, NULL, choice, true);

        if (ControllerSettings.UseDNS)
        {
          addFormTextBox(TXBuffer.buf,  F("Controller Hostname"), F("controllerhostname"), ControllerSettings.HostName, sizeof(ControllerSettings.HostName)-1);
        }
        else
        {
          addFormIPBox(TXBuffer.buf,  F("Controller IP"), F("controllerip"), ControllerSettings.IP);
        }

        addFormNumericBox(TXBuffer.buf,  F("Controller Port"), F("controllerport"), ControllerSettings.Port, 1, 65535);

        if (Protocol[ProtocolIndex].usesAccount)
        {
           String protoDisplayName;
          if (!getControllerProtocolDisplayName(ProtocolIndex, CONTROLLER_USER, protoDisplayName)) {
            protoDisplayName = F("Controller User");
          }
          addFormTextBox(TXBuffer.buf, protoDisplayName, F("controlleruser"), SecuritySettings.ControllerUser[controllerindex], sizeof(SecuritySettings.ControllerUser[0])-1);
         }
        if (Protocol[ProtocolIndex].usesPassword)
        {
          String protoDisplayName;
          if (getControllerProtocolDisplayName(ProtocolIndex, CONTROLLER_PASS, protoDisplayName)) {
            // It is not a regular password, thus use normal text field.
            addFormTextBox(TXBuffer.buf, protoDisplayName, F("controllerpassword"), SecuritySettings.ControllerPassword[controllerindex], sizeof(SecuritySettings.ControllerPassword[0])-1);
          } else {
            addFormPasswordBox(TXBuffer.buf, F("Controller Password"), F("controllerpassword"), SecuritySettings.ControllerPassword[controllerindex], sizeof(SecuritySettings.ControllerPassword[0])-1);
          }
        }

        if (Protocol[ProtocolIndex].usesTemplate || Protocol[ProtocolIndex].usesMQTT)
        {
           String protoDisplayName;
          if (!getControllerProtocolDisplayName(ProtocolIndex, CONTROLLER_SUBSCRIBE, protoDisplayName)) {
            protoDisplayName = F("Controller Subscribe");
          }
          addFormTextBox(TXBuffer.buf, protoDisplayName, F("controllersubscribe"), ControllerSettings.Subscribe, sizeof(ControllerSettings.Subscribe)-1);
         }

        if (Protocol[ProtocolIndex].usesTemplate || Protocol[ProtocolIndex].usesMQTT)
        {
           String protoDisplayName;
          if (!getControllerProtocolDisplayName(ProtocolIndex, CONTROLLER_PUBLISH, protoDisplayName)) {
            protoDisplayName = F("Controller Publish");
          }
          addFormTextBox(TXBuffer.buf, protoDisplayName, F("controllerpublish"), ControllerSettings.Publish, sizeof(ControllerSettings.Publish)-1);
         }

      }

      addFormCheckBox(TXBuffer.buf,  F("Enabled"), F("controllerenabled"), Settings.ControllerEnabled[controllerindex]);

      TempEvent.ControllerIndex = controllerindex;
      TempEvent.ProtocolIndex = ProtocolIndex;
      CPlugin_ptr[ProtocolIndex](CPLUGIN_WEBFORM_LOAD, &TempEvent,TXBuffer.buf);

    }

    addFormSeparator (TXBuffer.buf);

    TXBuffer += F("<TR><TD><TD><a class='button link' href=\"controllers\">Close</a>");
    addSubmitButton (TXBuffer.buf);
    TXBuffer += F("</table></form>");
  }

    sendHeadandTail(F("TmplStd"),_TAIL);
    TXBuffer.endStream();
}


//********************************************************************************
// Web Interface notifcations page
//********************************************************************************
void handle_notifications() {
  if (!isLoggedIn()) return;
  navMenuIndex = 6;
  TXBuffer.startStream();
  sendHeadandTail(F("TmplStd"),_HEAD);

  struct EventStruct TempEvent;
  // char tmpString[64];


  byte notificationindex = WebServer.arg(F("index")).toInt();
  boolean notificationindexNotSet = notificationindex == 0;
  --notificationindex;

  String notification = WebServer.arg(F("notification"));
  String domain = WebServer.arg(F("domain"));
  String server = WebServer.arg(F("server"));
  String port = WebServer.arg(F("port"));
  String sender = WebServer.arg(F("sender"));
  String receiver = WebServer.arg(F("receiver"));
  String subject = WebServer.arg(F("subject"));
  String user = WebServer.arg(F("user"));
  String pass = WebServer.arg(F("pass"));
  String body = WebServer.arg(F("body"));
  String pin1 = WebServer.arg(F("pin1"));
  String pin2 = WebServer.arg(F("pin2"));
  String notificationenabled = WebServer.arg(F("notificationenabled"));




  if (notification.length() != 0 && !notificationindexNotSet)
  {
    NotificationSettingsStruct NotificationSettings;
    if (Settings.Notification[notificationindex] != notification.toInt())
    {
      Settings.Notification[notificationindex] = notification.toInt();
    }
    else
    {
      if (Settings.Notification != 0)
      {
        byte NotificationProtocolIndex = getNotificationProtocolIndex(Settings.Notification[notificationindex]);
        if (NotificationProtocolIndex!=NPLUGIN_NOT_FOUND)
          NPlugin_ptr[NotificationProtocolIndex](NPLUGIN_WEBFORM_SAVE, 0, dummyString);
        NotificationSettings.Port = port.toInt();
        NotificationSettings.Pin1 = pin1.toInt();
        NotificationSettings.Pin2 = pin2.toInt();
        Settings.NotificationEnabled[notificationindex] = (notificationenabled == "on");
        strncpy(NotificationSettings.Domain, domain.c_str(), sizeof(NotificationSettings.Domain));
        strncpy(NotificationSettings.Server, server.c_str(), sizeof(NotificationSettings.Server));
        strncpy(NotificationSettings.Sender, sender.c_str(), sizeof(NotificationSettings.Sender));
        strncpy(NotificationSettings.Receiver, receiver.c_str(), sizeof(NotificationSettings.Receiver));
        strncpy(NotificationSettings.Subject, subject.c_str(), sizeof(NotificationSettings.Subject));
        strncpy(NotificationSettings.User, user.c_str(), sizeof(NotificationSettings.User));
        strncpy(NotificationSettings.Pass, pass.c_str(), sizeof(NotificationSettings.Pass));
        strncpy(NotificationSettings.Body, body.c_str(), sizeof(NotificationSettings.Body));
      }
    }
    // Save the settings.
    addHtmlError(  SaveNotificationSettings(notificationindex, (byte*)&NotificationSettings, sizeof(NotificationSettings)));
    addHtmlError(  SaveSettings());
    if (WebServer.hasArg(F("test"))) {
      // Perform tests with the settings in the form.
      byte NotificationProtocolIndex = getNotificationProtocolIndex(Settings.Notification[notificationindex]);
      if (NotificationProtocolIndex != NPLUGIN_NOT_FOUND)
      {
        // TempEvent.NotificationProtocolIndex = NotificationProtocolIndex;
        TempEvent.NotificationIndex = notificationindex;
        NPlugin_ptr[NotificationProtocolIndex](NPLUGIN_NOTIFY, &TempEvent, dummyString);
      }
    }
  }

  TXBuffer += F("<form name='frmselect' method='post'>");

  if (notificationindexNotSet)
  {
    TXBuffer += F("<table border=1px frame='box' rules='all'><TR><TH>");
    TXBuffer += F("<TH>Nr<TH>Enabled<TH>Service<TH>Server<TH>Port");

    NotificationSettingsStruct NotificationSettings;
    for (byte x = 0; x < NOTIFICATION_MAX; x++)
    {
      LoadNotificationSettings(x, (byte*)&NotificationSettings, sizeof(NotificationSettings));
      TXBuffer += F("<TR><TD>");
      TXBuffer += F("<a class='button link' href=\"notifications?index=");
      TXBuffer +=  x + 1;
      TXBuffer += F("\">Edit</a>");
      TXBuffer += F("<TD>");
      TXBuffer +=  x + 1;
      TXBuffer += F("<TD>");
      if (Settings.Notification[x] != 0)
      {
        addEnabled( TXBuffer.buf, Settings.NotificationEnabled[x]);

        TXBuffer += F("<TD>");
        byte NotificationProtocolIndex = getNotificationProtocolIndex(Settings.Notification[x]);
        String NotificationName = F("(plugin not found?)");
        if (NotificationProtocolIndex!=NPLUGIN_NOT_FOUND)
        {
          NPlugin_ptr[NotificationProtocolIndex](NPLUGIN_GET_DEVICENAME, 0, NotificationName);
        }
        TXBuffer +=  NotificationName;
        TXBuffer += F("<TD>");
        TXBuffer +=  NotificationSettings.Server;
        TXBuffer += F("<TD>");
        TXBuffer +=  NotificationSettings.Port;
      }
      else
        TXBuffer += F("<TD><TD><TD>");
    }
    TXBuffer += F("</table></form>");
  }
  else
  {
    TXBuffer += F("<table><TR><TH>Notification Settings<TH>");
    TXBuffer += F("<TR><TD>Notification:");
    byte choice = Settings.Notification[notificationindex];
    TXBuffer += F("<TD>");
    addSelector_Head( TXBuffer.buf, F("notification"), true);
    addSelector_Item( TXBuffer.buf, F("- None -"), 0, false, false, F(""));
    for (byte x = 0; x <= notificationCount; x++)
    {
      String NotificationName = "";
      NPlugin_ptr[x](NPLUGIN_GET_DEVICENAME, 0, NotificationName);
      addSelector_Item( TXBuffer.buf,
                       NotificationName,
                       Notification[x].Number,
                       choice == Notification[x].Number,
                       false,
                       F(""));
    }
    addSelector_Foot (TXBuffer.buf);

    addHelpButton(TXBuffer.buf,  F("EasyNotifications"));


    // char str[20];

    if (Settings.Notification[notificationindex])
    {
      NotificationSettingsStruct NotificationSettings;
      LoadNotificationSettings(notificationindex, (byte*)&NotificationSettings, sizeof(NotificationSettings));

      byte NotificationProtocolIndex = getNotificationProtocolIndex(Settings.Notification[notificationindex]);
      if (NotificationProtocolIndex!=NPLUGIN_NOT_FOUND)
      {

        if (Notification[NotificationProtocolIndex].usesMessaging)
        {
          TXBuffer += F("<TR><TD>Domain:<TD><input type='text' name='domain' size=64 value='");
          TXBuffer +=  NotificationSettings.Domain;
          TXBuffer += F("'>");

          TXBuffer += F("<TR><TD>Server:<TD><input type='text' name='server' size=64 value='");
          TXBuffer +=  NotificationSettings.Server;
          TXBuffer += F("'>");

          TXBuffer += F("<TR><TD>Port:<TD><input type='text' name='port' value='");
          TXBuffer +=  NotificationSettings.Port;
          TXBuffer += F("'>");

          TXBuffer += F("<TR><TD>Sender:<TD><input type='text' name='sender' size=64 value='");
          TXBuffer +=  NotificationSettings.Sender;
          TXBuffer += F("'>");

          TXBuffer += F("<TR><TD>Receiver:<TD><input type='text' name='receiver' size=64 value='");
          TXBuffer +=  NotificationSettings.Receiver;
          TXBuffer += F("'>");

          TXBuffer += F("<TR><TD>Subject:<TD><input type='text' name='subject' size=64 value='");
          TXBuffer +=  NotificationSettings.Subject;
          TXBuffer += F("'>");

          TXBuffer += F("<TR><TD>User:<TD><input type='text' name='user' size=48 value='");
          TXBuffer +=  NotificationSettings.User;
          TXBuffer += F("'>");

          TXBuffer += F("<TR><TD>Pass:<TD><input type='text' name='pass' size=32 value='");
          TXBuffer +=  NotificationSettings.Pass;
          TXBuffer += F("'>");

          TXBuffer += F("<TR><TD>Body:<TD><textarea name='body' rows='5' cols='80' size=512 wrap='off'>");
          TXBuffer +=  NotificationSettings.Body;
          TXBuffer += F("</textarea>");
        }

        if (Notification[NotificationProtocolIndex].usesGPIO > 0)
        {
          TXBuffer += F("<TR><TD>1st GPIO:<TD>");
          addPinSelect(false, TXBuffer.buf,  "pin1", NotificationSettings.Pin1);
        }

        TXBuffer += F("<TR><TD>Enabled:<TD>");
        addCheckBox(TXBuffer.buf,  F("notificationenabled"), Settings.NotificationEnabled[notificationindex]);

        TempEvent.NotificationIndex = notificationindex;
        NPlugin_ptr[NotificationProtocolIndex](NPLUGIN_WEBFORM_LOAD, &TempEvent,TXBuffer.buf);
      }
    }

    addFormSeparator (TXBuffer.buf);

    TXBuffer += F("<TR><TD><TD><a class='button link' href=\"notifications\">Close</a>");
    addSubmitButton (TXBuffer.buf);
    addSubmitButton(TXBuffer.buf,  F("Test"), F("test"));
    TXBuffer += F("</table></form>");
  }
    sendHeadandTail(F("TmplStd"),_TAIL);
    TXBuffer.endStream();
}


//********************************************************************************
// Web Interface hardware page
//********************************************************************************
void handle_hardware() {
  if (!isLoggedIn()) return;
  navMenuIndex = 3;
  TXBuffer.startStream();
  sendHeadandTail(F("TmplStd"),_HEAD);

  if (isFormItem(F("psda")))
  {
    Settings.Pin_status_led  = getFormItemInt(F("pled"));
    Settings.Pin_status_led_Inversed  = isFormItemChecked(F("pledi"));
    Settings.Pin_i2c_sda     = getFormItemInt(F("psda"));
    Settings.Pin_i2c_scl     = getFormItemInt(F("pscl"));
    Settings.InitSPI = isFormItemChecked(F("initspi"));      // SPI Init
    Settings.Pin_sd_cs  = getFormItemInt(F("sd"));
    Settings.PinBootStates[0]  =  getFormItemInt(F("p0"));
    Settings.PinBootStates[2]  =  getFormItemInt(F("p2"));
    Settings.PinBootStates[4]  =  getFormItemInt(F("p4"));
    Settings.PinBootStates[5]  =  getFormItemInt(F("p5"));
    Settings.PinBootStates[9]  =  getFormItemInt(F("p9"));
    Settings.PinBootStates[10] =  getFormItemInt(F("p10"));
    Settings.PinBootStates[12] =  getFormItemInt(F("p12"));
    Settings.PinBootStates[13] =  getFormItemInt(F("p13"));
    Settings.PinBootStates[14] =  getFormItemInt(F("p14"));
    Settings.PinBootStates[15] =  getFormItemInt(F("p15"));
    Settings.PinBootStates[16] =  getFormItemInt(F("p16"));

    addHtmlError(TXBuffer.buf, SaveSettings());
  }

  TXBuffer += F("<form  method='post'><table><TR><TH>Hardware Settings<TH><TR><TD>");

  addFormSubHeader(TXBuffer.buf, F("Wifi Status LED"));
  addFormPinSelect( TXBuffer.buf,F("GPIO &rarr; LED"), "pled", Settings.Pin_status_led);
  addFormCheckBox(TXBuffer.buf,  F("Inversed LED"), F("pledi"), Settings.Pin_status_led_Inversed);
  addFormNote(TXBuffer.buf,      F("Use &rsquo;GPIO-2 (D4)&rsquo; with &rsquo;Inversed&rsquo; checked for onboard LED"));
  addFormSubHeader(TXBuffer.buf, F("I2C Interface"));
  addFormPinSelectI2C(TXBuffer.buf, F("GPIO &#8703; SDA"), F("psda"), Settings.Pin_i2c_sda);
  addFormPinSelectI2C(TXBuffer.buf, F("GPIO &#8702; SCL"), F("pscl"), Settings.Pin_i2c_scl);

  // SPI Init
  addFormSubHeader(TXBuffer.buf, F("SPI Interface"));
  addFormCheckBox(TXBuffer.buf,  F("Init SPI"), F("initspi"), Settings.InitSPI);
  addFormNote( TXBuffer.buf,     F("CLK=GPIO-14 (D5), MISO=GPIO-12 (D6), MOSI=GPIO-13 (D7)"));
  addFormNote( TXBuffer.buf,     F("Chip Select (CS) config must be done in the plugin"));
#ifdef FEATURE_SD
  addFormPinSelect( TXBuffer.buf,F("GPIO &rarr; SD Card CS"), "sd", Settings.Pin_sd_cs);
#endif

  addFormSubHeader(TXBuffer.buf, F("GPIO boot states"));
  addFormPinStateSelect(TXBuffer.buf, F("Pin mode 0 (D3)"), F("p0"), Settings.PinBootStates[0]);
  addFormPinStateSelect(TXBuffer.buf, F("Pin mode 2 (D4)"), F("p2"), Settings.PinBootStates[2]);
  addFormPinStateSelect(TXBuffer.buf, F("Pin mode 4 (D2)"), F("p4"), Settings.PinBootStates[4]);
  addFormPinStateSelect(TXBuffer.buf, F("Pin mode 5 (D1)"), F("p5"), Settings.PinBootStates[5]);
  addFormPinStateSelect(TXBuffer.buf, F("Pin mode 9 (D11)"), F("p9"), Settings.PinBootStates[9]);
  addFormPinStateSelect(TXBuffer.buf, F("Pin mode 10 (D12)"), F("p10"), Settings.PinBootStates[10]);
  addFormPinStateSelect(TXBuffer.buf, F("Pin mode 12 (D6)"), F("p12"), Settings.PinBootStates[12]);
  addFormPinStateSelect(TXBuffer.buf, F("Pin mode 13 (D7)"), F("p13"), Settings.PinBootStates[13]);
  addFormPinStateSelect(TXBuffer.buf, F("Pin mode 14 (D5)"), F("p14"), Settings.PinBootStates[14]);
  addFormPinStateSelect(TXBuffer.buf, F("Pin mode 15 (D8)"), F("p15"), Settings.PinBootStates[15]);
  addFormPinStateSelect(TXBuffer.buf, F("Pin mode 16 (D0)"), F("p16"), Settings.PinBootStates[16]);
  addFormSeparator(TXBuffer.buf);

  TXBuffer += F("<TR><TD><TD>");
  addSubmitButton(TXBuffer.buf);
  addHelpButton(TXBuffer.buf, F("ESPEasy#Hardware_page"));
  TXBuffer += F("<TR><TD></table></form>");

  sendHeadandTail(F("TmplStd"),_TAIL);
  TXBuffer.endStream();

}

//********************************************************************************
// Add a GPIO pin select dropdown list
//********************************************************************************
void addFormPinStateSelect(String& str, const String& label, const String& id, int choice)
{
  addRowLabel(str, label);
  addPinStateSelect(str, id, choice);
  TXBuffer.checkFull();
}

void addPinStateSelect(String& str, String name, int choice)
{
  String options[4] = { F("Default"), F("Output Low"), F("Output High"), F("Input") };
  addSelector(str, name, 4, options, NULL, NULL, choice, false);
  TXBuffer.checkFull();
}

//********************************************************************************
// Add a IP Access Control select dropdown list
//********************************************************************************
void addFormIPaccessControlSelect(String& str, const String& label, const String& id, int choice)
{
  addRowLabel(str, label);
  addIPaccessControlSelect(str, id, choice);
  TXBuffer.checkFull();
}

void addIPaccessControlSelect(String& str, String name, int choice)
{
  String options[3] = { F("Allow All"), F("Allow Local Subnet"), F("Allow IP range") };
  addSelector(str, name, 3, options, NULL, NULL, choice, false);
  TXBuffer.checkFull();
}




//********************************************************************************
// Web Interface device page
//********************************************************************************
//19480 (11128)
void handle_devices() {
  if (!isLoggedIn()) return;
  navMenuIndex = 4;
  TXBuffer.startStream();
  sendHeadandTail(F("TmplStd"),_HEAD);


  // char tmpString[41];
  struct EventStruct TempEvent;

  // String taskindex = WebServer.arg(F("index"));

  byte taskdevicenumber;
  if (WebServer.hasArg(F("del")))
    taskdevicenumber=0;
  else
    taskdevicenumber = WebServer.arg(F("TDNUM")).toInt();


  unsigned long taskdevicetimer = WebServer.arg(F("TDT")).toInt();
  // String taskdeviceid[CONTROLLER_MAX];
  // String taskdevicepin1 = WebServer.arg(F("taskdevicepin1"));   // "taskdevicepin*" should not be changed because it is uses by plugins and expected to be saved by this code
  // String taskdevicepin2 = WebServer.arg(F("taskdevicepin2"));
  // String taskdevicepin3 = WebServer.arg(F("taskdevicepin3"));
  // String taskdevicepin1pullup = WebServer.arg(F("TDPPU"));
  // String taskdevicepin1inversed = WebServer.arg(F("TDPI"));
  // String taskdevicename = WebServer.arg(F("TDN"));
  // String taskdeviceport = WebServer.arg(F("TDP"));
  // String taskdeviceformula[VARS_PER_TASK];
  // String taskdevicevaluename[VARS_PER_TASK];
  // String taskdevicevaluedecimals[VARS_PER_TASK];
  // String taskdevicesenddata[CONTROLLER_MAX];
  // String taskdeviceglobalsync = WebServer.arg(F("TDGS"));
  // String taskdeviceenabled = WebServer.arg(F("TDE"));

  // for (byte varNr = 0; varNr < VARS_PER_TASK; varNr++)
  // {
  //   char argc[25];
  //   String arg = F("TDF");
  //   arg += varNr + 1;
  //   arg.toCharArray(argc, 25);
  //   taskdeviceformula[varNr] = WebServer.arg(argc);
  //
  //   arg = F("TDVN");
  //   arg += varNr + 1;
  //   arg.toCharArray(argc, 25);
  //   taskdevicevaluename[varNr] = WebServer.arg(argc);
  //
  //   arg = F("TDVD");
  //   arg += varNr + 1;
  //   arg.toCharArray(argc, 25);
  //   taskdevicevaluedecimals[varNr] = WebServer.arg(argc);
  // }

  // for (byte controllerNr = 0; controllerNr < CONTROLLER_MAX; controllerNr++)
  // {
  //   char argc[25];
  //   String arg = F("TDID");
  //   arg += controllerNr + 1;
  //   arg.toCharArray(argc, 25);
  //   taskdeviceid[controllerNr] = WebServer.arg(argc);
  //
  //   arg = F("TDSD");
  //   arg += controllerNr + 1;
  //   arg.toCharArray(argc, 25);
  //   taskdevicesenddata[controllerNr] = WebServer.arg(argc);
  // }

  String edit = WebServer.arg(F("edit"));



  byte page = WebServer.arg(F("page")).toInt();
  if (page == 0)
    page = 1;
  byte setpage = WebServer.arg(F("setpage")).toInt();
  if (setpage > 0)
  {
    if (setpage <= (TASKS_MAX / TASKS_PER_PAGE))
      page = setpage;
    else
      page = TASKS_MAX / TASKS_PER_PAGE;
  }




  byte taskIndex = WebServer.arg(F("index")).toInt();
  boolean taskIndexNotSet = taskIndex == 0;
  --taskIndex;

  byte DeviceIndex = 0;

  if (edit.toInt() != 0  && !taskIndexNotSet) // when form submitted
  {
    if (Settings.TaskDeviceNumber[taskIndex] != taskdevicenumber) // change of device: cleanup old device and reset default settings
    {
      //let the plugin do its cleanup by calling PLUGIN_EXIT with this TaskIndex
      TempEvent.TaskIndex = taskIndex;
      PluginCall(PLUGIN_EXIT, &TempEvent, dummyString);

      taskClear(taskIndex, false); // clear settings, but do not save

      Settings.TaskDeviceNumber[taskIndex] = taskdevicenumber;
      if (taskdevicenumber != 0) // set default values if a new device has been selected
      {
        //NOTE: do not enable task by default. allow user to enter sensible valus first and let him enable it when ready.
        if (ExtraTaskSettings.TaskDeviceValueNames[0][0] == 0) // if field set empty, reload defaults
          PluginCall(PLUGIN_GET_DEVICEVALUENAMES, &TempEvent, dummyString); //the plugin should populate ExtraTaskSettings with its default values.

          ClearCustomTaskSettings(taskIndex);
      }
    }
    else if (taskdevicenumber != 0) //save settings
    {
      Settings.TaskDeviceNumber[taskIndex] = taskdevicenumber;
      DeviceIndex = getDeviceIndex(Settings.TaskDeviceNumber[taskIndex]);

      if (taskdevicetimer > 0)
        Settings.TaskDeviceTimer[taskIndex] = taskdevicetimer;
      else
      {
        if (!Device[DeviceIndex].TimerOptional) // Set default delay, unless it's optional...
          Settings.TaskDeviceTimer[taskIndex] = Settings.Delay;
        else
          Settings.TaskDeviceTimer[taskIndex] = 0;
      }

      Settings.TaskDeviceEnabled[taskIndex] = (WebServer.arg(F("TDE")) == F("on"));
      strcpy(ExtraTaskSettings.TaskDeviceName, WebServer.arg(F("TDN")).c_str());
      Settings.TaskDevicePort[taskIndex] =  WebServer.arg(F("TDP")).toInt();

      for (byte controllerNr = 0; controllerNr < CONTROLLER_MAX; controllerNr++)
      {

        Settings.TaskDeviceID[controllerNr][taskIndex] = WebServer.arg(String(F("TDID")) + (controllerNr + 1)).toInt();
        Settings.TaskDeviceSendData[controllerNr][taskIndex] = (WebServer.arg(String(F("TDSD")) + (controllerNr + 1)) == F("on"));
      }

      if (WebServer.arg(F("taskdevicepin1")).length() != 0)
        Settings.TaskDevicePin1[taskIndex] = WebServer.arg(F("taskdevicepin1")).toInt();

      if (WebServer.arg(F("taskdevicepin2")).length() != 0)
        Settings.TaskDevicePin2[taskIndex] = WebServer.arg(F("taskdevicepin2")).toInt();

      if (WebServer.arg(F("taskdevicepin3")).length() != 0)
        Settings.TaskDevicePin3[taskIndex] = WebServer.arg(F("taskdevicepin3")).toInt();

      if (Device[DeviceIndex].PullUpOption)
        Settings.TaskDevicePin1PullUp[taskIndex] = (WebServer.arg(F("TDPPU")) == F("on"));

      if (Device[DeviceIndex].InverseLogicOption)
        Settings.TaskDevicePin1Inversed[taskIndex] = (WebServer.arg(F("TDPI")) == F("on"));

      for (byte varNr = 0; varNr < Device[DeviceIndex].ValueCount; varNr++)
      {

        strcpy(ExtraTaskSettings.TaskDeviceFormula[varNr], WebServer.arg(String(F("TDF")) + (varNr + 1)).c_str());
        ExtraTaskSettings.TaskDeviceValueDecimals[varNr] = WebServer.arg(String(F("TDVD")) + (varNr + 1)).toInt();
        strcpy(ExtraTaskSettings.TaskDeviceValueNames[varNr], WebServer.arg(String(F("TDVN")) + (varNr + 1)).c_str());

        // taskdeviceformula[varNr].toCharArray(tmpString, 41);
        // strcpy(ExtraTaskSettings.TaskDeviceFormula[varNr], tmpString);
        // ExtraTaskSettings.TaskDeviceValueDecimals[varNr] = taskdevicevaluedecimals[varNr].toInt();
        // taskdevicevaluename[varNr].toCharArray(tmpString, 41);

      }

      // // task value names handling.
      // for (byte varNr = 0; varNr < Device[DeviceIndex].ValueCount; varNr++)
      // {
      //   taskdevicevaluename[varNr].toCharArray(tmpString, 41);
      //   strcpy(ExtraTaskSettings.TaskDeviceValueNames[varNr], tmpString);
      // }

      TempEvent.TaskIndex = taskIndex;
      if (ExtraTaskSettings.TaskDeviceValueNames[0][0] == 0) // if field set empty, reload defaults
        PluginCall(PLUGIN_GET_DEVICEVALUENAMES, &TempEvent, dummyString);

      //allow the plugin to save plugin-specific form settings.
      PluginCall(PLUGIN_WEBFORM_SAVE, &TempEvent, dummyString);

      // notify controllers: CPLUGIN_TASK_CHANGE_NOTIFICATION
      for (byte x=0; x < CONTROLLER_MAX; x++)
        {
          TempEvent.ControllerIndex = x;
          if (Settings.TaskDeviceSendData[TempEvent.ControllerIndex][TempEvent.TaskIndex] &&
            Settings.ControllerEnabled[TempEvent.ControllerIndex] && Settings.Protocol[TempEvent.ControllerIndex])
            {
              TempEvent.ProtocolIndex = getProtocolIndex(Settings.Protocol[TempEvent.ControllerIndex]);
              CPlugin_ptr[TempEvent.ProtocolIndex](CPLUGIN_TASK_CHANGE_NOTIFICATION, &TempEvent, dummyString);
            }
        }
    }
    addHtmlError(  SaveTaskSettings(taskIndex));

    addHtmlError(  SaveSettings());

    if (taskdevicenumber != 0 && Settings.TaskDeviceEnabled[taskIndex])
      PluginCall(PLUGIN_INIT, &TempEvent, dummyString);
  }

  // show all tasks as table
  if (taskIndexNotSet)
  {
    TXBuffer += F("<table border=1px frame='box' rules='all'><TR><TH>");

    if (TASKS_MAX != TASKS_PER_PAGE)
    {
      TXBuffer += F("<a class='button link' href=\"devices?setpage=");
      if (page > 1)
        TXBuffer +=  page - 1;
      else
        TXBuffer +=  page;
      TXBuffer += F("\">&lt;</a>");
      TXBuffer += F("<a class='button link' href=\"devices?setpage=");
      if (page < (TASKS_MAX / TASKS_PER_PAGE))
        TXBuffer +=  page + 1;
      else
        TXBuffer +=  page;
      TXBuffer += F("\">&gt;</a>");
    }

    TXBuffer += F("<TH>Task<TH>Enabled<TH>Device<TH>Name<TH>Port<TH>Ctr (IDX)<TH>GPIO<TH>Values");

    String deviceName;

    for (byte x = (page - 1) * TASKS_PER_PAGE; x < ((page) * TASKS_PER_PAGE); x++)
    {
      TXBuffer += F("<TR><TD>");
      TXBuffer += F("<a class='button link' href=\"devices?index=");
      TXBuffer +=  x + 1;
      TXBuffer += F("&page=");
      TXBuffer +=  page;
      TXBuffer += F("\">Edit</a>");
      TXBuffer += F("<TD>");
      TXBuffer +=  x + 1;
      TXBuffer += F("<TD>");

      if (Settings.TaskDeviceNumber[x] != 0)
      {
        LoadTaskSettings(x);
        DeviceIndex = getDeviceIndex(Settings.TaskDeviceNumber[x]);
        TempEvent.TaskIndex = x;
        addEnabled(TXBuffer.buf,  Settings.TaskDeviceEnabled[x]);

        TXBuffer += F("<TD>");
        TXBuffer +=  getPluginNameFromDeviceIndex(DeviceIndex);
        TXBuffer += F("<TD>");
        TXBuffer +=  ExtraTaskSettings.TaskDeviceName;
        TXBuffer += F("<TD>");

        byte customConfig = false;
        customConfig = PluginCall(PLUGIN_WEBFORM_SHOW_CONFIG, &TempEvent,TXBuffer.buf);
        if (!customConfig)
          if (Device[DeviceIndex].Ports != 0)
            TXBuffer +=  Settings.TaskDevicePort[x];

        TXBuffer += F("<TD>");

        if (Device[DeviceIndex].SendDataOption)
        {
          boolean doBR = false;
          for (byte controllerNr = 0; controllerNr < CONTROLLER_MAX; controllerNr++)
          {
            byte ProtocolIndex = getProtocolIndex(Settings.Protocol[controllerNr]);
            if (Settings.TaskDeviceSendData[controllerNr][x])
            {
              if (doBR)
                TXBuffer += F("<BR>");
              TXBuffer +=  getControllerSymbol(controllerNr);
              if (Protocol[ProtocolIndex].usesID && Settings.Protocol[controllerNr] != 0)
              {
                TXBuffer += F(" (");
                TXBuffer +=  Settings.TaskDeviceID[controllerNr][x];
                TXBuffer += F(")");
                if (Settings.TaskDeviceID[controllerNr][x] == 0)
                  TXBuffer += F(" " HTML_SYMBOL_WARNING);
              }
              doBR = true;
            }
          }
        }

        TXBuffer += F("<TD>");

        if (Settings.TaskDeviceDataFeed[x] == 0)
        {
          if (Device[DeviceIndex].Type == DEVICE_TYPE_I2C)
          {
            TXBuffer += F("GPIO-");
            TXBuffer +=  Settings.Pin_i2c_sda;
            TXBuffer += F("<BR>GPIO-");
            TXBuffer +=  Settings.Pin_i2c_scl;
          }
          if (Device[DeviceIndex].Type == DEVICE_TYPE_ANALOG)
            TXBuffer += F("ADC (TOUT)");

          if (Settings.TaskDevicePin1[x] != -1)
          {
            TXBuffer += F("GPIO-");
            TXBuffer +=  Settings.TaskDevicePin1[x];
          }

          if (Settings.TaskDevicePin2[x] != -1)
          {
            TXBuffer += F("<BR>GPIO-");
            TXBuffer +=  Settings.TaskDevicePin2[x];
          }

          if (Settings.TaskDevicePin3[x] != -1)
          {
            TXBuffer += F("<BR>GPIO-");
            TXBuffer +=  Settings.TaskDevicePin3[x];
          }
        }

        TXBuffer += F("<TD>");
        byte customValues = false;
        customValues = PluginCall(PLUGIN_WEBFORM_SHOW_VALUES, &TempEvent,TXBuffer.buf);
        if (!customValues)
        {
          if (Device[DeviceIndex].VType == SENSOR_TYPE_LONG)
          {
            TXBuffer  += F("<div class=\"div_l\">");
            TXBuffer  += ExtraTaskSettings.TaskDeviceValueNames[0];
            TXBuffer  += F(":</div><div class=\"div_r\">");
            TXBuffer  += (unsigned long)UserVar[x * VARS_PER_TASK] + ((unsigned long)UserVar[x * VARS_PER_TASK + 1] << 16);
            TXBuffer  += F("</div>");
          }
          else
          {
            for (byte varNr = 0; varNr < VARS_PER_TASK; varNr++)
            {
              if ((Settings.TaskDeviceNumber[x] != 0) and (varNr < Device[DeviceIndex].ValueCount))
              {
                if (varNr > 0)
                  TXBuffer += F("<div class=\"div_br\"></div>");
                TXBuffer += F("<div class=\"div_l\">");
                TXBuffer +=  ExtraTaskSettings.TaskDeviceValueNames[varNr];
                TXBuffer += F(":</div><div class=\"div_r\">");
                TXBuffer +=  String(UserVar[x * VARS_PER_TASK + varNr], ExtraTaskSettings.TaskDeviceValueDecimals[varNr]);
                TXBuffer +=  "</div>";
              }
            }
          }
        }
      }
      else
        TXBuffer += F("<TD><TD><TD><TD><TD><TD>");

    } // next
    TXBuffer += F("</table></form>");
  }
  // Show edit form if a specific entry is chosen with the edit button
  else
  {
    LoadTaskSettings(taskIndex);
    DeviceIndex = getDeviceIndex(Settings.TaskDeviceNumber[taskIndex]);
    TempEvent.TaskIndex = taskIndex;

    TXBuffer += F("<form name='frmselect' method='post'><table>");
    addFormHeader(TXBuffer.buf,  F("Task Settings"));


    TXBuffer += F("<TR><TD>Device:<TD>");

    //no device selected
    if (Settings.TaskDeviceNumber[taskIndex] == 0 )
    {
      //takes lots of memory/time so call this only when needed.
      addDeviceSelect(TXBuffer.buf,  "TDNUM", Settings.TaskDeviceNumber[taskIndex]);   //="taskdevicenumber"

    }
    // device selected
    else
    {
      //remember selected device number
      TXBuffer += F("<input type='hidden' name='TDNUM' value='");
      TXBuffer +=  Settings.TaskDeviceNumber[taskIndex];
      TXBuffer += F("'>");

      //show selected device name and delete button
      TXBuffer +=  getPluginNameFromDeviceIndex(DeviceIndex);

      addHelpButton( TXBuffer.buf, String(F("Plugin")) + Settings.TaskDeviceNumber[taskIndex]);

      addFormTextBox(TXBuffer.buf,  F("Name"), F("TDN"), ExtraTaskSettings.TaskDeviceName, 40);   //="taskdevicename"

      addFormCheckBox(TXBuffer.buf,  F("Enabled"), F("TDE"), Settings.TaskDeviceEnabled[taskIndex]);   //="taskdeviceenabled"

      // section: Sensor / Actuator
      if (!Device[DeviceIndex].Custom && Settings.TaskDeviceDataFeed[taskIndex] == 0 &&
          ((Device[DeviceIndex].Ports != 0) || (Device[DeviceIndex].PullUpOption) || (Device[DeviceIndex].InverseLogicOption) || (Device[DeviceIndex].Type >= DEVICE_TYPE_SINGLE && Device[DeviceIndex].Type <= DEVICE_TYPE_TRIPLE)) )
      {
        addFormSubHeader(TXBuffer.buf,  (Device[DeviceIndex].SendDataOption) ? F("Sensor") : F("Actuator"));

        if (Device[DeviceIndex].Ports != 0)
          addFormNumericBox( TXBuffer.buf, F("Port"), F("TDP"), Settings.TaskDevicePort[taskIndex]);   //="taskdeviceport"

        if (Device[DeviceIndex].PullUpOption)
        {
          addFormCheckBox(TXBuffer.buf,  F("Internal PullUp"), F("TDPPU"), Settings.TaskDevicePin1PullUp[taskIndex]);   //="taskdevicepin1pullup"
          if ((Settings.TaskDevicePin1[taskIndex] == 16) || (Settings.TaskDevicePin2[taskIndex] == 16) || (Settings.TaskDevicePin3[taskIndex] == 16))
            addFormNote(TXBuffer.buf,  F("GPIO-16 (D0) does not support PullUp"));
        }

        if (Device[DeviceIndex].InverseLogicOption)
        {
          addFormCheckBox( TXBuffer.buf, F("Inversed Logic"), F("TDPI"), Settings.TaskDevicePin1Inversed[taskIndex]);   //="taskdevicepin1inversed"
          addFormNote( TXBuffer.buf, F("Will go into effect on next input change."));
        }

        //get descriptive GPIO-names from plugin
        TempEvent.String1 = F("1st GPIO");
        TempEvent.String2 = F("2nd GPIO");
        TempEvent.String3 = F("3rd GPIO");
        PluginCall(PLUGIN_GET_DEVICEGPIONAMES, &TempEvent, dummyString);

        if (Device[DeviceIndex].Type >= DEVICE_TYPE_SINGLE && Device[DeviceIndex].Type <= DEVICE_TYPE_TRIPLE)
          addFormPinSelect( TXBuffer.buf, TempEvent.String1, F("taskdevicepin1"), Settings.TaskDevicePin1[taskIndex]);
        if (Device[DeviceIndex].Type >= DEVICE_TYPE_DUAL && Device[DeviceIndex].Type <= DEVICE_TYPE_TRIPLE)
          addFormPinSelect(TXBuffer.buf,  TempEvent.String2, F("taskdevicepin2"), Settings.TaskDevicePin2[taskIndex]);
        if (Device[DeviceIndex].Type == DEVICE_TYPE_TRIPLE)
          addFormPinSelect( TXBuffer.buf, TempEvent.String3, F("taskdevicepin3"), Settings.TaskDevicePin3[taskIndex]);
      }

      //add plugins content
      if (Settings.TaskDeviceDataFeed[taskIndex] == 0) // only show additional config for local connected sensors
        PluginCall(PLUGIN_WEBFORM_LOAD, &TempEvent,TXBuffer.buf);

      //section: Data Acquisition
      if (Device[DeviceIndex].SendDataOption)
      {
        addFormSubHeader( TXBuffer.buf, F("Data Acquisition"));

        for (byte controllerNr = 0; controllerNr < CONTROLLER_MAX; controllerNr++)
        {
          if (Settings.Protocol[controllerNr] != 0)
          {
            String id = F("TDSD");   //="taskdevicesenddata"
            id += controllerNr + 1;

            TXBuffer += F("<TR><TD>Send to Controller ");
            TXBuffer +=  getControllerSymbol(controllerNr);
            TXBuffer += F("<TD>");
            addCheckBox(TXBuffer.buf,  id, Settings.TaskDeviceSendData[controllerNr][taskIndex]);

            byte ProtocolIndex = getProtocolIndex(Settings.Protocol[controllerNr]);
            if (Protocol[ProtocolIndex].usesID && Settings.Protocol[controllerNr] != 0)
            {
              TXBuffer += F(" &nbsp; IDX: ");
              id = F("TDID");   //="taskdeviceid"
              id += controllerNr + 1;
              addNumericBox(TXBuffer.buf,  id, Settings.TaskDeviceID[controllerNr][taskIndex], 0, 9999);
            }
          }
        }
      }

      addFormSeparator (TXBuffer.buf);

      if (Device[DeviceIndex].TimerOption)
      {
        //FIXME: shoudn't the max be ULONG_MAX because Settings.TaskDeviceTimer is an unsigned long? addFormNumericBox only supports ints for min and max specification
        addFormNumericBox(TXBuffer.buf,  F("Delay"), F("TDT"), Settings.TaskDeviceTimer[taskIndex], 0, 65535);   //="taskdevicetimer"
        addUnit(TXBuffer.buf,  F("sec"));
        if (Device[DeviceIndex].TimerOptional)
          TXBuffer += F(" (Optional for this Device)");
      }

      //section: Values
      if (!Device[DeviceIndex].Custom && Device[DeviceIndex].ValueCount > 0)
      {
        addFormSubHeader( TXBuffer.buf, F("Values"));
        TXBuffer += F("</table><table>");

        //table header
        TXBuffer += F("<TR><TH>Value");
        TXBuffer += F("<TH>Name");

        if (Device[DeviceIndex].FormulaOption)
        {
          TXBuffer += F("<TH>Formula");
          addHelpButton( TXBuffer.buf, F("EasyFormula"));
        }

        if (Device[DeviceIndex].FormulaOption || Device[DeviceIndex].DecimalsOnly)
        {
          TXBuffer += F("<TH>Decimals");
        }

        //table body
        for (byte varNr = 0; varNr < Device[DeviceIndex].ValueCount; varNr++)
        {
          TXBuffer += F("<TR><TD>");
          TXBuffer +=  varNr + 1;
          TXBuffer += F("<TD>");
          String id = F("TDVN");   //="taskdevicevaluename"
          id += (varNr + 1);
          addTextBox( TXBuffer.buf, id, ExtraTaskSettings.TaskDeviceValueNames[varNr], 40);

          if (Device[DeviceIndex].FormulaOption)
          {
            TXBuffer += F("<TD>");
            String id = F("TDF");   //="taskdeviceformula"
            id += (varNr + 1);
            addTextBox( TXBuffer.buf, id, ExtraTaskSettings.TaskDeviceFormula[varNr], 40);
          }

          if (Device[DeviceIndex].FormulaOption || Device[DeviceIndex].DecimalsOnly)
          {
            TXBuffer += F("<TD>");
            String id = F("TDVD");   //="taskdevicevaluedecimals"
            id += (varNr + 1);
            addNumericBox(TXBuffer.buf,  id, ExtraTaskSettings.TaskDeviceValueDecimals[varNr], 0, 6);
          }
        }
      }
    }

    addFormSeparator (TXBuffer.buf);

    TXBuffer += F("<TR><TD><TD><a class='button link' href=\"devices?setpage=");
    TXBuffer +=  page;
    TXBuffer += F("\">Close</a>");
    addSubmitButton (TXBuffer.buf);
    TXBuffer += F("<input type='hidden' name='edit' value='1'>");
    TXBuffer += F("<input type='hidden' name='page' value='1'>");

    //if user selected a device, add the delete button
    if (Settings.TaskDeviceNumber[taskIndex] != 0 )
      addSubmitButton( TXBuffer.buf, F("Delete"), F("del"));



    TXBuffer += F("</table></form>");
  }


  checkRAM(F("handle_devices"));
  String log = F("DEBUG: String size:");
  log += String(TXBuffer.sentBytes);
  addLog(LOG_LEVEL_DEBUG_DEV, log);
  sendHeadandTail(F("TmplStd"),_TAIL);
  TXBuffer.endStream();
}


byte sortedIndex[DEVICES_MAX + 1];
//********************************************************************************
// Add a device select dropdown list
//********************************************************************************
void addDeviceSelect(String& str, String name,  int choice)
{
  // first get the list in alphabetic order
  for (byte x = 0; x <= deviceCount; x++)
    sortedIndex[x] = x;
  sortDeviceArray();

  String deviceName;

  addSelector_Head(str, name, true);
  addSelector_Item(str, F("- None -"), 0, false, false, F(""));
  for (byte x = 0; x <= deviceCount; x++)
  {
    byte deviceIndex = sortedIndex[x];
    if (Plugin_id[deviceIndex] != 0)
      deviceName = getPluginNameFromDeviceIndex(deviceIndex);

#ifdef PLUGIN_BUILD_DEV
    int num = deviceIndex + 1;
    String plugin = F("P");
    if (num < 10) plugin += F("0");
    if (num < 100) plugin += F("0");
    plugin += num;
    plugin += F(" - ");
    deviceName = plugin + deviceName;
#endif

    addSelector_Item(str,
                     deviceName,
                     Device[deviceIndex].Number,
                     choice == Device[deviceIndex].Number,
                     false,
                     F(""));
  }
  addSelector_Foot(str);
  TXBuffer.checkFull();

}
//********************************************************************************
// Device Sort routine, switch array entries
//********************************************************************************
void switchArray(byte value)
{
  byte temp;
  temp = sortedIndex[value - 1];
  sortedIndex[value - 1] = sortedIndex[value];
  sortedIndex[value] = temp;
}


//********************************************************************************
// Device Sort routine, compare two array entries
//********************************************************************************
boolean arrayLessThan(const String& ptr_1, const String& ptr_2)
{
  unsigned int i = 0;
  while (i < ptr_1.length())    // For each character in string 1, starting with the first:
  {
    if (ptr_2.length() < i)    // If string 2 is shorter, then switch them
    {
      return true;
    }
    else
    {
      const char check1 = (char)ptr_1[i];  // get the same char from string 1 and string 2
      const char check2 = (char)ptr_2[i];
      if (check1 == check2) {
        // they're equal so far; check the next char !!
        i++;
      } else {
        return (check2 > check1);
      }
    }
  }
  return false;
}


//********************************************************************************
// Device Sort routine, actual sorting
//********************************************************************************
void sortDeviceArray()
{
  int innerLoop ;
  int mainLoop ;
  for ( mainLoop = 1; mainLoop <= deviceCount; mainLoop++)
  {
    innerLoop = mainLoop;
    while (innerLoop  >= 1)
    {
      if (arrayLessThan(
        getPluginNameFromDeviceIndex(sortedIndex[innerLoop]),
        getPluginNameFromDeviceIndex(sortedIndex[innerLoop - 1])))
      {
        switchArray(innerLoop);
      }
      innerLoop--;
    }
  }
}

void addFormPinSelect(String& str, const String& label, const String& id, int choice)
{
  addRowLabel(str, label);
  addPinSelect(false, str, id, choice);
  TXBuffer.checkFull();
}


void addFormPinSelectI2C(String& str, const String& label, const String& id, int choice)
{
  addRowLabel(str, label);
  addPinSelect(true, str, id, choice);
  TXBuffer.checkFull();
}


//********************************************************************************
// Add a GPIO pin select dropdown list for both 8266 and 8285
//********************************************************************************
#if defined(ESP8285)
// Code for the ESP8285

//********************************************************************************
// Add a GPIO pin select dropdown list
//********************************************************************************
void addPinSelect(boolean forI2C, String& str, String name,  int choice)
{
  String options[18];
  options[0] = F("- None -");
  options[1] = F("GPIO-0 (D3)");
  options[2] = F("GPIO-1 (D10)");
  options[3] = F("GPIO-2 (D4)");
  options[4] = F("GPIO-3 (D9)");
  options[5] = F("GPIO-4 (D2)");
  options[6] = F("GPIO-5 (D1)");
  options[7] = F("GPIO-6");
  options[8] = F("GPIO-7");
  options[9] = F("GPIO-8");
  options[10] = F("GPIO-9 (D11)");
  options[11] = F("GPIO-10 (D12)");
  options[12] = F("GPIO-11");
  options[13] = F("GPIO-12 (D6)");
  options[14] = F("GPIO-13 (D7)");
  options[15] = F("GPIO-14 (D5)");
  options[16] = F("GPIO-15 (D8)");
  options[17] = F("GPIO-16 (D0)");
  int optionValues[18];
  optionValues[0] = -1;
  optionValues[1] = 0;
  optionValues[2] = 1;
  optionValues[3] = 2;
  optionValues[4] = 3;
  optionValues[5] = 4;
  optionValues[6] = 5;
  optionValues[7] = 7;
  optionValues[8] = 7;
  optionValues[9] = 8;
  optionValues[10] = 9;
  optionValues[11] = 10;
  optionValues[12] = 11;
  optionValues[13] = 12;
  optionValues[14] = 13;
  optionValues[15] = 14;
  optionValues[16] = 15;
  optionValues[17] = 16;
  renderHTMLForPinSelect(options, optionValues, forI2C, str, name, choice, 18);

}

#else
#if defined(ESP8266)
// Code for the ESP8266

//********************************************************************************
// Add a GPIO pin select dropdown list
//********************************************************************************
void addPinSelect(boolean forI2C, String& str, String name,  int choice)
{
  String options[14];
  options[0] = F("- None -");
  options[1] = F("GPIO-0 (D3)");
  options[2] = F("GPIO-1 (D10)");
  options[3] = F("GPIO-2 (D4)");
  options[4] = F("GPIO-3 (D9)");
  options[5] = F("GPIO-4 (D2)");
  options[6] = F("GPIO-5 (D1)");
  options[7] = F("GPIO-9 (D11) " HTML_SYMBOL_WARNING);
  options[8] = F("GPIO-10 (D12)");
  options[9] = F("GPIO-12 (D6)");
  options[10] = F("GPIO-13 (D7)");
  options[11] = F("GPIO-14 (D5)");
  options[12] = F("GPIO-15 (D8)");
  options[13] = F("GPIO-16 (D0)");
  int optionValues[14];
  optionValues[0] = -1;
  optionValues[1] = 0;
  optionValues[2] = 1;
  optionValues[3] = 2;
  optionValues[4] = 3;
  optionValues[5] = 4;
  optionValues[6] = 5;
  optionValues[7] = 9;
  optionValues[8] = 10;
  optionValues[9] = 12;
  optionValues[10] = 13;
  optionValues[11] = 14;
  optionValues[12] = 15;
  optionValues[13] = 16;
  renderHTMLForPinSelect(options, optionValues, forI2C, str, name, choice, 14);
}
#endif

#if defined(ESP32)
//********************************************************************************
// Add a GPIO pin select dropdown list
//********************************************************************************
void addPinSelect(boolean forI2C, String& str, String name,  int choice)
{
  String options[PIN_D_MAX+1];
  int optionValues[PIN_D_MAX+1];
  options[0] = F("- None -");
  optionValues[0] = -1;
  for(byte x=1; x < PIN_D_MAX+1; x++)
  {
    options[x] = F("GPIO-");
    options[x] += x;
    optionValues[x] = x;
  }
  renderHTMLForPinSelect(options, optionValues, forI2C, str, name, choice, PIN_D_MAX+1);
}
#endif


#endif

//********************************************************************************
// Helper function actually rendering dropdown list for addPinSelect()
//********************************************************************************
void renderHTMLForPinSelect(String options[], int optionValues[], boolean forI2C, String& str, String name,  int choice, int count) {
  addSelector_Head(str, name, false);
  for (byte x = 0; x < count; x++)
  {
    boolean disabled = false;

    if (optionValues[x] != -1) // empty selection can never be disabled...
    {
      if (!forI2C && ((optionValues[x] == Settings.Pin_i2c_sda) || (optionValues[x] == Settings.Pin_i2c_scl)))
        disabled = true;
      if (Settings.UseSerial && ((optionValues[x] == 1) || (optionValues[x] == 3)))
        disabled = true;
    }
    addSelector_Item(str,
                     options[x],
                     optionValues[x],
                     choice == optionValues[x],
                     disabled,
                     F(""));
  }
  addSelector_Foot(str);
  TXBuffer.checkFull();

}


void addFormSelectorI2C(String& str, const String& id, int addressCount, const int addresses[], int selectedIndex)
{
  String options[addressCount];
  for (byte x = 0; x < addressCount; x++)
  {
    options[x] = F("0x");
    options[x] += String(addresses[x], HEX);
    if (x == 0)
      options[x] += F(" - (default)");
  }
  addFormSelector(str, F("I2C Address"), id, addressCount, options, addresses, NULL, selectedIndex, false);
  TXBuffer.checkFull();
}

void addFormSelector(String& str, const String& label, const String& id, int optionCount, const String options[], const int indices[], int selectedIndex)
{
  addFormSelector(str, label, id, optionCount, options, indices, NULL, selectedIndex, false);
}

void addFormSelector(String& str, const String& label, const String& id, int optionCount, const String options[], const int indices[], const String attr[], int selectedIndex, boolean reloadonchange)
{
  addRowLabel(str, label);
  addSelector(str, id, optionCount, options, indices, attr, selectedIndex, reloadonchange);
  TXBuffer.checkFull();
}

void addSelector(String& str, const String& id, int optionCount, const String options[], const int indices[], const String attr[], int selectedIndex, boolean reloadonchange)
{
  int index;

  str += F("<select name='");
  str += id;
  str += F("'");
  if (reloadonchange)
    str += F(" onchange=\"return dept_onchange(frmselect)\"");
  str += F(">");
  for (byte x = 0; x < optionCount; x++)
  {
    if (indices)
      index = indices[x];
    else
      index = x;
    str += F("<option value=");
    str += index;
    if (selectedIndex == index)
      str += F(" selected");
    if (attr)
    {
      str += F(" ");
      str += attr[x];
    }
    str += ">";
    str += options[x];
    str += F("</option>");
    TXBuffer.checkFull();
  }
  str += F("</select>");
}


void addSelector_Head(String& str, const String& id, boolean reloadonchange)
{
  str += F("<select name='");
  str += id;
  str += F("'");
  if (reloadonchange)
    str += F(" onchange=\"return dept_onchange(frmselect)\"");
  str += F(">");
  TXBuffer.checkFull();

}

void addSelector_Item(String& str, const String& option, int index, boolean selected, boolean disabled, const String& attr)
{
  str += F("<option value=");
  str += index;
  if (selected)
    str += F(" selected");
  if (disabled)
    str += F(" disabled");
  if (attr && attr.length() > 0)
  {
    str += F(" ");
    str += attr;
  }
  str += ">";
  str += option;
  str += F("</option>");
  TXBuffer.checkFull();

}


void addSelector_Foot(String& str)
{
  str += F("</select>");
}


void addUnit(String& str, const String& unit)
{
  str += F(" [");
  str += unit;
  str += F("]");
  TXBuffer.checkFull();
}


void addRowLabel(String& str, const String& label)
{
  str += F("<TR><TD>");
  str += label;
  str += F(":<TD>");
  TXBuffer.checkFull();
}

void addButton(String& str, const String &url, const String &label)
{
  str += F("<a class='button link' href='");
  str += url;
  str += F("'>");
  str += label;
  str += F("</a>");
  TXBuffer.checkFull();
}

void addSubmitButton(String& str)
{
  str += F("<input class='button link' type='submit' value='Submit'>");
  TXBuffer.checkFull();
}

//add submit button with different label and name
void addSubmitButton(String& str, const String &value, const String &name)
{
  str += F("<input class='button link' type='submit' value='");
  str += value;
  str += F("' name='");
  str += name;
  str += F("'>");
  TXBuffer.checkFull();
}



//********************************************************************************
// Add a header
//********************************************************************************
void addFormHeader(String& str, const String& header1, const String& header2)
{
   str += F("<TR><TH>");
  str += header1;
  str += F("<TH>");
  str += header2;
  str += F("");
  TXBuffer.checkFull();
}

void addFormHeader(String& str, const String& header)
{
  str += F("<TR><TD colspan='2'><h2>");
  str += header;
  str += F("</h2>");
  TXBuffer.checkFull();

}


//********************************************************************************
// Add a sub header
//********************************************************************************
void addFormSubHeader(String& str, const String& header)
{
  str += F("<TR><TD colspan='2'><h3>");
  str += header;
  str += F("</h3>");
  TXBuffer.checkFull();
}


//********************************************************************************
// Add a note as row start
//********************************************************************************
void addFormNote(String& str, const String& text)
{
  str += F("<TR><TD><TD><div class='note'>Note: ");
  str += text;
  str += F("</div>");
  TXBuffer.checkFull();
}


//********************************************************************************
// Add a separator as row start
//********************************************************************************
void addFormSeparator(String& str)
{
  str += F("<TR><TD colspan='2'><hr>");
  TXBuffer.checkFull();
}


//********************************************************************************
// Add a checkbox
//********************************************************************************
void addCheckBox(String& str, const String& id, boolean checked)
{
  str += F("<input type=checkbox id='");
  str += id;
  str += F("' name='");
  str += id;
  str += F("'");
  if (checked)
    str += F(" checked");
  str += F(">");
  TXBuffer.checkFull();
}

void addFormCheckBox(String& str, const String& label, const String& id, boolean checked)
{
  addRowLabel(str, label);
  addCheckBox(str, id, checked);
  TXBuffer.checkFull();
}


//********************************************************************************
// Add a numeric box
//********************************************************************************
void addNumericBox(String& str, const String& id, int value, int min, int max)
{
  str += F("<input type='number' name='");
  str += id;
  str += F("'");
  if (min != INT_MIN)
  {
    str += F(" min=");
    str += min;
  }
  if (max != INT_MAX)
  {
    str += F(" max=");
    str += max;
  }
  str += F(" style='width:5em;' value=");
  str += value;
  str += F(">");
  TXBuffer.checkFull();
}

void addNumericBox(String& str, const String& id, int value)
{
  addNumericBox(str, id, value, INT_MIN, INT_MAX);
}

void addFormNumericBox(String& str, const String& label, const String& id, int value, int min, int max)
{
  addRowLabel(str,  label);
  addNumericBox(str, id, value, min, max);
}

void addFormNumericBox(String& str, const String& label, const String& id, int value)
{
  addFormNumericBox(str, label, id, value, INT_MIN, INT_MAX);
}



void addTextBox(String& str, const String& id, const String&  value, int maxlength)
{
  str += F("<input type='text' name='");
  str += id;
  str += F("' maxlength=");
  str += maxlength;
  str += F(" value='");
  str += value;
  str += F("'>");
  TXBuffer.checkFull();
}

void addFormTextBox(String& str, const String& label, const String& id, const String&  value, int maxlength)
{
  addRowLabel(str, label);
  addTextBox(str, id, value, maxlength);
}


void addFormPasswordBox(String& str, const String& label, const String& id, const String& password, int maxlength)
{
  addRowLabel(str, label);
  str += F("<input type='password' name='");
  str += id;
  str += F("' maxlength=");
  str += maxlength;
  str += F(" value='");
  if (password != F(""))   //no password?
    str += F("*****");
  //str += password;   //password will not published over HTTP
  str += F("'>");
  TXBuffer.checkFull();
}

void copyFormPassword(const String& id, char* pPassword, int maxlength)
{
  String password = WebServer.arg(id);
  if (password == F("*****"))   //no change?
    return;
  strncpy(pPassword, password.c_str(), maxlength);
}

void addFormIPBox(String& str, const String& label, const String& id, const byte ip[4])
{
  char strip[20];
  if (ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0)
    strip[0] = 0;
  else {
    formatIP(ip, strip);
  }

  addRowLabel(str, label);
  str += F("<input type='text' name='");
  str += id;
  str += F("' value='");
  str += strip;
  str += F("'>");
  TXBuffer.checkFull();
}

// adds a Help Button with points to the the given Wiki Subpage
void addHelpButton(String& str, const String& url)
{
  str += F(" <a class=\"button help\" href=\"http://www.letscontrolit.com/wiki/index.php/");
  str += url;
  str += F("\" target=\"_blank\">&#10068;</a>");
  TXBuffer.checkFull();
}


void addEnabled(String& str, boolean enabled)
{
  if (enabled)
    str += F("<span class='enabled on'>&#10004;</span>");
  else
    str += F("<span class='enabled off'>&#10008;</span>");

  TXBuffer.checkFull();
}


//********************************************************************************
// Add a task select dropdown list
//********************************************************************************
void addTaskSelect(String& str, String name,  int choice)
{
  struct EventStruct TempEvent;
  String deviceName;

  str += F("<select name='");
  str += name;
  str += F("' onchange=\"return dept_onchange(frmselect)\">");

  for (byte x = 0; x < TASKS_MAX; x++)
  {
    deviceName = "";
    if (Settings.TaskDeviceNumber[x] != 0 )
    {
      byte DeviceIndex = getDeviceIndex(Settings.TaskDeviceNumber[x]);

      if (Plugin_id[DeviceIndex] != 0)
        deviceName = getPluginNameFromDeviceIndex(DeviceIndex);
    }
    LoadTaskSettings(x);
    str += F("<option value='");
    str += x;
    str += "'";
    if (choice == x)
      str += " selected";
    if (Settings.TaskDeviceNumber[x] == 0)
      str += " disabled";
    str += ">";
    str += x + 1;
    str += " - ";
    str += deviceName;
    str += " - ";
    str += ExtraTaskSettings.TaskDeviceName;
    str += "</option>";
    TXBuffer.checkFull();

  }
}



bool isFormItemChecked(const String& id)
{
  return WebServer.arg(id) == "on";
}

int getFormItemInt(const String& id)
{
  String val = WebServer.arg(id);
  return val.toInt();
}

float getFormItemFloat(const String& id)
{
  String val = WebServer.arg(id);
  return val.toFloat();
}

bool isFormItem(const String& id)
{
  return (WebServer.arg(id).length() != 0);
}


//********************************************************************************
// Add a Value select dropdown list, based on TaskIndex
//********************************************************************************
void addTaskValueSelect(String& str, String name,  int choice, byte TaskIndex)
{
  str += F("<select name='");
  str += name;
  str += "'>";

  byte DeviceIndex = getDeviceIndex(Settings.TaskDeviceNumber[TaskIndex]);

  for (byte x = 0; x < Device[DeviceIndex].ValueCount; x++)
  {
    str += F("<option value='");
    str += x;
    str += "'";
    if (choice == x)
      str += " selected";
    str += ">";
    str += ExtraTaskSettings.TaskDeviceValueNames[x];
    str += "</option>";
    TXBuffer.checkFull();

  }
}



//********************************************************************************
// Web Interface log page
//********************************************************************************
void handle_log() {
  if (!isLoggedIn()) return;
  navMenuIndex = 7;
  TXBuffer.startStream();
  sendHeadandTail(F("TmplStd"),_HEAD);

  TXBuffer += F("<script>function RefreshMe(){window.location = window.location}setTimeout('RefreshMe()', 3000);</script>");
  TXBuffer += F("<table><TR><TH>Log<TR><TD>");
  for (int i = 0; i< LOG_STRUCT_MESSAGE_LINES; i++){
    Logging.get(TXBuffer.buf, F("<BR>"),i);
    TXBuffer.checkFull();
  }
  //Logging.getAll( TXBuffer.buf, F("<BR>"));
  TXBuffer += F("</table>");
  sendHeadandTail(F("TmplStd"),_TAIL);
  TXBuffer.endStream();
}


//********************************************************************************
// Web Interface debug page
//********************************************************************************
void handle_tools() {
  if (!isLoggedIn()) return;
  navMenuIndex = 7;
  TXBuffer.startStream();
  sendHeadandTail(F("TmplStd"),_HEAD);


  String webrequest = WebServer.arg(F("cmd"));

  TXBuffer += F("<form>");
  TXBuffer += F("<table>");

  addFormHeader(TXBuffer.buf,  F("Tools"));

  addFormSubHeader(TXBuffer.buf,  F("Command"));
    TXBuffer += F("<TR><TD HEIGHT=\"30\">");
    TXBuffer += F("<input type='text' name='cmd' value='");
    TXBuffer +=  webrequest;
    TXBuffer += F("'>");
    addHelpButton( TXBuffer.buf, F("ESPEasy_Command_Reference"));
    TXBuffer += F("<TD>");
    addSubmitButton(TXBuffer.buf );
    TXBuffer += F("<TR><TD>");

    printToWeb = true;
    printWebString = "";

    if (webrequest.length() > 0)
    {
      struct EventStruct TempEvent;
      parseCommandString(&TempEvent, webrequest);
      TempEvent.Source = VALUE_SOURCE_HTTP;
      if (!PluginCall(PLUGIN_WRITE, &TempEvent, webrequest))
        ExecuteCommand(VALUE_SOURCE_HTTP, webrequest.c_str());
    }

    if (printWebString.length() > 0)
    {
      TXBuffer += F("<TR><TD>Command Output<TD><textarea readonly rows='10' cols='60' wrap='on'>");
      TXBuffer +=  printWebString;
      TXBuffer += F("</textarea>");
    }

  addFormSubHeader( TXBuffer.buf, F("System"));

  TXBuffer += F("<TR><TD HEIGHT=\"30\">");
  addButton(TXBuffer.buf,  F("/?cmd=reboot"), F("Reboot"));
  TXBuffer += F("<TD>");
  TXBuffer += F("Reboots ESP");

  TXBuffer += F("<TR><TD HEIGHT=\"30\">");
  addButton(TXBuffer.buf,  F("log"), F("Log"));
  TXBuffer += F("<TD>");
  TXBuffer += F("Open log output");

  TXBuffer += F("<TR><TD HEIGHT=\"30\">");
  addButton(TXBuffer.buf,  F("sysinfo"), F("Info"));
  TXBuffer += F("<TD>");
  TXBuffer += F("Open system info page");

  TXBuffer += F("<TR><TD HEIGHT=\"30\">");
  addButton(TXBuffer.buf,  F("advanced"), F("Advanced"));
  TXBuffer += F("<TD>");
  TXBuffer += F("Open advanced settings");

  TXBuffer += F("<TR><TD HEIGHT=\"30\">");
  addButton(TXBuffer.buf,  F("json"), F("Show JSON"));
  TXBuffer += F("<TD>");
  TXBuffer += F("Open JSON output");

  TXBuffer += F("<TR><TD HEIGHT=\"30\">");
  addButton(TXBuffer.buf,  F("pinstates"), F("Pin state buffer"));
  TXBuffer += F("<TD>");
  TXBuffer += F("Show Pin state buffer");

  addFormSubHeader(TXBuffer.buf,  F("Wifi"));

  TXBuffer += F("<TR><TD HEIGHT=\"30\">");
  addButton(TXBuffer.buf,  F("/?cmd=wificonnect"), F("Connect"));
  TXBuffer += F("<TD>");
  TXBuffer += F("Connects to known Wifi network");

  TXBuffer += F("<TR><TD HEIGHT=\"30\">");
  addButton(TXBuffer.buf,  F("/?cmd=wifidisconnect"), F("Disconnect"));
  TXBuffer += F("<TD>");
  TXBuffer += F("Disconnect from wifi network");

  TXBuffer += F("<TR><TD HEIGHT=\"30\">");
  addButton(TXBuffer.buf,  F("wifiscanner"), F("Scan"));
  TXBuffer += F("<TD>");
  TXBuffer += F("Scan for wifi networks");

  addFormSubHeader(TXBuffer.buf,  F("Interfaces"));

  TXBuffer += F("<TR><TD HEIGHT=\"30\">");
  addButton(TXBuffer.buf,  F("i2cscanner"), F("I2C Scan"));
  TXBuffer += F("<TD>");
  TXBuffer += F("Scan for I2C devices");

  addFormSubHeader( TXBuffer.buf, F("Settings"));

  TXBuffer += F("<TR><TD HEIGHT=\"30\">");
  addButton(TXBuffer.buf,  F("upload"), F("Load"));
  TXBuffer += F("<TD>");
  TXBuffer += F("Loads a settings file");
  addFormNote( TXBuffer.buf, F("(File MUST be renamed to \"config.dat\" before upload!)"));

  TXBuffer += F("<TR><TD HEIGHT=\"30\">");
  addButton(TXBuffer.buf,  F("download"), F("Save"));
  TXBuffer += F("<TD>");
  TXBuffer += F("Saves a settings file");

  TXBuffer += F("<TR><TD HEIGHT=\"30\">");
  addButton(TXBuffer.buf,  F("/?cmd=reset"), F("Factory Reset"));
  TXBuffer += F("<TD>");
  TXBuffer += F("Erase all settings files");

#if defined(ESP8266)
  if (ESP.getFlashChipRealSize() > 524288)
  {
    addFormSubHeader(TXBuffer.buf,  F("Firmware"));
    TXBuffer += F("<TR><TD HEIGHT=\"30\">");
    addButton(TXBuffer.buf,  F("update"), F("Load"));
    addHelpButton(TXBuffer.buf,  F("EasyOTA"));
    TXBuffer += F("<TD>");
    TXBuffer += F("Load a new firmware");
  }
#endif

  addFormSubHeader( TXBuffer.buf, F("Filesystem"));

  TXBuffer += F("<TR><TD HEIGHT=\"30\">");
  addButton(TXBuffer.buf,  F("filelist"), F("Flash"));
  TXBuffer += F("<TD>");
  TXBuffer += F("Show files on internal flash");

#ifdef FEATURE_SD
  TXBuffer += F("<TR><TD HEIGHT=\"30\">");
  addButton(TXBuffer.buf,  F("SDfilelist"), F("SD Card"));
  TXBuffer += F("<TD>");
  TXBuffer += F("Show files on SD-Card");
#endif

  TXBuffer += F("</table></form>");
  sendHeadandTail(F("TmplStd"),_TAIL);
  TXBuffer.endStream();
  printWebString = "";
  printToWeb = false;
}


//********************************************************************************
// Web Interface pin state list
//********************************************************************************
void handle_pinstates() {
  if (!isLoggedIn()) return;
  navMenuIndex = 7;
  TXBuffer.startStream();
  sendHeadandTail(F("TmplStd"),_HEAD);





  //addFormSubHeader(  F("Pin state table<TR>"));

  TXBuffer += F("<table border=1px frame='box' rules='all'><TH>Plugin");
  addHelpButton(TXBuffer.buf,  F("Official_plugin_list"));
  TXBuffer += F("<TH>GPIO<TH>Mode<TH>Value/State");
  for (byte x = 0; x < PINSTATE_TABLE_MAX; x++)
    if (pinStates[x].plugin != 0)
    {
      TXBuffer += F("<TR><TD>P");
      if (pinStates[x].plugin < 100)
      {
        TXBuffer += F("0");
      }
      if (pinStates[x].plugin < 10)
      {
        TXBuffer += F("0");
      }
      TXBuffer +=  pinStates[x].plugin;
      TXBuffer += F("<TD>");
      TXBuffer +=  pinStates[x].index;
      TXBuffer += F("<TD>");
      byte mode = pinStates[x].mode;
      switch (mode)
      {
        case PIN_MODE_UNDEFINED:
          TXBuffer += F("undefined");
          break;
        case PIN_MODE_INPUT:
          TXBuffer += F("input");
          break;
        case PIN_MODE_OUTPUT:
          TXBuffer += F("output");
          break;
        case PIN_MODE_PWM:
          TXBuffer += F("PWM");
          break;
        case PIN_MODE_SERVO:
          TXBuffer += F("servo");
          break;
      }
      TXBuffer += F("<TD>");
      TXBuffer +=  pinStates[x].value;
    }

  TXBuffer += F("</table>");
    sendHeadandTail(F("TmplStd"),_TAIL);
    TXBuffer.endStream();
}


//********************************************************************************
// Web Interface I2C scanner
//********************************************************************************
void handle_i2cscanner() {
  if (!isLoggedIn()) return;
  navMenuIndex = 7;
  TXBuffer.startStream();
  sendHeadandTail(F("TmplStd"),_HEAD);

  char *TempString = (char*)malloc(80);



  TXBuffer += F("<table border=1px frame='box' rules='all'><TH>I2C Addresses in use<TH>Supported devices");

  byte error, address;
  int nDevices;
  nDevices = 0;
  for (address = 1; address <= 127; address++ )
  {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();
    if (error == 0)
    {
      TXBuffer +=  "<TR><TD>0x";
      TXBuffer +=  String(address, HEX);
      TXBuffer +=  "<TD>";
      switch (address)
      {
        case 0x20:
        case 0x21:
        case 0x22:
        case 0x25:
        case 0x26:
        case 0x27:
          TXBuffer += F("PCF8574<BR>MCP23017<BR>LCD");
          break;
        case 0x23:
          TXBuffer += F("PCF8574<BR>MCP23017<BR>LCD<BR>BH1750");
          break;
        case 0x24:
          TXBuffer += F("PCF8574<BR>MCP23017<BR>LCD<BR>PN532");
          break;
        case 0x29:
          TXBuffer += F("TSL2561");
          break;
        case 0x38:
        case 0x3A:
        case 0x3B:
        case 0x3E:
        case 0x3F:
          TXBuffer += F("PCF8574A");
          break;
        case 0x39:
          TXBuffer += F("PCF8574A<BR>TSL2561<BR>APDS9960");
          break;
        case 0x3C:
        case 0x3D:
          TXBuffer += F("PCF8574A<BR>OLED");
          break;
        case 0x40:
          TXBuffer += F("SI7021<BR>HTU21D<BR>INA219<BR>PCA9685");
          break;
        case 0x41:
        case 0x42:
        case 0x43:
          TXBuffer += F("INA219");
          break;
        case 0x44:
        case 0x45:
          TXBuffer += F("SHT30/31/35");
          break;
        case 0x48:
        case 0x4A:
        case 0x4B:
          TXBuffer += F("PCF8591<BR>ADS1115<BR>LM75A");
          break;
        case 0x49:
          TXBuffer += F("PCF8591<BR>ADS1115<BR>TSL2561<BR>LM75A");
          break;
        case 0x4C:
        case 0x4E:
        case 0x4F:
          TXBuffer += F("PCF8591<BR>LM75A");
          break;
        case 0x4D:
          TXBuffer += F("PCF8591<BR>MCP3221<BR>LM75A");
          break;
        case 0x5A:
          TXBuffer += F("MLX90614<BR>MPR121");
          break;
        case 0x5B:
          TXBuffer += F("MPR121");
          break;
        case 0x5C:
          TXBuffer += F("DHT12<BR>AM2320<BR>BH1750<BR>MPR121");
          break;
        case 0x5D:
          TXBuffer += F("MPR121");
          break;
        case 0x60:
          TXBuffer += F("Adafruit Motorshield v2<BR>SI1145");
          break;
        case 0x70:
          TXBuffer += F("Adafruit Motorshield v2 (Catchall)<BR>HT16K33");
          break;
        case 0x71:
        case 0x72:
        case 0x73:
        case 0x74:
        case 0x75:
          TXBuffer += F("HT16K33");
          break;
        case 0x76:
          TXBuffer += F("BME280<BR>BMP280<BR>MS5607<BR>MS5611<BR>HT16K33");
          break;
        case 0x77:
          TXBuffer += F("BMP085<BR>BMP180<BR>BME280<BR>BMP280<BR>MS5607<BR>MS5611<BR>HT16K33");
          break;
        case 0x7f:
          TXBuffer += F("Arduino PME");
          break;
      }
      nDevices++;
    }
    else if (error == 4)
    {
      TXBuffer += F("<TR><TD>Unknown error at address 0x");
      TXBuffer +=  String(address, HEX);
    }
  }

  if (nDevices == 0)
    TXBuffer += F("<TR>No I2C devices found");

  TXBuffer += F("</table>");
  sendHeadandTail(F("TmplStd"),_TAIL);
  TXBuffer.endStream();
  free(TempString);
}


//********************************************************************************
// Web Interface Wifi scanner
//********************************************************************************
void handle_wifiscanner() {
  if (!isLoggedIn()) return;
  navMenuIndex = 7;
  TXBuffer.startStream();
  sendHeadandTail(F("TmplStd"),_HEAD);

   char *TempString = (char*)malloc(80);



  TXBuffer += F("<table><TR><TH>Access Points:<TH>RSSI");

  int n = WiFi.scanNetworks();
  if (n == 0)
    TXBuffer += F("No Access Points found");
  else
  {
    for (int i = 0; i < n; ++i)
    {
      TXBuffer += F("<TR><TD>");
      TXBuffer +=  WiFi.SSID(i);
      TXBuffer +=  "<TD>";
      TXBuffer +=  WiFi.RSSI(i);
    }
  }


  TXBuffer += F("</table>");
  sendHeadandTail(F("TmplStd"),_TAIL);
  TXBuffer.endStream();
  free(TempString);
}


//********************************************************************************
// Web Interface login page
//********************************************************************************
void handle_login() {
  TXBuffer.startStream();
  sendHeadandTail(F("TmplStd"),_HEAD);

  String webrequest = WebServer.arg(F("password"));
  char command[80];
  command[0] = 0;
  webrequest.toCharArray(command, 80);


  TXBuffer += F("<form method='post'>");
  TXBuffer += F("<table><TR><TD>Password<TD>");
  TXBuffer += F("<input type='password' name='password' value='");
  TXBuffer +=  webrequest;
  TXBuffer += F("'><TR><TD><TD>");
  addSubmitButton (TXBuffer.buf);
  TXBuffer += F("<TR><TD>");
  TXBuffer += F("</table></form>");

  if (webrequest.length() != 0)
  {
    // compare with stored password and set timer if there's a match
    if ((strcasecmp(command, SecuritySettings.Password) == 0) || (SecuritySettings.Password[0] == 0))
    {
      WebLoggedIn = true;
      WebLoggedInTimer = 0;
      TXBuffer = F("<script>window.location = '.'</script>");
    }
    else
    {
      TXBuffer += F("Invalid password!");
      if (Settings.UseRules)
      {
        String event = F("Login#Failed");
        rulesProcessing(event);
      }
    }
  }

  sendHeadandTail(F("TmplStd"),_TAIL);
  TXBuffer.endStream();
  printWebString = "";
  printToWeb = false;
}


//********************************************************************************
// Web Interface control page (no password!)
//********************************************************************************
void handle_control() {
  //TXBuffer.startStream(true); // true= json
  // sendHeadandTail(F("TmplStd"),_HEAD);
  String webrequest = WebServer.arg(F("cmd"));

  // in case of event, store to buffer and return...
  String command = parseString(webrequest, 1);
  if (command == F("event"))
  {
    eventBuffer = webrequest.substring(6);
    WebServer.send(200, "text/html", "OK");
    return;
  }

  struct EventStruct TempEvent;
  parseCommandString(&TempEvent, webrequest);
  TempEvent.Source = VALUE_SOURCE_HTTP;

  printToWeb = true;
  printWebString = "";

  if (printToWebJSON)
    TXBuffer.startJsonStream();
  else
    TXBuffer.startStream();

  if (PluginCall(PLUGIN_WRITE, &TempEvent, webrequest));
  else if (remoteConfig(&TempEvent, webrequest));
  else
    TXBuffer += F("Unknown or restricted command!");

  TXBuffer += printWebString;
  TXBuffer.endStream();

  printWebString = "";
  printToWeb = false;
  printToWebJSON = false;
}


//********************************************************************************
// Web Interface JSON page (no password!)
//********************************************************************************
void handle_json()
{
  String tasknr = WebServer.arg("tasknr");
  String reply;
  if (tasknr.length() == 0)
  {
    reply += F("{\"System\":{\n");
    reply += to_json_object_value(F("Build"), String(BUILD));
    reply += F(",\n");
    reply += to_json_object_value(F("Git Build"), String(BUILD_GIT));
    reply += F(",\n");
    reply += to_json_object_value(F("Local time"), getDateTimeString('-',':',' '));
    reply += F(",\n");
    reply += to_json_object_value(F("Unit"), String(Settings.Unit));
    reply += F(",\n");
    reply += to_json_object_value(F("Name"), String(Settings.Name));
    reply += F(",\n");
    reply += to_json_object_value(F("Uptime"), String(wdcounter / 2));
    reply += F(",\n");
    reply += to_json_object_value(F("Free RAM"), String(ESP.getFreeHeap()));
    reply += F("\n},\n");

    reply += F("\"WiFi\":{\n");
    reply += to_json_object_value(F("Hostname"), WiFi.hostname());
    reply += F(",\n");
    reply += to_json_object_value(F("IP"), WiFi.localIP().toString());
    reply += F(",\n");
    reply += to_json_object_value(F("Subnet Mask"), WiFi.subnetMask().toString());
    reply += F(",\n");
    reply += to_json_object_value(F("Gateway IP"), WiFi.gatewayIP().toString());
    reply += F(",\n");
    reply += to_json_object_value(F("MAC address"), WiFi.macAddress());
    reply += F(",\n");
    reply += to_json_object_value(F("DNS 1"), WiFi.dnsIP(0).toString());
    reply += F(",\n");
    reply += to_json_object_value(F("DNS 2"), WiFi.dnsIP(1).toString());
    reply += F(",\n");
    reply += to_json_object_value(F("SSID"), WiFi.SSID());
    reply += F(",\n");
    reply += to_json_object_value(F("BSSID"), WiFi.BSSIDstr());
    reply += F(",\n");
    reply += to_json_object_value(F("RSSI"), String(WiFi.RSSI()));
    reply += F("\n},\n");
  }

  byte taskNr = tasknr.toInt();
  byte firstTaskIndex = 0;
  byte lastTaskIndex = TASKS_MAX - 1;
  if (taskNr != 0 )
  {
    firstTaskIndex = taskNr - 1;
    lastTaskIndex = taskNr - 1;
  }
   byte lastActiveTaskIndex = 0;
  for (byte TaskIndex = firstTaskIndex; TaskIndex <= lastTaskIndex; TaskIndex++)
    if (Settings.TaskDeviceNumber[TaskIndex])
      lastActiveTaskIndex = TaskIndex;

  if (taskNr == 0 )
    reply += F("\"Sensors\":[\n");
  for (byte TaskIndex = firstTaskIndex; TaskIndex <= lastTaskIndex; TaskIndex++)
  {
    if (Settings.TaskDeviceNumber[TaskIndex])
    {
      byte BaseVarIndex = TaskIndex * VARS_PER_TASK;
      byte DeviceIndex = getDeviceIndex(Settings.TaskDeviceNumber[TaskIndex]);
      LoadTaskSettings(TaskIndex);
      reply += F("{\n");

      reply += to_json_object_value(F("tasknr"), String(TaskIndex + 1));
      reply += F(",\n");
      reply += to_json_object_value(F("TaskName"), String(ExtraTaskSettings.TaskDeviceName));
      reply += F(",\n");
      reply += to_json_object_value(F("Type"), getPluginNameFromDeviceIndex(DeviceIndex));
      if (Device[DeviceIndex].ValueCount != 0)
        reply += F(",");
      reply += F("\n");

      for (byte x = 0; x < Device[DeviceIndex].ValueCount; x++)
      {
        reply += to_json_object_value(ExtraTaskSettings.TaskDeviceValueNames[x],
                             toString(UserVar[BaseVarIndex + x], ExtraTaskSettings.TaskDeviceValueDecimals[x]));
        if (x < (Device[DeviceIndex].ValueCount - 1))
          reply += F(",");
        reply += F("\n");
      }
      reply += F("}");
      if (TaskIndex != lastActiveTaskIndex)
        reply += F(",");
      reply += F("\n");
    }
  }
  if (taskNr == 0 )
    reply += F("]}\n");

  WebServer.send(200, "application/json", reply);
}

//********************************************************************************
// Web Interface config page
//********************************************************************************
void handle_advanced() {
  if (!isLoggedIn()) return;
  navMenuIndex = 7;
  TXBuffer.startStream();
  sendHeadandTail(F("TmplStd"));

  char tmpString[81];

  String messagedelay = WebServer.arg(F("messagedelay"));
  String ip = WebServer.arg(F("ip"));
  String syslogip = WebServer.arg(F("syslogip"));
  String ntphost = WebServer.arg(F("ntphost"));
  int timezone = WebServer.arg(F("timezone")).toInt();
  int dststartweek = WebServer.arg(F("dststartweek")).toInt();
  int dststartdow = WebServer.arg(F("dststartdow")).toInt();
  int dststartmonth = WebServer.arg(F("dststartmonth")).toInt();
  int dststarthour = WebServer.arg(F("dststarthour")).toInt();
  int dstendweek = WebServer.arg(F("dstendweek")).toInt();
  int dstenddow = WebServer.arg(F("dstenddow")).toInt();
  int dstendmonth = WebServer.arg(F("dstendmonth")).toInt();
  int dstendhour = WebServer.arg(F("dstendhour")).toInt();
  String dst = WebServer.arg(F("dst"));
  String sysloglevel = WebServer.arg(F("sysloglevel"));
  String udpport = WebServer.arg(F("udpport"));
  String useserial = WebServer.arg(F("useserial"));
  String serialloglevel = WebServer.arg(F("serialloglevel"));
  String webloglevel = WebServer.arg(F("webloglevel"));
  String sdloglevel = WebServer.arg(F("sdloglevel"));
  String baudrate = WebServer.arg(F("baudrate"));
  String usentp = WebServer.arg(F("usentp"));
  String wdi2caddress = WebServer.arg(F("wdi2caddress"));
  String usessdp = WebServer.arg(F("usessdp"));
  String edit = WebServer.arg(F("edit"));
  String wireclockstretchlimit = WebServer.arg(F("wireclockstretchlimit"));
  String userules = WebServer.arg(F("userules"));
  String cft = WebServer.arg(F("cft"));
  String MQTTRetainFlag = WebServer.arg(F("mqttretainflag"));


  if (edit.length() != 0)
  {
    Settings.MessageDelay = messagedelay.toInt();
    Settings.IP_Octet = ip.toInt();
    ntphost.toCharArray(tmpString, 64);
    strcpy(Settings.NTPHost, tmpString);
    Settings.TimeZone = timezone;
    TimeChangeRule dst_start(dststartweek, dststartdow, dststartmonth, dststarthour, timezone);
    if (dst_start.isValid()) { Settings.DST_Start = dst_start.toFlashStoredValue(); }
    TimeChangeRule dst_end(dstendweek, dstenddow, dstendmonth, dstendhour, timezone);
    if (dst_end.isValid()) { Settings.DST_End = dst_end.toFlashStoredValue(); }
    str2ip(syslogip.c_str(), Settings.Syslog_IP);
    Settings.UDPPort = udpport.toInt();
    Settings.SyslogLevel = sysloglevel.toInt();
    Settings.UseSerial = (useserial == "on");
    Settings.SerialLogLevel = serialloglevel.toInt();
    Settings.WebLogLevel = webloglevel.toInt();
    Settings.SDLogLevel = sdloglevel.toInt();
    Settings.UseValueLogger = isFormItemChecked(F("valuelogger"));
    Settings.BaudRate = baudrate.toInt();
    Settings.UseNTP = (usentp == "on");
    Settings.DST = (dst == "on");
    Settings.WDI2CAddress = wdi2caddress.toInt();
    Settings.UseSSDP = (usessdp == "on");
    Settings.WireClockStretchLimit = wireclockstretchlimit.toInt();
    Settings.UseRules = (userules == "on");
    Settings.ConnectionFailuresThreshold = cft.toInt();
    Settings.MQTTRetainFlag = (MQTTRetainFlag == "on");

    addHtmlError( TXBuffer.buf, SaveSettings());
    if (Settings.UseNTP)
      initTime();
  }

  // char str[20];

  TXBuffer += F("<form  method='post'><table>");

  addFormHeader(TXBuffer.buf,  F("Advanced Settings"));

  addFormCheckBox(TXBuffer.buf,  F("Rules"), F("userules"), Settings.UseRules);

  addFormSubHeader(TXBuffer.buf,  F("Controller Settings"));

  addFormCheckBox(TXBuffer.buf,  F("MQTT Retain Msg"), F("mqttretainflag"), Settings.MQTTRetainFlag);
  addFormNumericBox(TXBuffer.buf,  F("Message Delay"), F("messagedelay"), Settings.MessageDelay, 0, INT_MAX);
  addUnit(TXBuffer.buf,  F("ms"));

  addFormSubHeader(TXBuffer.buf, F("NTP Settings"));

  addFormCheckBox(TXBuffer.buf,  F("Use NTP"), F("usentp"), Settings.UseNTP);
  addFormTextBox(TXBuffer.buf,  F("NTP Hostname"), F("ntphost"), Settings.NTPHost, 63);

  addFormSubHeader(TXBuffer.buf,  F("DST Settings"));
  addFormDstSelect(TXBuffer.buf,  true, Settings.DST_Start);
  addFormDstSelect( TXBuffer.buf, false, Settings.DST_End);
  addFormNumericBox( TXBuffer.buf, F("Timezone Offset (UTC +)"), F("timezone"), Settings.TimeZone, -720, 840);   // UTC-12H ... UTC+14h
  addUnit( TXBuffer.buf, F("minutes"));
  addFormCheckBox( TXBuffer.buf, F("DST"), F("dst"), Settings.DST);

  addFormSubHeader(TXBuffer.buf,  F("Log Settings"));

  addFormIPBox( TXBuffer.buf, F("Syslog IP"), F("syslogip"), Settings.Syslog_IP);
  addFormLogLevelSelect( TXBuffer.buf, F("Syslog Level"),      F("sysloglevel"),    Settings.SyslogLevel);
  addFormLogLevelSelect(TXBuffer.buf,  F("Serial log Level"),  F("serialloglevel"), Settings.SerialLogLevel);
  addFormLogLevelSelect(TXBuffer.buf,  F("Web log Level"),     F("webloglevel"),    Settings.WebLogLevel);

#ifdef FEATURE_SD
  addFormLogLevelSelect(TXBuffer.buf,  F("SD Card log Level"), F("sdloglevel"),     Settings.SDLogLevel);

  addFormCheckBox(TXBuffer.buf,  F("SD Card Value Logger"), F("valuelogger"), Settings.UseValueLogger);
#endif


  addFormSubHeader(TXBuffer.buf,  F("Serial Settings"));

  addFormCheckBox(TXBuffer.buf,  F("Enable Serial port"), F("useserial"), Settings.UseSerial);
  addFormNumericBox(TXBuffer.buf,  F("Baud Rate"), F("baudrate"), Settings.BaudRate, 0, 1000000);


  addFormSubHeader(TXBuffer.buf,  F("Inter-ESPEasy Network"));

  addFormNumericBox(TXBuffer.buf,  F("UDP port"), F("udpport"), Settings.UDPPort, 0, 65535);


  //TODO sort settings in groups or move to other pages/groups
  addFormSubHeader(TXBuffer.buf,  F("Special and Experimental Settings"));

  addFormNumericBox(TXBuffer.buf,  F("Fixed IP Octet"), F("ip"), Settings.IP_Octet, 0, 255);

  addFormNumericBox(TXBuffer.buf,  F("WD I2C Address"), F("wdi2caddress"), Settings.WDI2CAddress, 0, 127);
  TXBuffer += F(" (decimal)");

  addFormCheckBox(TXBuffer.buf,  F("Use SSDP"), F("usessdp"), Settings.UseSSDP);

  addFormNumericBox(TXBuffer.buf,  F("Connection Failure Threshold"), F("cft"), Settings.ConnectionFailuresThreshold, 0, 100);

  addFormNumericBox(TXBuffer.buf,  F("I2C ClockStretchLimit"), F("wireclockstretchlimit"), Settings.WireClockStretchLimit);   //TODO define limits

  addFormSeparator (TXBuffer.buf);

  TXBuffer += F("<TR><TD><TD>");
  addSubmitButton (TXBuffer.buf);
  TXBuffer += F("<input type='hidden' name='edit' value='1'>");
  TXBuffer += F("</table></form>");
    sendHeadandTail(F("TmplStd"),true);
    TXBuffer.endStream();
}

void addFormDstSelect(String& str, bool isStart, uint16_t choice) {
  String weekid  = isStart ? F("dststartweek")  : F("dstendweek");
  String dowid   = isStart ? F("dststartdow")   : F("dstenddow");
  String monthid = isStart ? F("dststartmonth") : F("dstendmonth");
  String hourid  = isStart ? F("dststarthour")  : F("dstendhour");

  String weeklabel  = isStart ? F("Start (week, dow, month)")  : F("End (week, dow, month)");
  String hourlabel  = isStart ? F("Start (localtime, e.g. 2h&rarr;3h)")  : F("End (localtime, e.g. 3h&rarr;2h)");

  String week[5] = {F("Last"), F("1st"), F("2nd"), F("3rd"), F("4th")};
  int weekValues[5] = {0, 1, 2, 3, 4};
  String dow[7] = {F("Sun"), F("Mon"), F("Tue"), F("Wed"), F("Thu"), F("Fri"), F("Sat")};
  int dowValues[7] = {1, 2, 3, 4, 5, 6, 7};
  String month[12] = {F("Jan"), F("Feb"), F("Mar"), F("Apr"), F("May"), F("Jun"), F("Jul"), F("Aug"), F("Sep"), F("Oct"), F("Nov"), F("Dec")};
  int monthValues[12] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};

  uint16_t tmpstart(choice);
  uint16_t tmpend(choice);
  if (!TimeChangeRule(choice, 0).isValid()) {
    getDefaultDst_flash_values(tmpstart, tmpend);
  }
  TimeChangeRule rule(isStart ? tmpstart : tmpend, 0);
  addRowLabel(str, weeklabel);
  addSelector(str, weekid, 5, week, weekValues, NULL, rule.week, false);
  addSelector(str,  dowid, 7, dow, dowValues, NULL, rule.dow, false);
  addSelector(str,  monthid, 12, month, monthValues, NULL, rule.month, false);

  addFormNumericBox( TXBuffer.buf, hourlabel, hourid, rule.hour, 0, 23);
  addUnit(str, isStart ? F("hour &#x21b7;") : F("hour &#x21b6;"));
}

void addFormLogLevelSelect(String& str, const String& label, const String& id, int choice)
{
  addRowLabel(str, label);
  addLogLevelSelect(str, id, choice);
}

void addLogLevelSelect(String& str, String name, int choice)
{
  String options[6] = { F("None"), F("Error"), F("Info"), F("Debug"), F("Debug More"), F("Debug dev")};
  int optionValues[6] = { 0 , LOG_LEVEL_ERROR, LOG_LEVEL_INFO, LOG_LEVEL_DEBUG, LOG_LEVEL_DEBUG_MORE, LOG_LEVEL_DEBUG_DEV};
  addSelector(str, name, 6, options, optionValues, NULL, choice, false);
}


//********************************************************************************
// Login state check
//********************************************************************************
boolean isLoggedIn()
{
  if (SecuritySettings.Password[0] == 0)
    WebLoggedIn = true;

  if (!WebLoggedIn)
  {
    WebServer.sendContent(F("HTTP/1.1 302 \r\nLocation: /login\r\n"));
  }
  else
  {
    WebLoggedInTimer = 0;
  }

  return WebLoggedIn;
}


//********************************************************************************
// Web Interface download page
//********************************************************************************
void handle_download()
{
  if (!isLoggedIn()) return;
  navMenuIndex = 7;
//  TXBuffer.startStream();
//  sendHeadandTail(F("TmplStd"));


  fs::File dataFile = SPIFFS.open(F(FILE_CONFIG), "r");
  if (!dataFile)
    return;

  String str = F("attachment; filename=config_");
  str += Settings.Name;
  str += "_U";
  str += Settings.Unit;
  str += F("_Build");
  str += BUILD;
  str += F("_");
  if (Settings.UseNTP)
  {
    str += getDateTimeString('\0', '\0', '\0');
  }
  str += (".dat");

  WebServer.sendHeader("Content-Disposition", str);
  WebServer.streamFile(dataFile, "application/octet-stream");
}


//********************************************************************************
// Web Interface upload page
//********************************************************************************
byte uploadResult = 0;
void handle_upload() {
  if (!isLoggedIn()) return;
  navMenuIndex = 7;
  TXBuffer.startStream();
  sendHeadandTail(F("TmplStd"));

  TXBuffer += F("<form enctype=\"multipart/form-data\" method=\"post\"><p>Upload settings file:<br><input type=\"file\" name=\"datafile\" size=\"40\"></p><div><input class='button link' type='submit' value='Upload'></div><input type='hidden' name='edit' value='1'></form>");
     sendHeadandTail(F("TmplStd"),true);
    TXBuffer.endStream();
  printWebString = "";
  printToWeb = false;
}


//********************************************************************************
// Web Interface upload page
//********************************************************************************
void handle_upload_post() {
  if (!isLoggedIn()) return;

  navMenuIndex = 7;
  TXBuffer.startStream();
  sendHeadandTail(F("TmplStd"));



  if (uploadResult == 1)
  {
    TXBuffer += F("Upload OK!<BR>You may need to reboot to apply all settings...");
    LoadSettings();
  }

  if (uploadResult == 2)
    TXBuffer += F("<font color=\"red\">Upload file invalid!</font>");

  if (uploadResult == 3)
    TXBuffer += F("<font color=\"red\">No filename!</font>");


  TXBuffer += F("Upload finished");
  sendHeadandTail(F("TmplStd"),true);
  TXBuffer.endStream();
  printWebString = "";
  printToWeb = false;
}


//********************************************************************************
// Web Interface upload handler
//********************************************************************************
fs::File uploadFile;
void handleFileUpload() {
  if (!isLoggedIn()) return;

  static boolean valid = false;
  String log = "";

  HTTPUpload& upload = WebServer.upload();

  if (upload.filename.c_str()[0] == 0)
  {
    uploadResult = 3;
    return;
  }

  if (upload.status == UPLOAD_FILE_START)
  {
    log = F("Upload: START, filename: ");
    log += upload.filename;
    addLog(LOG_LEVEL_INFO, log);
    valid = false;
    uploadResult = 0;
  }
  else if (upload.status == UPLOAD_FILE_WRITE)
  {
    // first data block, if this is the config file, check PID/Version
    if (upload.totalSize == 0)
    {
      if (strcasecmp(upload.filename.c_str(), FILE_CONFIG) == 0)
      {
        struct TempStruct {
          unsigned long PID;
          int Version;
        } Temp;
        for (unsigned int x = 0; x < sizeof(struct TempStruct); x++)
        {
          byte b = upload.buf[x];
          memcpy((byte*)&Temp + x, &b, 1);
        }
        if (Temp.Version == VERSION && Temp.PID == ESP_PROJECT_PID)
          valid = true;
      }
      else
      {
        // other files are always valid...
        valid = true;
      }
      if (valid)
      {
        // once we're safe, remove file and create empty one...
        SPIFFS.remove((char *)upload.filename.c_str());
        uploadFile = SPIFFS.open(upload.filename.c_str(), "w");
        // dont count manual uploads: flashCount();
      }
    }
    if (uploadFile) uploadFile.write(upload.buf, upload.currentSize);
    log = F("Upload: WRITE, Bytes: ");
    log += upload.currentSize;
    addLog(LOG_LEVEL_INFO, log);
  }
  else if (upload.status == UPLOAD_FILE_END)
  {
    if (uploadFile) uploadFile.close();
    log = F("Upload: END, Size: ");
    log += upload.totalSize;
    addLog(LOG_LEVEL_INFO, log);
  }

  if (valid)
    uploadResult = 1;
  else
    uploadResult = 2;

}


//********************************************************************************
// Web Interface server web file from SPIFFS
//********************************************************************************
bool loadFromFS(boolean spiffs, String path) {
  if (!isLoggedIn()) return false;

  statusLED(true);

  String dataType = F("text/plain");
  if (path.endsWith("/")) path += F("index.htm");

  if (path.endsWith(".src")) path = path.substring(0, path.lastIndexOf("."));
  else if (path.endsWith(".htm")) dataType = F("text/html");
  else if (path.endsWith(".css")) dataType = F("text/css");
  else if (path.endsWith(".js")) dataType = F("application/javascript");
  else if (path.endsWith(".png")) dataType = F("image/png");
  else if (path.endsWith(".gif")) dataType = F("image/gif");
  else if (path.endsWith(".jpg")) dataType = F("image/jpeg");
  else if (path.endsWith(".ico")) dataType = F("image/x-icon");
  else if (path.endsWith(".txt")) dataType = F("application/octet-stream");
  else if (path.endsWith(".dat")) dataType = F("application/octet-stream");
  else if (path.endsWith(".esp")) return handle_custom(path);

  String log = F("HTML : Request file ");
  log += path;

  path = path.substring(1);
  if (spiffs)
  {
    fs::File dataFile = SPIFFS.open(path.c_str(), "r");
    if (!dataFile)
      return false;

    //prevent reloading stuff on every click
    WebServer.sendHeader("Cache-Control","max-age=3600, public");
    WebServer.sendHeader("Vary","*");
    WebServer.sendHeader("ETag","\"2.0.0\"");

    if (path.endsWith(".dat"))
      WebServer.sendHeader("Content-Disposition", "attachment;");
    WebServer.streamFile(dataFile, dataType);
    dataFile.close();
  }
  else
  {
#ifdef FEATURE_SD
    File dataFile = SD.open(path.c_str());
    if (!dataFile)
      return false;
    if (path.endsWith(".DAT"))
      WebServer.sendHeader("Content-Disposition", "attachment;");
    WebServer.streamFile(dataFile, dataType);
    dataFile.close();
#endif
  }
  statusLED(true);

  addLog(LOG_LEVEL_DEBUG, log);
  return true;
}

//********************************************************************************
// Web Interface custom page handler
//********************************************************************************
boolean handle_custom(String path) {
  path = path.substring(1);
  String reply = "";

  if (path.startsWith(F("dashboard"))) // for the dashboard page, create a default unit dropdown selector
  {
    reply += F("<script><!--\n"
             "function dept_onchange(frmselect) {frmselect.submit();}"
             "\n//--></script>");

    reply += F("<form name='frmselect' method='post'>");

    // handle page redirects to other unit's as requested by the unit dropdown selector
    byte unit = WebServer.arg(F("unit")).toInt();
    byte btnunit = WebServer.arg(F("btnunit")).toInt();
    if(!unit) unit = btnunit; // unit element prevails, if not used then set to btnunit
    if (unit && unit != Settings.Unit)
    {
      char url[20];
      sprintf_P(url, PSTR("http://%u.%u.%u.%u/dashboard.esp"), Nodes[unit].ip[0], Nodes[unit].ip[1], Nodes[unit].ip[2], Nodes[unit].ip[3]);
      reply = F("<meta http-equiv=\"refresh\" content=\"0; URL=");
      reply += url;
      reply += F("\">");
      WebServer.send(200, F("text/html"), reply);
      return true;
    }

    // create unit selector dropdown
    addSelector_Head(reply, F("unit"), true);
    byte choice = Settings.Unit;
    for (byte x = 0; x < UNIT_MAX; x++)
    {
      if (Nodes[x].ip[0] != 0 || x == Settings.Unit)
      {
      String name = String(x) + F(" - ");
      if (x != Settings.Unit)
        name += Nodes[x].nodeName;
      else
        name += Settings.Name;

      addSelector_Item(reply, name, x, choice == x, false, F(""));
      }
    }
    addSelector_Foot(reply);

    // create <> navigation buttons
    byte prev=Settings.Unit;
    byte next=Settings.Unit;
    for (byte x = Settings.Unit-1; x > 0; x--)
      if (Nodes[x].ip[0] != 0) {prev = x; break;}
    for (byte x = Settings.Unit+1; x < UNIT_MAX; x++)
      if (Nodes[x].ip[0] != 0) {next = x; break;}

    reply += F("<a class='button link' href=");
    reply += path;
    reply += F("?btnunit=");
    reply += prev;
    reply += F(">&lt;</a>");
    reply += F("<a class='button link' href=");
    reply += path;
    reply += F("?btnunit=");
    reply += next;
    reply += F(">&gt;</a>");
  }

  // handle commands from a custom page
  String webrequest = WebServer.arg(F("cmd"));
  if (webrequest.length() > 0 ){
    struct EventStruct TempEvent;
    parseCommandString(&TempEvent, webrequest);
    TempEvent.Source = VALUE_SOURCE_HTTP;

    if (PluginCall(PLUGIN_WRITE, &TempEvent, webrequest));
    else if (remoteConfig(&TempEvent, webrequest));
    else if (webrequest.startsWith(F("event")))
      ExecuteCommand(VALUE_SOURCE_HTTP, webrequest.c_str());

    // handle some update processes first, before returning page update...
    PluginCall(PLUGIN_TEN_PER_SECOND, 0, dummyString);
  }

  // create a dynamic custom page, parsing task values into [<taskname>#<taskvalue>] placeholders and parsing %xx% system variables
  fs::File dataFile = SPIFFS.open(path.c_str(), "r");
  if (dataFile)
  {
    String page = "";
    page.reserve(dataFile.size());
    while (dataFile.available())
      page += ((char)dataFile.read());

    reply += parseTemplate(page,0);
    dataFile.close();
  }
  else // if the requestef file does not exist, create a default action in case the page is named "dashboard*"
  {
    if (path.startsWith(F("dashboard")))
    {
      // if the custom page does not exist, create a basic task value overview page in case of dashboard request...
      reply += F("<meta name=\"viewport\" content=\"width=width=device-width, initial-scale=1\"><STYLE>* {font-family:sans-serif; font-size:16pt;}.button {margin:4px; padding:4px 16px; background-color:#07D; color:#FFF; text-decoration:none; border-radius:4px}</STYLE>");
      reply += F("<table>");
      for (byte x = 0; x < TASKS_MAX; x++)
      {
        if (Settings.TaskDeviceNumber[x] != 0)
          {
            LoadTaskSettings(x);
            byte DeviceIndex = getDeviceIndex(Settings.TaskDeviceNumber[x]);
            reply += F("<TR><TD>");
            reply += ExtraTaskSettings.TaskDeviceName;
            for (byte varNr = 0; varNr < VARS_PER_TASK; varNr++)
              {
                if ((Settings.TaskDeviceNumber[x] != 0) && (varNr < Device[DeviceIndex].ValueCount) && ExtraTaskSettings.TaskDeviceValueNames[varNr][0] !=0)
                {
                  if (varNr > 0)
                    reply += F("<TR><TD>");
                  reply += F("<TD>");
                  reply += ExtraTaskSettings.TaskDeviceValueNames[varNr];
                  reply += F("<TD>");
                  reply += String(UserVar[x * VARS_PER_TASK + varNr], ExtraTaskSettings.TaskDeviceValueDecimals[varNr]);
                }
              }
          }
      }
    }
    else
      return false; // unknown file that does not exist...
  }
  WebServer.send(200, "text/html", reply);
  return true;
}



//********************************************************************************
// Web Interface file list
//********************************************************************************
void handle_filelist() {
  navMenuIndex = 7;
  TXBuffer.startStream();
  sendHeadandTail(F("TmplStd"));

#if defined(ESP8266)

  String fdelete = WebServer.arg(F("delete"));

  if (fdelete.length() > 0)
  {
    SPIFFS.remove(fdelete);
    checkRuleSets();
  }



  TXBuffer += F("<table border=1px frame='box' rules='all'><TH><TH>Filename:<TH>Size");

  fs::Dir dir = SPIFFS.openDir("");
  while (dir.next())
  {
    TXBuffer += F("<TR><TD>");
    if (dir.fileName() != FILE_CONFIG && dir.fileName() != FILE_SECURITY && dir.fileName() != FILE_NOTIFICATION)
    {
      TXBuffer += F("<a class='button link' href=\"filelist?delete=");
      TXBuffer +=  dir.fileName();
      TXBuffer += F("\">Del</a>");
    }

    TXBuffer += F("<TD><a href=\"");
    TXBuffer +=  dir.fileName();
    TXBuffer += F("\">");
    TXBuffer +=  dir.fileName();
    TXBuffer += F("</a>");
    fs::File f = dir.openFile("r");
    TXBuffer += F("<TD>");
    TXBuffer +=  f.size();
  }
  TXBuffer += F("</table></form>");
  TXBuffer += F("<BR><a class='button link' href=\"/upload\">Upload</a>");
    sendHeadandTail(F("TmplStd"),true);
    TXBuffer.endStream();
#endif
#if defined(ESP32)
  String fdelete = WebServer.arg(F("delete"));

  if (fdelete.length() > 0)
  {
    SPIFFS.remove(fdelete);
    // flashCount();
  }



  TXBuffer += F("<table border=1px frame='box' rules='all'><TH><TH>Filename:<TH>Size");

  File root = SPIFFS.open("/");
  File file = root.openNextFile();
  while (file)
  {
    if(!file.isDirectory()){
      TXBuffer += F("<TR><TD>");
      if (file.name() != "/config.dat" && file.name() != "/security.dat" && file.name() != "/notification.dat")
      {
        TXBuffer += F("<a class='button link' href=\"filelist?delete=");
        TXBuffer +=  file.name();
        TXBuffer += F("\">Del</a>");
      }

      TXBuffer += F("<TD><a href=\"");
      TXBuffer +=  file.name();
      TXBuffer += F("\">");
      TXBuffer +=  file.name();
      TXBuffer += F("</a>");
      TXBuffer += F("<TD>");
      TXBuffer +=  file.size();
      file = root.openNextFile();
    }
  }
  TXBuffer += F("</table></form>");
  TXBuffer += F("<BR><a class='button link' href=\"/upload\">Upload</a>");
    sendHeadandTail(F("TmplStd"),true);
    TXBuffer.endStream();
#endif
}


//********************************************************************************
// Web Interface SD card file and directory list
//********************************************************************************
#ifdef FEATURE_SD
void handle_SDfilelist() {
  navMenuIndex = 7;
  TXBuffer.startStream();
  sendHeadandTail(F("TmplStd"));


  String fdelete = "";
  String ddelete = "";
  String change_to_dir = "";
  String current_dir = "";
  String parent_dir = "";
  char SDcardDir[80];

  for (uint8_t i = 0; i < WebServer.args(); i++) {
    if (WebServer.argName(i) == "delete")
    {
      fdelete = WebServer.arg(i);
    }
    if (WebServer.argName(i) == "deletedir")
    {
      ddelete = WebServer.arg(i);
    }
    if (WebServer.argName(i) == "chgto")
    {
      change_to_dir = WebServer.arg(i);
    }
  }

  if (fdelete.length() > 0)
  {
    SD.remove((char*)fdelete.c_str());
  }
  if (ddelete.length() > 0)
  {
    SD.rmdir((char*)ddelete.c_str());
  }
  if (change_to_dir.length() > 0)
  {
    current_dir = change_to_dir;
  }
  else
  {
    current_dir = "/";
  }

  current_dir.toCharArray(SDcardDir, current_dir.length()+1);
  File root = SD.open(SDcardDir);
  root.rewindDirectory();
  File entry = root.openNextFile();
  parent_dir = current_dir;
  if (!current_dir.equals("/"))
  {
    /* calculate the position to remove
    /
    / current_dir = /dir1/dir2/   =>   parent_dir = /dir1/
    /                     ^ position to remove, second last index of "/" + 1
    /
    / current_dir = /dir1/   =>   parent_dir = /
    /                ^ position to remove, second last index of "/" + 1
    */
    parent_dir.remove(parent_dir.lastIndexOf("/", parent_dir.lastIndexOf("/") - 1) + 1);
  }



  String subheader = "SD Card: " + current_dir;
  addFormSubHeader(  subheader,TXBuffer.buf);
  TXBuffer += F("<BR>");
  TXBuffer += F("<table border=1px frame='box' rules='all'><TH>Action<TH>Name<TH>Size");
  TXBuffer += F("<TR><TD>");
  TXBuffer += F("<TD><a href=\"SDfilelist?chgto=");
  TXBuffer +=  parent_dir;
  TXBuffer += F("\">..");
  TXBuffer += F("</a>");
  TXBuffer += F("<TD>");
  while (entry)
  {
    if (entry.isDirectory())
    {
      char SDcardChildDir[80];
      TXBuffer += F("<TR><TD>");
      // take a look in the directory for entries
      String child_dir = current_dir + entry.name();
      child_dir.toCharArray(SDcardChildDir, child_dir.length()+1);
      File child = SD.open(SDcardChildDir);
      File dir_has_entry = child.openNextFile();
      // when the directory is empty, display the button to delete them
      if (!dir_has_entry)
      {
        TXBuffer += F("<a class='button link' onclick=\"return confirm('Delete this directory?')\" href=\"SDfilelist?deletedir=");
        TXBuffer +=  current_dir;
        TXBuffer +=  entry.name();
        TXBuffer += F("/");
        TXBuffer += F("&chgto=");
        TXBuffer +=  current_dir;
        TXBuffer += F("\">Del</a>");
      }
      TXBuffer += F("<TD><a href=\"SDfilelist?chgto=");
      TXBuffer +=  current_dir;
      TXBuffer +=  entry.name();
      TXBuffer += F("/");
      TXBuffer += F("\">");
      TXBuffer +=  entry.name();
      TXBuffer += F("</a>");
      TXBuffer += F("<TD>");
      TXBuffer += F("dir");
      dir_has_entry.close();
    }
    else
    {
      TXBuffer += F("<TR><TD>");
      if (entry.name() != String(F(FILE_CONFIG)).c_str() && entry.name() != String(F(FILE_SECURITY)).c_str())
      {
        TXBuffer += F("<a class='button link' onclick=\"return confirm('Delete this file?')\" href=\"SDfilelist?delete=");
        TXBuffer +=  current_dir;
        TXBuffer +=  entry.name();
        TXBuffer += F("&chgto=");
        TXBuffer +=  current_dir;
        TXBuffer += F("\">Del</a>");
      }
      TXBuffer += F("<TD><a href=\"");
      TXBuffer +=  current_dir;
      TXBuffer +=  entry.name();
      TXBuffer += F("\">");
      TXBuffer +=  entry.name();
      TXBuffer += F("</a>");
      TXBuffer += F("<TD>");
      TXBuffer +=  entry.size();
    }
    entry.close();
    entry = root.openNextFile();
  }
  root.close();
  TXBuffer += F("</table></form>");
  //TXBuffer += F("<BR><a class='button link' href=\"/upload\">Upload</a>");
     sendHeadandTail(F("TmplStd"),true);
    TXBuffer.endStream();
}
#endif


//********************************************************************************
// Web Interface handle other requests
//********************************************************************************
void handleNotFound() {

  if (wifiSetup)
  {
    WebServer.send(200, "text/html", "<meta HTTP-EQUIV='REFRESH' content='0; url=/setup'>");
    return;
  }

  if (!isLoggedIn()) return;
  if (loadFromFS(true, WebServer.uri())) return;
  if (loadFromFS(false, WebServer.uri())) return;
  String message = "URI: ";
  message += WebServer.uri();
  message += "\nMethod: ";
  message += (WebServer.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += WebServer.args();
  message += "\n";
  for (uint8_t i = 0; i < WebServer.args(); i++) {
    message += " NAME:" + WebServer.argName(i) + "\n VALUE:" + WebServer.arg(i) + "\n";
  }
  WebServer.send(404, "text/plain", message);
}


//********************************************************************************
// Web Interface Setup Wizard
//********************************************************************************
void handle_setup() {
  // Do not check client IP range allowed.
  TXBuffer.startStream();
  sendHeadandTail(F("TmplAP"));

  addHeader(false,TXBuffer.buf);

  if (WiFi.status() == WL_CONNECTED)
  {
    addHtmlError(  SaveSettings());
    const IPAddress ip = WiFi.localIP();
    char host[20];
    formatIP(ip, host);
    TXBuffer += F("<BR>ESP is connected and using IP Address: <BR><h1>");
    TXBuffer +=  host;
    TXBuffer += F("</h1><BR><BR>Connect your laptop / tablet / phone<BR>back to your main Wifi network and<BR><BR>");
    TXBuffer += F("<a class='button' href='http://");
    TXBuffer +=  host;
    TXBuffer += F("/config'>Proceed to main config</a>");

    sendHeadandTail(F("TmplAP"),true);
    TXBuffer.endStream();

    wifiSetup = false;
    //WifiAPMode(false);  //this forces the iPhone to exit safari and this page was never displayed
    timerAPoff = millis() + 60000L;  //switch the AP off in 1 minute
    return;
  }

  static byte status = 0;
  static int n = 0;
  static byte refreshCount = 0;
  String ssid = WebServer.arg(F("ssid"));
  String other = WebServer.arg(F("other"));
  String password = WebServer.arg(F("pass"));

  if (other.length() != 0)
  {
    ssid = other;
  }

  // if ssid config not set and params are both provided
  if (status == 0 && ssid.length() != 0 && strcasecmp(SecuritySettings.WifiSSID, "ssid") == 0)
  {
    strncpy(SecuritySettings.WifiKey, password.c_str(), sizeof(SecuritySettings.WifiKey));
    strncpy(SecuritySettings.WifiSSID, ssid.c_str(), sizeof(SecuritySettings.WifiSSID));
    wifiSetupConnect = true;
    status = 1;
    refreshCount = 0;
  }

  TXBuffer += F("<h1>Wifi Setup wizard</h1><BR>");
  TXBuffer += F("<form name='frmselect' method='post'>");

  if (status == 0)  // first step, scan and show access points within reach...
  {
    if (n == 0)
      n = WiFi.scanNetworks();

    if (n == 0)
      TXBuffer += F("No Access Points found");
    else
    {
      for (int i = 0; i < n; ++i)
      {
        TXBuffer += F("<input type='radio' name='ssid' value='");
        TXBuffer +=  WiFi.SSID(i);
        TXBuffer += F("'");
        if (WiFi.SSID(i) == ssid)
          TXBuffer += F(" checked ");
        TXBuffer += F(">");
        TXBuffer +=  WiFi.SSID(i);
        TXBuffer += F("</input><br>");
      }
    }

    TXBuffer += F("<input type='radio' name='ssid' id='other_ssid' value='other' >other SSID:</input>");
    TXBuffer += F("<input type ='text' name='other' value='");
    TXBuffer += other;
    TXBuffer += F("'><br><br>");
    TXBuffer += F("Password: <input type ='text' name='pass' value='");
    TXBuffer +=  password;
    TXBuffer += F("'><br>");

    TXBuffer += F("<input type='submit' value='Connect'>");
  }

  if (status == 1)  // connecting stage...
  {
    if (refreshCount > 0)
    {
      status = 0;
      strncpy(SecuritySettings.WifiSSID, "ssid", sizeof(SecuritySettings.WifiSSID));
      SecuritySettings.WifiKey[0] = 0;
      TXBuffer += F("<a class=\"button\" href=\"setup\">Back to Setup</a>");
    }
    else
    {
      int wait = 20;
      if (refreshCount != 0)
        wait = 3;
      TXBuffer += F("Please wait for <h1 id=\"countdown\">20..</h1>");
      TXBuffer += F("<script type=\"text/JavaScript\">");
      TXBuffer += F("function timedRefresh(timeoutPeriod) {");
      TXBuffer += F("   var timer = setInterval(function() {");
      TXBuffer += F("   if (timeoutPeriod > 0) {");
      TXBuffer += F("       timeoutPeriod -= 1;");
      TXBuffer += F("       document.getElementById(\"countdown\").innerHTML = timeoutPeriod + \"..\" + \"<br />\";");
      TXBuffer += F("   } else {");
      TXBuffer += F("       clearInterval(timer);");
      TXBuffer += F("            window.location.href = window.location.href;");
      TXBuffer += F("       };");
      TXBuffer += F("   }, 1000);");
      TXBuffer += F("};");
      TXBuffer += F("timedRefresh(");
      TXBuffer += wait;
      TXBuffer += F(");");
      TXBuffer += F("</script>");
      TXBuffer += F("seconds while trying to connect");
    }
    refreshCount++;
  }

  TXBuffer += F("</form>");
   sendHeadandTail(F("TmplAP"),true);
  TXBuffer.endStream();
  delay(10);
}


//********************************************************************************
// Web Interface rules page
//********************************************************************************
void handle_rules() {
  if (!isLoggedIn()) return;
  navMenuIndex = 5;
  TXBuffer.startStream();
  sendHeadandTail(F("TmplStd"));
  static byte currentSet = 1;


  String set = WebServer.arg(F("set"));
  byte rulesSet = 1;
  if (set.length() > 0)
  {
    rulesSet = set.toInt();
  }

  #if defined(ESP8266)
    String fileName = F("rules");
  #endif
  #if defined(ESP32)
    String fileName = F("/rules");
  #endif
  fileName += rulesSet;
  fileName += F(".txt");


  checkRAM(F("handle_rules"));



  if (WebServer.args() > 0)
  {
    if (currentSet == rulesSet) // only save when the dropbox was not used to change set
    {
      String rules = WebServer.arg(F("rules"));
      if (rules.length() > RULES_MAX_SIZE)
        TXBuffer += F("<span style=\"color:red\">Data was not saved, exceeds web editor limit!</span>");
      else
      {

        // if (RTC.flashDayCounter > MAX_FLASHWRITES_PER_DAY)
        // {
        //   String log = F("FS   : Daily flash write rate exceeded! (powercyle to reset this)");
        //   addLog(LOG_LEVEL_ERROR, log);
        //   TXBuffer += F("<span style=\"color:red\">Error saving to flash!</span>");
        // }
        // else
        // {
          fs::File f = SPIFFS.open(fileName, "w");
          if (f)
          {
            f.print(rules);
            f.close();
            // flashCount();
          }
        // }
      }
    }
    else // changed set, check if file exists and create new
    {
      if (!SPIFFS.exists(fileName))
      {
        fs::File f = SPIFFS.open(fileName, "w");
        f.close();
      }
    }
  }

  if (rulesSet != currentSet)
    currentSet = rulesSet;

  TXBuffer += F("<form name = 'frmselect' method = 'post'><table><TR><TH>Rules");

  byte choice = rulesSet;
  String options[RULESETS_MAX];
  int optionValues[RULESETS_MAX];
  for (byte x = 0; x < RULESETS_MAX; x++)
  {
    options[x] = F("Rules Set ");
    options[x] += x + 1;
    optionValues[x] = x + 1;
  }

   TXBuffer += F("<TR><TD>Edit: ");
  addSelector(TXBuffer.buf,  F("set"), RULESETS_MAX, options, optionValues, NULL, choice, true);
  addButton(TXBuffer.buf, fileName, F("Download to file"));
  addHelpButton(TXBuffer.buf,  F("Tutorial_Rules"));

  // load form data from flash

  int size = 0;
  fs::File f = SPIFFS.open(fileName, "r+");
  if (f)
  {
    size = f.size();
    if (size > RULES_MAX_SIZE)
       TXBuffer += F("<span style=\"color:red\">Filesize exceeds web editor limit!</span>");
    else
    {
       TXBuffer += F("<TR><TD><textarea name='rules' rows='15' cols='80' wrap='off'>");
      while (f.available())
      {
        String c((char)f.read());
        htmlEscape(c);
         TXBuffer += c;
      }
       TXBuffer += F("</textarea>");
    }
    f.close();
  }

   TXBuffer += F("<TR><TD>Current size: ");
   TXBuffer += size;
   TXBuffer += F(" characters (Max ");
   TXBuffer += RULES_MAX_SIZE;
   TXBuffer += F(")");

  addFormSeparator( TXBuffer.buf);

   TXBuffer += F("<TR><TD>");
  addSubmitButton( TXBuffer.buf);
   TXBuffer += F("</table></form>");
  sendHeadandTail(F("TmplStd"),true);
  TXBuffer.endStream();

  checkRuleSets();
}


//********************************************************************************
// Web Interface sysinfo page
//********************************************************************************
void handle_sysinfo() {
  if (!isLoggedIn()) return;
  TXBuffer.startStream();
  sendHeadandTail(F("TmplStd"));

  int freeMem = ESP.getFreeHeap();

  addHeader(true,  TXBuffer.buf);
   TXBuffer += printWebString;
   TXBuffer += F("<form>");
   TXBuffer += F("<table><TR><TH width=120>System Info<TH>");

   TXBuffer += F("<TR><TD>Unit<TD>");
   TXBuffer += Settings.Unit;

  if (Settings.UseNTP)
  {

     TXBuffer += F("<TR><TD>Local Time<TD>");
     TXBuffer += getDateTimeString('-', ':', ' ');
  }

   TXBuffer += F("<TR><TD>Uptime<TD>");
  char strUpTime[40];
  int minutes = wdcounter / 2;
  int days = minutes / 1440;
  minutes = minutes % 1440;
  int hrs = minutes / 60;
  minutes = minutes % 60;
  sprintf_P(strUpTime, PSTR("%d days %d hours %d minutes"), days, hrs, minutes);
   TXBuffer += strUpTime;

   TXBuffer += F("<TR><TD>Load<TD>");
  if (wdcounter > 0)
  {
     TXBuffer += 100 - (100 * loopCounterLast / loopCounterMax);
     TXBuffer += F("% (LC=");
     TXBuffer += int(loopCounterLast / 30);
     TXBuffer += F(")");
  }

   TXBuffer += F("<TR><TD>Free Mem<TD>");
   TXBuffer += freeMem;
   TXBuffer += F(" (");
   TXBuffer += lowestRAM;
   TXBuffer += F(" - ");
   TXBuffer += lowestRAMfunction;
   TXBuffer += F(")");

   TXBuffer += F("<TR><TD>Boot<TD>");
  switch (lastBootCause)
  {
    case BOOT_CAUSE_MANUAL_REBOOT:
       TXBuffer += F("Manual reboot");
      break;
    case BOOT_CAUSE_DEEP_SLEEP: //nobody should ever see this, since it should sleep again right away.
       TXBuffer += F("Deep sleep");
      break;
    case BOOT_CAUSE_COLD_BOOT:
       TXBuffer += F("Cold boot");
      break;
    case BOOT_CAUSE_EXT_WD:
       TXBuffer += F("External Watchdog");
      break;
  }
   TXBuffer += F(" (");
   TXBuffer += RTC.bootCounter;
   TXBuffer += F(")");

   TXBuffer += F("<TR><TD colspan=2><H3>Network</H3></TD></TR>");

  if (WiFi.status() == WL_CONNECTED)
  {
     TXBuffer += F("<TR><TD>Wifi<TD>");
    #if defined(ESP8266)
      byte PHYmode = wifi_get_phy_mode();
    #endif
    #if defined(ESP32)
      byte PHYmode = 3; // wifi_get_phy_mode();
    #endif
    switch (PHYmode)
    {
      case 1:
         TXBuffer += F("802.11B");
        break;
      case 2:
         TXBuffer += F("802.11G");
        break;
      case 3:
         TXBuffer += F("802.11N");
        break;
    }
     TXBuffer += F(" (RSSI ");
     TXBuffer += WiFi.RSSI();
     TXBuffer += F(" dB)");
  }

   TXBuffer += F("<TR><TD>IP / subnet<TD>");
   TXBuffer += formatIP(WiFi.localIP());
   TXBuffer += F(" / ");
   TXBuffer += formatIP(WiFi.subnetMask());

   TXBuffer += F("<TR><TD>GW<TD>");
   TXBuffer += formatIP(WiFi.gatewayIP());

  {
     TXBuffer += F("<TR><TD>Client IP<TD>");
     WiFiClient client(WebServer.client());
     TXBuffer += formatIP(client.remoteIP());
  }

  TXBuffer += F("<TR><TD>Serial Port available:<TD>");
  TXBuffer += String(SerialAvailableForWrite());
  TXBuffer += F(" (");
  TXBuffer += Serial.availableForWrite();
  TXBuffer += F(" , ");
  TXBuffer += Serial.available();
  TXBuffer += F(")");

  TXBuffer += F("<TR><TD>STA MAC<TD>");

  uint8_t mac[] = {0, 0, 0, 0, 0, 0};
  uint8_t* macread = WiFi.macAddress(mac);
  char macaddress[20];
  formatMAC(macread, macaddress);
   TXBuffer += macaddress;

   TXBuffer += F("<TR><TD>AP MAC<TD>");
  macread = WiFi.softAPmacAddress(mac);
  formatMAC(macread, macaddress);
   TXBuffer += macaddress;

   TXBuffer += F("<TR><TD colspan=2><H3>Firmware</H3></TD></TR>");

   TXBuffer += F("<TR><TD>Build<TD>");
   TXBuffer += BUILD;
   TXBuffer += F(" ");
   TXBuffer += F(BUILD_NOTES);
  #if defined(ESP8266)
     TXBuffer += F(" (core ");
     TXBuffer += ESP.getCoreVersion();
     TXBuffer += F(")");
  #endif

   TXBuffer += F("<TR><TD>GIT version<TD>");
   TXBuffer += BUILD_GIT;

   TXBuffer += F("<TR><TD>Plugins<TD>");
   TXBuffer += deviceCount + 1;

  #ifdef PLUGIN_BUILD_NORMAL
     TXBuffer += F(" [Normal]");
  #endif

  #ifdef PLUGIN_BUILD_TESTING
     TXBuffer += F(" [Testing]");
  #endif

  #ifdef PLUGIN_BUILD_DEV
     TXBuffer += F(" [Development]");
  #endif

   TXBuffer += F("<TR><TD colspan=2><H3>ESP board</H3></TD></TR>");

   TXBuffer += F("<TR><TD>ESP Chip ID<TD>");
  #if defined(ESP8266)
     TXBuffer += ESP.getChipId();
     TXBuffer += F(" (0x");
     String espChipId(ESP.getChipId(), HEX);
     espChipId.toUpperCase();
     TXBuffer += espChipId;
     TXBuffer += F(")");

     TXBuffer += F("<TR><TD>ESP Chip Freq:<TD>");
     TXBuffer += ESP.getCpuFreqMHz();
     TXBuffer += F(" MHz");
  #endif

   TXBuffer += F("<TR><TD colspan=2><H3>Storage</H3></TD></TR>");

   TXBuffer += F("<TR><TD>Flash Chip ID<TD>");
  #if defined(ESP8266)
    uint32_t flashChipId = ESP.getFlashChipId();
    // Set to HEX may be something like 0x1640E0.
    // Where manufacturer is 0xE0 and device is 0x4016.
     TXBuffer += F("Vendor: 0x");
    String flashVendor(flashChipId & 0xFF, HEX);
    flashVendor.toUpperCase();
     TXBuffer += flashVendor;
     TXBuffer += F(" Device: 0x");
    uint32_t flashDevice = (flashChipId & 0xFF00) | ((flashChipId >> 16) & 0xFF);
    String flashDeviceString(flashDevice, HEX);
    flashDeviceString.toUpperCase();
     TXBuffer += flashDeviceString;
  #endif
  uint32_t realSize = 0;
  #if defined(ESP8266)
    realSize = ESP.getFlashChipRealSize(); //ESP.getFlashChipSize();
  #endif
  #if defined(ESP32)
    realSize = ESP.getFlashChipSize();
  #endif
  uint32_t ideSize = ESP.getFlashChipSize();

   TXBuffer += F("<TR><TD>Flash Chip Real Size:<TD>");
   TXBuffer += realSize / 1024;
   TXBuffer += F(" kB");

   TXBuffer += F("<TR><TD>Flash IDE Size:<TD>");
   TXBuffer += ideSize / 1024;
   TXBuffer += F(" kB");

  // Please check what is supported for the ESP32
  #if defined(ESP8266)
     TXBuffer += F("<TR><TD>Flash IDE speed:<TD>");
     TXBuffer += ESP.getFlashChipSpeed() / 1000000;
     TXBuffer += F(" MHz");

    FlashMode_t ideMode = ESP.getFlashChipMode();
     TXBuffer += F("<TR><TD>Flash IDE mode:<TD>");
    switch (ideMode) {
      case FM_QIO:   TXBuffer += F("QIO");  break;
      case FM_QOUT:  TXBuffer += F("QOUT"); break;
      case FM_DIO:   TXBuffer += F("DIO");  break;
      case FM_DOUT:  TXBuffer += F("DOUT"); break;
      default:
          TXBuffer += F("Unknown"); break;
    }
  #endif

   TXBuffer += F("<TR><TD>Flash Writes<TD>");
   TXBuffer += RTC.flashDayCounter;
   TXBuffer += F(" daily / ");
   TXBuffer += RTC.flashCounter;
   TXBuffer += F(" boot");

   TXBuffer += F("<TR><TD>Sketch Size<TD>");
  #if defined(ESP8266)
   TXBuffer += ESP.getSketchSize() / 1024;
   TXBuffer += F(" kB (");
   TXBuffer += ESP.getFreeSketchSpace() / 1024;
   TXBuffer += F(" kB free)");
  #endif

   TXBuffer += F("</table></form>");
   sendHeadandTail(F("TmplStd"),true);
  TXBuffer.endStream();
}


//********************************************************************************
// URNEncode char string to string object
//********************************************************************************
String URLEncode(const char* msg)
{
  const char *hex = "0123456789abcdef";
  String encodedMsg = "";

  while (*msg != '\0') {
    if ( ('a' <= *msg && *msg <= 'z')
         || ('A' <= *msg && *msg <= 'Z')
         || ('0' <= *msg && *msg <= '9')
         || ('-' == *msg) || ('_' == *msg)
         || ('.' == *msg) || ('~' == *msg) ) {
      encodedMsg += *msg;
    } else {
      encodedMsg += '%';
      encodedMsg += hex[*msg >> 4];
      encodedMsg += hex[*msg & 15];
    }
    msg++;
  }
  return encodedMsg;
}


String getControllerSymbol(byte index)
{
  String ret = F("&#");
  ret += 10102 + index;
  ret += F(";");
  return ret;
}

String getValueSymbol(byte index)
{
  String ret = F("&#");
  ret += 10112 + index;
  ret += F(";");
  return ret;
}


/*********************************************************************************************\
 * ESP Easy logo Favicon.ico 16x16 8 bit
\*********************************************************************************************/
// Generated using xxd:   xxd -i favicon.ico > favicon.ino
static const char favicon_8b_ico[] PROGMEM = {
  0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x10, 0x10, 0x00, 0x00, 0x01, 0x00,
  0x20, 0x00, 0x68, 0x04, 0x00, 0x00, 0x16, 0x00, 0x00, 0x00, 0x28, 0x00,
  0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x01, 0x00,
  0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x12, 0x0b,
  0x00, 0x00, 0x12, 0x0b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0xef, 0xc0, 0x89, 0x11, 0xfe, 0xfb, 0xf8, 0xac, 0xff, 0xff,
  0xff, 0xff, 0xfe, 0xfa, 0xf5, 0xff, 0xfe, 0xfa, 0xf5, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0xfa, 0xf5, 0xff, 0xfe, 0xfa,
  0xf5, 0xff, 0xfe, 0xfa, 0xf5, 0xff, 0xfe, 0xfa, 0xf5, 0xff, 0xfe, 0xfa,
  0xf5, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfc, 0xf3,
  0xe9, 0xac, 0xef, 0xc0, 0x89, 0x11, 0xfe, 0xfb, 0xf8, 0xac, 0xf1, 0xc8,
  0x95, 0xff, 0xef, 0xc2, 0x8a, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xf1, 0xc8, 0x95, 0xff, 0xef, 0xc2, 0x8a, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xfe, 0xfa, 0xf5, 0xff, 0xef, 0xc2, 0x8a, 0xff, 0xef, 0xc2,
  0x8a, 0xff, 0xfe, 0xfa, 0xf5, 0xff, 0xfc, 0xf3, 0xe9, 0xac, 0xff, 0xff,
  0xff, 0xff, 0xef, 0xc2, 0x8a, 0xff, 0xd8, 0x63, 0x00, 0xff, 0xef, 0xbc,
  0x80, 0xff, 0xff, 0xff, 0xff, 0xff, 0xef, 0xbc, 0x80, 0xff, 0xd8, 0x63,
  0x00, 0xff, 0xef, 0xc2, 0x8a, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0xfa,
  0xf5, 0xff, 0xff, 0xff, 0xff, 0xff, 0xef, 0xbc, 0x80, 0xff, 0xd9, 0x69,
  0x00, 0xff, 0xd9, 0x69, 0x00, 0xff, 0xef, 0xc2, 0x8a, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xfe, 0xfa, 0xf5, 0xff, 0xff, 0xff, 0xff, 0xff, 0xef, 0xbc,
  0x80, 0xff, 0xd8, 0x63, 0x00, 0xff, 0xef, 0xbc, 0x80, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xef, 0xbc, 0x80, 0xff, 0xd8, 0x63, 0x00, 0xff, 0xef, 0xc2,
  0x8a, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xef, 0xbc,
  0x80, 0xff, 0xd9, 0x69, 0x00, 0xff, 0xd9, 0x69, 0x00, 0xff, 0xef, 0xc2,
  0x8a, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0xfa, 0xf5, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xef, 0xbc, 0x80, 0xff, 0xd8, 0x63,
  0x00, 0xff, 0xef, 0xbc, 0x80, 0xff, 0xff, 0xff, 0xff, 0xff, 0xef, 0xbc,
  0x80, 0xff, 0xd8, 0x63, 0x00, 0xff, 0xef, 0xc2, 0x8a, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xfe, 0xfa, 0xf5, 0xff, 0xef, 0xbc, 0x80, 0xff, 0xef, 0xbc,
  0x80, 0xff, 0xfe, 0xfa, 0xf5, 0xff, 0xfe, 0xfa, 0xf5, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xf1, 0xc8, 0x95, 0xff, 0xef, 0xbc, 0x80, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xef, 0xbc, 0x80, 0xff, 0xd8, 0x63, 0x00, 0xff, 0xef, 0xbc,
  0x80, 0xff, 0xff, 0xff, 0xff, 0xff, 0xef, 0xbc, 0x80, 0xff, 0xd8, 0x63,
  0x00, 0xff, 0xef, 0xc2, 0x8a, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0xfa,
  0xf5, 0xff, 0xff, 0xff, 0xff, 0xff, 0xef, 0xc2, 0x8a, 0xff, 0xd8, 0x63,
  0x00, 0xff, 0xef, 0xbc, 0x80, 0xff, 0xff, 0xff, 0xff, 0xff, 0xef, 0xbc,
  0x80, 0xff, 0xd8, 0x63, 0x00, 0xff, 0xef, 0xbc, 0x80, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xef, 0xbc, 0x80, 0xff, 0xd8, 0x63, 0x00, 0xff, 0xef, 0xc2,
  0x8a, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0xfa, 0xf5, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xfe, 0xfa, 0xf5, 0xff, 0xfe, 0xfa, 0xf5, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xef, 0xc2, 0x8a, 0xff, 0xd8, 0x63, 0x00, 0xff, 0xef, 0xbc,
  0x80, 0xff, 0xff, 0xff, 0xff, 0xff, 0xef, 0xbc, 0x80, 0xff, 0xd8, 0x63,
  0x00, 0xff, 0xef, 0xbc, 0x80, 0xff, 0xff, 0xff, 0xff, 0xff, 0xef, 0xbc,
  0x80, 0xff, 0xd8, 0x63, 0x00, 0xff, 0xef, 0xc2, 0x8a, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0xfa, 0xf5, 0xff, 0xfe, 0xfa,
  0xf5, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xef, 0xc2,
  0x8a, 0xff, 0xd8, 0x63, 0x00, 0xff, 0xef, 0xbc, 0x80, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xef, 0xbc, 0x80, 0xff, 0xd8, 0x63, 0x00, 0xff, 0xef, 0xbc,
  0x80, 0xff, 0xff, 0xff, 0xff, 0xff, 0xef, 0xbc, 0x80, 0xff, 0xd8, 0x63,
  0x00, 0xff, 0xef, 0xc2, 0x8a, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0xfa,
  0xf5, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf1, 0xc8, 0x95, 0xff, 0xef, 0xbc,
  0x80, 0xff, 0xff, 0xff, 0xff, 0xff, 0xef, 0xc2, 0x8a, 0xff, 0xd8, 0x63,
  0x00, 0xff, 0xef, 0xbc, 0x80, 0xff, 0xff, 0xff, 0xff, 0xff, 0xef, 0xbc,
  0x80, 0xff, 0xd8, 0x63, 0x00, 0xff, 0xef, 0xbc, 0x80, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xef, 0xbc, 0x80, 0xff, 0xd8, 0x63, 0x00, 0xff, 0xef, 0xc2,
  0x8a, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf1, 0xc8,
  0x95, 0xff, 0xd8, 0x63, 0x00, 0xff, 0xef, 0xbc, 0x80, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xef, 0xc2, 0x8a, 0xff, 0xd8, 0x63, 0x00, 0xff, 0xef, 0xbc,
  0x80, 0xff, 0xff, 0xff, 0xff, 0xff, 0xef, 0xbc, 0x80, 0xff, 0xd8, 0x63,
  0x00, 0xff, 0xef, 0xbc, 0x80, 0xff, 0xff, 0xff, 0xff, 0xff, 0xef, 0xbc,
  0x80, 0xff, 0xf1, 0xc8, 0x95, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0xfa,
  0xf5, 0xff, 0xff, 0xff, 0xff, 0xff, 0xef, 0xc2, 0x8a, 0xff, 0xd8, 0x63,
  0x00, 0xff, 0xef, 0xbc, 0x80, 0xff, 0xff, 0xff, 0xff, 0xff, 0xef, 0xc2,
  0x8a, 0xff, 0xd8, 0x63, 0x00, 0xff, 0xef, 0xbc, 0x80, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xef, 0xbc, 0x80, 0xff, 0xd8, 0x63, 0x00, 0xff, 0xef, 0xbc,
  0x80, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0xfa,
  0xf5, 0xff, 0xfe, 0xfa, 0xf5, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xef, 0xc2, 0x8a, 0xff, 0xd8, 0x63, 0x00, 0xff, 0xef, 0xbc,
  0x80, 0xff, 0xff, 0xff, 0xff, 0xff, 0xef, 0xc2, 0x8a, 0xff, 0xd8, 0x63,
  0x00, 0xff, 0xef, 0xbc, 0x80, 0xff, 0xff, 0xff, 0xff, 0xff, 0xef, 0xbc,
  0x80, 0xff, 0xd8, 0x63, 0x00, 0xff, 0xef, 0xbc, 0x80, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xfe, 0xfa, 0xf5, 0xff, 0xfe, 0xfa, 0xf5, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xfe, 0xfa, 0xf5, 0xff, 0xff, 0xff, 0xff, 0xff, 0xef, 0xc2,
  0x8a, 0xff, 0xd8, 0x63, 0x00, 0xff, 0xef, 0xbc, 0x80, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xef, 0xc2, 0x8a, 0xff, 0xd8, 0x63, 0x00, 0xff, 0xef, 0xbc,
  0x80, 0xff, 0xff, 0xff, 0xff, 0xff, 0xef, 0xbc, 0x80, 0xff, 0xd8, 0x63,
  0x00, 0xff, 0xef, 0xc2, 0x8a, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfb, 0xf1,
  0xe6, 0xac, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf1, 0xc8, 0x95, 0xff, 0xf1, 0xc8,
  0x95, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xef, 0xc2,
  0x8a, 0xff, 0xf1, 0xc8, 0x95, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xef, 0xc2, 0x8a, 0xff, 0xf1, 0xc8, 0x95, 0xff, 0xfe, 0xfb,
  0xf8, 0xac, 0xef, 0xc0, 0x89, 0x11, 0xfb, 0xf1, 0xe6, 0xac, 0xfe, 0xfa,
  0xf5, 0xff, 0xfe, 0xfa, 0xf5, 0xff, 0xfe, 0xfa, 0xf5, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0xfa, 0xf5, 0xff, 0xfe, 0xfa,
  0xf5, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0xfa,
  0xf5, 0xff, 0xfe, 0xfa, 0xf5, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0xfb,
  0xf8, 0xac, 0xef, 0xc0, 0x89, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
unsigned int favicon_8b_ico_len = 1150;

void handle_favicon() {
  WebServer.send_P(200, PSTR("image/x-icon"), favicon_8b_ico, favicon_8b_ico_len);
}
