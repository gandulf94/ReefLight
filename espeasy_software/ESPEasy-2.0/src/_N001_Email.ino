//#######################################################################################################
//########################### Notification Plugin 001: Email ############################################
//#######################################################################################################

#define NPLUGIN_001
#define NPLUGIN_ID_001         1
#define NPLUGIN_NAME_001       "Email (SMTP)"

#define NPLUGIN_001_TIMEOUT 5000

boolean NPlugin_001(byte function, struct EventStruct *event, String& string)
{
  boolean success = false;

  switch (function)
  {
    case NPLUGIN_PROTOCOL_ADD:
      {
        Notification[++notificationCount].Number = NPLUGIN_ID_001;
        Notification[notificationCount].usesMessaging = true;
        Notification[notificationCount].usesGPIO=0;
        break;
      }

    case NPLUGIN_GET_DEVICENAME:
      {
        string = F(NPLUGIN_NAME_001);
        break;
      }

    // Edwin: NPLUGIN_WRITE seems to be not implemented/not used yet? Disabled because its confusing now.
    // case NPLUGIN_WRITE:
    //   {
    //     String log = "";
    //     String command = parseString(string, 1);
    //
    //     if (command == F("email"))
    //     {
    //       NotificationSettingsStruct NotificationSettings;
    //       LoadNotificationSettings(event->NotificationIndex, (byte*)&NotificationSettings, sizeof(NotificationSettings));
    //       NPlugin_001_send(NotificationSettings.Domain, NotificationSettings.Receiver, NotificationSettings.Sender, NotificationSettings.Subject, NotificationSettings.Body, NotificationSettings.Server, NotificationSettings.Port);
    //       success = true;
    //     }
    //     break;
    //   }

    case NPLUGIN_NOTIFY:
      {
        NotificationSettingsStruct NotificationSettings;
        LoadNotificationSettings(event->NotificationIndex, (byte*)&NotificationSettings, sizeof(NotificationSettings));
        String subject = NotificationSettings.Subject;
        String body = "";
        if (string.length() >0)
          body = string;
        else
          body = NotificationSettings.Body;
        subject = parseTemplate(subject, subject.length());
        body = parseTemplate(body, body.length());
        NPlugin_001_send(NotificationSettings, subject, body);
        success = true;
      }

  }
  return success;
}

boolean NPlugin_001_send(const NotificationSettingsStruct& notificationsettings, const String& aSub, const String& aMesg) {
//  String& aDomain , String aTo, String aFrom, String aSub, String aMesg, String aHost, int aPort)
  boolean myStatus = false;

  // Use WiFiClient class to create TCP connections
  WiFiClient client;
  String aHost = notificationsettings.Server;
  addLog(LOG_LEVEL_DEBUG, String(F("EMAIL: Connecting to "))+aHost + notificationsettings.Port);
  if (!client.connect(aHost.c_str(), notificationsettings.Port)) {
    addLog(LOG_LEVEL_ERROR, String(F("EMAIL: Error connecting to "))+aHost + notificationsettings.Port);
    myStatus = false;
  }
  else {
    String mailheader = F(
      "From: $nodename <$emailfrom>\r\n"
      "To: $ato\r\n"
      "Subject: $subject\r\n"
      "Reply-To: $nodename <$emailfrom>\r\n"
      "MIME-VERSION: 1.0\r\n"
      "Content-type: text/html; charset=UTF-8\r\n"
      "X-Mailer: EspEasy v$espeasyversion\r\n\r\n"
    );

    mailheader.replace(String(F("$nodename")), Settings.Name);
    mailheader.replace(String(F("$emailfrom")), notificationsettings.Sender);
    mailheader.replace(String(F("$ato")), notificationsettings.Receiver);
    mailheader.replace(String(F("$subject")), aSub);
    mailheader.replace(String(F("$espeasyversion")), String(BUILD));

    // Wait for Client to Start Sending
    // The MTA Exchange
    while (true) {

      if (!NPlugin_001_MTA(client, "",                                    F("220 "))) break;
      if (!NPlugin_001_MTA(client, String(F("EHLO ")) + notificationsettings.Domain,          F("250 "))) break;
      if (!NPlugin_001_Auth(client, notificationsettings.User, notificationsettings.Pass)) break;
      if (!NPlugin_001_MTA(client, String(F("MAIL FROM:")) + notificationsettings.Sender + "",  F("250 "))) break;
      if (!NPlugin_001_MTA(client, String(F("RCPT TO:")) + notificationsettings.Receiver + "",      F("250 "))) break;
      if (!NPlugin_001_MTA(client, F("DATA"),                             F("354 "))) break;
      if (!NPlugin_001_MTA(client, mailheader + aMesg + String(F("\r\n.\r\n")),   F("250 "))) break;

      myStatus = true;
      break;

    }

    client.flush();
    client.stop();

    if (myStatus == true) {
      addLog(LOG_LEVEL_INFO, F("EMAIL: Connection Closed Successfully"));
    }
    else {
      String log = F("EMAIL: Connection Closed With Error. Used header: ");
      log += mailheader;
      addLog(LOG_LEVEL_ERROR, log);
    }
  }
  return myStatus;
}

boolean NPlugin_001_Auth(WiFiClient& client, String user, String pass) {
  if (user.length() == 0 || pass.length() == 0) {
    // No user/password given.
    return true;
  }
  if (!NPlugin_001_MTA(client, String(F("AUTH LOGIN")), F("334 "))) return false;
  base64 encoder;
  String auth;
  auth = encoder.encode(user);
  if (!NPlugin_001_MTA(client, auth, F("334 "))) return false;
  auth = encoder.encode(pass);
  if (!NPlugin_001_MTA(client, auth, F("235 "))) return false;

  return true;
}

boolean NPlugin_001_MTA(WiFiClient& client, String aStr, const String &aWaitForPattern)
{
  addLog(LOG_LEVEL_DEBUG, aStr);

  if (aStr.length() ) client.println(aStr);

  yield();

  // Wait For Response
  unsigned long timer = millis() + NPLUGIN_001_TIMEOUT;
  while (true) {
    if (timeOutReached(timer)) {
      String log = F("Plugin_001_MTA: timeout. ");
      log += aStr;
      addLog(LOG_LEVEL_ERROR, log);
      return false;
    }

    yield();

    // String line = client.readStringUntil('\n');
    String line;
    safeReadStringUntil(client, line, '\n');

    addLog(LOG_LEVEL_DEBUG, line);

    if (line.indexOf(aWaitForPattern) >= 0) {
      return true;
    }
  }

  return false;
}
