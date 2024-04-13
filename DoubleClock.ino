// Double clock



#include <ESP8266WiFi.h>         
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>         // See https://github.com/tzapu/WiFiManager for documentation
#include <EEPROM.h>
#include <ezTime.h>
#include "Logger.h"
#include <Wire.h>
//#include <Adafruit_MCP23X17.h>
#include <Adafruit_GFX.h>
#include "Adafruit_LEDBackpack.h"


// Default value
#define DEFAULT_LOGIN ""        // AuthBasic credentials
#define DEFAULT_PASSWORD ""     // (default no auth)

// Internal constant
#define AUTHBASIC_LEN 21        // Login or password 20 char max
#define BUF_SIZE 256            // Used for string buffers
#define VERSION "1.2"

#define STATE_OFF 1
#define STATE_RING 2
#define STATE_ON 3

#define DISP1_ADDRESS 0x72  // 7 segments
#define DISP2_ADDRESS 0x73  // 7 segments
#define DISP3_ADDRESS 0x74  // 13 segments
#define DISP4_ADDRESS 0x75  // 13 segments
#define GPIO_BUTTON 12
#define GPIO_LIGHT 14
#define PWMRANGE 255 // 1023
#define RISING_TIME 180 // seconde

struct ST_SETTINGS {
  bool debug;
  bool serial;
  char login[AUTHBASIC_LEN];
  char password[AUTHBASIC_LEN];
  // TODO  manage multiple alarm with TZ
  uint8_t alarm_hr;
  uint8_t alarm_min;
};

struct ST_SETTINGS_FLAGS {
  bool debug;
  bool serial;
  bool login;
  bool password;
  bool alarm_hr;
  bool alarm_min;
};


// Global variables
ESP8266WebServer server(80);
Logger logger = Logger();
ST_SETTINGS settings;
bool shouldSaveConfig = false;    // Flag for WifiManager custom parameters
char buffer[BUF_SIZE];            // Global char* to avoir multiple String concatenation which causes RAM fragmentation
Timezone Ireland;
Timezone Spain;

Adafruit_AlphaNum4 displayA = Adafruit_AlphaNum4();
Adafruit_7segment timeA = Adafruit_7segment();
Display tzA;
Adafruit_AlphaNum4 displayB = Adafruit_AlphaNum4();
Adafruit_7segment timeB = Adafruit_7segment();
Display tzB;

//Adafruit_MCP23X17 mcp;

long last_display_refresh = 0L;
bool dot;

// TODO move to struct
uint8_t state = STATE_OFF;
long alarm_start_time = 0;
bool previous_button = false;
// add alarm pointer ?


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
  ST_SETTINGS_FLAGS isNew = { false, false, false, false };

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
    else if(param == "minute")
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
  snprintf(buffer, BUF_SIZE, "{ \"login\": \"%s\", \"password\": \"<hidden>\", \"debug\": %s, \"serial\": %s, \"hour\": %i, \"minutes\": %i }\r\n",
    settings.login,
    settings.debug ? "true" : "false",
    settings.serial ? "true" : "false",
    settings.alarm_hr,
    settings.alarm_min
  );

  return buffer;
}

/**
 * Flash memory helpers 
 ********************************************************************************/

// CRC8 simple calculation
// Based on https://github.com/PaulStoffregen/OneWire/blob/master/OneWire.cpp
uint8_t crc8(const uint8_t *addr, uint8_t len)
{
  uint8_t crc = 0;

  while (len--) {
    uint8_t inbyte = *addr++;
    for (uint8_t i = 8; i; i--) {
      uint8_t mix = (crc ^ inbyte) & 0x01;
      crc >>= 1;
      if (mix) crc ^= 0x8C;
      inbyte >>= 1;
    }
  }
  return crc;
}

void saveSettings()
{
  uint8_t buffer[sizeof(settings) + 1];  // Use the last byte for CRC

  memcpy(buffer, &settings, sizeof(settings));
  buffer[sizeof(settings)] = crc8(buffer, sizeof(settings));

  for(int i=0; i < sizeof(buffer); i++)
  {
    EEPROM.write(i, buffer[i]);
  }
  EEPROM.commit();
}

