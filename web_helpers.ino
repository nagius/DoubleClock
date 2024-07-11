/*
 * This file is part of DoubleClock Arduino sketch
 * All HTTP/Web handlers for ESP8266WebServer are here  for ease of navigation.
 *
 * Global variables are defined in the main ino file.
 */

/**
 * HTTP route handlers
 ********************************************************************************/

/**
 * GET /
 */
void handleGETRoot() 
{
  // I always loved this HTTP code
  server.send(418, F("text/plain"), F("\
            _           \r\n\
         _,(_)._            \r\n\
    ___,(_______).          \r\n\
  ,'__.           \\    /\\_  \r\n\
 /,' /             \\  /  /  \r\n\
| | |              |,'  /   \r\n\
 \\`.|                  /    \r\n\
  `. :           :    /     \r\n\
    `.            :.,'      \r\n\
      `-.________,-'        \r\n\
  \r\n"));
}


/**
 * GET /debug
 */
void handleGETDebug()
{
  if(!isAuthBasicOK())
    return;
 
  server.send(200, F("text/plain"), logger.getLog());
}


/**
 * GET /settings
 */
void handleGETSettings()
{
  if(!isAuthBasicOK())
    return;
 
  sendJSONSettings();
}

/**
 * POST /settings
 * Args :
 *   - debug = <bool>
 *   - login = <str>
 *   - password = <str>
 */
void handlePOSTSettings()
{
  StaticJsonDocument<BUF_SIZE> json;

  if(!isAuthBasicOK())
    return;

  DeserializationError error = deserializeJson(json, server.arg("plain"));
  if(error)
  {
    sendJSONError("deserialize failed: %s", error.c_str());
    return;
  }

  if(json.containsKey("debug"))
  {
    settings.debug = json["debug"];
    logger.setDebug(settings.debug);
    logger.info("Updated debug to %s.", settings.debug ? "true" : "false");
  }

  if(json.containsKey("serial"))
  {
    settings.serial = json["serial"];
    logger.setSerial(settings.serial);
    logger.info("Updated serial to %s.", settings.serial ? "true" : "false");
  }

  if(json.containsKey("ntp"))
  {
    const char* ntp_server = json["ntp"];
    strncpy(settings.ntp_server, ntp_server, DNS_SIZE);
    setServer(settings.ntp_server);
    logger.info("Updated NTP server to \"%s\".", settings.ntp_server);
  }

  if(json.containsKey("login"))
  {
    const char* login = json["login"];
    strncpy(settings.login, login, AUTHBASIC_LEN);
    logger.info("Updated login to \"%s\".", settings.login);
  }

  if(json.containsKey("password"))
  {
    const char* password = json["password"];
    strncpy(settings.password, password, AUTHBASIC_LEN);
    logger.info("Updated password.");
  }

  if(json.containsKey("delays"))
  {
    JsonObject delays = json["delays"];
    if(delays.containsKey("chime"))
    {
      const uint16_t seconds = delays["chime"];
      if(seconds>= 0)
      {
        settings.alarm_chime_delay = seconds;
        logger.info("Updated chime delay to %i.", seconds);
      }
      else
      {
        sendJSONError("Invalid chime delay");
        return;
      }
    }

    if(delays.containsKey("buzzer"))
    {
      const uint16_t seconds = delays["buzzer"];
      if(seconds>= 0)
      {
        settings.alarm_buzzer_delay = seconds;
        logger.info("Updated buzzer delay to %i.", seconds);
      }
      else
      {
        sendJSONError("Invalid buzzer delay");
        return;
      }
    }
  }
  
  if(json.containsKey("mqtt"))
  {
    JsonObject mqtt = json["mqtt"];
    if(mqtt.containsKey("server"))
    {
      const char* server = mqtt["server"];
      strncpy(settings.mqtt_server, server, AUTHBASIC_LEN);
    }

    if(mqtt.containsKey("port"))
    {
      const uint16_t port = mqtt["port"];
      settings.mqtt_port = port;
    }

    setupMQTT();
  }

  if(json.containsKey("alarms"))
  {
    JsonArray alarms = json["alarms"];
    if(alarms.size()>ALARM_COUNT)
    {
      sendJSONError("Invalid number of alarms");
      return;
    }

    for(int i=0; i<alarms.size(); i++)
    {
      if(alarms[i].containsKey("hour"))
      {
        const int hour = alarms[i]["hour"];
        if(hour>= 0 && hour <=23)
        {
          settings.alarms[i].hour = hour;
          logger.info("Updated alarm #%d hour to %d.", i, hour);
        }
        else
        {
          sendJSONError("Invalid hour for alarm #%d", i);
          return;
        }
      }

      if(alarms[i].containsKey("minute"))
      {
        const int minute = alarms[i]["minute"];
        if(minute>= 0 && minute <=59)
        {
          settings.alarms[i].minute = minute;
          logger.info("Updated alarm #%d minute to %d.", i, minute);
        }
        else
        {
          sendJSONError("Invalid minute for alarm #%d", i);
          return;
        }
      }

      if(alarms[i].containsKey("primary"))
      {
        settings.alarms[i].primary = alarms[i]["primary"];
      }

      if(alarms[i].containsKey("days"))
      {
        JsonArray days = alarms[i]["days"];
        if(days.size() != 7)
        {
          sendJSONError("Invalid number of days for alarm #%d", i);
          return;
        }

        for(int j=0; j<7; j++)
        {
          const bool day = days[j];
          settings.alarms[i].days[j] = day;
        }
        logger.info("Updated alarm #%d days", i);
      }
    }
  }

  saveSettings();

  // Reply with current settings
  sendJSONSettings();
}

