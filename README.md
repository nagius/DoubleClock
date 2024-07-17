# DoubleClock 

## Concept

DoubleClock is a dual timezone alarm clock, with sunrise simulation and music player

Beside displaying two clocks with their respective timezones, this connected alarm clock is meant to provide a soft and natural wake-up at the end of your sleep cycle, like the Philips Wake-up Light but not overpriced.
After a certain delay, a music can be played (like sound of birds or any MP3) to make sure you wake up gradually. 

The choice of red 7 and 14 segments 4-digits display is to give a retro look, 80's style (you can choose any color for your displays, as long as it works with i2c protocol).

This alarm clock can also act as a bedside light if the led strip is powerful enough. It has been designed for 12v or 24v led strip but any voltage should do.

A model for a 3D printed case is available in the `Case/` directory.

## Features

 - Single control button
 - Captive portal on first boot to configure Wifi
 - Sunrise simulation (PWM) for a smooth wake-up.
 - Light sensor for adaptive britghness
 - Configurable numbers of alarms (need recompilation)
 - Support for two music file slots (Chime and Buzzer)
 - Configurable delays for Chime music and Buzzer music when the alarm is on
 - mDNS advertisement at `doubleclock.local`
 - MQTT connection to send events (alarm on, alarm off...)
 - Custom NTP server (to avoid cloud dependencies)
 - JSON API and web page configuration

## Configuration via web interface

A simple web page is available at `http://doubleclock.local/index.html` to configure the alarms.

Javascript required.

## API

A JSON API is available for configuration at `http://doubleclock.local/settings`.
```json
{
  "login": "",                  # Authbasic. Password not displayed.
  "debug": true,
  "serial": true,               # Send logs on serial port
  "ntp": "pool.ntp.org",
  "delays": {
    "chime": 200,               # Seconds after alarm has been triggered
    "buzzer": 500               # Seconds after Chime has been triggered
  },
  "mqtt": {
    "server": "192.168.1.1",    # Disabled if 0.0.0.0
    "port": 1883
  },
  "alarms": [
    {
      "hour": 9,
      "minute": 31,
      "primary": true,          # primary means the alarm time is against the primary timezone.
      "days": [
        true,                   # Monday
        false,                  # Tuesday
        false,                  # ...
        false,
        false,
        false,
        false
      ]
    },
    {
      "hour": 6,
      "minute": 52,
      "primary": false,
      "days": [
        false,
        true,
        false,
        false,
        false,
        false,
        false
      ]
    }
  ]
}

```

To update the configuration, POST the same payload with the new values. All settings are stored into flash memory and are persistent upon reboot and power loss.

## Authentication

DoubleClock support HTTP Auth Basic authentication (but not https). By default, there is no login required. To enable authentication, set the login and password usng the settings API: 

```
curl -X POST http://doubleclock.local/settings -d '{"login": "myuser", "password":"secret"}'
```

## Bill of materials

- ESP-12E (Node MCU board)
- YS-M3 (Mp3 player board)
- 2x 7-segments 4 digits with HT16K33 backback
- 2x 14-segments 4 digits with HT16K33 backback
- 1x Mosfet BS170
- 3x NPN 2N3904
- Photoresistor
- Resistors : 200, 1k, 4.7k, 10k, 22k, 39k
- Speaker
- Push switch
- Led strip
- 5v regulator board (if led strip not in 5v)

See [Schematics](docs/schematics.png) for more details.

## Compilation and upload

Compile this sketch with Arduino IDE and select board `NodeMCU 1.0 (ESP-12E)`.

To upload the static assets to the flash filesystem, you need to install the plugin [arduino-littlefs-upload](https://github.com/earlephilhower/arduino-littlefs-upload). Then hit `ctrl-shift-p` and select `Upload LittleFS to pico/ESP8266/ESP32`. Make sure the serial console is closed before.

## License

Copyleft 2024 - Nicolas AGIUS - GNU GPLv3
