#pragma once

// Windows
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

// STL
#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <thread>
#include <queue>
#include <unordered_map>
#include <set>
#include <algorithm>
#include <cstring>

// C++20
#include <format>
#include <concepts>
#include <source_location>
#include <stop_token>

// Boost.Asio
#include <boost/asio.hpp>

// Database (ODBC)
#include <nanodbc/nanodbc.h>

// Protobuf
#include <google/protobuf/message.h>

// Project common
#include "Utils/Types.h"
#include "Utils/Logger.h"
#include "Utils/EnumUtils.h"
#include "Utils/JsonUtils.h"
#include "Utils/TimeManager.h"

// magic enum
#include <magic_enum.hpp>

// nlohmann json
#include <nlohmann/json.hpp>