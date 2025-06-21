#pragma once

#include <Arduino.h>
#include <SPI.h>
#include "Config.h"
#include "lib/Utils/Logger.h"
#include <queue>
#include <driver/spi_slave.h>
#include <freertos/semphr.h>

// Added buffer pool size constant
#define SPI_BUFFER_POOL_SIZE 6  // Number of pre-allocated buffers in the pool

namespace Communication {

/**
 * @brief Command codes for SPI communication
 * These must match with the master device
 */
enum class SPICommand : uint8_t {
  PING = 0x01,
  PONG = 0x02,
  CAMERA_DATA_REQUEST = 0x20,        // New command for requesting camera data
  CAMERA_DATA_RESPONSE = 0x21,       // Response with camera data
  CAMERA_DATA_BLOCK_REQUEST = 0x22,  // Request for a specific block of camera data
  CAMERA_DATA_BLOCK_RESPONSE = 0x23, // Response with a specific block of camera data
  BUFFER_STATUS_REQUEST = 0x30,      // New command to check buffer status
  BUFFER_STATUS_RESPONSE = 0x31,     // Response with buffer status
  ACK = 0xAA,
  NACK = 0xFF
};

// Enhanced response codes
enum class SPIResponseCode : uint8_t {
  OK = 0x00,
  INCOMPLETE_PACKET = 0x10,
  LENGTH_MISMATCH = 0x11,
  CHECKSUM_ERROR = 0x12,
  BUFFER_FULL = 0x20,     // Added for flow control
  NOT_READY = 0x21,       // Added for flow control
  CAMERA_NOT_AVAILABLE = 0x30,
  CAMERA_CAPTURE_FAILED = 0x31,
  INVALID_BLOCK_INDEX = 0x32,
  MEMORY_ERROR = 0x40,
};

// Struct to store SPI data for the receive queue
struct SPIDataPacket {
  uint8_t* data;
  size_t length;
  bool processed;
  
  SPIDataPacket(const uint8_t* srcData, size_t len) {
    length = len;
    processed = false;
    data = new uint8_t[length];
    memcpy(data, srcData, length);
  }
  
  ~SPIDataPacket() {
    if (data) {
      delete[] data;
    }
  }
};

// New buffer pool structure
struct SPIBuffer {
  uint8_t* data;
  bool inUse;
  
  SPIBuffer() : data(nullptr), inUse(false) {}
};

/**
 * @brief SPI slave handler for ESP32
 * 
 * This class handles the SPI slave functionality including:
 * - Initializing the SPI slave interface
 * - Receiving data from the master
 * - Sending data to the master
 * - Implementing a websocket-like flow where reception of data queues up for handling
 */
class SPISlaveHandler {
public:
  /**
   * @brief Get the singleton instance of SPISlaveHandler
   * @return SPISlaveHandler& Reference to the singleton instance
   */
  static SPISlaveHandler& getInstance();

  /**
   * @brief Initialize the SPI slave interface
   * @param sckPin SCK pin number (optional, default is CONFIG defined)
   * @param misoPin MISO pin number (optional, default is CONFIG defined)
   * @param mosiPin MOSI pin number (optional, default is CONFIG defined)
   * @param csPin CS pin number (optional, default is CONFIG defined)
   * @param mode SPI mode (0-3)
   * @return true if initialization was successful, false otherwise
   */
  bool init(int sckPin = -1, int misoPin = -1, int mosiPin = -1, int csPin = -1, uint8_t mode = SPI_MODE0);

  /**
   * @brief Prepare data to be sent to master when it initiates a transaction
   * @param txData Pointer to data to prepare for sending
   * @param length Length of data to send in bytes
   * @return true if preparation was successful, false otherwise
   */
  bool prepareDataToSend(const uint8_t* txData, size_t length);

  /**
   * @brief Process the next pending receive data packet
   * @return true if a packet was processed, false if queue is empty
   */
  bool processNextReceive();
  
