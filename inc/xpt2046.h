/**
 * @file xpt2046.h
 */

#ifndef __XPT2046_H
#define __XPT2046_H

/* ============== INCLUDES ===================== */

#include <stdint.h>
#include <stdbool.h>
#include "main.h"

/* ============== CONFIGURATION ============== */

// SPI Port (should be same as ST7789 or separate)
#define XPT2046_SPI_PORT hspi1
extern SPI_HandleTypeDef XPT2046_SPI_PORT;

// Pin Definitions
#define XPT2046_CS_PORT  XPT2046_CS_GPIO_Port
#define XPT2046_CS_PIN   XPT2046_CS_Pin

// Optional: IRQ pin for touch detection (comment out if not used)
// #define XPT2046_IRQ_PORT XPT2046_IRQ_GPIO_Port
// #define XPT2046_IRQ_PIN  XPT2046_IRQ_Pin

// Calibration values (adjust based on your display)
// These should be calibrated for your specific touchscreen
#define XPT2046_X_MIN               160
#define XPT2046_Y_MIN               215
#define XPT2046_X_MAX               3870
#define XPT2046_Y_MAX               3910

// Coordinate adjustments (set to 1 to enable)
#define XPT2046_X_INV               0      // Invert X coordinate
#define XPT2046_Y_INV               0      // Invert Y coordinate
#define XPT2046_XY_SWAP             0      // Swap X and Y

// Touch detection threshold (increase if too sensitive)
#define XPT2046_TOUCH_THRESHOLD     500
#define XPT2046_READ_SAMPLES        7

// Number of samples to average (reduces noise)
#define XPT2046_AVG_SAMPLES         10

/* ============== PUBLIC API ============== */

/**
 * @brief Initialize XPT2046 touch controller
 */
void XPT2046_Init(void);

/**
 * @brief Read touch coordinates
 * @param x Pointer to store X coordinate (0 to screen width)
 * @param y Pointer to store Y coordinate (0 to screen height)
 * @return true if touch detected, false otherwise
 */
bool XPT2046_Read(int16_t *x, int16_t *y);

/**
 * @brief Check if touch is currently pressed
 * @return true if pressed, false otherwise
 */
bool XPT2046_IsTouched(void);

/**
 * @brief Read raw touch data (uncalibrated)
 * @param x Pointer to store raw X value
 * @param y Pointer to store raw Y value
 * @return true if touch detected, false otherwise
 */
bool XPT2046_ReadRaw(int16_t *x, int16_t *y);

/**
 * @brief Calibrate touch screen
 * @param x_min Minimum X value (touch top-left corner)
 * @param y_min Minimum Y value (touch top-left corner)
 * @param x_max Maximum X value (touch bottom-right corner)
 * @param y_max Maximum Y value (touch bottom-right corner)
 */
void XPT2046_Calibrate(int16_t x_min, int16_t y_min, int16_t x_max, int16_t y_max);

/**
 * @brief Set screen dimensions (must match ST7789 settings)
 * @param width Screen width in pixels
 * @param height Screen height in pixels
 */
void XPT2046_SetScreenSize(uint16_t width, uint16_t height);

/**
 * @brief Test: Display touch coordinates on screen.
 * Note: Requires ST7789_USE_FONTS enabled.
 */
void XPT2046_Test(void);

/**
 * @brief Check XPT2046 hardware.
 */
void XPT2046_HardwareTest(void);

/**
 * @brief Show real-time values.
 */
void XPT2046_LiveTest(void);

/**
 * @brief Run interactive 5-point calibration.
 */
void XPT2046_Calibration(void);

/**
 * @brief Show raw touch values real-time.
 */
void XPT2046_RawDiagnostic(void);

#endif // __XPT2046_H
