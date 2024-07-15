/*************************************************************************
 *
 * This file is part of the DoubleClock Arduino sketch.
 * Copyleft 2024 Nicolas Agius <nicolas.agius@lps-it.fr>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * ***********************************************************************/


#include <ESP8266WiFi.h>         
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFiManager.h>              // See https://github.com/tzapu/WiFiManager for documentation
#include <EEPROM.h>
#include <ezTime.h>
#include "Logger.h"
#include "display.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include "Adafruit_LEDBackpack.h"
#include <LittleFS.h>

// Behavior configuration
#define RISING_TIME 180               // seconds to full britghness when alarm triggers
#define ADC_MIN_THRESHOLD 300         // mV from photoresistor in darkness
#define ADC_MAX_THRESHOLD 1000        // mV from photoresistor in sunlight
#define ALARM_COUNT 2                 // Number of alarms
//#define ALLOW_CORS_FOR_LOCAL_DEV

// Default value
#define DEFAULT_HOSTNAME "DoubleClock"
#define DEFAULT_LOGIN ""              // AuthBasic credentials
#define DEFAULT_PASSWORD ""           // (default no auth)
#define DEFAULT_DELAY 0               // Chime and buzzer disabled by default
#define DEFAULT_NTP_SERVER "pool.ntp.org"
#define DEFAULT_MQTT_SERVER "0.0.0.0"
#define DEFAULT_MQTT_PORT 1883
#define DEFAULT_MQTT_TOPIC "doubleclock"
#define DEFAULT_ALARM_TIMEOUT 1800    // 30 minutes
#define DEFAULT_TIMEZONE_A "Europe/London"
#define DEFAULT_TIMEZONE_B "Europe/Madrid"

// Internal constant
#define VERSION "2.0"
#define AUTHBASIC_LEN 21        // Login or password 20 char max
#define BUF_SIZE 512            // Used for string buffers
#define DNS_SIZE 63             // used for DNS names (should be 255 but 63 is enough)
#define PWMRANGE 255            // Max value for pwm ouptut

#define STATE_OFF 1             // Nothing
#define STATE_ON 2              // Light on, no alarm
#define STATE_RING 3            // Alarm trigerred with lights on
#define STATE_RING_CHIME 4      // Alarm trigerred with lights on and music A
#define STATE_RING_BUZZER 5     // Alarm trigerred with lights on and music B

// GPIO configuration
#define I2C_ADDR_TIME_A 0x73    // 7 segments
#define I2C_ADDR_TIME_B 0x72    // 7 segments
#define I2C_ADDR_TEXT_A 0x74    // 13 segments
#define I2C_ADDR_TEXT_B 0x75    // 13 segments
#define GPIO_BUTTON 9           // SD2
#define GPIO_LIGHT 14           // D5
#define GPIO_MUSIC_CHIME 12     // D6
#define GPIO_MUSIC_BUZZER 13    // D7
#define GPIO_MUSIC_OFF 15       // D8
#define GPIO_MUSIC_IN 10        // SD3
#define GPIO_LIGHT_SENSOR A0    // ADC0

struct ST_ALARM {
  uint8_t hour;
  uint8_t minute;
  bool days[7];     // 7 days a week: 0-Monday..6-Sunday
  bool primary;     // Primary is tzA, seconday is tzB
};

struct ST_SETTINGS {
  bool debug;
  bool serial;
  char login[AUTHBASIC_LEN];
  char password[AUTHBASIC_LEN];
  char ntp_server[DNS_SIZE];
  char mqtt_server[DNS_SIZE];
  uint16_t mqtt_port;
  uint16_t alarm_chime_delay;
  uint16_t alarm_buzzer_delay;
  ST_ALARM alarms[ALARM_COUNT];
};

// Global variables
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
ESP8266WebServer server(80);
Logger logger = Logger();
ST_SETTINGS settings;
bool shouldSaveConfig = false;    // Flag for WifiManager custom parameters
char buffer[BUF_SIZE];            // Global char* to avoir multiple String concatenation which causes RAM fragmentation
StaticJsonDocument<BUF_SIZE> json_output;