void loadSettings()
{
  uint8_t buffer[sizeof(settings) + 1];  // Use the last byte for CRC

  for(int i=0; i < sizeof(buffer); i++)
  {
    buffer[i] = uint8_t(EEPROM.read(i));
  }

  // Check CRC
  if(crc8(buffer, sizeof(settings)) == buffer[sizeof(settings)])
  {
    memcpy(&settings, buffer, sizeof(settings));
    logger.setDebug(settings.debug);
    logger.setSerial(settings.serial);
    logger.info("Loaded settings from flash");

    // Display loaded setting on debug
    logger.debug("FLASH: %s", getJSONSettings());
  }
  else
  {
    logger.info("Bad CRC, loading default settings.");
    setDefaultSettings();
    saveSettings();
  }
}

void setDefaultSettings()
{
    strcpy(settings.login, DEFAULT_LOGIN);
    strcpy(settings.password, DEFAULT_PASSWORD);
    settings.debug = true;
    settings.serial = true;
    settings.alarm_hr = 255;
    settings.alarm_min = 255;
}

/**
 * General helpers 
 ********************************************************************************/



/**
 * Time helpers 
 ********************************************************************************/


void alarm()
{
  if(state != STATE_RING)
  {
    logger.info("Alarm trigerred");
    state = STATE_RING;
    alarm_start_time = millis();
  }
}

void light_off()
{
  analogWrite(GPIO_LIGHT, 0);
}

void light_on(int duty_cycle)
{
  analogWrite(GPIO_LIGHT, duty_cycle);
}

void ring()
{
  long alarm_duration = (millis() - alarm_start_time);

  int duty_cycle = alarm_duration * PWMRANGE / (RISING_TIME * 1000);
 
  if(duty_cycle > PWMRANGE)
    duty_cycle=PWMRANGE;

//  logger.debug("Alarm duration= %i duty_cycle=%i", alarm_duration/1000, duty_cycle);

  light_on(duty_cycle);

  // if alarm_duration > 2eme alarm -> piou piou
}

/*

// i between 0 and 7
void musicOn(uint8_t i)
{
  Serial.println("on");
  mcp.digitalWrite(i, LOW);
  delay(1000);
  mcp.digitalWrite(i, HIGH);
}

void musicOff()
{
  Serial.println("off");
  mcp.digitalWrite(10, LOW);
  delay(100);
  mcp.digitalWrite(10, HIGH);
}
*/

// Detect rising edge
bool isButtonPressed()
{
  bool pressed = !digitalRead(GPIO_BUTTON);
  if(pressed)
  {
    if(!previous_button)
    {
      logger.info("Button pressed");
      previous_button=true;
      return true;
    }
  }
  else
  {
    previous_button = false;
  }
  
  return false;
}

void setupDisplays()
{
  displayA.begin(DISP3_ADDRESS);
  displayA.setBrightness(0);
  displayA.clear();
  displayA.writeDisplay();
  tzA = Display(displayA);

  displayB.begin(DISP4_ADDRESS);
  displayB.setBrightness(0);
  displayB.clear();
  displayB.writeDisplay();
  tzB = Display(displayB);

  timeA.begin(DISP2_ADDRESS);
  timeA.setBrightness(0);
  timeA.clear();
  timeA.writeDisplay();

  timeB.begin(DISP1_ADDRESS);
  timeB.setBrightness(0);
  timeB.clear();
  timeB.writeDisplay();
  
}

/*
void setupMCP()
{
   if (!mcp.begin_I2C()) {
    Serial.println("Error MCP.");
  }


  // TODO faire ca mieux
  for(int i=0; i <15; i++)
  {
    mcp.pinMode(i, OUTPUT);
    mcp.digitalWrite(i, HIGH);
  }
  
  mcp.pinMode(8, INPUT);
  mcp.pinMode(9, INPUT);
  
}
*/
void setupNTP()
{
  Ireland.setLocation("Europe/London");
  Spain.setLocation("Europe/Madrid");
  //setInterval(300);  // 5 minutes  - default 30 minutes
  setDebug(INFO);
  // TODO display error
  waitForSync(60); // 60s timeout on initial NTP request
}

