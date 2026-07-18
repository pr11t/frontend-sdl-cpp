#pragma once

#include <Poco/Util/Application.h>
#include <Poco/Util/MapConfiguration.h>
#include <Poco/Util/PropertyFileConfiguration.h>

class ProjectMSDLApplication : public Poco::Util::Application
{
public:
    ProjectMSDLApplication();

    const char* name() const override;

    /**
     * @brief Returns the instance of the projectMSDL application.
     * @return The instance of the projectMSDL application.
     */
    static ProjectMSDLApplication& instance();

    /**
     * @brief Returns the user configuration layer.
     * @return The configuration instance which stores the settings for the current user.
     */
    Poco::AutoPtr<Poco::Util::PropertyFileConfiguration> UserConfiguration();

    /**
     * @brief Returns the command line override map.
     * @return The properties file instance which stores the UI settings.
     */
    Poco::AutoPtr<Poco::Util::MapConfiguration> CommandLineConfiguration();

    /**
     * @brief Returns the runtime override map.
     *
     * This layer is added at the highest precedence (above command-line and user
     * configuration), so values written here (e.g. via the HTTP config API) take
     * effect immediately and win over everything else, including launch flags.
     * @return The map configuration which stores runtime overrides.
     */
    Poco::AutoPtr<Poco::Util::MapConfiguration> RuntimeConfiguration();

protected:
    void initialize(Application& self) override;

    void uninitialize() override;

    void defineOptions(Poco::Util::OptionSet& options) override;

    int main(const std::vector<std::string>& args) override;

    /**
     * @brief Display help and exit.
     * @param name Unused.
     * @param value Unused.
     */
    void DisplayHelp(const std::string& name, const std::string& value);

    void ListAudioDevices(const std::string& name, const std::string& value);
    void EnableVisualPostProcessing(const std::string& name, const std::string& value);

    Poco::AutoPtr<Poco::Util::PropertyFileConfiguration> _userConfiguration{
        new Poco::Util::PropertyFileConfiguration()}; //!< The current user's configuration, used to store/reset changes made in the UI's settings dialog.
    Poco::AutoPtr<Poco::Util::MapConfiguration> _commandLineOverrides{
        new Poco::Util::MapConfiguration()}; //!< Map configuration with overrides set by command line arguments.
    Poco::AutoPtr<Poco::Util::MapConfiguration> _runtimeOverrides{
        new Poco::Util::MapConfiguration()}; //!< Highest-precedence layer for runtime overrides set via the HTTP config API.
};
