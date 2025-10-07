/**
 * @file st7789.c
 */

/* ============== INCLUDES ===================== */
#include "st7789.h"
#include <string.h>
#include <stdlib.h>

/* ============== PRIVATE DEFINES ============== */

/* ST7789 Commands */

// General Control Commands
#define ST7789_NOP        0x00	// No Operation (placeholder)
#define ST7789_SWRESET    0x01	// Software Reset
#define ST7789_RDDID      0x04	// Read Display ID

// Display Power Control
#define ST7789_SLPIN      0x10	// Sleep Mode On
#define ST7789_SLPOUT     0x11  // Sleep Mode Off
#define ST7789_DISPOFF    0x28	// Display Off
#define ST7789_DISPON     0x29	// Display On

// Display Mode Control
#define ST7789_PTLON      0x12	// Partial Display Mode On
#define ST7789_NORON      0x13	// Normal Display Mode On
#define ST7789_PTLAR      0x30	// Partial Display Area

// Display Mode Control
#define ST7789_INVOFF     0x20	// Display Inversion Off
#define ST7789_INVON      0x21	// Display Inversion On
#define ST7789_COLMOD     0x3A	// Interface Pixel Format (e.g., RGB565)

// Memory and Address Control
#define ST7789_CASET      0x2A	// Column Address Set
#define ST7789_RASET      0x2B	// Row Address Set
#define ST7789_RAMWR      0x2C	// Memory Write
#define ST7789_RAMRD      0x2E	// Memory Read
#define ST7789_MADCTL     0x36	// Memory Access Control (rotation, order)

// MADCTL bits
#define MADCTL_MY   0x80  // Row Address Order
#define MADCTL_MX   0x40  // Column Address Order
#define MADCTL_MV   0x20  // Row/Column Exchange
#define MADCTL_ML   0x10  // Vertical Refresh Order
#define MADCTL_RGB  0x00  // RGB Order
#define MADCTL_BGR  0x08  // BGR Order

// Color Mode
#define ST7789_COLOR_MODE_16bit 0x55  // RGB565

// GPIO Macros
#ifdef ST7789_USE_CS
    #define CS_LOW()   HAL_GPIO_WritePin(ST7789_CS_PORT, ST7789_CS_PIN, GPIO_PIN_RESET)
    #define CS_HIGH()  HAL_GPIO_WritePin(ST7789_CS_PORT, ST7789_CS_PIN, GPIO_PIN_SET)
#else
    #define CS_LOW()
    #define CS_HIGH()
#endif

#define DC_LOW()   HAL_GPIO_WritePin(ST7789_DC_PORT, ST7789_DC_PIN, GPIO_PIN_RESET)
#define DC_HIGH()  HAL_GPIO_WritePin(ST7789_DC_PORT, ST7789_DC_PIN, GPIO_PIN_SET)
#define RST_LOW()  HAL_GPIO_WritePin(ST7789_RST_PORT, ST7789_RST_PIN, GPIO_PIN_RESET)
#define RST_HIGH() HAL_GPIO_WritePin(ST7789_RST_PORT, ST7789_RST_PIN, GPIO_PIN_SET)

#define ABS(x) ((x) > 0 ? (x) : -(x))

/* ============== PRIVATE VARIABLES ============== */

#ifdef ST7789_USE_DMA
static uint16_t dma_buffer[ST7789_WIDTH * ST7789_DMA_BUFFER_LINES];
#endif

/* ============== PRIVATE FUNCTIONS ============== */

/**
 * @brief Write command to ST7789
 */
static inline void ST7789_WriteCommand(uint8_t cmd)
{
    CS_LOW();
    DC_LOW();
    HAL_SPI_Transmit(&ST7789_SPI_PORT, &cmd, 1, HAL_MAX_DELAY);
    CS_HIGH();
}

/**
 * @brief Write single byte data
 */
static inline void ST7789_WriteData8(uint8_t data)
{
    CS_LOW();
    DC_HIGH();
    HAL_SPI_Transmit(&ST7789_SPI_PORT, &data, 1, HAL_MAX_DELAY);
    CS_HIGH();
}

/**
 * @brief Write bulk data with DMA support
 */
static void ST7789_WriteData(uint8_t *data, size_t len)
{
    CS_LOW();
    DC_HIGH();

    while (len > 0)
    {
        uint16_t chunk = (len > 65535) ? 65535 : len;

        #ifdef ST7789_USE_DMA
        if (chunk >= ST7789_DMA_MIN_SIZE)
        {
            HAL_SPI_Transmit_DMA(&ST7789_SPI_PORT, data, chunk);
            while (ST7789_SPI_PORT.hdmatx->State != HAL_DMA_STATE_READY);
        }
        else
        {
            HAL_SPI_Transmit(&ST7789_SPI_PORT, data, chunk, HAL_MAX_DELAY);
        }
        #else
        HAL_SPI_Transmit(&ST7789_SPI_PORT, data, chunk, HAL_MAX_DELAY);
        #endif

        data += chunk;
        len -= chunk;
    }

    CS_HIGH();
}

