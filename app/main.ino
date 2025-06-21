#include <Arduino.h>
#include <algorithm>
#include "app.h"
#include "Config.h"
#include "lib/Communication/SPISlaveHandler.h"

Communication::SPISlaveHandler* spiSlaveHandler = nullptr;
Sensors::Camera* camera = nullptr;
Sensors::TemperatureSensor* temperatureSensor = nullptr;
Utils::FileManager* fileManager = nullptr;
Utils::Logger* logger = nullptr;
Utils::CommandMapper* commandMapper = nullptr;

int cameraBufferSended = 0;

// Global camera frame structure that holds all camera-related data
CameraFrame cameraFrame = {
  nullptr,    // data
  0,          // length
  0,          // width
  0,          // height
  0,          // totalBlocks
  1024,       // blockSize (default)
  false,      // isValid
  0,          // captureTime
  nullptr     // frameBuffer
};

// Initialize the camera frame structure
void initializeCameraFrame() {
  // Make sure any existing data is released
  releaseCameraFrame();
  
  // Set default values
  cameraFrame.data = nullptr;
  cameraFrame.length = 0;
  cameraFrame.width = 0;
  cameraFrame.height = 0;
  cameraFrame.totalBlocks = 0;
  cameraFrame.blockSize = SPI_BUFFER_SIZE - (16*8); // Default block size
  cameraFrame.isValid = false;
  cameraFrame.captureTime = 0;
  cameraFrame.frameBuffer = nullptr;
  
  if (logger) {
    logger->debug("Camera frame initialized");
  }
}

// Release camera frame resources
void releaseCameraFrame() {
  if (cameraFrame.data != nullptr) {
    free(cameraFrame.data);
    cameraFrame.data = nullptr;
  }
  
  if (cameraFrame.frameBuffer != nullptr) {
    if (camera) {
      camera->returnFrame(cameraFrame.frameBuffer);
    }
    cameraFrame.frameBuffer = nullptr;
  }
  
  cameraFrame.isValid = false;
  
  if (logger) {
    logger->debug("Camera frame resources released");
  }
}

// Capture a new camera frame and store it in the global structure
bool captureCameraFrame() {
  if (!camera || !CAMERA_ENABLED) {
    if (logger) {
      logger->error("Camera not available");
    }
    return false;
  }
  
  // Release any existing frame
  releaseCameraFrame();
  
  // Capture a new frame
  cameraFrame.frameBuffer = camera->captureFrame();
  
  if (!cameraFrame.frameBuffer) {
    if (logger) {
      logger->error("Failed to capture camera frame");
    }
    return false;
  }
  
  // Store the frame data
  cameraFrame.length = cameraFrame.frameBuffer->len;
  cameraFrame.data = (uint8_t*)malloc(cameraFrame.length);
  
  if (!cameraFrame.data) {
    if (logger) {
      logger->error("Failed to allocate memory for camera frame");
    }
    camera->returnFrame(cameraFrame.frameBuffer);
    cameraFrame.frameBuffer = nullptr;
    return false;
  }
  
  // Copy the data from frame buffer
  memcpy(cameraFrame.data, cameraFrame.frameBuffer->buf, cameraFrame.length);
  
  // Get frame dimensions
  framesize_t resolution = camera->getResolution();
  switch (resolution) {
    case FRAMESIZE_QQVGA: cameraFrame.width = 160; cameraFrame.height = 120; break;
    case FRAMESIZE_QVGA: cameraFrame.width = 320; cameraFrame.height = 240; break;
    case FRAMESIZE_VGA: cameraFrame.width = 640; cameraFrame.height = 480; break;
    case FRAMESIZE_SVGA: cameraFrame.width = 800; cameraFrame.height = 600; break;
    case FRAMESIZE_XGA: cameraFrame.width = 1024; cameraFrame.height = 768; break;
    case FRAMESIZE_HD: cameraFrame.width = 1280; cameraFrame.height = 720; break;
    case FRAMESIZE_SXGA: cameraFrame.width = 1280; cameraFrame.height = 1024; break;
    case FRAMESIZE_UXGA: cameraFrame.width = 1600; cameraFrame.height = 1200; break;
    default: cameraFrame.width = 320; cameraFrame.height = 240; break;
  }
  
  // Calculate total blocks
  cameraFrame.totalBlocks = (cameraFrame.length + cameraFrame.blockSize - 1) / cameraFrame.blockSize;
  
  // Update frame metadata
  cameraFrame.isValid = true;
  cameraFrame.captureTime = millis();
  
  // Release the original frame buffer after copying its data
  camera->returnFrame(cameraFrame.frameBuffer);
  cameraFrame.frameBuffer = nullptr;
  
  if (logger) {
    logger->info("Camera frame captured: %dx%d, %d bytes, %d blocks", 
                cameraFrame.width, cameraFrame.height, 
                cameraFrame.length, cameraFrame.totalBlocks);
  }
  
  return true;
}

