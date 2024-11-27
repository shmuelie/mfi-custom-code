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
{
}

std::string FunctionBase::getName() const
{
    return m_function_name;
}

std::string FunctionBase::getCleanName() const
{
    return getValidHassString(getName());
}

std::string FunctionBase::getId() const
{
    auto parent = m_parent_device.lock();
    if(!parent)
    {
        LOG_ERROR("Parent device is not available.");
        return "";
    }
    return parent->getFullId() + "_" + getCleanName();
}

std::string FunctionBase::getBaseTopic() const
{
    auto parent = m_parent_device.lock();
    if(!parent)
    {
        LOG_ERROR("Parent device is not available.");
        return "";
    }
    return "home/" + parent->getFullId() + "/" + getCleanName() + "/";
};
