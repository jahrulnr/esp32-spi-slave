#ifndef INIT_H
#define INIT_H

#include "Config.h"

#include "lib/Communication/SPISlaveHandler.h"
#include "lib/Sensors/Camera.h"
#include "lib/Sensors/TemperatureSensor.h"
#include "lib/Utils/FileManager.h"
#include "lib/Utils/HealthCheck.h"
#include "lib/Utils/Logger.h"
#include "lib/Utils/SpiAllocator.h"
#include "lib/Utils/I2CScanner.h"
#include "lib/Utils/I2CManager.h"
#include "lib/Utils/Sstring.h"
#include "lib/Utils/CommandMapper.h"

// Camera frame structure to hold all camera-related data in one place
struct CameraFrame {
  uint8_t* data;           // Pointer to the camera frame data
  size_t length;           // Total length of the camera frame data
  uint16_t width;          // Width of the camera frame in pixels
  uint16_t height;         // Height of the camera frame in pixels
  uint16_t totalBlocks;    // Total number of blocks for this frame
  uint16_t blockSize;      // Size of each block in bytes
  bool isValid;            // Flag indicating if the frame data is valid
  uint32_t captureTime;    // Timestamp of when the frame was captured
  camera_fb_t* frameBuffer; // Original camera frame buffer (if still needed)
};

// Global camera frame variable
extern CameraFrame cameraFrame;

extern Communication::SPISlaveHandler* spiSlaveHandler;
extern Sensors::Camera* camera;
extern Sensors::TemperatureSensor* temperatureSensor;
extern Utils::FileManager* fileManager;
extern Utils::Logger* logger;
extern Utils::CommandMapper* commandMapper;

// Task handles
extern TaskHandle_t cameraStreamTaskHandle;

// Function prototypes
void setupSPISlaveCommunication();
void initializeCameraFrame();
void releaseCameraFrame();
bool captureCameraFrame();
bool isCameraFrameValid();

#endif