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
 
#include "display.h"

Display::Display(Adafruit_AlphaNum4 display)
{
    _display = display;
    _msg = "";
    clearBuffer();
}

void Display::setMsg(String msg)
{
  if(_msg != msg)
  {
    _msg = msg;
    clearBuffer();
    _index = 0;

    // Stay still if len <= 4
    // With buffer filler up with space, it's left aligned
    if (_msg.length() <= DISPLAY_SIZE)
    {
      for (int i=0; i<_msg.length(); i++)
        _buffer[i] = _msg.charAt(i);

      write();
    }
  }
}

void Display::tick()
{
  if (_msg.length() > DISPLAY_SIZE)
  {
    if(_index >= _msg.length())
      _index = 0;

    // Move the existing characters one position to the left
    for(int i = 0; i < DISPLAY_SIZE-1; i++)
      _buffer[i] = _buffer[i + 1];

    // Replace the right-most character with the next
    // character from the message to display
    _buffer[DISPLAY_SIZE-1] = _msg.charAt(_index++);

    write();
  }
}


// private

void Display::clearBuffer()
{
  for (int i = 0; i < DISPLAY_SIZE; i++)
  {
    _buffer[i] = ' ';
  }
}

void Display::write() 
{
  // send the text to the display
  for(int i = 0; i < DISPLAY_SIZE; i++)
  {
    _display.writeDigitAscii(i, _buffer[i]);
  }
  _display.writeDisplay();
}
