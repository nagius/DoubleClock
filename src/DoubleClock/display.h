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

 // This is a library to show text on 4 digit 13 segments alphanumeric display with HT16K33 backpack
 // https://www.adafruit.com/product/1911

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
