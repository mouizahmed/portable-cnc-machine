#include "ui/display/tft_display.h"

#include <Adafruit_GFX.h>
#include <Arduino.h>
#include <SPI.h>
#include <string.h>

#include "board/pins.h"

class ILI9488Display : public Adafruit_GFX {
public:
    ILI9488Display(uint8_t cs, uint8_t dc, uint8_t rst)
        : Adafruit_GFX(UI_LCD_WIDTH, UI_LCD_HEIGHT), cs_(cs), dc_(dc), rst_(rst)
    {
    }

    void begin()
    {
        pinMode(cs_, OUTPUT);
        pinMode(dc_, OUTPUT);
        pinMode(rst_, OUTPUT);
        digitalWrite(cs_, HIGH);
        digitalWrite(dc_, HIGH);

        SPI.setMOSI(CNC_PIN_TFT_MOSI);
        SPI.setSCK(CNC_PIN_TFT_SCK);
        SPI.begin();

        digitalWrite(rst_, HIGH);
        delay(5);
        digitalWrite(rst_, LOW);
        delay(20);
        digitalWrite(rst_, HIGH);
        delay(150);

        command(0x01);
        delay(120);
        command(0x11);
        delay(120);

        command(0x3A);
        data(0x66);

        command(0x36);
        data(0x28);

        command(0xB6);
        data(0x02);
        data(0x02);
        data(0x3B);

        command(0x21);
        command(0x29);
        delay(20);
    }

    void drawPixel(int16_t x, int16_t y, uint16_t color) override
    {
        if(x < 0 || y < 0 || x >= width() || y >= height())
            return;

        setAddressWindow(x, y, x, y);
        writeColor(color, 1);
    }

    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override
    {
        if(w <= 0 || h <= 0)
            return;

        if(x < 0) {
            w += x;
            x = 0;
        }
        if(y < 0) {
            h += y;
            y = 0;
        }
        if(x >= width() || y >= height())
            return;
        if(x + w > width())
            w = width() - x;
        if(y + h > height())
            h = height() - y;

        setAddressWindow(x, y, x + w - 1, y + h - 1);
        writeColor(color, (uint32_t)w * (uint32_t)h);
    }

    void fillScreen(uint16_t color) override
    {
        fillRect(0, 0, width(), height(), color);
    }

private:
    void select()
    {
        SPI.beginTransaction(SPISettings(30000000, MSBFIRST, SPI_MODE0));
        digitalWrite(cs_, LOW);
    }

    void deselect()
    {
        digitalWrite(cs_, HIGH);
        SPI.endTransaction();
    }

    void command(uint8_t value)
    {
        select();
        digitalWrite(dc_, LOW);
        SPI.transfer(value);
        deselect();
    }

    void data(uint8_t value)
    {
        select();
        digitalWrite(dc_, HIGH);
        SPI.transfer(value);
        deselect();
    }

    void data16(uint16_t value)
    {
        SPI.transfer(value >> 8);
        SPI.transfer(value & 0xFF);
    }

    void setAddressWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
    {
        select();
        digitalWrite(dc_, LOW);
        SPI.transfer(0x2A);
        digitalWrite(dc_, HIGH);
        data16(x0);
        data16(x1);

        digitalWrite(dc_, LOW);
        SPI.transfer(0x2B);
        digitalWrite(dc_, HIGH);
        data16(y0);
        data16(y1);

        digitalWrite(dc_, LOW);
        SPI.transfer(0x2C);
        digitalWrite(dc_, HIGH);
        deselect();
    }

    void writeColor(uint16_t color, uint32_t count)
    {
        const uint8_t r = ((color >> 11) & 0x1F) << 3;
        const uint8_t g = ((color >> 5) & 0x3F) << 2;
        const uint8_t b = (color & 0x1F) << 3;

        select();
        digitalWrite(dc_, HIGH);
        while(count--) {
            SPI.transfer(r);
            SPI.transfer(g);
            SPI.transfer(b);
        }
        deselect();
    }

    uint8_t cs_;
    uint8_t dc_;
    uint8_t rst_;
};

static ILI9488Display tft(CNC_PIN_TFT_CS, CNC_PIN_TFT_DC, CNC_PIN_TFT_RESET);
static bool initialized = false;

bool tft_display_init(void)
{
    if(initialized)
        return true;

    tft.begin();
    initialized = true;

    return true;
}

void tft_display_fill_screen(uint16_t color)
{
    if(tft_display_init())
        tft.fillScreen(color);
}

void tft_display_fill_rect(ui_rect_t rect, uint16_t color)
{
    if(tft_display_init())
        tft.fillRect(rect.x, rect.y, rect.w, rect.h, color);
}

void tft_display_draw_rect(ui_rect_t rect, uint16_t color)
{
    if(tft_display_init())
        tft.drawRect(rect.x, rect.y, rect.w, rect.h, color);
}

void tft_display_draw_hline(int16_t x, int16_t y, int16_t w, uint16_t color)
{
    if(tft_display_init())
        tft.drawFastHLine(x, y, w, color);
}

void tft_display_draw_vline(int16_t x, int16_t y, int16_t h, uint16_t color)
{
    if(tft_display_init())
        tft.drawFastVLine(x, y, h, color);
}

void tft_display_draw_circle(int16_t x, int16_t y, int16_t r, uint16_t color)
{
    if(tft_display_init())
        tft.drawCircle(x, y, r, color);
}

void tft_display_draw_text(int16_t x, int16_t y, const char *text, uint16_t fg, uint16_t bg, uint8_t scale)
{
    if(!tft_display_init() || text == NULL)
        return;

    tft.setTextColor(fg, bg);
    tft.setTextSize(scale);
    tft.setCursor(x, y);
    tft.print(text);
}

int16_t tft_display_text_width(const char *text, uint8_t scale)
{
    return text == NULL ? 0 : (int16_t)(strlen(text) * 6 * scale);
}

int16_t tft_display_text_height(uint8_t scale)
{
    return (int16_t)(8 * scale);
}