Adafruit_AlphaNum4 alnumA = Adafruit_AlphaNum4();
Adafruit_AlphaNum4 alnumB = Adafruit_AlphaNum4();
Adafruit_7segment timeA = Adafruit_7segment();
Adafruit_7segment timeB = Adafruit_7segment();
Display textA = Display(alnumA);
Display textB = Display(alnumB);
Timezone tzA;
Timezone tzB;

uint8_t state = STATE_OFF;
long alarm_start_time = 0;        // counter for alarm delay
bool previous_button = false;     // Used for debouncing
long last_display_refresh = 0L;   // Timer to refresh displays every seconds
bool dot;                         // Flip-flop for dot display


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

// Return the ideal brightness for the display between 0 (min) and 15 (max)
// based on the photoresistor value
uint8_t get_brightness()
{
  // Sensor in is the dark at 300mV and full light at 1024mV
  // Convert mV to brightness level with affine function y=ax+b
  // Low point (MIN, 0); high point (MAX, 15)
  
  // a = (y2 – y1)/(x2 – x1)
  float a = (15.0 - 0.0) / ( ADC_MAX_THRESHOLD - ADC_MIN_THRESHOLD ) ;

  // b = y1 - ax1
  float b = 0.0 -(a * ADC_MIN_THRESHOLD);
  
  int x = analogRead(A0);
  float y = ((a * x) + b);
  
  if(y<0)
  {
    return 0;
  }
  else if(y>15)
  {
    return 15;
  }
  return (int)y;
}


/**
 * MQTT helpers
 ********************************************************************************/

void setupMQTT()
{
  if(is_mqtt_enabled())
  {
    textA.setMsg("MQTT");
    mqtt.setServer(settings.mqtt_server, settings.mqtt_port);
    logger.info("MQTT enabled at %s:%i", settings.mqtt_server, settings.mqtt_port);
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
    logger.info("Connecting to MQTT server...");
    if (mqtt.connect("ESP8266Client"))
    {
      logger.info("MQTT connected.");
    }
    else
    {
      logger.info("MQTT ERROR (%i). Retry in 5s...", mqtt.state());
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
      logger.info("MQTT publish failed");
    }
  }
}

/**
 * Time helpers 
 ********************************************************************************/


void alarm()
{
  if(state == STATE_ON || state == STATE_OFF)
  {
    logger.info("Alarm trigerred");
    state = STATE_RING;
    alarm_start_time = millis();
    mqtt_publish("alarm");
  }
}

void stop_alarm()
{
  if(state >= STATE_RING)
    logger.info("Alarm stopped");

  state = STATE_OFF;
  light_off();
  music_off();
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
    if(state >= STATE_RING) {
      textA.setMsg("WAKE UP   ");
      textB.setMsg(" today is " + dayStr(tzA.weekday()) + "  ");
    } else {
      textA.setMsg(tzA.dateTime("T"));
      textB.setMsg(tzB.dateTime("T"));
    }
    textA.tick();
    textB.tick();

    dot=!dot;
  
    timeA.print(tzA.hour()*100 + tzA.minute());
    timeA.drawColon(dot);
    timeA.writeDisplay();

    timeB.print(tzB.hour()*100 + tzB.minute());
    timeB.drawColon(dot);
    timeB.writeDisplay();
}


void setupDisplays()
{
  alnumA.begin(I2C_ADDR_TEXT_A);
  alnumA.clear();
  alnumA.writeDisplay();
  textA = Display(alnumA);

  alnumB.begin(I2C_ADDR_TEXT_B);
  alnumB.clear();
  alnumB.writeDisplay();
  textB = Display(alnumB);

  timeA.begin(I2C_ADDR_TIME_A);
  timeA.clear();
  timeA.writeDisplay();

  timeB.begin(I2C_ADDR_TIME_B);
  timeB.clear();
  timeB.writeDisplay();

  setBrightness();
}

void setBrightness()
{
  uint8_t b = get_brightness();
  alnumA.setBrightness(b);
  alnumB.setBrightness(b);    
  timeA.setBrightness(b);
  timeB.setBrightness(b);    
}

void setupNTP()
{
  textA.setMsg("NTP");
  tzA.setLocation(DEFAULT_TIMEZONE_A);
  tzB.setLocation(DEFAULT_TIMEZONE_B);
  setDebug(INFO);   // ezTime debug
  setServer(settings.ntp_server);
  waitForSync(60);  // 60s timeout on initial NTP request
}

