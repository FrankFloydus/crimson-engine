#pragma once
#include <crimson/renderer/forward/Environment.h>
namespace crimson {
std::shared_ptr<const PreparedEnvironment> prepareEnvironmentCpu(const std::filesystem::path&,const std::string&);
}