/**
 * @brief Set drawing window
 */
static void ST7789_SetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    x0 += ST7789_X_SHIFT;
    x1 += ST7789_X_SHIFT;
    y0 += ST7789_Y_SHIFT;
    y1 += ST7789_Y_SHIFT;

    // Column Address Set
    ST7789_WriteCommand(ST7789_CASET);
    uint8_t data[] = {
        x0 >> 8, x0 & 0xFF,
        x1 >> 8, x1 & 0xFF
    };
    ST7789_WriteData(data, 4);

    // Row Address Set
    ST7789_WriteCommand(ST7789_RASET);
    data[0] = y0 >> 8;
    data[1] = y0 & 0xFF;
    data[2] = y1 >> 8;
    data[3] = y1 & 0xFF;
    ST7789_WriteData(data, 4);

    // Write to RAM
    ST7789_WriteCommand(ST7789_RAMWR);
}

/**
 * @brief Hardware reset
 */
static void ST7789_HardReset(void)
{
    RST_HIGH();
    HAL_Delay(5);
    RST_LOW();
    HAL_Delay(20);
    RST_HIGH();
    HAL_Delay(150);
}

/**
 * @brief Set drawing horizontal line
 */
static void ST7789_DrawHorizontalLine(int16_t x0, int16_t y, int16_t x_left, int16_t x_right, uint16_t color)
{
    if (y < 0 || y >= ST7789_HEIGHT) return;

    // Clamp x coordinates
    if (x_left < 0) x_left = 0;
    if (x_right >= ST7789_WIDTH) x_right = ST7789_WIDTH - 1;

    if (x_left > x_right) return;

    uint16_t width = x_right - x_left + 1;
    ST7789_FillRect(x_left, y, width, 1, color);
}

/* ============== PUBLIC FUNCTIONS ============== */

/**
 * @brief Initialize ST7789
 */
void ST7789_Init(void)
{
    #ifdef ST7789_USE_DMA
    memset(dma_buffer, 0, sizeof(dma_buffer));
    #endif

    // Hardware Reset
    ST7789_HardReset();

    // Software Reset
    ST7789_WriteCommand(ST7789_SWRESET);
    HAL_Delay(150);

    // Sleep Out
    ST7789_WriteCommand(ST7789_SLPOUT);
    HAL_Delay(10);

    // Color Mode - 16bit RGB565
    ST7789_WriteCommand(ST7789_COLMOD);
    ST7789_WriteData8(ST7789_COLOR_MODE_16bit);

    // Porch Control
    ST7789_WriteCommand(0xB2);
    uint8_t porch[] = {0x0C, 0x0C, 0x00, 0x33, 0x33};
    ST7789_WriteData(porch, 5);

    // Gate Control
    ST7789_WriteCommand(0xB7);
    ST7789_WriteData8(0x35);

    // VCOM Setting
    ST7789_WriteCommand(0xBB);
    ST7789_WriteData8(0x19);

    // LCM Control
    ST7789_WriteCommand(0xC0);
    ST7789_WriteData8(0x2C);

    // VDV and VRH Enable
    ST7789_WriteCommand(0xC2);
    ST7789_WriteData8(0x01);

    // VRH Set
    ST7789_WriteCommand(0xC3);
    ST7789_WriteData8(0x12);

    // VDV Set
    ST7789_WriteCommand(0xC4);
    ST7789_WriteData8(0x20);

    // Frame Rate Control
    ST7789_WriteCommand(0xC6);
    ST7789_WriteData8(0x0F);

    // Power Control
    ST7789_WriteCommand(0xD0);
    uint8_t power[] = {0xA4, 0xA1};
    ST7789_WriteData(power, 2);

    // Positive Voltage Gamma
    ST7789_WriteCommand(0xE0);
    uint8_t pvgamma[] = {0xD0, 0x04, 0x0D, 0x11, 0x13, 0x2B, 0x3F,
                         0x54, 0x4C, 0x18, 0x0D, 0x0B, 0x1F, 0x23};
    ST7789_WriteData(pvgamma, 14);

    // Negative Voltage Gamma
    ST7789_WriteCommand(0xE1);
    uint8_t nvgamma[] = {0xD0, 0x04, 0x0C, 0x11, 0x13, 0x2C, 0x3F,
                         0x44, 0x51, 0x2F, 0x1F, 0x1F, 0x20, 0x23};
    ST7789_WriteData(nvgamma, 14);

    // Set Rotation
    ST7789_SetRotation(ST7789_ROTATION);

    // Inversion off
    ST7789_WriteCommand(ST7789_INVOFF);

    // Normal Display Mode
    ST7789_WriteCommand(ST7789_NORON);
    HAL_Delay(10);

    // Display On
    ST7789_WriteCommand(ST7789_DISPON);
    HAL_Delay(10);

    // Clear screen
    ST7789_FillScreen(ST7789_BLACK);
}