// Check if the camera frame is valid
bool isCameraFrameValid() {
  return cameraFrame.isValid && cameraFrame.data != nullptr && cameraFrame.length > 0;
}

// Callback function to handle received SPI data
void onDataReceived(const uint8_t* data, size_t length) {
  if (logger) {
    logger->info("Received %d bytes of data via SPI", length);
    
    // Print out the received data for debugging
    String dataHex = "";
    for (size_t i = 0; i < min(length, (size_t)16); i++) {
      char buf[8];
      snprintf(buf, sizeof(buf), "0x%02X ", data[i]);
      dataHex += buf;
    }
    logger->debug("Data: %s", dataHex.c_str());
  }
  
  // Process received data
  if (length > 0 && spiSlaveHandler) {
    Communication::SPICommand cmd = static_cast<Communication::SPICommand>(data[0]);
    
    switch (cmd) {
      case Communication::SPICommand::PING: {
        logger->info("Received PING command, responding with PONG");
        // Echo back any additional data that came with the PING
        size_t responseLen = length > 16 ? 16 : length;
        uint8_t* response = new uint8_t[responseLen];
        
        response[0] = static_cast<uint8_t>(Communication::SPICommand::PONG);
        // Copy any additional data from the PING command
        if (length > 1) {
          memcpy(response + 1, data + 1, responseLen - 1);
        } else {
          response[1] = 0x00;  // Default response if no additional data
        }
        
        spiSlaveHandler->prepareDataToSend(response, responseLen);
        delete[] response;
        break;
      }
      
      case Communication::SPICommand::CAMERA_DATA_REQUEST: {
        logger->info("Received camera data request");
        
        // Check if camera is available
        if (!CAMERA_ENABLED || !camera) {
          logger->warning("Camera not available");
          uint8_t response[2] = {static_cast<uint8_t>(Communication::SPICommand::NACK), 0x01};
          spiSlaveHandler->prepareDataToSend(response, 2);
          break;
        }
        
        // Capture a new frame using our helper function
        if (!captureCameraFrame()) {
          uint8_t response[2] = {static_cast<uint8_t>(Communication::SPICommand::NACK), 0x02};
          spiSlaveHandler->prepareDataToSend(response, 2);
          break;
        }
        
        // Prepare response with metadata about the camera frame
        uint8_t response[16] = {
          static_cast<uint8_t>(Communication::SPICommand::CAMERA_DATA_RESPONSE),
          0x01,  // Data version
          static_cast<uint8_t>((cameraFrame.width >> 8) & 0xFF),  // Width high byte
          static_cast<uint8_t>(cameraFrame.width & 0xFF),         // Width low byte
          static_cast<uint8_t>((cameraFrame.height >> 8) & 0xFF), // Height high byte
          static_cast<uint8_t>(cameraFrame.height & 0xFF),        // Height low byte
          static_cast<uint8_t>((cameraFrame.totalBlocks >> 8) & 0xFF),   // Total blocks high byte
          static_cast<uint8_t>(cameraFrame.totalBlocks & 0xFF),          // Total blocks low byte
          static_cast<uint8_t>((cameraFrame.blockSize >> 8) & 0xFF),     // Block size high byte
          static_cast<uint8_t>(cameraFrame.blockSize & 0xFF),            // Block size low byte
          static_cast<uint8_t>((cameraFrame.length >> 24) & 0xFF), // Length byte 3
          static_cast<uint8_t>((cameraFrame.length >> 16) & 0xFF), // Length byte 2
          static_cast<uint8_t>((cameraFrame.length >> 8) & 0xFF),  // Length byte 1
          static_cast<uint8_t>(cameraFrame.length & 0xFF),         // Length byte 0
          0x00, 0x00  // Reserved
        };
        
        spiSlaveHandler->prepareDataToSend(response, sizeof(response));
        logger->info("Camera data ready: %d bytes, %d blocks", cameraFrame.length, cameraFrame.totalBlocks);
        cameraBufferSended = 0;
        break;
      }
      
      case Communication::SPICommand::CAMERA_DATA_BLOCK_REQUEST: {
        if (length < 3) {
          logger->error("Invalid block request format");
          uint8_t response[2] = {static_cast<uint8_t>(Communication::SPICommand::NACK), 0x04};
          spiSlaveHandler->prepareDataToSend(response, 2);
          break;
        }
        
        // Extract block index from request
        uint16_t blockIndex = (data[1] << 8) | data[2];

        if (cameraBufferSended < blockIndex) {
          logger->info("Received request for camera data block %d, but already sended. send next block %d", blockIndex, cameraBufferSended);
          blockIndex = cameraBufferSended;
        } else {
          logger->info("Received request for camera data block %d", blockIndex);        
        }
        
        // Validate request using our helper function
        if (!isCameraFrameValid()) {
          logger->error("No camera frame data available");
          uint8_t response[2] = {static_cast<uint8_t>(Communication::SPICommand::NACK), 0x05};
          spiSlaveHandler->prepareDataToSend(response, 2);
          break;
        }
        
        if (blockIndex >= cameraFrame.totalBlocks) {
          logger->error("Invalid block index: %d >= %d", blockIndex, cameraFrame.totalBlocks);
          uint8_t response[2] = {static_cast<uint8_t>(Communication::SPICommand::NACK), 0x06};
          spiSlaveHandler->prepareDataToSend(response, 2);
          break;
        }
        
        // Calculate start offset and data length for this block
        size_t startOffset = blockIndex * cameraFrame.blockSize;
        size_t remainingBytes = (startOffset < cameraFrame.length) ? (cameraFrame.length - startOffset) : 0;
        size_t dataLength = (cameraFrame.blockSize < remainingBytes) ? cameraFrame.blockSize : remainingBytes;
        
        // Prepare header (command + block index + data length)
        uint8_t* response = (uint8_t*)malloc(5 + dataLength);
        if (!response) {
          logger->error("Failed to allocate memory for block response");
          uint8_t errResponse[2] = {static_cast<uint8_t>(Communication::SPICommand::NACK), 0x07};
          spiSlaveHandler->prepareDataToSend(errResponse, 2);
          break;
        }
        
        response[0] = static_cast<uint8_t>(Communication::SPICommand::CAMERA_DATA_BLOCK_RESPONSE);
        response[1] = data[1]; // Block index high byte
        response[2] = data[2]; // Block index low byte
        response[3] = (dataLength >> 8) & 0xFF; // Data length high byte
        response[4] = dataLength & 0xFF;        // Data length low byte
        
        // Copy the block data
        memcpy(response + 5, cameraFrame.data + startOffset, dataLength);
        
        // Send the response
        spiSlaveHandler->prepareDataToSend(response, 5 + dataLength);
        free(response);
        
        cameraBufferSended = blockIndex + 1;

        logger->info("Sent camera data block %d, %d bytes", blockIndex, dataLength);
        break;
      }
      
      case Communication::SPICommand::ACK:
        logger->debug("Received ACK");
        break;
        
      case Communication::SPICommand::NACK:
        logger->warning("Received NACK");
        break;
        
      default:
        logger->debug("Received unknown command: 0x%02X", static_cast<uint8_t>(cmd));
        // Respond with ACK by default
        uint8_t response[2] = {static_cast<uint8_t>(Communication::SPICommand::ACK), data[0]};
        spiSlaveHandler->prepareDataToSend(response, 2);
        break;
    }
  }
}