void setupMDNS()
{
  textA.setMsg("mDNS");
  if(MDNS.begin(DEFAULT_HOSTNAME))
  {
    MDNS.addService("http", "tcp", 80);
    logger.info("mDNS activated.");
  }
  else
  {
    logger.info("Error setting up mDNS responder");
  }
  delay(500);
}

void setup()
{
  WiFiManager wifiManager;
  
  Serial.begin(115200);
  EEPROM.begin(512);
  if(!LittleFS.begin())
  {
    logger.info("ERROR: LittleFS failed");
    return;
  }

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
  setupDisplays();

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
  textA.setMsg("WIFI");
  wifiManager.setHostname(DEFAULT_HOSTNAME);
  wifiManager.autoConnect(DEFAULT_HOSTNAME);

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
#ifdef ALLOW_CORS_FOR_LOCAL_DEV
  server.enableCORS(true);
  server.on("/settings", HTTP_OPTIONS, []() {
    server.sendHeader("Access-Control-Allow-Headers", "*");
    server.sendHeader("Access-Control-Allow-Methods", "POST,GET,OPTIONS");
    server.send(204);
 });
#endif
  server.on("/", handleGETRoot );
  server.on("/debug", HTTP_GET, handleGETDebug);
  server.on("/settings", HTTP_GET, handleGETSettings);
  server.on("/settings", HTTP_POST, handlePOSTSettings);
  server.on("/reset", HTTP_POST, handlePOSTReset);
  server.onNotFound([]() {
    // Serve arbitrary requested file from LittleFS if exist
    if (!handleFileRead(server.uri()))
      server.send(404, "text/plain", "Not found\r\n");
  });
  server.begin();
  
  logger.info("HTTP server started.");

  setupNTP();
  setupMQTT();
  setupMDNS();
}

void loop()
{
  MDNS.update();
  server.handleClient();
  events();  // EZTime events
  mqtt_loop();

  if(isButtonPressed())
  {
    if(state == STATE_OFF)
    {
        state = STATE_ON;
        light_on(255);
        mqtt_publish("on");
    }
    else
    {
        stop_alarm();
        mqtt_publish("off");
    }
  } 
  else if(state >= STATE_RING)
  {
    long alarm_duration_ms = (millis() - alarm_start_time);
    
    if(state == STATE_RING && settings.alarm_chime_delay != 0 && alarm_duration_ms >= settings.alarm_chime_delay*1000)
    {
      logger.debug("ALARM chime on");
      state = STATE_RING_CHIME;
      mqtt_publish("alarm-chime");
    }

    if(state == STATE_RING_CHIME && settings.alarm_buzzer_delay != 0 && alarm_duration_ms >= (settings.alarm_chime_delay+settings.alarm_buzzer_delay)*1000)
    {
      logger.debug("ALARM buzzer on");
      state = STATE_RING_BUZZER;
      music_off();
      mqtt_publish("alarm-buzzer");
    }

    if(alarm_duration_ms >= DEFAULT_ALARM_TIMEOUT*1000)
    {
      stop_alarm();
      mqtt_publish("alarm-cancelled");
    }

    ring();
  }
  else
  {
    // Trigger alarm
    if(tzA.second() == 0) // Only trigger alarm during one second per minute
    {
      for(int i; i<ALARM_COUNT; i++)
      {
        tmElements_t tm;
        if(settings.alarms[i].primary)
        {
          // use breakTime to get day, hour and minute in an atomic way
          breakTime(tzA.now(), tm);
        }
        else
        {
          breakTime(tzB.now(), tm);
        }

        // today is Monday = 0 Sunday = 7; tm.Wday is Sunday = 1
        int today = (tm.Wday + 5) % 7;
        if(settings.alarms[i].days[today] && settings.alarms[i].hour == tm.Hour && settings.alarms[i].minute == tm.Minute)
        {
          alarm();
          break;
        }
      }
    }
  }

  long current_millis = millis();
  if(current_millis - last_display_refresh > 1000)
  {
    setBrightness();
    refresh_displays();
    
    // update the timing variable
    last_display_refresh = current_millis;
  }
}