/**
 * @brief Set display rotation
 */
void ST7789_SetRotation(uint8_t rotation)
{
    ST7789_WriteCommand(ST7789_MADCTL);

    switch (rotation % 4)
    {
        case 0:  // Portrait
            ST7789_WriteData8(MADCTL_MX | MADCTL_MY | MADCTL_RGB);
            break;
        case 1:  // Landscape
            ST7789_WriteData8(MADCTL_MY | MADCTL_MV | MADCTL_RGB);
            break;
        case 2:  // Portrait Inverted
            ST7789_WriteData8(MADCTL_RGB);
            break;
        case 3:  // Landscape Inverted
            ST7789_WriteData8(MADCTL_MX | MADCTL_MV | MADCTL_RGB);
            break;
    }
}

/**
 * @brief Invert display colors
 */
void ST7789_InvertDisplay(bool invert)
{
    ST7789_WriteCommand(invert ? ST7789_INVON : ST7789_INVOFF);
}

/**
 * @brief Sleep mode control
 */
void ST7789_Sleep(bool sleep)
{
    ST7789_WriteCommand(sleep ? ST7789_SLPIN : ST7789_SLPOUT);
    HAL_Delay(120);
}

/**
 * @brief Fill entire screen with color
 */
void ST7789_FillScreen(uint16_t color)
{
    ST7789_FillRect(0, 0, ST7789_WIDTH, ST7789_HEIGHT, color);
}

/**
 * @brief Fill rectangle
 */
void ST7789_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    if (x >= ST7789_WIDTH || y >= ST7789_HEIGHT) return;

    if (x + w > ST7789_WIDTH) w = ST7789_WIDTH - x;

    if (y + h > ST7789_HEIGHT) h = ST7789_HEIGHT - y;

    ST7789_SetWindow(x, y, x + w - 1, y + h - 1);

    #ifdef ST7789_USE_DMA
    // Fill buffer with color
    for (uint16_t i = 0; i < ST7789_WIDTH * ST7789_DMA_BUFFER_LINES; i++)
    {
        dma_buffer[i] = color;
    }

    uint32_t total_pixels = (uint32_t)w * h;
    uint32_t buffer_pixels = ST7789_WIDTH * ST7789_DMA_BUFFER_LINES;

    CS_LOW();
    DC_HIGH();

    while (total_pixels >= buffer_pixels)
    {
        ST7789_WriteData((uint8_t*)dma_buffer, buffer_pixels * 2);
        total_pixels -= buffer_pixels;
    }

    if (total_pixels > 0)
    {
        ST7789_WriteData((uint8_t*)dma_buffer, total_pixels * 2);
    }

    CS_HIGH();
    #else
    uint8_t colorH = color >> 8;
    uint8_t colorL = color & 0xFF;
    uint32_t pixels = (uint32_t)w * h;

    CS_LOW();
    DC_HIGH();

    // Use 128-byte buffer for optimization
    uint8_t buffer[128];
    for (uint16_t i = 0; i < 128; i += 2)
    {
        buffer[i] = colorH;
        buffer[i + 1] = colorL;
    }

    while (pixels >= 64)
    {
        HAL_SPI_Transmit(&ST7789_SPI_PORT, buffer, 128, HAL_MAX_DELAY);
        pixels -= 64;
    }

    while (pixels--)
    {
        HAL_SPI_Transmit(&ST7789_SPI_PORT, &colorH, 1, HAL_MAX_DELAY);
        HAL_SPI_Transmit(&ST7789_SPI_PORT, &colorL, 1, HAL_MAX_DELAY);
    }

    CS_HIGH();
    #endif
}

/**
 * @brief Draw single pixel
 */
void ST7789_DrawPixel(uint16_t x, uint16_t y, uint16_t color)
{
    if (x >= ST7789_WIDTH || y >= ST7789_HEIGHT) return;

    ST7789_SetWindow(x, y, x, y);
    uint8_t data[] = {color >> 8, color & 0xFF};
    ST7789_WriteData(data, 2);
}