  /**
   * @brief Register a callback function for received data
   * @param callback Function to call when data is received
   */
  typedef void (*ReceiveCallback)(const uint8_t* data, size_t length);
  void setReceiveCallback(ReceiveCallback callback);
  
  /**
   * @brief Check if there are pending receive packets
   * @return Number of pending packets
   */
  size_t pendingReceiveCount();

  /**
   * @brief Check if the slave is currently ready to send data
   * @return true if ready to send, false otherwise
   */
  bool isReadyToSend();
  
  /**
   * @brief Ensure that a transaction is queued up for the next SPI communication
   * @return true if a new transaction was queued, false otherwise
   */
  bool ensureTransactionQueued();
  
  /**
   * @brief Check for stalled SPI transactions and recover if necessary
   * This should be called periodically from the main loop
   * @return true if a recovery action was taken
   */
  bool checkAndRecoverFromStalledTransaction();
  
  /**
   * @brief Reset the SPI interface in case of errors
   * This will reinitialize the SPI interface with the same parameters
   * @return true if reset was successful
   */
  bool resetSPIInterface();

  /**
   * @brief Get buffer status for flow control
   * @return percentage of receive queue filled (0-100)
   */
  uint8_t getBufferStatus();

private:
  SPISlaveHandler();  // Private constructor for singleton
  ~SPISlaveHandler();

  // Prevent copying
  SPISlaveHandler(const SPISlaveHandler&) = delete;
  SPISlaveHandler& operator=(const SPISlaveHandler&) = delete;

  /**
   * @brief Handle SPI transaction events
   */
  static void IRAM_ATTR onSpiTransaction(spi_slave_transaction_t *trans);
  static void IRAM_ATTR onSpiPreTransaction(spi_slave_transaction_t *trans);
  
  // Mutex for thread safety
  portMUX_TYPE _mux;

  /**
   * @brief Handle received data internally or through callback
   * @param data Pointer to received data
   * @param length Length of received data
   */
  void handleReceivedData(const uint8_t* data, size_t length);

  /**
   * @brief Get a buffer from the pool or allocate a new one
   * @return Pointer to available buffer, or nullptr if none available
   */
  uint8_t* getBuffer();

  /**
   * @brief Return buffer to pool when done
   * @param buffer Pointer to buffer to return
   */
  void returnBuffer(uint8_t* buffer);

  // Buffer management
  uint8_t* _txBuffer;
  uint8_t* _rxBuffer;
  size_t _bufferSize;
  volatile size_t _txLength;
  volatile bool _dataReady;
  volatile bool _needsNewTransaction;  // Flag to indicate a new transaction needs to be queued
  
  // Transaction watchdog variables
  unsigned long _lastTransactionTime;  // Time of last transaction activity
  unsigned long _transactionTimeout;   // Timeout in milliseconds
  bool _transactionActive;             // Flag to track if a transaction is active
  uint32_t _transactionCount;          // Counter for completed transactions
  uint32_t _recoveryAttempts;          // Counter for recovery attempts
  
  // Queue for received data packets
  std::queue<SPIDataPacket*> _receiveQueue;
  
  // Semaphore for controlling queue access
  SemaphoreHandle_t _queueSemaphore;
  
  // Buffer pool
  SPIBuffer _bufferPool[SPI_BUFFER_POOL_SIZE];
  
  // Max queue size for flow control
  const size_t _maxQueueSize = 10;  // Limit receive queue size for flow control
  
  // Callback for receive events
  ReceiveCallback _receiveCallback;
  
  // SPI hardware handle
  spi_slave_transaction_t _transaction;
  spi_slave_interface_config_t _slaveConfig;
  spi_bus_config_t _busConfig;
  
  // Logger instance
  Utils::Logger* _logger;

  // Flag to track initialization state
  bool _initialized;
  
  // SPI pins
  uint8_t _sckPin;
  uint8_t _misoPin;
  uint8_t _mosiPin;
  uint8_t _csPin;
  uint8_t _mode;
};

} // namespace Communication
