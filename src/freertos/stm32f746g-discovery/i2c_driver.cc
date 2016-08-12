// Copyright (c) 2016, the Dartino project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE.md file.

#include "src/freertos/stm32f746g-discovery/i2c_driver.h"

#include <stdlib.h>

#include <stm32f7xx_hal.h>

#include "src/freertos/device_manager_api.h"

// Reference to the instance in the code generated by STM32CubeMX.
extern I2C_HandleTypeDef hi2c1;

// Bits set from the interrupt handler.
const int kResultReadyBit = 1 << 0;
const int kErrorBit = 1 << 1;

static I2CDriverImpl *i2c1;

I2CDriverImpl::I2CDriverImpl()
  : mutex_(dartino::Platform::CreateMutex()),
    i2c_(&hi2c1),
    signalThread_(NULL),
    device_id_(kIllegalDeviceId),
    state_(IDLE),
    address_(0),
    reg_(0),
    buffer_(NULL),
    count_(0) {}

// This is declared as friend, so it cannot be static here.
void __I2CTask(const void *arg) {
  const_cast<I2CDriverImpl*>(
      reinterpret_cast<const I2CDriverImpl*>(arg))->Task();
}

void I2CDriverImpl::Initialize(uintptr_t device_id) {
  i2c1 = this;
  ASSERT(device_id_ == kIllegalDeviceId);
  ASSERT(device_id != kIllegalDeviceId);
  device_id_ = device_id;
  osThreadDef(I2C_TASK, __I2CTask, osPriorityHigh, 0, 1280);
  signalThread_ =
      osThreadCreate(osThread(I2C_TASK), reinterpret_cast<void*>(this));

  // TODO(sgjesse): Generalize when we support multiple I2Cs.
  HAL_NVIC_SetPriority(I2C1_EV_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(I2C1_EV_IRQn);
  HAL_NVIC_SetPriority(I2C1_ER_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(I2C1_ER_IRQn);
}

void I2CDriverImpl::DeInitialize() {
  FATAL("NOT IMPLEMENTED");
}

int I2CDriverImpl::IsDeviceReady(uint16_t address) {
  dartino::ScopedLock lock(mutex_);
  if (address > 0x7f) return INVALID_ARGUMENTS;
  if (state_ != IDLE) return INVALID_ARGUMENTS;

  HAL_StatusTypeDef result = HAL_I2C_IsDeviceReady(i2c_, address << 1, 1, 1);
  return result == HAL_OK ? NO_ERROR : TIMEOUT;
}

int I2CDriverImpl::RequestRead(
    uint16_t address, uint8_t* buffer, size_t count) {
  dartino::ScopedLock lock(mutex_);
  if (address > 0x7f) return INVALID_ARGUMENTS;
  if (count > 0xff) return INVALID_ARGUMENTS;
  if (state_ != IDLE) return INVALID_ARGUMENTS;

  address_ = address;
  buffer_ = buffer;
  count_ = count;
  error_code_ = NO_ERROR;

  // Start the state machine preparing to read.
  SetupTransfer(I2C_AUTOEND_MODE | I2C_GENERATE_START_READ, count);
  state_ = READ_DATA;

  // Enable RX interrupt for reading the data.
  EnableRXInterrupts();

  return NO_ERROR;
}

int I2CDriverImpl::RequestWrite(
    uint16_t address, uint8_t* buffer, size_t count) {
  dartino::ScopedLock lock(mutex_);
  if (address > 0x7f) return INVALID_ARGUMENTS;
  if (count > 0xff) return INVALID_ARGUMENTS;
  if (state_ != IDLE) return INVALID_ARGUMENTS;

  address_ = address;
  buffer_ = buffer;
  count_ = count;
  error_code_ = NO_ERROR;

  // Start the state machine preparing to write.
  SetupTransfer(I2C_AUTOEND_MODE | I2C_GENERATE_START_WRITE, count);
  state_ = WRITE_DATA;

  // Enable TX interrupt for sending the data.
  EnableTXInterrupts();

  return NO_ERROR;
}

int I2CDriverImpl::RequestReadRegisters(
    uint16_t address, uint16_t reg, uint8_t* buffer, size_t count) {
  dartino::ScopedLock lock(mutex_);
  if (reg > 0xff) return INVALID_ARGUMENTS;
  if (address > 0x7f) return INVALID_ARGUMENTS;
  if (count > 0xff) return INVALID_ARGUMENTS;
  if (state_ != IDLE) return INVALID_ARGUMENTS;

  address_ = address;
  reg_ = reg;
  buffer_ = buffer;
  count_ = count;
  error_code_ = NO_ERROR;

  // Start the state machine preparing to write the register.
  SetupTransfer(I2C_SOFTEND_MODE | I2C_GENERATE_START_WRITE,
                1);  // TODO(sgjesse): Only 8-bit register size supported.
  state_ = SEND_REGISTER_READ;

  // Enable TX interrupt for sending the register.
  EnableTXInterrupts();

  return NO_ERROR;
}

int I2CDriverImpl::RequestWriteRegisters(
    uint16_t address, uint16_t reg, uint8_t* buffer, size_t count) {
  dartino::ScopedLock lock(mutex_);
  if (reg > 0xff) return INVALID_ARGUMENTS;
  if (address > 0x7f) return INVALID_ARGUMENTS;
  if (count > 0xff) return INVALID_ARGUMENTS;
  if (state_ != IDLE) return INVALID_ARGUMENTS;

  address_ = address;
  reg_ = reg;
  buffer_ = buffer;
  count_ = count;
  error_code_ = NO_ERROR;

  // Start the state machine preparing to write the register.
  SetupTransfer(I2C_RELOAD_MODE | I2C_GENERATE_START_WRITE,
                1);  // TODO(sgjesse): Only 8-bit register size supported.
  state_ = SEND_REGISTER_WRITE;

  // Enable TX interrupt for sending the register.
  EnableTXInterrupts();

  return NO_ERROR;
}

int I2CDriverImpl::AcknowledgeResult() {
  dartino::ScopedLock lock(mutex_);
  if (state_ == IDLE) return NO_PENDING_REQUEST;
  if (state_ != DONE) return RESULT_NOT_READY;
  state_ = IDLE;
  DeviceManagerClearFlags(device_id_, kResultReadyBit | kErrorBit);
  return error_code_;
}

void I2CDriverImpl::Task() {
  // Process notifications from the interrupt handlers.
  for (;;) {
    // Wait for a signal.
    osEvent event = osSignalWait(0x0000FFFF, osWaitForever);
    if (event.status == osEventSignal) {
      dartino::ScopedLock lock(mutex_);
      uint32_t flags = event.value.signals;
      // This will send a message on the event handler,
      // if there currently is an eligible listener.
      DeviceManagerSetFlags(device_id_, flags);
    }
  }
}

void I2CDriverImpl::SetupTransfer(uint32_t flags, uint8_t count) {
  // Get the CR2 register value.
  uint32_t cr2 = i2c_->Instance->CR2;

  // Update CR2.
  ResetCR2Value(&cr2);
  const int kNBytesShift = 16;
  ASSERT((count << kNBytesShift & ~I2C_CR2_NBYTES) == 0);
  // In 7-bit address mode, bit 0 is ignored. Transfer direction is controlled
  // by the flags.
  uint32_t address = (address_ << 1);
  cr2 |= address | count << kNBytesShift | flags;

  /* update CR2 register */
  i2c_->Instance->CR2 = cr2;
}

void I2CDriverImpl::ResetCR2Value(uint32_t* cr2) {
  uint32_t bits = I2C_CR2_SADD | I2C_CR2_NBYTES |
                  I2C_CR2_RELOAD | I2C_CR2_AUTOEND |
                  I2C_CR2_RD_WRN | I2C_CR2_START | I2C_CR2_STOP;
  uint32_t tmp = *cr2;
  *cr2 = tmp & (~bits);
}

void I2CDriverImpl::ResetCR2() {
  uint32_t cr2 = i2c_->Instance->CR2;
  ResetCR2Value(&cr2);
  i2c_->Instance->CR2 = cr2;
}

void I2CDriverImpl::FlushTXDR() {
  // If a pending TXIS flag is set write dummy data in TXDR to clear
  // it.
  if (__HAL_I2C_GET_FLAG(i2c_, I2C_FLAG_TXIS) != RESET) {
     i2c_->Instance->TXDR = 0x00;
  }

  // Flush TX register if not empty.
  if (__HAL_I2C_GET_FLAG(i2c_, I2C_FLAG_TXE) == RESET) {
    __HAL_I2C_CLEAR_FLAG(i2c_, I2C_FLAG_TXE);
  }
}

void I2CDriverImpl::SignalSuccess() {
  // Clear the STOP flag and CR2.
  __HAL_I2C_CLEAR_FLAG(i2c_, I2C_FLAG_STOPF);
  ResetCR2();

  // Disable interrupts.
  DisableInterrupts();

  // Flush TX register.
  FlushTXDR();

  osSignalSet(signalThread_, kResultReadyBit);
}

void I2CDriverImpl::SignalError(int error_code) {
  DisableInterrupts();
  error_code_ = error_code;
  uint32_t result = osSignalSet(signalThread_, kErrorBit);
  ASSERT(result == osOK);
}

void I2CDriverImpl::InternalStateError() {
  SignalError(INTERNAL_ERROR);
}

void I2CDriverImpl::InterruptHandler() {
  uint32_t it_flags = i2c_->Instance->ISR;
  uint32_t it_sources = i2c_->Instance->CR1;

  // The interrupt handler runs a state machine.
  //
  // Reading a register goes through these states:
  //   SEND_REGISTER_READ -> PREPARE_READ_REGISTER -> READ_DATA
  //
  // Writing a register goes through these states:
  //   SEND_REGISTER_WRITE -> PREPARE_WRITE_REGISTER -> WRITE_DATA
  //
  // When reading a register the communication will start with writing
  // (the register number) and then switch to reading for reading the
  // actual value.
  //
  // When writing a register the communication will start with writing
  // (the register number) and then continue with writing for for
  // writing the actual value.
  //
  // The difference between these two scenarios is handled by using
  // either I2C_SOFTEND_MODE or I2C_AUTOEND_MODE for the register
  // write, and the TCR or TC flags after writing the register.
  if (IsNACKF(it_flags, it_sources)) {
    // Clear the NACK flag.
    __HAL_I2C_CLEAR_FLAG(i2c_, I2C_FLAG_AF);

    // Signal error.
    error_code_ = RECEIVED_NACK;

    // No need to generate stop - it is done automatically. Error will
    // be handled in stop.

    // Flush TX register.
    FlushTXDR();
  } else if (IsTXIS(it_flags, it_sources)) {
    if (state_ == SEND_REGISTER_READ || state_ == SEND_REGISTER_WRITE) {
      i2c_->Instance->TXDR = reg_;
      state_ = state_ == SEND_REGISTER_READ
          ? PREPARE_READ_REGISTER
          : PREPARE_WRITE_REGISTER;
    } else if (state_ == WRITE_DATA) {
      i2c_->Instance->TXDR = *buffer_++;
      count_--;
    } else {
      InternalStateError();
    }
  } else if (IsRXNE(it_flags, it_sources)) {
    if (state_ == READ_DATA) {
      // Reading RXDR clears RXNE.
      *buffer_++ = i2c_->Instance->RXDR;
      count_--;
    } else {
      InternalStateError();
    }
  } else if (IsTC(it_flags, it_sources)) {
    if (state_ == PREPARE_READ_REGISTER) {
      // Prepare to read when transmission is complete.
      SetupTransfer(I2C_AUTOEND_MODE | I2C_GENERATE_START_READ, count_);
      state_ = READ_DATA;

      // Switching direction from writing to reading.
      EnableRXInterrupts();
    } else if (state_ == WRITE_DATA ||
               state_ == READ_DATA) {
      if (count_ == 0) {
        SignalSuccess();
      } else {
        // Stop before expected number of bytes where received.
        SignalError(SHORT_READ_WRITE);
      }
    } else {
      InternalStateError();
    }
  } else if (IsTCR(it_flags, it_sources)) {
    if (state_ == PREPARE_WRITE_REGISTER) {
      // Prepare to write when transmission is complete.
      SetupTransfer(I2C_AUTOEND_MODE | I2C_NO_STARTSTOP, count_);
      state_ = WRITE_DATA;
    } else {
      InternalStateError();
    }
  } else if (IsSTOPF(it_flags, it_sources)) {
    state_ = DONE;
    if (error_code_ != NO_ERROR) {
      SignalError(error_code_);
    } else {
      SignalSuccess();
    }
  }
}

void I2CDriverImpl::ErrorInterruptHandler() {
  uint32_t it_flags = i2c_->Instance->ISR;
  uint32_t it_sources = i2c_->Instance->CR1;
  int error_code = NO_ERROR;

  if (IsBERR(it_flags, it_sources)) {
    __HAL_I2C_CLEAR_FLAG(i2c_, I2C_FLAG_BERR);
    error_code = BUS_ERROR;
  }

  if (IsOVR(it_flags, it_sources)) {
    __HAL_I2C_CLEAR_FLAG(i2c_, I2C_FLAG_OVR);
    error_code = OVERRUN_ERROR;
  }

  if (IsARLO(it_flags, it_sources)) {
    __HAL_I2C_CLEAR_FLAG(i2c_, I2C_FLAG_ARLO);
    error_code = ARBITRATION_LOSS;
  }

  if (error_code != NO_ERROR) {
    SignalError(error_code);
  }
}

extern "C" void I2C1_EV_IRQHandler(void) {
  i2c1->InterruptHandler();
}

extern "C" void I2C1_ER_IRQHandler(void) {
  i2c1->ErrorInterruptHandler();
}

static void Initialize(I2CDriver* driver) {
  I2CDriverImpl* i2c = new I2CDriverImpl();
  driver->context = reinterpret_cast<uintptr_t>(i2c);
  i2c->Initialize(driver->device_id);
}

static void DeInitialize(I2CDriver* driver) {
  I2CDriverImpl* i2c = reinterpret_cast<I2CDriverImpl*>(driver->context);
  i2c->DeInitialize();
  delete i2c;
  driver->context = 0;
}

static int IsDeviceReady(I2CDriver* driver, uint16_t address) {
  I2CDriverImpl* i2c = reinterpret_cast<I2CDriverImpl*>(driver->context);
  return i2c->IsDeviceReady(address);
}

static int RequestRead(I2CDriver* driver, uint16_t address,
                       uint8_t* buffer, size_t count) {
  I2CDriverImpl* i2c = reinterpret_cast<I2CDriverImpl*>(driver->context);
  return i2c->RequestRead(address, buffer, count);
}

static int RequestWrite(I2CDriver* driver, uint16_t address,
                        uint8_t* buffer, size_t count) {
  I2CDriverImpl* i2c = reinterpret_cast<I2CDriverImpl*>(driver->context);
  return i2c->RequestWrite(address, buffer, count);
}

static int RequestReadRegisters(I2CDriver* driver,
                                uint16_t address, uint16_t reg,
                                uint8_t* buffer, size_t count) {
  I2CDriverImpl* i2c = reinterpret_cast<I2CDriverImpl*>(driver->context);
  return i2c->RequestReadRegisters(address, reg, buffer, count);
}

static int RequestWriteRegisters(I2CDriver* driver,
                                 uint16_t address, uint16_t reg,
                                 uint8_t* buffer, size_t count) {
  I2CDriverImpl* i2c = reinterpret_cast<I2CDriverImpl*>(driver->context);
  return i2c->RequestWriteRegisters(address, reg, buffer, count);
}

static int AcknowledgeResult(I2CDriver* driver) {
  I2CDriverImpl* i2c = reinterpret_cast<I2CDriverImpl*>(driver->context);
  return i2c->AcknowledgeResult();
}

extern "C" void FillI2CDriver(I2CDriver* driver) {
  driver->context = 0;
  driver->device_id = kIllegalDeviceId;
  driver->Initialize = Initialize;
  driver->DeInitialize = DeInitialize;
  driver->IsDeviceReady = IsDeviceReady;
  driver->RequestRead = RequestRead;
  driver->RequestWrite = RequestWrite;
  driver->RequestReadRegisters = RequestReadRegisters;
  driver->RequestWriteRegisters = RequestWriteRegisters;
  driver->AcknowledgeResult = AcknowledgeResult;
}
