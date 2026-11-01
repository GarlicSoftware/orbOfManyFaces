#pragma once
#include "Arduino.h"
struct DNSServer {inline static bool fail=false;bool start(int,const char*,IPAddress){return !fail;}void stop(){}void processNextRequest(){}};
