#include "../JuceLibraryCode/JuceHeader.h"
#include "IconMenu.hpp"
#include <memory> // Add std::unique_ptr support

#if ! (JUCE_PLUGINHOST_VST || JUCE_PLUGINHOST_VST3 || JUCE_PLUGINHOST_AU)
 #error "If you're building the audio plugin host, you probably want to enable VST and/or AU support"
#endif

class PluginHostApp  : public JUCEApplication
{

public:
    PluginHostApp() {}

    void initialise (const String&) override
    {
        // Improved high-DPI support on Windows 11 with better scaling detection
        #if JUCE_WINDOWS
        // Get the display where the app is starting
        const Desktop::Displays::Display& display = Desktop::getInstance().getDisplays().getMainDisplay();
        // Apply the appropriate scale factor from the display
        Desktop::getInstance().setGlobalScaleFactor(display.scale);
        #endif
        
        PropertiesFile::Options options;
        options.applicationName     = getApplicationName();
        options.filenameSuffix      = "settings";
        options.osxLibrarySubFolder = "Preferences";

        checkArguments(&options);

        appProperties = std::make_unique<ApplicationProperties>();
        appProperties->setStorageParameters (options);

        LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

        mainWindow = std::make_unique<IconMenu>();
		#if JUCE_MAC
			Process::setDockIconVisible(false);
		#endif
    }

    void shutdown() override
    {
        mainWindow.reset();
        appProperties.reset();
        LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override
    {
        JUCEApplicationBase::quit();
    }

    const String getApplicationName() override       { return "Nova Host"; }
    const String getApplicationVersion() override    { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override       {
        StringArray multiInstance = getParameter("-multi-instance");
        return multiInstance.size() == 2;
    }

    ApplicationCommandManager commandManager;
    std::unique_ptr<ApplicationProperties> appProperties;
    LookAndFeel_V3 lookAndFeel;

private:
    std::unique_ptr<IconMenu> mainWindow;

    StringArray getParameter(String lookFor) {
        StringArray parameters = getCommandLineParameterArray();
        StringArray found;
        for (int i = 0; i < parameters.size(); ++i)
        {
            String param = parameters[i];
            if (param.contains(lookFor))
            {
                found.add(lookFor);
                int delimiter = param.indexOf(0, "=") + 1;
                String val = param.substring(delimiter);
                found.add(val);
                return found;
            }
        }
        return found;
    }

    void checkArguments(PropertiesFile::Options *options) {
        StringArray multiInstance = getParameter("-multi-instance");
        if (multiInstance.size() == 2)
            options->filenameSuffix = multiInstance[1] + "." + options->filenameSuffix;
    }
};

static PluginHostApp& getApp()                      { return *dynamic_cast<PluginHostApp*>(JUCEApplication::getInstance()); }
ApplicationCommandManager& getCommandManager()      { return getApp().commandManager; }
ApplicationProperties& getAppProperties()           { return *getApp().appProperties; }

START_JUCE_APPLICATION (PluginHostApp)
