#pragma once

namespace usb_monitor {

constexpr int MAX_PORTS = 127;
constexpr int MAX_CONFIGS = 8;
constexpr int MAX_INTERFACES = 32;
constexpr int MAX_ENDPOINTS = 32;
constexpr int MAX_STRING_LENGTH = 256;

constexpr int DEFAULT_TIMEOUT = 1000;  // ms
constexpr int POLLING_INTERVAL = 1000; // ms
constexpr int BANDWIDTH_WINDOW = 5000; // ms

namespace ErrorCodes {
    constexpr int SUCCESS = 0;
    constexpr int DEVICE_NOT_FOUND = -1;
    constexpr int ACCESS_DENIED = -2;
    constexpr int INVALID_PARAM = -3;
    constexpr int IO_ERROR = -4;
    constexpr int BUFFER_OVERFLOW = -5;
    constexpr int PIPE_ERROR = -6;
    constexpr int SYSTEM_ERROR = -7;
    constexpr int BUSY = -8;
    constexpr int NOT_SUPPORTED = -9;
    constexpr int TIMEOUT = -10;
}

}
