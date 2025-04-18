#pragma once

#include "../JuceLibraryCode/JuceHeader.h"
#include "SafePluginScanner.h"

/**
 * A simple splash screen component that displays on application startup
 * showing version information, logo, and brief loading status.
 */
class SplashScreen : public juce::Component,
                     private juce::Timer,
                     public PluginScanProgressListener
{
public:
    SplashScreen();
    ~SplashScreen() override;

    void paint(juce::Graphics& g) override;
    void timerCallback() override;
    
    // PluginScanProgressListener implementation
    void onScanProgressUpdate(float progressPercent, const juce::String& statusMessage) override;
    
    /** Update the loading progress (0.0 to 1.0) */
    void setProgress(float newProgress) { progress = newProgress; repaint(); }
    
    /** Set the status message shown during loading */
    void setStatusMessage(const juce::String& message) { statusMessage = message; repaint(); }
    
private:
    juce::Image logoImage;
    juce::String versionString;
    juce::String buildDateString;
    juce::String statusMessage;
    float progress;
    int displayTimeMs;
    int fadeOutTimeMs;
    juce::uint32 startTime;
    float opacity;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SplashScreen)
};