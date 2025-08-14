#include "lcd_i2c.h"
#include <string.h>

// Definisi Command
const int LCD_CLEARDISPLAY = 0x01;
const int LCD_RETURNHOME = 0x02;
const int LCD_ENTRYMODESET = 0x04;
const int LCD_DISPLAYCONTROL = 0x08;
const int LCD_CURSORSHIFT = 0x10;
const int LCD_FUNCTIONSET = 0x20;
const int LCD_SETCGRAMADDR = 0x40;
const int LCD_SETDDRAMADDR = 0x80;

// Flag untuk entry mode
const int LCD_ENTRYRIGHT = 0x00;
const int LCD_ENTRYLEFT = 0x02;
const int LCD_ENTRYSHIFTINCREMENT = 0x01;
const int LCD_ENTRYSHIFTDECREMENT = 0x00;

// Flag untuk display on/off
const int LCD_DISPLAYON = 0x04;
const int LCD_DISPLAYOFF = 0x00;
const int LCD_CURSORON = 0x02;
const int LCD_CURSOROFF = 0x00;
const int LCD_BLINKON = 0x01;
const int LCD_BLINKOFF = 0x00;

// Flag untuk display/cursor shift
const int LCD_DISPLAYMOVE = 0x08;
const int LCD_CURSORMOVE = 0x00;
const int LCD_MOVERIGHT = 0x04;
const int LCD_MOVELEFT = 0x00;

// Flag untuk function set
const int LCD_8BITMODE = 0x10;
const int LCD_4BITMODE = 0x00;
const int LCD_2LINE = 0x08;
const int LCD_1LINE = 0x00;
const int LCD_5x10DOTS = 0x04;
const int LCD_5x8DOTS = 0x00;

// Flag untuk backlight
const int LCD_BACKLIGHT = 0x08;
const int LCD_NOBACKLIGHT = 0x00;

#define ENABLE 0x04     // Enable bit
#define REG_CHARACTER 1 // Mode - Sending data
#define REG_COMMAND 0   // Mode - Sending command

static i2c_inst_t *i2c_instance_ptr;
static uint8_t i2c_addr;
static uint8_t backlight_val = LCD_BACKLIGHT;

void i2c_write_byte(uint8_t val)
{
    i2c_write_blocking(i2c_instance_ptr, i2c_addr, &val, 1, false);
}

void lcd_toggle_enable(uint8_t val)
{
    // Toggle enable pin on LCD display
    // We cannot do this too quickly
    sleep_us(600);
    i2c_write_byte(val | ENABLE);
    sleep_us(600);
    i2c_write_byte(val & ~ENABLE);
    sleep_us(600);
}

// The display is sent data in two halves, each half being 4 bits.
void lcd_send_byte(uint8_t val, int mode)
{
    uint8_t high_nibble = mode | (val & 0xF0) | backlight_val;
    uint8_t low_nibble = mode | ((val << 4) & 0xF0) | backlight_val;

    i2c_write_byte(high_nibble);
    lcd_toggle_enable(high_nibble);
    i2c_write_byte(low_nibble);
    lcd_toggle_enable(low_nibble);
}

void lcd_send_cmd(uint8_t cmd)
{
    lcd_send_byte(cmd, REG_COMMAND);
}

void lcd_send_char(uint8_t val)
{
    lcd_send_byte(val, REG_CHARACTER);
}

void lcd_clear(void)
{
    lcd_send_cmd(LCD_CLEARDISPLAY);
    sleep_ms(2);
}

void lcd_set_cursor(int line, int position)
{
    int val = (line == 0) ? 0x80 + position : 0xC0 + position;
    lcd_send_cmd(val);
}

void lcd_string(const char *s)
{
    while (*s)
    {
        lcd_send_char(*s++);
    }
}

void lcd_backlight(bool on)
{
    backlight_val = on ? LCD_BACKLIGHT : LCD_NOBACKLIGHT;
    i2c_write_byte(backlight_val); // Langsung tulis untuk update state
}

void lcd_init(i2c_inst_t *i2c, uint8_t addr)
{
    i2c_instance_ptr = i2c;
    i2c_addr = addr;

    // Inisialisasi 4-bit mode
    lcd_send_byte(0x03, REG_COMMAND);
    lcd_send_byte(0x03, REG_COMMAND);
    lcd_send_byte(0x03, REG_COMMAND);
    lcd_send_byte(0x02, REG_COMMAND);

    // Konfigurasi display
    lcd_send_cmd(LCD_FUNCTIONSET | LCD_2LINE | LCD_5x8DOTS | LCD_4BITMODE);
    lcd_send_cmd(LCD_DISPLAYCONTROL | LCD_DISPLAYON);
    lcd_clear();
    lcd_send_cmd(LCD_ENTRYMODESET | LCD_ENTRYLEFT);
    sleep_ms(5);
}