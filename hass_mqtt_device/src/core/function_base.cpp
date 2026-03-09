/**
 * @author      Morgan Tørvolt
 * @contributors somebody, hopefully@someday.com
 * @copyright   See LICENSE file
 */

// Include the corresponding header file
#include "hass_mqtt_device/core/function_base.h"

// Include any other necessary headers
#include "hass_mqtt_device/core/helper_functions.hpp"
#include "hass_mqtt_device/logger/logger.hpp" // For logging
#include <vector>

FunctionBase::FunctionBase(const std::string& function_name)
	: m_function_name(function_name)
	, m_clean_name(getValidHassString(function_name))
	, m_logger(spdlog::default_logger())
{
}

std::string FunctionBase::getName() const
{
	return m_function_name;
}

std::string FunctionBase::getCleanName() const
{
	return m_clean_name;
}

std::string FunctionBase::getId() const
{
	return m_id;
}

std::string FunctionBase::getBaseTopic() const
{
	return m_base_topic;
};
