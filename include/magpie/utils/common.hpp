#pragma once

#include <string>

namespace magpie {

// Seconds since UNIX epoch, UTC, as double
double getUtcTimestamp();

// ULID-like unique ID as a string
std::string getUniqueId();

} // namespace magpie
