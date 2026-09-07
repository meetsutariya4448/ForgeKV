#pragma once

#include <string>
#include <string_view>

namespace forgekv::benchmark {

[[nodiscard]] std::string json_escape(std::string_view value);
[[nodiscard]] std::string csv_escape(std::string_view value);

}  // namespace forgekv::benchmark