// Function to initialize camera
bool initializeCamera() {
  if (!camera) {
    camera = new Sensors::Camera();
  }
  
  if (!camera) {
    logger->error("Failed to create camera instance");
    return false;
  }
  
  bool result = camera->init();
  if (result) {
    logger->info("Camera initialized successfully");
    camera->setResolution(CAMERA_FRAME_SIZE);
    logger->info("Camera resolution set to %d", (int)camera->getResolution());
  } else {
    logger->error("Failed to initialize camera");
  }
  
  return result;
}

// Implementation of setupSPISlaveCommunication
void setupSPISlaveCommunication() {
  // Get SPISlaveHandler instance
  spiSlaveHandler = &Communication::SPISlaveHandler::getInstance();
  
  // Initialize SPI with configured pins and SPI mode
  if (!spiSlaveHandler->init(SPI_SCK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN, SPI_ESP32_SS, SPI_MODE1)) {
    logger->error("Failed to initialize SPI slave");
    return;
  }
  
  // Register our callback for received data
  spiSlaveHandler->setReceiveCallback(onDataReceived);
  
  // Prepare initial response data (idle data that will be sent on first transaction)
  uint8_t initialData[4] = {0xAA, 0x55, 0xAA, 0x55}; // Recognizable pattern
  spiSlaveHandler->prepareDataToSend(initialData, sizeof(initialData));
  
  logger->info("SPI slave communication initialized successfully");
}

