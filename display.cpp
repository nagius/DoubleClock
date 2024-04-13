
#include "display.h"


Class Display { // TODO verifier syntaxe

  Display(Adafruit_AlphaNum4 d) {
      display = d
      msg = "";
      clearBuffer();
  }

  void setMsg(String msg) {
    this.msg = msg;  // TODO verifier syntaxe
    clearBuffer();
    index = 0;

    // Stay still if len <= 4
    // With previous space, it's left aligned
    if (msg.length() <= DISPLAY_SIZE) {
      for (int i=0; i<msg.length(); i++) {
        buffer[i] = msg.charAt(i);
      }
      write();
    }
  }

  void tick() {
    if (msg.length() > DISPLAY_SIZE) {
      if(index >= msg.length())
        index = 0;
  
      // Move the existing characters one position to the left
      for(int u = 0; u < 3; u++)
        buffer[u] = buffer[u + 1];
  
      // Replace the right-most character with the next
      // character from the message to display
      buffer[3] = msg.charAt(index++);
 
      write());
    }
  }


// private

  void clearBuffer() {
    for (int i=0; i<len(buffer); i++) {
      buffer[i] = ' ';
    }
  }

  void write() {
    // send the text to the display
    for(int i = 0; i < DISPLAY_SIZE; i++)
    {
      display.writeDigitAscii(i, buffer[i]);
    }
    display.writeDisplay();
   }

}