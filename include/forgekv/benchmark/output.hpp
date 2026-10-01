#pragma once

#include <ostream>
#include <string>
#include <string_view>

namespace forgekv::benchmark {

[[nodiscard]] std::string json_escape(std::string_view value);
[[nodiscard]] std::string csv_escape(std::string_view value);
void require_output_success(std::ostream& output, std::string_view description);

}  // namespace forgekv::benchmark
