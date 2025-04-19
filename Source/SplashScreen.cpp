#include "SplashScreen.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_core/juce_core.h>
#include <BinaryData.h>
#include <juce_gui_extra/juce_gui_extra.h>

using namespace juce;

SplashScreen::SplashScreen()
    : statusMessage("Loading plugins..."),
      progress(0.0f),
      displayTimeMs(2000),
      fadeOutTimeMs(500),
      opacity(1.0f)
{
    // Load the application icon for the splash screen
    File iconFile(File::getSpecialLocation(File::currentApplicationFile)
                  .getParentDirectory()
                  .getParentDirectory()
                  .getChildFile("Resources/icon.png"));
                  
    if (iconFile.existsAsFile())
        logoImage = ImageCache::getFromFile(iconFile);
    else
        logoImage = ImageCache::getFromMemory(BinaryData::icon_png, BinaryData::icon_pngSize);
    
    versionString = "Version " + JUCEApplication::getInstance()->getApplicationVersion();
    buildDateString = "Build date: " + String(__DATE__) + " " + String(__TIME__);
    
    setSize(400, 300);
    setOpaque(false);
    
    // Center the splash screen on the main display
    Rectangle<int> screenArea = Desktop::getInstance().getDisplays().getMainDisplay().userArea;
    setCentrePosition(screenArea.getCentreX(), screenArea.getCentreY());
    
    startTime = Time::getMillisecondCounter();
    startTimer(30); // Update timer for animation
    
    // Make sure we're visible on top of other windows
    setAlwaysOnTop(true);
    toFront(true);
}

SplashScreen::~SplashScreen()
{
    stopTimer();
}

void SplashScreen::paint(juce::Graphics& g)
{
    // Create a subtle gradient background
    ColourGradient gradient(
        Colours::darkblue.withAlpha(0.7f * opacity),
        0.0f, 0.0f,
        Colours::black.withAlpha(0.8f * opacity),
        (float)getWidth(), (float)getHeight(),
        false);
    
    gradient.addColour(0.4, Colours::darkblue.withAlpha(0.6f * opacity));
    gradient.addColour(0.6, Colours::midnightblue.withAlpha(0.7f * opacity));
    
    g.setGradientFill(gradient);
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 12.0f);
    
    // Add a subtle border
    g.setColour(Colours::lightblue.withAlpha(0.6f * opacity));
    g.drawRoundedRectangle(getLocalBounds().reduced(1).toFloat(), 11.0f, 1.5f);
    
    // Draw logo
    if (logoImage.isValid())
    {
        int imageSize = jmin(getWidth(), getHeight()) / 3;
        g.setOpacity(opacity);
        g.drawImage(logoImage, 
                   (getWidth() - imageSize) / 2, 
                   (getHeight() - imageSize) / 2 - 40, 
                   imageSize, 
                   imageSize, 
                   0, 0, 
                   logoImage.getWidth(), 
                   logoImage.getHeight());
    }
    
    // Draw version info
    g.setColour(Colours::white.withAlpha(opacity));
    g.setFont(Font(24.0f).boldened());
    g.drawText("Nova Host", getLocalBounds().reduced(20, 0).withY(20), Justification::centredTop, true);
    
    g.setFont(16.0f);
    g.drawText(versionString, getLocalBounds().reduced(20, 0).withY(60), Justification::centredTop, true);
    
    g.setFont(12.0f);
    g.drawText(buildDateString, getLocalBounds().reduced(20), Justification::centredBottom, true);
    
    // Status text
    g.setFont(14.0f);
    g.drawText(statusMessage, getLocalBounds().reduced(20).withY(getHeight() - 80), 
               Justification::centredTop, true);
               
    // Progress bar
    const int progressBarHeight = 10;
    const Rectangle<float> progressBarBounds(20.0f, (float)getHeight() - 50.0f, 
                                         (float)getWidth() - 40.0f, (float)progressBarHeight);
    
    // Draw progress bar background
    g.setColour(Colours::darkgrey.withAlpha(0.4f * opacity));
    g.fillRoundedRectangle(progressBarBounds, (float)progressBarHeight / 2);
    
    // Draw progress bar fill
    if (progress > 0.0f)
    {
        // Create gradient for progress bar
        ColourGradient progressGradient(
            Colours::skyblue.withAlpha(opacity),
            progressBarBounds.getX(), progressBarBounds.getY(),
            Colours::lightblue.withAlpha(opacity),
            progressBarBounds.getRight(), progressBarBounds.getY(),
            false);
        
        g.setGradientFill(progressGradient);
        g.fillRoundedRectangle(progressBarBounds.withWidth(progressBarBounds.getWidth() * progress), 
                               (float)progressBarHeight / 2);
    }
}

void SplashScreen::timerCallback()
{
    auto now = Time::getMillisecondCounter();
    auto elapsedMs = now - startTime;
    
    if (elapsedMs >= displayTimeMs + fadeOutTimeMs)
    {
        // Time to close
        stopTimer();
        
        // Invoke close callback if it exists
        if (onCloseCallback)
            onCloseCallback();
        
        // Find and close the parent dialog window if it exists
        if (DialogWindow* dialogWindow = dynamic_cast<DialogWindow*>(getParentComponent()))
        {
            // Use a small delay to ensure smooth closing animation
            Timer::callAfterDelay(50, [dialogWindow]() {
                dialogWindow->exitModalState(0);
                delete dialogWindow;
            });
        }
        else if (Component* parent = getParentComponent())
        {
            parent->removeChildComponent(this);
            if (auto safeThis = WeakReference<Component>(this))
                safeThis->deleteAfterRemoval();
        }
        else
        else if (Component* parent = getParentComponent())
        {
            parent->removeChildComponent(this);
            if (auto safeThis = WeakReference<Component>(this))
                Timer::callAfterDelay(50, [safeThis]() { delete safeThis.get(); });
        }
        else
        {
            if (auto safeThis = WeakReference<Component>(this))
                Timer::callAfterDelay(50, [safeThis]() { delete safeThis.get(); });
        }
    }
    
    repaint();
}

void SplashScreen::onScanProgressUpdate(float progressPercent, const juce::String& statusMessage)
{
    // Handle the progress update from the plugin scanner
    // This will be called from a background thread, so we need to use MessageManager to update the UI
    juce::MessageManager::callAsync([this, progressPercent, statusMessage]() {
        setProgress(progressPercent);
        setStatusMessage(statusMessage);
        
        // Extend display time during active plugin scanning
        startTime = Time::getMillisecondCounter();
    });
}