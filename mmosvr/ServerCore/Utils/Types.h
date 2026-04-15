#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <functional>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include <queue>
#include <unordered_map>
#include <set>
#include <algorithm>
#include <cstring>
#include <format>
#include <concepts>

#include <boost/asio.hpp>


using int8 = std::int8_t;
using int16 = std::int16_t;
using int32 = std::int32_t;
using int64 = std::int64_t;
using uint8 = std::uint8_t;
using uint16 = std::uint16_t;
using uint32 = std::uint32_t;
using uint64 = std::uint64_t;

namespace net = boost::asio;
using tcp = net::ip::tcp;
using udp = net::ip::udp;
