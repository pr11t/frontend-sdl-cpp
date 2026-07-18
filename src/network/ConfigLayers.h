#pragma once

#include <Poco/Util/AbstractConfiguration.h>

/**
 * @brief References to the individual configuration layers used by the HTTP config API.
 *
 * The API reads effective values from @c effective and reports which layer a value
 * originates from by probing the individual layers in precedence order
 * (runtime > commandLine > user > defaults).
 */
struct ConfigLayers
{
    Poco::Util::AbstractConfiguration::Ptr effective;   //!< Full layered (effective) configuration.
    Poco::Util::AbstractConfiguration::Ptr runtime;     //!< Highest-precedence runtime override layer (this API).
    Poco::Util::AbstractConfiguration::Ptr commandLine; //!< Command-line overrides.
    Poco::Util::AbstractConfiguration::Ptr user;        //!< User (UI settings) configuration.
};