/**
 * @brief Draw line - Bresenham's algorithm
 */
void ST7789_DrawLine(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color)
{
    int16_t steep = ABS(y1 - y0) > ABS(x1 - x0);

    if (steep)
    {
        uint16_t tmp;
        tmp = x0; x0 = y0; y0 = tmp;
        tmp = x1; x1 = y1; y1 = tmp;
    }

    if (x0 > x1)
    {
        uint16_t tmp;
        tmp = x0; x0 = x1; x1 = tmp;
        tmp = y0; y0 = y1; y1 = tmp;
    }

    int16_t dx = x1 - x0;
    int16_t dy = ABS(y1 - y0);
    int16_t err = dx / 2;
    int16_t ystep = (y0 < y1) ? 1 : -1;

    for (; x0 <= x1; x0++)
    {
        if (steep)
        {
            ST7789_DrawPixel(y0, x0, color);
        }
        else
        {
            ST7789_DrawPixel(x0, y0, color);
        }
        err -= dy;
        if (err < 0)
        {
            y0 += ystep;
            err += dx;
        }
    }
}

/**
 * @brief Draw rectangle outline
 */
void ST7789_DrawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    ST7789_DrawLine(x, y, x + w - 1, y, color);
    ST7789_DrawLine(x + w - 1, y, x + w - 1, y + h - 1, color);
    ST7789_DrawLine(x + w - 1, y + h - 1, x, y + h - 1, color);
    ST7789_DrawLine(x, y + h - 1, x, y, color);
}

/**
 * @brief Draw circle outline - Midpoint algorithm
 */
void ST7789_DrawCircle(uint16_t x0, uint16_t y0, uint16_t r, uint16_t color)
{
    int16_t f = 1 - r;
    int16_t ddF_x = 1;
    int16_t ddF_y = -2 * r;
    int16_t x = 0;
    int16_t y = r;

    ST7789_DrawPixel(x0, y0 + r, color);
    ST7789_DrawPixel(x0, y0 - r, color);
    ST7789_DrawPixel(x0 + r, y0, color);
    ST7789_DrawPixel(x0 - r, y0, color);

    while (x < y)
    {
        if (f >= 0)
        {
            y--;
            ddF_y += 2;
            f += ddF_y;
        }
        x++;
        ddF_x += 2;
        f += ddF_x;

        ST7789_DrawPixel(x0 + x, y0 + y, color);
        ST7789_DrawPixel(x0 - x, y0 + y, color);
        ST7789_DrawPixel(x0 + x, y0 - y, color);
        ST7789_DrawPixel(x0 - x, y0 - y, color);
        ST7789_DrawPixel(x0 + y, y0 + x, color);
        ST7789_DrawPixel(x0 - y, y0 + x, color);
        ST7789_DrawPixel(x0 + y, y0 - x, color);
        ST7789_DrawPixel(x0 - y, y0 - x, color);
    }
}

/**
 * @brief Draw filled circle
 */
void ST7789_FillCircle(uint16_t x0, uint16_t y0, uint16_t r, uint16_t color)
{
    if (r == 0) return;

    int16_t x = 0;
    int16_t y = r;
    int16_t d = 3 - 2 * r;  // Midpoint decision parameter

    while (y >= x)
    {
        ST7789_DrawHorizontalLine(x0, y0 + y, x0 - x, x0 + x, color);  // Bottom
        ST7789_DrawHorizontalLine(x0, y0 - y, x0 - x, x0 + x, color);  // Up

        if (x != y) {
            ST7789_DrawHorizontalLine(x0, y0 + x, x0 - y, x0 + y, color);  // Bottom
            ST7789_DrawHorizontalLine(x0, y0 - x, x0 - y, x0 + y, color);  // Up
        }

        // Update follow Midpoint Circle Algorithm
        if (d < 0)
        {
            d = d + 4 * x + 6;
        }
        else
        {
            d = d + 4 * (x - y) + 10;
            y--;
        }
        x++;
    }
}

/**
 * @brief Draw triangle outline
 */
void ST7789_DrawTriangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2,
                         uint16_t x3, uint16_t y3, uint16_t color)
{
    ST7789_DrawLine(x1, y1, x2, y2, color);
    ST7789_DrawLine(x2, y2, x3, y3, color);
    ST7789_DrawLine(x3, y3, x1, y1, color);
}

/**
 * @brief Draw filled triangle
 */
