#include "../JuceLibraryCode/JuceHeader.h"
#include "IconMenu.hpp"
#include "SafePluginScanner.h"
#include "SplashScreen.h"
#include "GPUAccelerationManager.h"

#if ! (JUCE_PLUGINHOST_VST || JUCE_PLUGINHOST_VST3 || JUCE_PLUGINHOST_AU)
 #error "If you're building the plugin host, you probably want to enable VST and/or AU support"
#endif

class PluginHostApp : public juce::JUCEApplication
{
public:
    PluginHostApp() {}

    void initialise(const String& commandLine) override
    {
        // Enable high-DPI support on all platforms
        #if JUCE_WINDOWS
        Desktop::getInstance().setGlobalScaleFactor(1.0);
        #endif
        
        // Show splash screen early in initialization
        showSplashScreen();
        
        // Initialize GPU acceleration
        initializeGPUAcceleration();
        
        PropertiesFile::Options options;
        options.applicationName     = getApplicationName();
        options.filenameSuffix      = "settings";
        options.osxLibrarySubFolder = "Preferences";

        checkArguments(&options);

        appProperties = std::make_unique<ApplicationProperties>();
        appProperties->setStorageParameters(options);

        LookAndFeel::setDefaultLookAndFeel(&lookAndFeel);

        mainWindow = std::make_unique<IconMenu>();
        #if JUCE_MAC
            Process::setDockIconVisible(false);
        #endif
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        appProperties = nullptr;
        LookAndFeel::setDefaultLookAndFeel(nullptr);
        
        // Clean up GPU acceleration manager
        GPUAccelerationManager::deleteInstance();
    }

    void systemRequestedQuit() override
    {
        JUCEApplicationBase::quit();
    }

    const String getApplicationName() override    { return "Nova Host"; }
    const String getApplicationVersion() override { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override    {
        StringArray multiInstance = getParameter("-multi-instance");
        return multiInstance.size() == 2;
    }

    ApplicationCommandManager commandManager;
    std::unique_ptr<ApplicationProperties> appProperties;
    LookAndFeel_V3 lookAndFeel;

private:
    std::unique_ptr<IconMenu> mainWindow;
    DialogWindow* splashWindow = nullptr;
    
    // Initialize GPU acceleration for the application
    void initializeGPUAcceleration()
    {
        // Get the GPU acceleration manager instance (creates it if needed)
        auto& gpuManager = GPUAccelerationManager::getInstance();
        
        // Check if GPU acceleration is available on this system
        if (gpuManager.isGPUAccelerationAvailable())
        {
            // Load setting from application properties, default to enabled
            PropertiesFile* props = appProperties != nullptr ? 
                appProperties->getUserSettings() : nullptr;
                
            bool enableGPU = props != nullptr ? 
                props->getBoolValue("enableGPUAcceleration", true) : true;
                
            // Configure optimal settings based on detected GPU
            gpuManager.configureOptimalSettings();
            
            // Enable GPU acceleration globally
            gpuManager.setGPUAccelerationEnabled(enableGPU);
            
            // Log GPU information
            Logger::writeToLog("GPU acceleration initialized: " + gpuManager.getGPUInfo());
        }
        else
        {
            Logger::writeToLog("GPU acceleration not available on this system");
        }
    }

    // Show a splash screen with version and build information
    void showSplashScreen()
    {
        splashWindow = new DialogWindow("Loading Nova Host", 
                                      Colours::transparentBlack, 
                                      true, 
                                      false);
                                      
        SplashScreen* content = new SplashScreen();
        splashWindow->setContentOwned(content, false);
        splashWindow->setUsingNativeTitleBar(false);
        splashWindow->setOpaque(false);
        splashWindow->setDropShadowEnabled(true);
        splashWindow->setVisible(true);
        splashWindow->toFront(true);
        
        // The SplashScreen component will delete itself when done
        // and also close the splashWindow that owns it
    }

    StringArray getParameter(String lookFor) {
        StringArray parameters = JUCEApplication::getCommandLineParameters();
        StringArray found;
        for (int i = 0; i < parameters.size(); ++i)
        {
            String param = parameters[i];
            if (param.contains(lookFor))
            {
                found.add(lookFor);
                int delimiter = param.indexOf("=") + 1;
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

static PluginHostApp& getApp()                       { return *dynamic_cast<PluginHostApp*>(JUCEApplication::getInstance()); }
ApplicationCommandManager& getCommandManager()       { return getApp().commandManager; }
ApplicationProperties& getAppProperties()            { return *getApp().appProperties; }

START_JUCE_APPLICATION(PluginHostApp)