// Implementation of loopSPISlaveHandler
void loopSPISlaveHandler() {
  if (!spiSlaveHandler) return;
  
  // Process any pending receive operations
  while (spiSlaveHandler->processNextReceive()) {
    // Each iteration processes one queued receive packet
  }
  
  // Ensure a transaction is queued and ready to receive data
  static unsigned long lastQueueTime = 0;
  const unsigned long QUEUE_CHECK_INTERVAL = 33; // Check every 50ms
  
  if (millis() - lastQueueTime > QUEUE_CHECK_INTERVAL) {
    lastQueueTime = millis();
    spiSlaveHandler->ensureTransactionQueued();
  }
  
  // Check for stalled transactions and recover if needed
  static unsigned long lastWatchdogCheck = 0;
  const unsigned long WATCHDOG_CHECK_INTERVAL = 15000; // Check every second
  
  if (millis() - lastWatchdogCheck > WATCHDOG_CHECK_INTERVAL) {
    lastWatchdogCheck = millis();
    
    // If a recovery action was taken, log it
    if (spiSlaveHandler->checkAndRecoverFromStalledTransaction()) {
      logger->warning("SPI transaction watchdog triggered recovery action");
    }
  }
  
  // Send periodic ping if no activity (optional)
  static unsigned long lastActivity = 0;
  const unsigned long PING_INTERVAL = 5000; // 5 seconds
  
  if (millis() - lastActivity > PING_INTERVAL && spiSlaveHandler->isReadyToSend()) {
    lastActivity = millis();
    
    // Only send ping in idle state
    if (spiSlaveHandler->pendingReceiveCount() == 0) {
      uint8_t pingData[2] = {static_cast<uint8_t>(Communication::SPICommand::PING), 0x00};
      spiSlaveHandler->prepareDataToSend(pingData, sizeof(pingData));
      logger->debug("Sent ping to master");
    }
  }
}

void setup() {
  // Initialize serial communication
  Serial.begin(SERIAL_BAUD_RATE);
  
  // Initialize Logger
  logger = &Utils::Logger::getInstance();
  logger->init(true);
  logger->setLogLevel(Utils::LogLevel::DEBUG);
  logger->info("Logger initialized");
  
  // Log SPI pin configuration for troubleshooting
  logger->info("SPI Configuration:");
  logger->info(" - SCK Pin: %d", SPI_SCK_PIN);
  logger->info(" - MISO Pin: %d", SPI_MISO_PIN);
  logger->info(" - MOSI Pin: %d", SPI_MOSI_PIN);
  logger->info(" - SS Pin: %d", SPI_ESP32_SS);
  logger->info(" - Mode: SPI_MODE1");

  // Initialize the camera frame structure
  initializeCameraFrame();

  // Initialize SPI Slave Handler
  setupSPISlaveCommunication();
  if (spiSlaveHandler && spiSlaveHandler->isReadyToSend()) {
    logger->info("SPI Slave Handler initialized successfully");
  } else {
    logger->error("Failed to initialize SPI Slave");
  }
  
  // Initialize camera if enabled
  if (CAMERA_ENABLED) {
    logger->info("Initializing camera...");
    if (initializeCamera()) {
      logger->info("Camera initialized successfully");
    } else {
      logger->error("Failed to initialize camera");
    }
  } else {
    logger->info("Camera disabled in configuration");
  }
  
  // Set up a periodic timer to send a ping if no data is received
  logger->info("SPI Slave is ready and waiting for master...");
}

void loop() {
  // Process any pending SPI receive operations
  loopSPISlaveHandler();
  
  // Give other tasks a chance to run
  vTaskDelay(33);
}