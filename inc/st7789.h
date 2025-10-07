/**
 * @file st7789.h
 */

#ifndef __ST7789_H
#define __ST7789_H

/* ============== INCLUDES ===================== */

#include <stdint.h>
#include <stdbool.h>
#include "main.h"

/* ============== CONFIGURATION ============== */

// SPI Port
#define ST7789_SPI_PORT hspi1
extern SPI_HandleTypeDef ST7789_SPI_PORT;

// DMA Support (comment to disable and save RAM)
//#define ST7789_USE_DMA
#ifdef ST7789_USE_DMA
    #define ST7789_DMA_MIN_SIZE 16         // Min bytes to use DMA
    #define ST7789_DMA_BUFFER_LINES 5      // Buffer size (lines)
#endif

// Font Support (comment to disable and save memory)
#define ST7789_USE_FONTS
#ifdef ST7789_USE_FONTS
    #include "fonts.h"
#endif

// Display Type (uncomment ONE only)
//#define ST7789_135x240    // 0.96 inch
//#define ST7789_240x240    // 1.3 inch
#define ST7789_240x320      // 2.8 inch
//#define ST7789_170x320    // 1.9 inch

// Display Rotation (0-3)
//#define ST7789_ROTATION 0    // 0=Portrait
//#define ST7789_ROTATION 1    // 1=Landscape
//#define ST7789_ROTATION 2    // 2=Portrait180
#define ST7789_ROTATION 3    // 3=Landscape180

// CS Control (comment if CS tied to GND)
#define ST7789_USE_CS

// Pin Definitions
#define ST7789_RST_PORT ST7789_RST_GPIO_Port
#define ST7789_RST_PIN  ST7789_RST_Pin
#define ST7789_DC_PORT  ST7789_DC_GPIO_Port
#define ST7789_DC_PIN   ST7789_DC_Pin
#ifdef ST7789_USE_CS
    #define ST7789_CS_PORT ST7789_CS_GPIO_Port
    #define ST7789_CS_PIN  ST7789_CS_Pin
#endif

/* ============== DISPLAY PARAMETERS ============== */

// 135x240 (0.96 inch)
#ifdef ST7789_135x240
    #if ST7789_ROTATION == 0 || ST7789_ROTATION == 2
        #define ST7789_WIDTH  135
        #define ST7789_HEIGHT 240
    #else
        #define ST7789_WIDTH  240
        #define ST7789_HEIGHT 135
    #endif
    #if ST7789_ROTATION == 0
        #define ST7789_X_SHIFT 53
        #define ST7789_Y_SHIFT 40
    #elif ST7789_ROTATION == 1
        #define ST7789_X_SHIFT 40
        #define ST7789_Y_SHIFT 52
    #elif ST7789_ROTATION == 2
        #define ST7789_X_SHIFT 52
        #define ST7789_Y_SHIFT 40
    #else
        #define ST7789_X_SHIFT 40
        #define ST7789_Y_SHIFT 53
    #endif
#endif

// 240x240 (1.3 inch)
#ifdef ST7789_240x240
    #define ST7789_WIDTH  240
    #define ST7789_HEIGHT 240
    #if ST7789_ROTATION == 0
        #define ST7789_X_SHIFT 0
        #define ST7789_Y_SHIFT 80
    #elif ST7789_ROTATION == 1
        #define ST7789_X_SHIFT 80
        #define ST7789_Y_SHIFT 0
    #else
        #define ST7789_X_SHIFT 0
        #define ST7789_Y_SHIFT 0
    #endif
#endif

// 240x320 (2.8 inch)
#ifdef ST7789_240x320
    #if ST7789_ROTATION == 0 || ST7789_ROTATION == 2
        #define ST7789_WIDTH  240
        #define ST7789_HEIGHT 320
    #else
        #define ST7789_WIDTH  320
        #define ST7789_HEIGHT 240
    #endif
    #define ST7789_X_SHIFT 0
    #define ST7789_Y_SHIFT 0
#endif

// 170x320 (1.9 inch)
#ifdef ST7789_170x320
    #if ST7789_ROTATION == 0 || ST7789_ROTATION == 2
        #define ST7789_WIDTH  170
        #define ST7789_HEIGHT 320
        #define ST7789_X_SHIFT 35
        #define ST7789_Y_SHIFT 0
    #else
        #define ST7789_WIDTH  320
        #define ST7789_HEIGHT 170
        #define ST7789_X_SHIFT 0
        #define ST7789_Y_SHIFT 35
    #endif
#endif

/* ============== COLOR DEFINITIONS (RGB565) ============== */

#define ST7789_BLACK       0x0000
#define ST7789_WHITE       0xFFFF
#define ST7789_RED         0xF800
#define ST7789_GREEN       0x07E0
#define ST7789_BLUE        0x001F
#define ST7789_YELLOW      0xFFE0
#define ST7789_CYAN        0x07FF
#define ST7789_MAGENTA     0xF81F
#define ST7789_ORANGE      0xFD20
#define ST7789_GRAY        0x8410
#define ST7789_DARKGRAY    0x4208
#define ST7789_LIGHTGRAY   0xC618
#define ST7789_BROWN       0xBC40
#define ST7789_DARKBLUE    0x01CF
#define ST7789_LIGHTBLUE   0x7D7C
#define ST7789_LIGHTGREEN  0x841F