void ST7789_FillTriangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2,
                         uint16_t x3, uint16_t y3, uint16_t color)
{
    int16_t deltax = ABS(x2 - x1);
    int16_t deltay = ABS(y2 - y1);
    int16_t x = x1, y = y1;
    int16_t xinc1, xinc2, yinc1, yinc2;
    int16_t den, num, numadd, numpixels;

    xinc1 = (x2 >= x1) ? 1 : -1;
    xinc2 = xinc1;
    yinc1 = (y2 >= y1) ? 1 : -1;
    yinc2 = yinc1;

    if (deltax >= deltay)
    {
        xinc1 = 0;
        yinc2 = 0;
        den = deltax;
        num = deltax / 2;
        numadd = deltay;
        numpixels = deltax;
    }
    else
    {
        xinc2 = 0;
        yinc1 = 0;
        den = deltay;
        num = deltay / 2;
        numadd = deltax;
        numpixels = deltay;
    }

    for (int16_t curpixel = 0; curpixel <= numpixels; curpixel++)
    {
        ST7789_DrawLine(x, y, x3, y3, color);

        num += numadd;
        if (num >= den)
        {
            num -= den;
            x += xinc1;
            y += yinc1;
        }
        x += xinc2;
        y += yinc2;
    }
}

/**
 * @brief Draw image from array
 */
void ST7789_DrawImage(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *data)
{
    if (x >= ST7789_WIDTH || y >= ST7789_HEIGHT) return;
    if (x + w > ST7789_WIDTH || y + h > ST7789_HEIGHT) return;

    ST7789_SetWindow(x, y, x + w - 1, y + h - 1);
    ST7789_WriteData((uint8_t*)data, w * h * 2);
}

#ifdef ST7789_USE_FONTS
/**
 * @brief Write single character
 */
void ST7789_WriteChar(uint16_t x, uint16_t y, char ch, FontDef font,
                      uint16_t color, uint16_t bgcolor)
{
    if (x + font.width > ST7789_WIDTH || y + font.height > ST7789_HEIGHT) return;

    ST7789_SetWindow(x, y, x + font.width - 1, y + font.height - 1);

    CS_LOW();
    DC_HIGH();

    for (uint16_t i = 0; i < font.height; i++)
    {
        uint16_t line = font.data[(ch - 32) * font.height + i];
        for (uint16_t j = 0; j < font.width; j++)
        {
            uint16_t pixel_color = (line << j) & 0x8000 ? color : bgcolor;
            uint8_t data[] = {pixel_color >> 8, pixel_color & 0xFF};
            HAL_SPI_Transmit(&ST7789_SPI_PORT, data, 2, HAL_MAX_DELAY);
        }
    }

    CS_HIGH();
}

/**
 * @brief Write string with word wrap
 */
void ST7789_WriteString(uint16_t x, uint16_t y, const char *str, FontDef font,
                        uint16_t color, uint16_t bgcolor)
{
    while (*str)
    {
        if (x + font.width > ST7789_WIDTH)
        {
            x = 0;
            y += font.height;
            if (y + font.height > ST7789_HEIGHT) break;
            if (*str == ' ')
            {
                str++;
                continue;
            }
        }

        ST7789_WriteChar(x, y, *str, font, color, bgcolor);
        x += font.width;
        str++;
    }
}
#endif

/**
 * @brief Convert RGB to RGB565
 */
uint16_t ST7789_Color565(uint8_t r, uint8_t g, uint8_t b)
{
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

/**
 * @brief Test function
 */
void ST7789_Test(void)
{
    const uint16_t colors[] = {
        ST7789_RED, ST7789_GREEN, ST7789_BLUE,
        ST7789_YELLOW, ST7789_CYAN, ST7789_MAGENTA,
        ST7789_WHITE, ST7789_BLACK
    };

    // Color test
    for (uint8_t i = 0; i < 8; i++)
    {
        ST7789_FillScreen(colors[i]);
        HAL_Delay(500);
    }

    #ifdef ST7789_USE_FONTS
    // Text test
    ST7789_FillScreen(ST7789_BLACK);
    ST7789_WriteString(10, 10, "ST7789 Test", Font_16x26, ST7789_WHITE, ST7789_BLACK);
    ST7789_WriteString(10, 40, "240x320 Display", Font_11x18, ST7789_CYAN, ST7789_BLACK);
    HAL_Delay(2000);
    #endif

    // Shape test
    ST7789_FillScreen(ST7789_BLACK);
    ST7789_DrawRect(20, 20, 100, 80, ST7789_GREEN);
    ST7789_FillCircle(180, 60, 30, ST7789_RED);
    ST7789_DrawTriangle(60, 150, 120, 200, 90, 250, ST7789_YELLOW);
    HAL_Delay(2000);
}
