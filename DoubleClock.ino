// Double clock

// TODO reome Ireland Span TZA TZB
// TODO move Settings to other file .h
// TODO add mqtt dans wifimanager
// Siplify settings with https://www.arduino.cc/reference/en/libraries/wifimqttmanager-library/


#include <ESP8266WiFi.h>         
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <PubSubClient.h>
#include <WiFiManager.h>         // See https://github.com/tzapu/WiFiManager for documentation
#include <EEPROM.h>
#include <ezTime.h>
#include "Logger.h"
#include "display.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include "Adafruit_LEDBackpack.h"


// Default value
#define DEFAULT_LOGIN ""        // AuthBasic credentials
#define DEFAULT_PASSWORD ""     // (default no auth)
#define DEFAULT_DELAY 0         // Chime and buzzer disabled by default
#define DEFAULT_MQTT_SERVER "0.0.0.0"
#define DEFAULT_MQTT_PORT 1883
#define DEFAULT_MQTT_TOPIC "doubleclock"


// Internal constant
#define AUTHBASIC_LEN 21        // Login or password 20 char max
#define BUF_SIZE 256            // Used for string buffers
#define VERSION "1.2"

#define STATE_OFF 1             // Nothing
#define STATE_ON 2              // Light on, no alarm
#define STATE_RING 3            // Alarm trigerred with lights on
#define STATE_RING_CHIME 4      // Alarm trigerred with lights on and music A
#define STATE_RING_BUZZER 5     // Alarm trigerred with lights on and music B

#define DISP1_ADDRESS 0x72  // 7 segments
#define DISP2_ADDRESS 0x73  // 7 segments
#define DISP3_ADDRESS 0x74  // 13 segments
#define DISP4_ADDRESS 0x75  // 13 segments
#define GPIO_BUTTON 9        // SD2
#define GPIO_LIGHT 14        // D5
#define GPIO_MUSIC_CHIME 12  // D6
#define GPIO_MUSIC_BUZZER 13 // D7
#define GPIO_MUSIC_OFF 15    // D8
#define GPIO_MUSIC_IN 10     // SD3

#define PWMRANGE 255 // 1023
#define RISING_TIME 180 // seconde

struct ST_SETTINGS {
  bool debug;
  bool serial;
  char login[AUTHBASIC_LEN];
  char password[AUTHBASIC_LEN];
  char mqtt_server[AUTHBASIC_LEN];  // TODO use right size for DNS
  uint8_t alarm_hr;
  uint8_t alarm_min;
  uint16_t alarm_chime_delay;
  uint16_t alarm_buzzer_delay;
};

struct ST_SETTINGS_FLAGS {
  bool debug;
  bool serial;
  bool login;
  bool password;
  bool mqtt_server;
  bool alarm_hr;
  bool alarm_min;
  bool alarm_chime_delay;
  bool alarm_buzzer_delay;
};


// Global variables
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
ESP8266WebServer server(80);
Logger logger = Logger();
ST_SETTINGS settings;
bool shouldSaveConfig = false;    // Flag for WifiManager custom parameters
char buffer[BUF_SIZE];            // Global char* to avoir multiple String concatenation which causes RAM fragmentation
Timezone Ireland;
Timezone Spain;

Adafruit_AlphaNum4 displayA = Adafruit_AlphaNum4();
Adafruit_7segment timeA = Adafruit_7segment();
Display tzA = Display(displayA);
Adafruit_AlphaNum4 displayB = Adafruit_AlphaNum4();
Adafruit_7segment timeB = Adafruit_7segment();
Display tzB = Display(displayB);

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
    strcpy(settings.mqtt_server, DEFAULT_MQTT_SERVER);
    settings.debug = true;
    settings.serial = true;
    settings.alarm_hr = 255;
    settings.alarm_min = 255;
    settings.alarm_chime_delay = DEFAULT_DELAY;
    settings.alarm_buzzer_delay = DEFAULT_DELAY;
}

/**
 * GPIO helpers 
 ********************************************************************************/

void light_off()
{
  analogWrite(GPIO_LIGHT, 0);
}

void light_on(int duty_cycle)
{
  analogWrite(GPIO_LIGHT, duty_cycle);
}

