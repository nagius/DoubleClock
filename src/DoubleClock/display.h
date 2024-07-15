

#ifndef DISPLAY_H
#define DISPLAY_H

#include "Arduino.h"
#include <Adafruit_GFX.h>
#include "Adafruit_LEDBackpack.h"

#define DISPLAY_SIZE 4

class Display
{
  private:
  
    String _msg;
    char _buffer[DISPLAY_SIZE];
    int _index = 0;
    Adafruit_AlphaNum4 _display;

  public:
  
    Display(Adafruit_AlphaNum4 display);
    
    void setMsg(String msg);
    void tick();

  private:
  
    void clearBuffer();
    void write();
    
};

#endif  // DISPLAY_H
