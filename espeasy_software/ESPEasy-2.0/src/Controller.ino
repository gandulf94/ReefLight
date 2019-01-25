//********************************************************************************
// Interface for Sending to Controllers
//********************************************************************************
void sendData(struct EventStruct *event)
{
  LoadTaskSettings(event->TaskIndex);
  if (Settings.UseRules)
    createRuleEvents(event->TaskIndex);

  if (Settings.UseValueLogger && Settings.InitSPI && Settings.Pin_sd_cs >= 0)
    SendValueLogger(event->TaskIndex);

//  if (!Settings.TaskDeviceSendData[event->TaskIndex])
//    return false;

  if (Settings.MessageDelay != 0)
  {
    const long dif = timePassedSince(lastSend);
    if (dif > 0 && dif < static_cast<long>(Settings.MessageDelay))
    {
      uint16_t delayms = Settings.MessageDelay - dif;
      //this is logged nowhere else, so might as well disable it here also:
      // addLog(LOG_LEVEL_DEBUG_MORE, String(F("CTRL : Message delay (ms): "))+delayms);

     delayBackground(delayms);

      // unsigned long timer = millis() + delayms;
      // while (!timeOutReached(timer))
      //   backgroundtasks();
    }
  }

  LoadTaskSettings(event->TaskIndex); // could have changed during background tasks.

  for (byte x=0; x < CONTROLLER_MAX; x++)
  {
    event->ControllerIndex = x;
    event->idx = Settings.TaskDeviceID[x][event->TaskIndex];
    if (Settings.TaskDeviceSendData[event->ControllerIndex][event->TaskIndex] &&
        Settings.ControllerEnabled[event->ControllerIndex] && Settings.Protocol[event->ControllerIndex])
    {
      event->ProtocolIndex = getProtocolIndex(Settings.Protocol[event->ControllerIndex]);
      if (validUserVar(event)) {
        CPlugin_ptr[event->ProtocolIndex](CPLUGIN_PROTOCOL_SEND, event, dummyString);
      } else {
        String log = F("Invalid value detected for controller ");
        String controllerName;
        CPlugin_ptr[event->ProtocolIndex](CPLUGIN_GET_DEVICENAME, event, controllerName);
        log += controllerName;
        addLog(LOG_LEVEL_DEBUG, log);
      }
    }
  }

  PluginCall(PLUGIN_EVENT_OUT, event, dummyString);
  lastSend = millis();
}

boolean validUserVar(struct EventStruct *event) {
  for (int i = 0; i < VARS_PER_TASK; ++i) {
    const float f(UserVar[event->BaseVarIndex + i]);
    if (!isValidFloat(f)) return false;
  }
  return true;
}

/*********************************************************************************************\
 * Handle incoming MQTT messages
\*********************************************************************************************/
// handle MQTT messages
void callback(char* c_topic, byte* b_payload, unsigned int length) {
  // char log[256];
  char c_payload[384];

  statusLED(true);
  int enabledMqttController = firstEnabledMQTTController();
  if (enabledMqttController < 0) {
    addLog(LOG_LEVEL_ERROR, F("MQTT : No enabled MQTT controller"));
    return;
  }
  if ((length + 1) > sizeof(c_payload))
  {
    addLog(LOG_LEVEL_ERROR, F("MQTT : Ignored too big message"));
    return;
  }

  //convert payload to string, and 0 terminate
  strncpy(c_payload,(char*)b_payload,length);
  c_payload[length] = 0;

  String log;
  log=F("MQTT : Topic: ");
  log+=c_topic;
  addLog(LOG_LEVEL_DEBUG, log);

  log=F("MQTT : Payload: ");
  log+=c_payload;
  addLog(LOG_LEVEL_DEBUG, log);

  // sprintf_P(log, PSTR("%s%s"), "MQTT : Topic: ", c_topic);
  // addLog(LOG_LEVEL_DEBUG, log);
  // sprintf_P(log, PSTR("%s%s"), "MQTT : Payload: ", c_payload);
  // addLog(LOG_LEVEL_DEBUG, log);

  struct EventStruct TempEvent;
  TempEvent.String1 = c_topic;
  TempEvent.String2 = c_payload;
  byte ProtocolIndex = getProtocolIndex(Settings.Protocol[enabledMqttController]);
  CPlugin_ptr[ProtocolIndex](CPLUGIN_PROTOCOL_RECV, &TempEvent, dummyString);
}


