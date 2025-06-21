# ESP32 Slave SPI Camera System

This project implements an ESP32-based slave device that communicates with a master device via SPI protocol. It provides camera functionality with the ability to capture and transmit images on demand.

## Features

- SPI Slave implementation for ESP32
- Camera capture and data block transmission
- Temperature sensing capabilities
- Health monitoring system
- Configurable development and production settings

## Hardware Requirements

- ESP32 or ESP32-CAM board
- Camera module (compatible with ESP32-CAM)
- SPI connection to master device

## Pin Configuration

### Development Environment
| Function | Pin Number |
|----------|------------|
| SPI SCK  | 12         |
| SPI MISO | 13         |
| SPI MOSI | 15         |
| SPI SS   | 14         |
| LED      | 2          |

### Production Environment
| Function | Pin Number |
|----------|------------|
| SPI SCK  | 36         |
| SPI MISO | 37         |
| SPI MOSI | 35         |
| SPI SS   | 38         |

## Software Architecture

The project is structured into several components:

- **Communication:** Handles SPI communication protocol
- **Sensors:** Manages camera and temperature sensors
- **Utils:** Provides utility functions for I2C, logging, health checks, etc.

## SPI Command Protocol

| Command                   | Value | Description                                     |
|---------------------------|-------|-------------------------------------------------|
| PING                      | 0x01  | Check if device is responsive                   |
| PONG                      | 0x02  | Response to PING command                        |
| ACK                       | 0x03  | Acknowledge receipt of command                  |
| NACK                      | 0x04  | Negative acknowledgment                         |
| CAMERA_DATA_REQUEST       | 0x10  | Request to capture and prepare camera data      |
| CAMERA_DATA_RESPONSE      | 0x11  | Response with metadata about camera frame       |
| CAMERA_DATA_BLOCK_REQUEST | 0x12  | Request a specific block of camera data         |
| CAMERA_DATA_BLOCK_RESPONSE| 0x13  | Response with a block of camera data            |

## Camera Frame Structure

The camera frame is divided into blocks for transmission:

```
struct CameraFrame {
  uint8_t* data;           // Pointer to the camera frame data
  size_t length;           // Total length of the camera frame data
  uint16_t width;          // Width of the camera frame in pixels
  uint16_t height;         // Height of the camera frame in pixels
  uint16_t totalBlocks;    // Total number of blocks for this frame
  uint16_t blockSize;      // Size of each block in bytes
  bool isValid;            // Flag indicating if the frame data is valid
  uint32_t captureTime;    // Timestamp when the frame was captured
  camera_fb_t* frameBuffer; // Original camera frame buffer
};
```

## Building and Uploading

To build and upload the code to your ESP32 device:

```bash
./pio.sh run -t upload
```

Alternatively, you can use the "Build and Upload ESP32 Slave Code" task in VS Code.

## Configuration

Configuration parameters are defined in `include/Config.h`. You can switch between development and production environments by enabling/disabling the `DEVELOPMENT` define.

## Debugging

The project includes a comprehensive logging system that can be controlled via the `DEBUG_ENABLED` and log level settings in the code.