bool isButtonPressed()
{
  // Detect rising edge
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

void music_off()
{
  if(is_music_playing())
  {
    digitalWrite(GPIO_MUSIC_OFF, HIGH);
    delay(250);
    digitalWrite(GPIO_MUSIC_OFF, LOW);
  }
}

void music_buzzer_on()
{
  digitalWrite(GPIO_MUSIC_BUZZER, HIGH);
  delay(250);
  digitalWrite(GPIO_MUSIC_BUZZER, LOW);
}

void music_chime_on()
{
  digitalWrite(GPIO_MUSIC_CHIME, HIGH);
  delay(250);
  digitalWrite(GPIO_MUSIC_CHIME, LOW);
}


bool is_music_playing()
{
  return digitalRead(GPIO_MUSIC_IN) == HIGH;
}

/**
 * MQTT helpers
 ********************************************************************************/

void setupMQTT()
{
  if(is_mqtt_enabled())
  {
    mqtt.setServer(settings.mqtt_server, DEFAULT_MQTT_PORT);
    logger.info("MQTT enabled at %s:%i", settings.mqtt_server, DEFAULT_MQTT_PORT);
  }
}


bool is_mqtt_enabled()
{
  return strcmp(settings.mqtt_server, "0.0.0.0") != 0;
}

void mqtt_connect()
{
  while (!mqtt.connected())
  {
    Serial.print("Connect to MQTT server...");
    if (mqtt.connect("ESP8266Client"))
    {
      Serial.println("OK");
    }
    else
    {
      Serial.print("Err: ");
      Serial.println(mqtt.state());
      Serial.println("Retry in 5s...");
      delay(5000);
    }
  }
}

void mqtt_loop()
{
  if(is_mqtt_enabled())
  {
    // MQTT reconnection
    if(!mqtt.loop())
    {
      mqtt_connect();
    }
  }
}

void mqtt_publish(char* event)
{
  if(is_mqtt_enabled())
  {
    snprintf(buffer, BUF_SIZE, "{ \"event\": \"%s\" }", event);
    logger.debug("Sending MQTT payload '%s' on topic %s", buffer, DEFAULT_MQTT_TOPIC);
    if(!mqtt.publish(DEFAULT_MQTT_TOPIC, buffer, true))
    {
      Serial.println("MQTT publish failed");
    }
  }
}

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

void ring()
{
  long alarm_duration = (millis() - alarm_start_time);

  // Switch on PWM light
  int duty_cycle = alarm_duration * PWMRANGE / (RISING_TIME * 1000);
 
  if(duty_cycle > PWMRANGE)
    duty_cycle=PWMRANGE;

  light_on(duty_cycle);

  // Switch on music
  if(state == STATE_RING_CHIME)
  {
    if(!is_music_playing() && settings.alarm_chime_delay!=0)
      music_chime_on();
  }

  if(state == STATE_RING_BUZZER)
  {
    if(!is_music_playing() && settings.alarm_buzzer_delay!=0)
      music_buzzer_on();
  }
  
}

void refresh_displays()
{
    if(is_music_playing())
      logger.debug("pusic on");
    
    logger.debug("state=%d", state);
      
    //logger.info("Dublin time: %s", Ireland.dateTime().c_str());
    //logger.info("Madrid time: %s", Spain.dateTime().c_str());
 //   Serial.println("UTC RFC822:           " + UTC.dateTime(RFC822));
   // Serial.println("UTC TZ:    " + UTC.dateTime("T"));
    
   // Serial.println("Europe/Dublin RFC822: " + Ireland.dateTime(RFC822));
    //Serial.println("Dublin TZ: " + Ireland.dateTime("T"));

        
    //logger.info("%i:%i %s", Ireland.hour(), Ireland.minute(), Ireland.dateTime(RFC822));
    //logger.info("%i:%i %s", Spain.hour(), Spain.minute(), Spain.dateTime(RFC822));

    if(state >= STATE_RING) {
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

  // Setup GPIO
  pinMode(GPIO_MUSIC_CHIME, OUTPUT);
  digitalWrite(GPIO_MUSIC_CHIME, LOW);
  pinMode(GPIO_MUSIC_BUZZER, OUTPUT);
  digitalWrite(GPIO_MUSIC_BUZZER, LOW);
  pinMode(GPIO_MUSIC_OFF, OUTPUT);
  music_off();
  pinMode(GPIO_MUSIC_IN, INPUT);
  pinMode(GPIO_BUTTON, INPUT_PULLUP);
  pinMode(GPIO_LIGHT, OUTPUT);
  light_off();

    
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
  wifiManager.autoConnect("DoubleClock");

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

  setupDisplays();
  setupNTP();
  setupMQTT();
}

void loop()
{
  server.handleClient();
  events();  // EZTime events
  mqtt_loop();

  if(isButtonPressed())
  {
    logger.debug("state=%d", state);
    if(state == STATE_OFF)
    {
        state = STATE_ON;
        light_on(255);
        mqtt_publish("on");
    }
    else
    {
        state = STATE_OFF;
        light_off();
        music_off();
        mqtt_publish("off");
    }
  } 
  else if(state >= STATE_RING)
  {
    long alarm_duration = (millis() - alarm_start_time);
    
    if(state == STATE_RING && alarm_duration >= settings.alarm_chime_delay*1000)
    {
      logger.debug("swtcih ring chime");
      state = STATE_RING_CHIME;
      mqtt_publish("alarm-chime");
    }

    if(state == STATE_RING_CHIME && alarm_duration >= (settings.alarm_chime_delay+settings.alarm_buzzer_delay)*1000)
    {
      logger.debug("swtcih ring chime");
      state = STATE_RING_BUZZER;
      music_off();
      mqtt_publish("alarm-buzzer");
    }

    ring();
  }
  else
  {
    // Trigger alarm
    tmElements_t tm;
    breakTime(Ireland.now(), tm);  // use breakTime to get hour, minute and second in an atomic way
    if(tm.Hour == settings.alarm_hr && tm.Minute == settings.alarm_min && tm.Second == 00)
    {
      if(tm.Wday != SUNDAY && tm.Wday != SATURDAY)
      {
        alarm();
        mqtt_publish("alarm");
      }
    }
  }

  long current_millis = millis();
  if(current_millis - last_display_refresh > 1000)
  {
    refresh_displays();
    
    // update the timing variable
    last_display_refresh = current_millis;
  }
}
