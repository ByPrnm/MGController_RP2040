#ifndef LCD_I2C_H
#define LCD_I2C_H

#include "pico/stdlib.h"
#include "hardware/i2c.h"

// Prototipe Fungsi
void lcd_init(i2c_inst_t *i2c_instance, uint8_t addr);
void lcd_send_cmd(uint8_t cmd);
void lcd_send_char(uint8_t val);
void lcd_string(const char *s);
void lcd_clear(void);
void lcd_set_cursor(int line, int position);
void lcd_backlight(bool on);

#endif