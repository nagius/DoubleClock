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
 
  server.send(200, F("application/json"), getJSONSettings());
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
  ST_SETTINGS st;
  ST_SETTINGS_FLAGS isNew = { false, false, false, false, false, false, false };

  if(!isAuthBasicOK())
    return;

   // Check if args have been supplied
  if(server.args() == 0)
  {
    server.send(400, F("test/plain"), F("Invalid parameters\r\n"));
    return;
  }

  // Parse args   
  for(uint8_t i=0; i<server.args(); i++ ) 
  {
    String param = server.argName(i);
    if(param == "debug")
    {
      st.debug = server.arg(i).equalsIgnoreCase("true");
      isNew.debug = true;
    }
    else if(param == "serial")
    {
      st.serial = server.arg(i).equalsIgnoreCase("true");
      isNew.serial = true;
    }
    else if(param == "login")
    {
      server.arg(i).toCharArray(st.login, AUTHBASIC_LEN);
      isNew.login = true;
    }
    else if(param == "password")
    {
      server.arg(i).toCharArray(st.password, AUTHBASIC_LEN);
      isNew.password = true;
    }
    else if(param == "mqtt-server")
    {
      server.arg(i).toCharArray(st.mqtt_server, AUTHBASIC_LEN);
      isNew.mqtt_server = true;
    }
    else if(param == "hour")
    {
      uint8_t hour = server.arg(i).toInt();
      if(hour>= 0 && hour <=23)
      {
        st.alarm_hr = hour;
        isNew.alarm_hr = true;
      }
      else
      {
        server.send(400, F("test/plain"), F("Invalid hours\r\n"));
        return;
      }
    }
    else if(param == "minutes")
    {
      uint8_t min = server.arg(i).toInt();
      if(min>= 0 && min <=59)
      {
        st.alarm_min = min;
        isNew.alarm_min = true;
      }
      else
      {
        server.send(400, F("test/plain"), F("Invalid minutes\r\n"));
        return;
      }
    }
    else if(param == "delay-chime")
    {
      uint16_t seconds = server.arg(i).toInt();
      if(seconds>= 0)
      {
        st.alarm_chime_delay = seconds;
        isNew.alarm_chime_delay = true;
      }
      else
      {
        server.send(400, F("test/plain"), F("Invalid chime delay\r\n"));
        return;
      }
    }
    else if(param == "delay-buzzer")
    {
      uint16_t seconds = server.arg(i).toInt();
      if(seconds>= 0)
      {
        st.alarm_buzzer_delay = seconds;
        isNew.alarm_buzzer_delay = true;
      }
      else
      {
        server.send(400, F("test/plain"), F("Invalid buzzer delay\r\n"));
        return;
      }
    }
    else
    {
      server.send(400, F("text/plain"), "Unknown parameter: " + param + "\r\n");
      return;
    }
  }

  // Save changes
  if(isNew.debug)
  {
    settings.debug = st.debug;
    logger.setDebug(st.debug);
    logger.info("Updated debug to %s.", st.debug ? "true" : "false");
  }

  if(isNew.serial)
  {
    settings.serial = st.serial;
    logger.setSerial(st.serial);
    logger.info("Updated serial to %s.", st.serial ? "true" : "false");
  }

  if(isNew.login)
  {
    strcpy(settings.login, st.login);
    logger.info("Updated login to \"%s\".", st.login);
  }

  if(isNew.password)
  {
    strcpy(settings.password, st.password);
    logger.info("Updated password.");
  }

  if(isNew.mqtt_server)
  {
    strcpy(settings.mqtt_server, st.mqtt_server);
    setupMQTT();
    logger.info("Updated MQTT serer to \"%s\".", st.mqtt_server);
  }

  if(isNew.alarm_hr)
  {
    settings.alarm_hr = st.alarm_hr;
    logger.info("Updated alarm hours.");
  }

  if(isNew.alarm_min)
  {
    settings.alarm_min = st.alarm_min;
    logger.info("Updated alarm minutes.");
  }

  if(isNew.alarm_chime_delay)
  {
    settings.alarm_chime_delay = st.alarm_chime_delay;
    logger.info("Updated chime delay.");
  }

  if(isNew.alarm_buzzer_delay)
  {
    settings.alarm_buzzer_delay = st.alarm_buzzer_delay;
    logger.info("Updated buzzer delay.");
  }


  saveSettings();

  // Reply with current settings
  server.send(201, F("application/json"), getJSONSettings());
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

char* getJSONSettings()
{
  //Generate JSON 
  snprintf(buffer, BUF_SIZE, "{ \"login\": \"%s\", \"password\": \"<hidden>\", \"mqtt-server\": \"%s\", \"debug\": %s, \"serial\": %s, \"hour\": %i, \"minutes\": %i, \"delay-chime\": %i, \"delay-buzzer\": %i }\r\n",
    settings.login,
    settings.mqtt_server,
    settings.debug ? "true" : "false",
    settings.serial ? "true" : "false",
    settings.alarm_hr,
    settings.alarm_min,
    settings.alarm_chime_delay,
    settings.alarm_buzzer_delay
  );

  return buffer;
}

