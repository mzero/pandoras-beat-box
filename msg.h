#pragma once

#include <stddef.h>

/* A nice messaging interface to Serial */

void statusMsg(const char* msg);
void errorMsg(const char* msg);
void statusMsgf(const char* fmt, ... );
void errorMsgf(const char* fmt, ... );
  // all of the above output a final newline

class MessageHoldBuffer {
public:
  MessageHoldBuffer(char* buf, size_t len);
  ~MessageHoldBuffer();
};

template<size_t n>
class MessageHolder {
public:
  MessageHolder() : mhb(buf, len) { }
  ~MessageHolder() { }
private:
  static const size_t len = n;
  char                buf[n];
  MessageHoldBuffer   mhb;
};

using MessageHold = MessageHolder<256>;