/**
 * POST /reset
 */
void handlePOSTReset()
{
  WiFiManager wifiManager;
  
  if(!isAuthBasicOK())
    return;

  logger.info("Reset settings to default");
    
  //reset saved settings
  wifiManager.resetSettings();
  setDefaultSettings();
  saveSettings();

  // Send response now
  server.send(200, F("text/plain"), F("Reset OK"));
  
  delay(3000);
  logger.info("Restarting...");
    
  ESP.restart();
}

/**
 *  GET *
 */
bool handleFileRead(String path)
{
  // If a folder is requested, send the index file
  if (path.endsWith("/")) path += "index.html";

  if (LittleFS.exists(path))
  {
    String contentType = getContentType(path);

    File file = LittleFS.open(path, "r");
    size_t sent = server.streamFile(file, contentType);
    file.close();
    logger.debug("Sent %d bytes for file %s", sent, path.c_str());
    return true;
  }

  return false;
}


/**
 * WEB helpers 
 ********************************************************************************/

bool isAuthBasicOK()
{
  // Disable auth if not credential provided
  if(strlen(settings.login) > 0 && strlen(settings.password) > 0)
  {
    if(!server.authenticate(settings.login, settings.password))
    {
      server.requestAuthentication();
      return false;
    }
  }
  return true;
}

void sendJSONSettings()
{
  json_output.clear();
  json_output["login"] = settings.login;
  json_output["debug"] = settings.debug;
  json_output["serial"] = settings.serial;
  json_output["ntp"] = settings.ntp_server;
  json_output["delays"]["chime"] = settings.alarm_chime_delay;
  json_output["delays"]["buzzer"] = settings.alarm_buzzer_delay;
  json_output["mqtt"]["server"] = settings.mqtt_server;
  json_output["mqtt"]["port"] = settings.mqtt_port;

  for(int i=0; i<ALARM_COUNT; i++)
  {
    json_output["alarms"][i]["hour"] = settings.alarms[i].hour;
    json_output["alarms"][i]["minute"] = settings.alarms[i].minute;
    json_output["alarms"][i]["primary"] = settings.alarms[i].primary;
    for(int j=0; j<7; j++)
    {
      json_output["alarms"][i]["days"][j] = settings.alarms[i].days[j];
    }
  }

  serializeJson(json_output, buffer);
  server.send(200, "application/json", buffer);
}

void sendJSONError(const char* fmt, ...)
{
  char msg[BUF_SIZE];

  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, BUF_SIZE, fmt, ap);
  va_end(ap);

  json_output.clear();
  json_output["error"] = msg;
  serializeJson(json_output, buffer);
  server.send(400, "application/json", buffer);
}

String getContentType(String filename)
{
  if (filename.endsWith(".html")) return "text/html";
  else if (filename.endsWith(".css")) return "text/css";
  else if (filename.endsWith(".js")) return "application/javascript";
  else if (filename.endsWith(".ico")) return "image/x-icon";
  return "text/plain";
}

