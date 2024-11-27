/**
 * @author      Morgan Tørvolt
 * @contributors somebody, hopefully@someday.com
 * @copyright   See LICENSE file
 */

#pragma once

#include <algorithm>
#include <string>

inline std::string getValidHassString(const std::string& value)
{
    std::string return_value = value;

    // Replace special characters with nothing
    std::string special_characters = "!@#$%^&*()[]{};:,./<>?\\|`~-=+";
    for(auto& character : special_characters)
    {
        return_value.erase(std::remove(return_value.begin(), return_value.end(), character),
                           return_value.end());
    }

    // Replace spaces and dashes with underscores
    std::replace(return_value.begin(), return_value.end(), ' ', '_');
    std::replace(return_value.begin(), return_value.end(), '-', '_');

    // Make the string lowercase
    std::transform(return_value.begin(), return_value.end(), return_value.begin(), ::tolower);

    // Make sure the string is not empty
    if(return_value.empty())
    {
        return_value = "empty";
    }

    return return_value;
}
