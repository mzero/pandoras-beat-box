#include "msg.h"

#include "Arduino.h"

namespace {
  char* msgBuf = nullptr;
  size_t msgBufLen = 0;
  size_t msgBufNext = 0;
  MessageHoldBuffer* msgHolder = nullptr;
}

MessageHoldBuffer::MessageHoldBuffer(char* buf, size_t len) {
  if (msgHolder != nullptr) return;

  msgHolder = this;
  msgBuf = buf;
  msgBufLen = len;
  msgBufNext = 0;
}

MessageHoldBuffer::~MessageHoldBuffer() {
  if (msgHolder != this) return;

  if (msgBuf && msgBufNext) {
    Serial.write(msgBuf, msgBufNext);
    Serial.flush();
  }

  msgHolder = nullptr;
  msgBuf = nullptr;
  msgBufLen = 0;
  msgBufNext = 0;
}

namespace {
  void queueMsg(const char* msg) {
    if (!msgBuf || !msgBufLen) {
      Serial.println(msg);
      return;
    }

    while (msgBufNext < msgBufLen - 1 && *msg) {
      msgBuf[msgBufNext++] = *msg++;
    }
    msgBuf[msgBufNext++] = '\n';
  }
}

void statusMsg(const char* msg) {
  queueMsg(msg);
}

void errorMsg(const char* msg) {
  char buf[128];
  snprintf(buf, sizeof(buf), "** %s", msg);
  queueMsg(buf);
}

void statusMsgf(const char* fmt, ... ) {
  char buf[128];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  statusMsg(buf);
}

void errorMsgf(const char* fmt, ... ) {
  char buf[128];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  errorMsg(buf);
}