/* ============== PUBLIC API ============== */

// Initialization & Control

/*
 * @brief Initialize ST7789 display with reset, color mode (RGB565), gamma, and display on.
 */
void ST7789_Init(void);

/**
 * @brief Set display rotation.
 * @param rotation 0: Portrait, 1: Landscape, 2: Portrait 180°, 3: Landscape 180°.
 */
void ST7789_SetRotation(uint8_t rotation);

/**
 * @brief Invert display colors.
 * @param invert true to invert, false for normal.
 */
void ST7789_InvertDisplay(bool invert);

/**
 * @brief Enter/exit sleep mode.
 * @param sleep true for sleep (power save), false to wake. Delay 120ms after.
 */
void ST7789_Sleep(bool sleep);

// Basic Drawing

/**
 * @brief Fill screen with color.
 * @param color RGB565 color.
 */
void ST7789_FillScreen(uint16_t color);

/**
 * @brief Fill rectangle with color.
 * @param x Start X.
 * @param y Start Y.
 * @param w Width.
 * @param h Height.
 * @param color RGB565 color.
 */
void ST7789_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);

/**
 * @brief Draw single pixel.
 * @param x X coordinate.
 * @param y Y coordinate.
 * @param color RGB565 color.
 */
void ST7789_DrawPixel(uint16_t x, uint16_t y, uint16_t color);

// Lines & Shapes

/**
 * @brief Draw line (Bresenham algorithm).
 * @param x0 Start X.
 * @param y0 Start Y.
 * @param x1 End X.
 * @param y1 End Y.
 * @param color RGB565 color.
 */
void ST7789_DrawLine(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color);

/**
 * @brief Draw rectangle outline.
 * @param x Start X.
 * @param y Start Y.
 * @param w Width.
 * @param h Height.
 * @param color RGB565 color.
 */
void ST7789_DrawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);

/**
 * @brief Draw circle outline (Midpoint algorithm).
 * @param x0 Center X.
 * @param y0 Center Y.
 * @param r Radius.
 * @param color RGB565 color.
 */
void ST7789_DrawCircle(uint16_t x0, uint16_t y0, uint16_t r, uint16_t color);

/**
 * @brief Fill circle.
 * @param x0 Center X.
 * @param y0 Center Y.
 * @param r Radius.
 * @param color RGB565 color.
 */
void ST7789_FillCircle(uint16_t x0, uint16_t y0, uint16_t r, uint16_t color);

/**
 * @brief Draw triangle outline.
 * @param x1 Vertex 1 X.
 * @param y1 Vertex 1 Y.
 * @param x2 Vertex 2 X.
 * @param y2 Vertex 2 Y.
 * @param x3 Vertex 3 X.
 * @param y3 Vertex 3 Y.
 * @param color RGB565 color.
 */
void ST7789_DrawTriangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2,
						 uint16_t x3, uint16_t y3, uint16_t color);

/**
 * @brief Fill triangle.
 * @param x1 Vertex 1 X.
 * @param y1 Vertex 1 Y.
 * @param x2 Vertex 2 X.
 * @param y2 Vertex 2 Y.
 * @param x3 Vertex 3 X.
 * @param y3 Vertex 3 Y.
 * @param color RGB565 color.
 */
void ST7789_FillTriangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2,
						 uint16_t x3, uint16_t y3, uint16_t color);

// Image Drawing

/**
 * @brief Draw bitmap image.
 * @param x Start X.
 * @param y Start Y.
 * @param w Width.
 * @param h Height.
 * @param data RGB565 data pointer.
 */
void ST7789_DrawImage(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *data);

// Text Functions (if fonts enabled)
#ifdef ST7789_USE_FONTS

/**
 * @brief Write single character.
 * @param x Start X.
 * @param y Start Y.
 * @param ch Character.
 * @param font Font definition.
 * @param color Foreground RGB565.
 * @param bgcolor Background RGB565.
 */
void ST7789_WriteChar(uint16_t x, uint16_t y, char ch, FontDef font,
                      uint16_t color, uint16_t bgcolor);

/**
 * @brief Write string with word wrap.
 * @param x Start X.
 * @param y Start Y.
 * @param str String pointer.
 * @param font Font definition.
 * @param color Foreground RGB565.
 * @param bgcolor Background RGB565.
 */
void ST7789_WriteString(uint16_t x, uint16_t y, const char *str, FontDef font,
                        uint16_t color, uint16_t bgcolor);
#endif

// Utility Functions

/**
 * @brief Convert RGB to RGB565.
 * @param r Red (0-255).
 * @param g Green (0-255).
 * @param b Blue (0-255).
 * @return RGB565 value.
 */
uint16_t ST7789_Color565(uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Run test sequence (colors, text, shapes).
 */
void ST7789_Test(void);

// Validation
#ifndef ST7789_ROTATION
    #error "ST7789_ROTATION must be defined (0-3)"
#endif

#if !defined(ST7789_135x240) && !defined(ST7789_240x240) && !defined(ST7789_240x320) && !defined(ST7789_170x320)
    #error "Must define one display type"
#endif

#endif // __ST7789_H