/*********************************************************************************************\
 * Connect to MQTT message broker
\*********************************************************************************************/
bool MQTTConnect(int controller_idx)
{
  ControllerSettingsStruct ControllerSettings;
  LoadControllerSettings(controller_idx, (byte*)&ControllerSettings, sizeof(ControllerSettings));
  if (!ControllerSettings.checkHostReachable(true))
    return false;
  if (MQTTclient.connected())
    MQTTclient.disconnect();
  if (ControllerSettings.UseDNS) {
    MQTTclient.setServer(ControllerSettings.getHost().c_str(), ControllerSettings.Port);
  } else {
    MQTTclient.setServer(ControllerSettings.getIP(), ControllerSettings.Port);
  }
  MQTTclient.setCallback(callback);

  // MQTT needs a unique clientname to subscribe to broker
  String clientid = F("ESPClient_");
  clientid += WiFi.macAddress();

  String LWTTopic = ControllerSettings.Subscribe;
  LWTTopic.replace(F("/#"), F("/status"));
  parseSystemVariables(LWTTopic, false);
  LWTTopic += F("/LWT"); // Extend the topic for status updates of connected/disconnected status.

  boolean MQTTresult = false;
  uint8_t willQos = 0;
  boolean willRetain = true;

  if ((SecuritySettings.ControllerUser[controller_idx] != 0) && (SecuritySettings.ControllerPassword[controller_idx] != 0)) {
    MQTTresult = MQTTclient.connect(clientid.c_str(), SecuritySettings.ControllerUser[controller_idx], SecuritySettings.ControllerPassword[controller_idx],
                                    LWTTopic.c_str(), willQos, willRetain, "Connection Lost");
  } else {
    MQTTresult = MQTTclient.connect(clientid.c_str(), LWTTopic.c_str(), willQos, willRetain, "Connection Lost");
  }
  yield();

  if (!MQTTresult) {
    addLog(LOG_LEVEL_ERROR, F("MQTT : Failed to connect to broker"));
    return false;
  }
  MQTTclient_should_reconnect = false;
  String log = F("MQTT : Connected to broker with client ID: ");
  log += clientid;
  addLog(LOG_LEVEL_INFO, log);
  String subscribeTo = ControllerSettings.Subscribe;
  parseSystemVariables(subscribeTo, false);
  MQTTclient.subscribe(subscribeTo.c_str());
  log = F("Subscribed to: ");
  log += subscribeTo;
  addLog(LOG_LEVEL_INFO, log);

  if (MQTTclient.publish(LWTTopic.c_str(), "Connected", 1)) {
    if (Settings.UseRules)
    {
      String event = F("MQTT#Connected");
      rulesProcessing(event);
    }
    statusLED(true);
    return true; // end loop if succesfull
  }
  return false;
}


/*********************************************************************************************\
 * Check connection MQTT message broker
\*********************************************************************************************/
bool MQTTCheck(int controller_idx)
{
  byte ProtocolIndex = getProtocolIndex(Settings.Protocol[controller_idx]);
  if (Protocol[ProtocolIndex].usesMQTT)
  {
    if (MQTTclient_should_reconnect || !WiFiConnected(10) || !MQTTclient.connected())
    {
      if (MQTTclient_should_reconnect) {
        addLog(LOG_LEVEL_ERROR, F("MQTT : Intentional reconnect"));
      } else {
        addLog(LOG_LEVEL_ERROR, F("MQTT : Connection lost"));
        connectionFailures += 2;
      }
      return MQTTConnect(controller_idx);
    } else if (connectionFailures) {
      connectionFailures--;
    }
  }
  // When no MQTT protocol is enabled, all is fine.
  return true;
}


/*********************************************************************************************\
 * Send status info to request source
\*********************************************************************************************/

void SendStatus(byte source, String status)
{
  switch(source)
  {
    case VALUE_SOURCE_HTTP:
      if (printToWeb)
        printWebString += status;
      break;
    case VALUE_SOURCE_MQTT:
      MQTTStatus(status);
      break;
    case VALUE_SOURCE_SERIAL:
      Serial.println(status);
      break;
  }
}

boolean MQTTpublish(int controller_idx, const char* topic, const char* payload, boolean retained)
{
  if (MQTTclient.publish(topic, payload, retained))
    return true;
  addLog(LOG_LEVEL_DEBUG, F("MQTT : publish failed"));
  return false;
}

/*********************************************************************************************\
 * Send status info back to channel where request came from
\*********************************************************************************************/
void MQTTStatus(String& status)
{
  ControllerSettingsStruct ControllerSettings;
  int enabledMqttController = firstEnabledMQTTController();
  if (enabledMqttController >= 0) {
    LoadControllerSettings(enabledMqttController, (byte*)&ControllerSettings, sizeof(ControllerSettings));
    String pubname = ControllerSettings.Subscribe;
    pubname.replace(F("/#"), F("/status"));
    parseSystemVariables(pubname, false);
    MQTTpublish(enabledMqttController, pubname.c_str(), status.c_str(),Settings.MQTTRetainFlag);
  }
}
