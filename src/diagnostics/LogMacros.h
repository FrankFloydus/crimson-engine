#pragma once

#define QUADER_LOG_TRACE(...) ::quader::Logger::trace(__VA_ARGS__)
#define QUADER_LOG_DEBUG(...) ::quader::Logger::debug(__VA_ARGS__)
#define QUADER_LOG_INFO(...) ::quader::Logger::info(__VA_ARGS__)
#define QUADER_LOG_WARN(...) ::quader::Logger::warn(__VA_ARGS__)
#define QUADER_LOG_ERROR(...) ::quader::Logger::error(__VA_ARGS__)
#define QUADER_LOG_CRITICAL(...) ::quader::Logger::critical(__VA_ARGS__)
