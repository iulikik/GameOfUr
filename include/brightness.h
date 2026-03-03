#ifndef BRIGHTNESS_H
#define BRIGHTNESS_H

#include <Arduino.h>
#include "pin_config.h"

// LilyGo T-Display-S3 control backlight chip has 16 levels of adjustment range
// The adjustable range is 0~16, 0 is the minimum brightness, 16 is the maximum brightness
void setBrightness(uint8_t value)
{
    static uint8_t level = 0;
    static uint8_t steps = 16;
    
    // Constrain value to valid range
    if (value > steps) value = steps;
    
    // If same brightness level, do nothing
    if (value == level) return;
    
    if (value == 0) {
        digitalWrite(PIN_LCD_BL, 0);
        delay(3);
        level = 0;
        return;
    }
    
    if (level == 0) {
        digitalWrite(PIN_LCD_BL, 1);
        level = steps;
        delayMicroseconds(30);
    }
    
    // Calculate number of pulses needed
    int from = steps - level;
    int to = steps - value;
    int num = (steps + to - from) % steps;
    
    // Send pulses with proper timing
    for (int i = 0; i < num; i++) {
        digitalWrite(PIN_LCD_BL, 0);
        delayMicroseconds(1);  // Add small delay for stability
        digitalWrite(PIN_LCD_BL, 1);
        delayMicroseconds(1);  // Add small delay for stability
    }
    level = value;
}

#endif 