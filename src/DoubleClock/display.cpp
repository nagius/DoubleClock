
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
