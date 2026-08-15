#pragma once

#include <Arduino.h>

template <typename SerialPort>
class TaggedSerial {
public:
  TaggedSerial(SerialPort& serialPort, const char* tag)
      : serialPort(serialPort), tag(tag) {}

  template <typename... Args>
  void begin(Args... args) {
    serialPort.begin(args...);
  }

  int available() {
    return serialPort.available();
  }

  int read() {
    return serialPort.read();
  }

  void flush() {
    serialPort.flush();
  }

  template <typename T>
  size_t print(const T& value) {
    writePrefix();
    return serialPort.print(value);
  }

  size_t print(const char* value) {
    writePrefix();
    size_t written = serialPort.print(value);
    updateLineState(value);
    return written;
  }

  size_t print(const String& value) {
    writePrefix();
    size_t written = serialPort.print(value);
    updateLineState(value.c_str());
    return written;
  }

  size_t print(char value) {
    writePrefix();
    size_t written = serialPort.print(value);
    lineStart = value == '\n' || value == '\r';
    return written;
  }

  template <typename T>
  size_t println(const T& value) {
    writePrefix();
    size_t written = serialPort.println(value);
    lineStart = true;
    return written;
  }

  size_t println() {
    writePrefix();
    size_t written = serialPort.println();
    lineStart = true;
    return written;
  }

  template <typename... Args>
  int printf(const char* format, Args... args) {
    writePrefix();
    int written = serialPort.printf(format, args...);
    updateLineState(format);
    return written;
  }

private:
  void writePrefix() {
    if (lineStart) {
      serialPort.print(tag);
      lineStart = false;
    }
  }

  void updateLineState(const char* text) {
    if (text == nullptr) {
      return;
    }

    for (const char* character = text; *character != '\0'; ++character) {
      lineStart = *character == '\n' || *character == '\r';
    }
  }

  SerialPort& serialPort;
  const char* tag;
  bool lineStart = true;
};
