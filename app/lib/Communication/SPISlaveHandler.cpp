#include "SPISlaveHandler.h"
#include <esp_system.h>

namespace Communication {

// Static instance pointer for access in ISR callbacks
static SPISlaveHandler* s_instance = nullptr;

SPISlaveHandler& SPISlaveHandler::getInstance() {
  static SPISlaveHandler instance;
  return instance;
}

SPISlaveHandler::SPISlaveHandler() : 
  _txBuffer(nullptr), 
  _rxBuffer(nullptr), 
  _bufferSize(SPI_BUFFER_SIZE), // Default buffer size
  _txLength(0),
  _dataReady(false),
  _needsNewTransaction(false),
  _lastTransactionTime(0),
  _transactionTimeout(3000), // 3 seconds timeout
  _transactionActive(false),
  _transactionCount(0),
  _recoveryAttempts(0),
  _receiveCallback(nullptr),
  _initialized(false),
  _sckPin(0),
  _misoPin(0),
  _mosiPin(0),
  _csPin(0),
  _mode(SPI_MODE0),
  _mux(portMUX_INITIALIZER_UNLOCKED) {
  
  _logger = &Utils::Logger::getInstance();
  
  // Create semaphore for queue access
  _queueSemaphore = xSemaphoreCreateMutex();
  if (!_queueSemaphore) {
    _logger->error("SPISlaveHandler: Failed to create queue semaphore");
  }
  
  // Allocate DMA-capable buffer pool
  for (int i = 0; i < SPI_BUFFER_POOL_SIZE; i++) {
    _bufferPool[i].data = (uint8_t*) heap_caps_malloc(_bufferSize, MALLOC_CAP_DMA | MALLOC_CAP_DEFAULT);
    _bufferPool[i].inUse = false;
    
    if (!_bufferPool[i].data) {
      _logger->error("SPISlaveHandler: Failed to allocate buffer %d for pool", i);
    } else {
      memset(_bufferPool[i].data, 0, _bufferSize);
    }
  }
  
  // Allocate main transaction buffers
  _txBuffer = (uint8_t*) heap_caps_malloc(_bufferSize, MALLOC_CAP_DMA | MALLOC_CAP_DEFAULT);
  _rxBuffer = (uint8_t*) heap_caps_malloc(_bufferSize, MALLOC_CAP_DMA | MALLOC_CAP_DEFAULT);
  
  if (!_txBuffer || !_rxBuffer) {
    _logger->error("SPISlaveHandler: Failed to allocate buffers");
  }
  
  // Clear the buffers
  memset(_txBuffer, 0, _bufferSize);
  memset(_rxBuffer, 0, _bufferSize);
  
  // Save instance pointer for ISR callbacks
  s_instance = this;
}

SPISlaveHandler::~SPISlaveHandler() {
  // Free ESP32 SPI hardware
  if (_initialized) {
    spi_slave_free(HSPI_HOST);
  }
  
  // Clean up buffers
  if (_txBuffer) {
    heap_caps_free(_txBuffer);
  }
  
  if (_rxBuffer) {
    heap_caps_free(_rxBuffer);
  }
  
  // Free buffer pool
  for (int i = 0; i < SPI_BUFFER_POOL_SIZE; i++) {
    if (_bufferPool[i].data) {
      heap_caps_free(_bufferPool[i].data);
      _bufferPool[i].data = nullptr;
    }
  }
  
  // Delete semaphore
  if (_queueSemaphore) {
    vSemaphoreDelete(_queueSemaphore);
  }
  
  // Clean up receive queue
  while (!_receiveQueue.empty()) {
    SPIDataPacket* packet = _receiveQueue.front();
    _receiveQueue.pop();
    delete packet;
  }
}

// Get a buffer from the pool
uint8_t* SPISlaveHandler::getBuffer() {
  // Try to get a buffer from the pool first
  for (int i = 0; i < SPI_BUFFER_POOL_SIZE; i++) {
    if (!_bufferPool[i].inUse && _bufferPool[i].data) {
      _bufferPool[i].inUse = true;
      return _bufferPool[i].data;
    }
  }
  
  // If no buffers available in pool, log warning and allocate a new one
  _logger->warning("SPISlaveHandler: No buffers available in pool, allocating new buffer");
  return (uint8_t*) heap_caps_malloc(_bufferSize, MALLOC_CAP_DMA | MALLOC_CAP_DEFAULT);
}

// Return a buffer to the pool
void SPISlaveHandler::returnBuffer(uint8_t* buffer) {
  // Check if buffer is from our pool
  for (int i = 0; i < SPI_BUFFER_POOL_SIZE; i++) {
    if (_bufferPool[i].data == buffer) {
      _bufferPool[i].inUse = false;
      memset(buffer, 0, _bufferSize); // Clear buffer for reuse
      return;
    }
  }
  
  // If not from pool, free it
  heap_caps_free(buffer);
}

// Static callback handlers that work with the ESP32 SPI slave driver
void IRAM_ATTR SPISlaveHandler::onSpiTransaction(spi_slave_transaction_t *trans) {
  if (!s_instance) return;
  
  // Update transaction activity tracking
  s_instance->_lastTransactionTime = millis();
  s_instance->_transactionActive = false;
  s_instance->_transactionCount++;
  
  // Calculate the actual number of bytes received
  size_t rxBytes = trans->trans_len / 8;
  if (rxBytes == 0) return;
  
  // Check if receive queue is getting full - drop packets if necessary for flow control
  bool queueFull = s_instance->pendingReceiveCount() >= s_instance->_maxQueueSize;
  
  if (!queueFull) {
    // Get a buffer from the pool for the packet
    uint8_t* packetBuffer = s_instance->getBuffer();
    
    if (packetBuffer) {
      // Copy data from rx buffer to packet buffer
      memcpy(packetBuffer, trans->rx_buffer, rxBytes);
      
      // Create packet object using pooled buffer
      SPIDataPacket* packet = new SPIDataPacket(packetBuffer, rxBytes);
      
      // Return buffer to pool now that it's been copied to the packet
      s_instance->returnBuffer(packetBuffer);
      
      // Try to acquire semaphore for queue access
      // Use ISR-safe version since we're in an interrupt
      if (xSemaphoreTakeFromISR(s_instance->_queueSemaphore, NULL) == pdTRUE) {
        s_instance->_receiveQueue.push(packet);
        xSemaphoreGiveFromISR(s_instance->_queueSemaphore, NULL);
      } else {
        // If we can't get the semaphore, drop the packet
        delete packet;
      }
    }
  }
  
  // Clear the rx buffer for next transaction
  memset(trans->rx_buffer, 0, s_instance->_bufferSize);
  
  // Reset the data ready flag since the transaction is complete
  s_instance->_dataReady = false;
  
  // Queue up another transaction
  // We can't call ensureTransactionQueued from ISR, so set a flag
  s_instance->_needsNewTransaction = true;
}

void IRAM_ATTR SPISlaveHandler::onSpiPreTransaction(spi_slave_transaction_t *trans) {
  // This is called before a transaction starts
  if (s_instance) {
    // Mark that a transaction is now active
    s_instance->_transactionActive = true;
  }
}

bool SPISlaveHandler::init(int sckPin, int misoPin, int mosiPin, int csPin, uint8_t mode) {
  if (_initialized) {
    _logger->warning("SPISlaveHandler: Already initialized");
    return true;
  }
  
  _mode = mode;
  
  // Use provided pins or defaults from Config
  _sckPin = (sckPin != -1) ? sckPin : SPI_SCK_PIN;
  _misoPin = (misoPin != -1) ? misoPin : SPI_MISO_PIN;
  _mosiPin = (mosiPin != -1) ? mosiPin : SPI_MOSI_PIN;
  _csPin = (csPin != -1) ? csPin : SPI_ESP32_SS;
  
  _logger->info("SPISlaveHandler: Initializing with SCK=%d, MISO=%d, MOSI=%d, CS=%d", 
               _sckPin, _misoPin, _mosiPin, _csPin);
  
  // Configure SPI bus
  _busConfig.flags = 0;
  _busConfig.intr_flags = 0;
  _busConfig.mosi_io_num = _mosiPin;
  _busConfig.miso_io_num = _misoPin;
  _busConfig.sclk_io_num = _sckPin;
  _busConfig.quadwp_io_num = -1;
  _busConfig.quadhd_io_num = -1;
  _busConfig.max_transfer_sz = _bufferSize;
  
  // Configure SPI slave interface
  _slaveConfig.mode = _mode;
  _slaveConfig.spics_io_num = _csPin;
  _slaveConfig.flags = 0;
  _slaveConfig.queue_size = 5;  // Increased queue size to 5 transactions
  _slaveConfig.post_setup_cb = onSpiPreTransaction;
  _slaveConfig.post_trans_cb = onSpiTransaction;
  
  // Initialize SPI slave driver
  esp_err_t ret = spi_slave_initialize(HSPI_HOST, &_busConfig, &_slaveConfig, SPI_DMA_CH_AUTO);
  if (ret != ESP_OK) {
    _logger->error("SPISlaveHandler: Failed to initialize SPI slave driver: %d", ret);
    return false;
  }
  
  // Set up initial transaction
  memset(&_transaction, 0, sizeof(_transaction));
  _transaction.length = _bufferSize * 8; // Length in bits
  _transaction.tx_buffer = _txBuffer;
  _transaction.rx_buffer = _rxBuffer;
  
  // Queue more transactions for faster response
  for (int i = 0; i < 3; i++) {
    ret = spi_slave_queue_trans(HSPI_HOST, &_transaction, portMAX_DELAY);
    if (ret != ESP_OK) {
      _logger->error("SPISlaveHandler: Failed to queue transaction %d: %d", i, ret);
      return false;
    }
  }
  
  _initialized = true;
  _logger->info("SPISlaveHandler: Initialized successfully with %d transactions queued", 3);
  return true;
}

bool SPISlaveHandler::prepareDataToSend(const uint8_t* txData, size_t length) {
  if (!_initialized) {
    _logger->error("SPISlaveHandler: Not initialized");
    return false;
  }
  
  if (!txData || length == 0 || length > _bufferSize) {
    _logger->error("SPISlaveHandler: Invalid data or length");
    return false;
  }
  
  // Critical section to avoid race condition with ISR
  portENTER_CRITICAL(&_mux);
  
  // Clear tx buffer first
  memset(_txBuffer, 0, _bufferSize);
  
  // Copy provided data to tx buffer
  memcpy(_txBuffer, txData, length);
  _txLength = length;
  _dataReady = true;
  
  portEXIT_CRITICAL(&_mux);
  
  _logger->debug("SPISlaveHandler: Data prepared for sending (%d bytes)", length);
  return true;
}

bool SPISlaveHandler::processNextReceive() {
  if (_receiveQueue.empty()) {
    return false;
  }
  
  // Acquire semaphore for thread-safe queue access
  if (xSemaphoreTake(_queueSemaphore, pdMS_TO_TICKS(100)) != pdTRUE) {
    _logger->error("SPISlaveHandler: Failed to acquire semaphore for queue access");
    return false;
  }
  
  // Check again after acquiring semaphore (queue could have been emptied by another thread)
  if (_receiveQueue.empty()) {
    xSemaphoreGive(_queueSemaphore);
    return false;
  }
  
  SPIDataPacket* packet = _receiveQueue.front();
  _receiveQueue.pop();
  
  // Release semaphore after modifying queue
  xSemaphoreGive(_queueSemaphore);
  
  // Process the received data
  handleReceivedData(packet->data, packet->length);
  
  // Clean up
  delete packet;
  return true;
}

void SPISlaveHandler::handleReceivedData(const uint8_t* data, size_t length) {
  _logger->debug("SPISlaveHandler: Processing received data, %d bytes", length);
  
  // If a callback is registered, call it
  if (_receiveCallback) {
    _receiveCallback(data, length);
  } else {
    // Basic handling when no callback is registered
    // Just log the first few bytes for debugging
    if (length > 0) {
      _logger->debug("SPISlaveHandler: First byte: 0x%02X", data[0]);
      
      // If it's a command byte, log it
      if (data[0] < 0xFF) {
        SPICommand cmd = static_cast<SPICommand>(data[0]);
        switch (cmd) {
          case SPICommand::PING: {
            _logger->debug("SPISlaveHandler: Received PING");
            // Auto-respond to PING with PONG
            uint8_t response[4] = {
              static_cast<uint8_t>(SPICommand::PONG), 
              0x00, 
              static_cast<uint8_t>(getBufferStatus()),  // Include buffer status
              0x00
            };
            prepareDataToSend(response, sizeof(response));
            break;
          }
          case SPICommand::CAMERA_DATA_REQUEST:
            _logger->debug("SPISlaveHandler: Received CAMERA_DATA_REQUEST");
            break;
          case SPICommand::CAMERA_DATA_BLOCK_REQUEST:
            _logger->debug("SPISlaveHandler: Received CAMERA_DATA_BLOCK_REQUEST");
            break;
          case SPICommand::ACK:
            _logger->debug("SPISlaveHandler: Received ACK");
            break;
          case SPICommand::NACK:
            _logger->debug("SPISlaveHandler: Received NACK");
            break;
          default:
            _logger->debug("SPISlaveHandler: Received command 0x%02X", static_cast<uint8_t>(cmd));
            break;
        }
      }
    }
  }
}

uint8_t SPISlaveHandler::getBufferStatus() {
  size_t queueSize = pendingReceiveCount();
  return (queueSize * 100) / _maxQueueSize;  // Convert to percentage of capacity
}

void SPISlaveHandler::setReceiveCallback(ReceiveCallback callback) {
  _receiveCallback = callback;
}

size_t SPISlaveHandler::pendingReceiveCount() {
  size_t count = 0;
  
  // Acquire semaphore for thread-safe queue access
  if (xSemaphoreTake(_queueSemaphore, pdMS_TO_TICKS(100)) == pdTRUE) {
    count = _receiveQueue.size();
    xSemaphoreGive(_queueSemaphore);
  }
  
  return count;
}

bool SPISlaveHandler::isReadyToSend() {
  return _initialized && !_dataReady;
}

bool SPISlaveHandler::ensureTransactionQueued() {
  if (!_initialized) {
    return false;
  }
  
  if (_needsNewTransaction) {
    // Reset the transaction
    memset(&_transaction, 0, sizeof(_transaction));
    _transaction.length = _bufferSize * 8; // Length in bits
    _transaction.tx_buffer = _txBuffer;
    _transaction.rx_buffer = _rxBuffer;
    
    // Queue a new transaction
    esp_err_t ret = spi_slave_queue_trans(HSPI_HOST, &_transaction, 500);
    if (ret != ESP_OK) {
      _logger->error("SPISlaveHandler: Failed to queue transaction: %d", ret);
      return false;
    }
    
    _needsNewTransaction = false;
    _logger->debug("SPISlaveHandler: New transaction queued");
    return true;
  }
  
  return false;
}

bool SPISlaveHandler::checkAndRecoverFromStalledTransaction() {
  if (!_initialized) {
    return false;
  }
  
  unsigned long currentTime = millis();
  unsigned long timeSinceLastTransaction = currentTime - _lastTransactionTime;
  
  // Check if we have a stalled transaction
  // A transaction is considered stalled if:
  // 1. It's been active for longer than the timeout period
  // 2. No transaction activity has occurred in the timeout period
  if (_transactionActive && timeSinceLastTransaction > _transactionTimeout) {
    _logger->warning("SPISlaveHandler: Detected stalled transaction, attempting recovery");
    _logger->warning("SPISlaveHandler: Transaction active for %lu ms", timeSinceLastTransaction);
    
    // Reset the SPI interface
    return resetSPIInterface();
  }
  
  // Check if we haven't had any transaction activity for a while
  // This could mean the master has been restarted or disconnected
  if (timeSinceLastTransaction > _transactionTimeout * 2) {
    // Only attempt recovery if we've been initialized and had at least one transaction
    if (_initialized && _transactionCount > 0) {
      _logger->warning("SPISlaveHandler: No SPI activity for %lu ms, attempting recovery", 
                     timeSinceLastTransaction);
      
      // Reset the SPI interface
      return resetSPIInterface();
    }
  }
  
  // Handle case where a transaction was queued but never completed
  if (_needsNewTransaction) {
    ensureTransactionQueued();
    return true;
  }
  
  return false;
}

bool SPISlaveHandler::resetSPIInterface() {
  _logger->warning("SPISlaveHandler: Resetting SPI interface (recovery attempt %d)", 
                 ++_recoveryAttempts);
  
  // Free the existing SPI slave driver
  if (_initialized) {
    spi_slave_free(HSPI_HOST);
    _initialized = false;
  }
  
  // Wait a moment to ensure everything is reset
  delay(100);
  
  // Reinitialize with the same parameters
  bool result = init(_sckPin, _misoPin, _mosiPin, _csPin, _mode);
  
  if (result) {
    _logger->info("SPISlaveHandler: SPI interface reset successful");
    
    // Reset transaction tracking
    _transactionActive = false;
    _lastTransactionTime = millis();
    
    // Clear any pending data
    memset(_txBuffer, 0, _bufferSize);
    memset(_rxBuffer, 0, _bufferSize);
    _txLength = 0;
    _dataReady = false;
    
    // Clean up receive queue
    if (xSemaphoreTake(_queueSemaphore, pdMS_TO_TICKS(1000)) == pdTRUE) {
      while (!_receiveQueue.empty()) {
        SPIDataPacket* packet = _receiveQueue.front();
        _receiveQueue.pop();
        delete packet;
      }
      xSemaphoreGive(_queueSemaphore);
    }
    
    // Send a recognizable pattern
    uint8_t initialData[4] = {0xAA, 0x55, 0xAA, 0x55};
    prepareDataToSend(initialData, sizeof(initialData));
  } else {
    _logger->error("SPISlaveHandler: SPI interface reset failed");
  }
  
  return result;
}

} // namespace Communication
