#pragma once

#define DEVELOPMENT

// SPI Configuration

#ifdef DEVELOPMENT
#define SPI_SCK_PIN (int)12
#define SPI_MISO_PIN (int)13
#define SPI_MOSI_PIN (int)15
#define SPI_ESP32_SS (int)14
#define SPI_DMA_CHANNEL SPI_DMA_CH_AUTO
#define SPI_BUFFER_SIZE 8096
#define SPI_ACTIVITY_LED_PIN (int)2  // Onboard LED on most ESP32 boards

// Camera configuration
#define CAMERA_ENABLED true
#define CAMERA_FRAME_SIZE FRAMESIZE_QVGA
#define CAMERA_QUALITY 12
#define CAMERA_FPS 15

// Health check configuration
#define HEALTH_CHECK_ENABLED true
#define HEALTH_CHECK_INTERVAL 10000  // milliseconds

// Miscellaneous
#define SERIAL_BAUD_RATE 115200
#define DEBUG_ENABLED true

#else
#define SPI_SCK_PIN 36
#define SPI_MISO_PIN 37
#define SPI_MOSI_PIN 35
#define SPI_ESP32_SS 38

// Camera configuration
#define CAMERA_ENABLED false
#define CAMERA_FRAME_SIZE FRAMESIZE_VGA
#define CAMERA_QUALITY 12
#define CAMERA_FPS 15

// Health check configuration
#define HEALTH_CHECK_ENABLED true
#define HEALTH_CHECK_INTERVAL 10000  // milliseconds

// Miscellaneous
#define SERIAL_BAUD_RATE 115200
#define DEBUG_ENABLED true
#endif