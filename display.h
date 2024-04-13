

#ifndef DISPLAY_H
#define DISPLAY_H

#include "Arduino.h"
#include <Adafruit_GFX.h>
#include "Adafruit_LEDBackpack.h"

#define DISPLAY_SIZE 4

class Display
{
  private:
  
    String msg;
    char buffer[DISPLAY_SIZE];
    int index = 0;
    Adafruit_AlphaNum4 display;

  public:
  
    Display(Adafruit_AlphaNum4 d);
    
    void setMsg(String msg);
    void tick();

  private:
  
    void clearBuffer();
    void write());
    
};

#endif  // DISPLAY_H