void setup()
{
  WiFiManager wifiManager;
  
  Serial.begin(115200);
  EEPROM.begin(512);
  logger.info("DoubleClock version %s started.", VERSION);
//  setupMCP();
    
  // Load settigns from flash
  loadSettings();
  
  // Configure custom parameters
  WiFiManagerParameter http_login("htlogin", "HTTP Login", settings.login, AUTHBASIC_LEN);
  WiFiManagerParameter http_password("htpassword", "HTTP Password", settings.password, AUTHBASIC_LEN, "type='password'");
  wifiManager.setSaveConfigCallback([](){
    shouldSaveConfig = true;
  });
  wifiManager.addParameter(&http_login);
  wifiManager.addParameter(&http_password);
  
  // Connect to Wifi or ask for SSID
  wifiManager.autoConnect("RemoteRelay");

  // Save new configuration set by captive portal
  if(shouldSaveConfig)
  {
    strncpy(settings.login, http_login.getValue(), AUTHBASIC_LEN);
    strncpy(settings.password, http_password.getValue(), AUTHBASIC_LEN);

    logger.info("Saving new config from portal web page");
    saveSettings();
  }

  // Display local ip
  logger.info("Connected. IP address: %s", WiFi.localIP().toString().c_str());

  // Setup HTTP handlers
  server.on("/", handleGETRoot );
  server.on("/debug", HTTP_GET, handleGETDebug);
  server.on("/settings", HTTP_GET, handleGETSettings);
  server.on("/settings", HTTP_POST, handlePOSTSettings);
  server.on("/reset", HTTP_POST, handlePOSTReset);
  server.onNotFound([]() {
    server.send(404, "text/plain", "Not found\r\n");
  });
  server.begin();
  
  logger.info("HTTP server started.");

  pinMode(GPIO_BUTTON, INPUT_PULLUP);
  pinMode(GPIO_LIGHT, OUTPUT);
  light_off();

  setupDisplays();
  setupNTP();
}

void loop()
{
  server.handleClient();
  events();  // EZTime events


  if(isButtonPressed())
  {
    switch(state) 
    {
      case STATE_ON:
      case STATE_RING:
        state = STATE_OFF;
        light_off();
//        musicOff();
        break;
      case STATE_OFF:
        state = STATE_ON;
        light_on(255);
//          alarm();
//        musicOn(3);
        break;
    }
  } 
  else if(state == STATE_RING)
  {
    ring();
  }

  // Trigger alarm
  tmElements_t tm;
  breakTime(Ireland.now(), tm);  // use breakTime to get hour, minute and second in an atomic way
  if(tm.Hour == settings.alarm_hr && tm.Minute == settings.alarm_min && tm.Second == 00)
  {
    if(tm.Wday != SUNDAY && tm.Wday != SATURDAY)
    {
      alarm();
    }
  }

  long current_millis = millis();

  if(current_millis - last_display_refresh > 1000)
  {
    //logger.info("Dublin time: %s", Ireland.dateTime().c_str());
    //logger.info("Madrid time: %s", Spain.dateTime().c_str());
 //   Serial.println("UTC RFC822:           " + UTC.dateTime(RFC822));
   // Serial.println("UTC TZ:    " + UTC.dateTime("T"));
    
   // Serial.println("Europe/Dublin RFC822: " + Ireland.dateTime(RFC822));
    //Serial.println("Dublin TZ: " + Ireland.dateTime("T"));

        
    //logger.info("%i:%i %s", Ireland.hour(), Ireland.minute(), Ireland.dateTime(RFC822));
    //logger.info("%i:%i %s", Spain.hour(), Spain.minute(), Spain.dateTime(RFC822));

    if(state == STATE_RING) {
      tzA.setMsg("WAKE UP    ");
      tzB.setMsg("    Today is " + Ireland.day());  // TODO verfifier syntaxe
    } else {
      tzA.setMsg(Ireland.dateTime("T"));
      tzB.setMsg(Spain.dateTime("T"));
    }
    tzA.tick();
    tzB.tick();

    dot=!dot;
  
  
    timeA.print(Ireland.hour()*100 + Ireland.minute());
    //timeA.print(String(Ireland.hour()*100 + Ireland.minute()));
    timeA.drawColon(dot);
    timeA.writeDisplay();

    //char buff[5];
    //snprintf(buff, 4, "%02i%02i", Spain.hour(), Spain.minute());
    //timeB.print(buff);
    timeB.print(Spain.hour()*100 + Spain.minute());
    timeB.drawColon(dot);
    timeB.writeDisplay();
  
    // update the timing variable
    last_display_refresh = current_millis;
  }
}
