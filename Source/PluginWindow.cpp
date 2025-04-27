#include "../JuceLibraryCode/JuceHeader.h"
#include "../JuceLibraryCode/JuceHeader.h"
#include "PluginWindow.h"

static Array<PluginWindow*> activePluginWindows;

PluginWindow::PluginWindow(Component* const pluginEditor,
                         AudioProcessorGraph::Node* const o,
                         WindowFormatType t)
    : DocumentWindow(pluginEditor->getName(), Colours::lightgrey,
                    DocumentWindow::minimiseButton | DocumentWindow::closeButton),
      owner(o),
      type(t)
{
    setSize(400, 300);
    setUsingNativeTitleBar(true);
    setContentOwned(pluginEditor, true);

    // Optimize window positioning with smarter placement strategy
    int defaultX, defaultY;

    #if JUCE_WINDOWS
    // Get screen dimensions for better positioning
    Rectangle<int> screenArea = Desktop::getInstance().getDisplays().getMainDisplay().userArea;
    
    // Create deterministic but distributed positions based on window count
    // This avoids random placement while ensuring windows don't stack exactly
    defaultX = screenArea.getWidth() / 4 + (activePluginWindows.size() * 30) % (screenArea.getWidth() / 2);
    defaultY = screenArea.getHeight() / 4 + (activePluginWindows.size() * 25) % (screenArea.getHeight() / 2);
    
    // Ensure window is visible on screen
    defaultX = jlimit(20, screenArea.getWidth() - 420, defaultX);
    defaultY = jlimit(20, screenArea.getHeight() - 320, defaultY);
    #else
    // Default positioning for other platforms
    defaultX = 100 + (activePluginWindows.size() * 30) % 300;
    defaultY = 100 + (activePluginWindows.size() * 25) % 200;
    #endif

    setTopLeftPosition(owner->properties.getWithDefault(getLastXProp(type), defaultX),
                     owner->properties.getWithDefault(getLastYProp(type), defaultY));

    owner->properties.set(getOpenProp(type), true);
    setVisible(true);
    activePluginWindows.add(this);
}

void PluginWindow::closeCurrentlyOpenWindowsFor(const uint32 nodeId)
{
    // Use a temporary array to avoid modifying while iterating
    Array<PluginWindow*> windowsToClose;
    
    for (auto* window : activePluginWindows)
        if (window->owner->nodeId == nodeId)
            windowsToClose.add(window);
            
    for (auto* window : windowsToClose)
        delete window;
}

void PluginWindow::closeAllCurrentlyOpenWindows()
{
    if (activePluginWindows.size() > 0)
    {
        // Make a copy to safely delete while iterating
        Array<PluginWindow*> windowsToClose;
        windowsToClose.addArray(activePluginWindows);
        
        for (auto* window : windowsToClose)
            delete window;

        // Only create a modal component if needed
        if (!activePluginWindows.isEmpty())
        {
            Component dummyModalComp;
            dummyModalComp.enterModalState();
            MessageManager::getInstance()->runDispatchLoopUntil(50);
        }
    }
}

bool PluginWindow::containsActiveWindows()
{
    return !activePluginWindows.isEmpty();
}

// ProcessorProgramPropertyComp class definition - simplified and optimized
class ProcessorProgramPropertyComp : public PropertyComponent, private AudioProcessorListener
{
public:
    ProcessorProgramPropertyComp(const String& name, AudioProcessor& p, int index_)
        : PropertyComponent(name),
          owner(p),
          index(index_)
    {
        owner.addListener(this);
    }

    ~ProcessorProgramPropertyComp() override
    {
        owner.removeListener(this);
    }

    void refresh() override {}
    void audioProcessorChanged(AudioProcessor*) override {}
    void audioProcessorParameterChanged(AudioProcessor*, int, float) override {}

private:
    AudioProcessor& owner;
    const int index;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProcessorProgramPropertyComp)
};

class ProgramAudioProcessorEditor : public AudioProcessorEditor
{
public:
    ProgramAudioProcessorEditor(AudioProcessor* const p)
        : AudioProcessorEditor(p)
    {
        jassert(p != nullptr);
        setOpaque(true);

        addAndMakeVisible(panel);

        const int numPrograms = p->getNumPrograms();
        int totalHeight = 0;

        // More efficient properties array allocation
        Array<PropertyComponent*> programs;
        programs.ensureStorageAllocated(numPrograms);

        for (int i = 0; i < numPrograms; ++i)
        {
            String name(p->getProgramName(i).trim());
            if (name.isEmpty())
                name = "Unnamed";

            auto* pc = new ProcessorProgramPropertyComp(name, *p, i);
            programs.add(pc);
            totalHeight += pc->getPreferredHeight();
        }

        panel.addProperties(programs);
        setSize(400, jlimit(25, 400, totalHeight));
    }

    void paint(Graphics& g) override
    {
        g.fillAll(Colours::grey);
    }

    void resized() override
    {
        panel.setBounds(getLocalBounds());
    }

private:
    PropertyPanel panel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProgramAudioProcessorEditor)
};

//==============================================================================
PluginWindow* PluginWindow::getWindowFor(AudioProcessorGraph::Node* const node,
                                       WindowFormatType type)
{
    jassert(node != nullptr);

    // First check if this window already exists
    for (auto* window : activePluginWindows)
        if (window->owner == node && window->type == type)
            return window;

    AudioProcessor* processor = node->getProcessor();
    AudioProcessorEditor* ui = nullptr;

    if (type == Normal)
    {
        ui = processor->createEditorIfNeeded();
        if (ui == nullptr)
            type = Generic;
    }

    if (ui == nullptr)
    {
        if (type == Generic || type == Parameters)
            ui = new GenericAudioProcessorEditor(processor);
        else if (type == Programs)
            ui = new ProgramAudioProcessorEditor(processor);
    }

    if (ui != nullptr)
    {
        if (AudioPluginInstance* const plugin = dynamic_cast<AudioPluginInstance*>(processor))
            ui->setName(plugin->getName());

        return new PluginWindow(ui, node, type);
    }

    return nullptr;
}

PluginWindow::~PluginWindow()
{
    activePluginWindows.removeFirstMatchingValue(this);
    clearContentComponent();
}

void PluginWindow::moved()
{
    owner->properties.set(getLastXProp(type), getX());
    owner->properties.set(getLastYProp(type), getY());
}

void PluginWindow::closeButtonPressed()
{
    owner->properties.set(getOpenProp(type), false);
    delete this;
}
#include "../JuceLibraryCode/JuceHeader.h"
#include "PluginWindow.h"

static Array<PluginWindow*> activePluginWindows;

PluginWindow::PluginWindow(Component* const pluginEditor,
                         AudioProcessorGraph::Node* const o,
                         WindowFormatType t)
    : DocumentWindow(pluginEditor->getName(), Colours::lightgrey,
                    DocumentWindow::minimiseButton | DocumentWindow::closeButton),
      owner(o),
      type(t)
{
    setSize(400, 300);
    setUsingNativeTitleBar(true);
    setContentOwned(pluginEditor, true);

    #if JUCE_WINDOWS
    // Improve positioning on Windows 11
    int defaultX = Random::getSystemRandom().nextInt(500);
    int defaultY = Random::getSystemRandom().nextInt(500);
    
    // Ensure window is visible on screen (Windows 11 has multiple virtual desktops)
    Rectangle<int> screenArea = Desktop::getInstance().getDisplays().getMainDisplay().userArea;
    defaultX = jmin(screenArea.getWidth() - 400, defaultX);
    defaultY = jmin(screenArea.getHeight() - 300, defaultY);
    #else
    int defaultX = Random::getSystemRandom().nextInt(500);
    int defaultY = Random::getSystemRandom().nextInt(500);
    #endif

    setTopLeftPosition(owner->properties.getWithDefault(getLastXProp(type), defaultX),
                     owner->properties.getWithDefault(getLastYProp(type), defaultY));

    owner->properties.set(getOpenProp(type), true);

    setVisible(true);

    activePluginWindows.add(this);
}

void PluginWindow::closeCurrentlyOpenWindowsFor(const uint32 nodeId)
{
    for (int i = activePluginWindows.size(); --i >= 0;)
        if (activePluginWindows.getUnchecked(i)->owner->nodeId == nodeId)
            delete activePluginWindows.getUnchecked(i);
}

void PluginWindow::closeAllCurrentlyOpenWindows()
{
    if (activePluginWindows.size() > 0)
    {
        for (int i = activePluginWindows.size(); --i >= 0;)
            delete activePluginWindows.getUnchecked(i);

        Component dummyModalComp;
        dummyModalComp.enterModalState();
        MessageManager::getInstance()->runDispatchLoopUntil(50);
    }
}

bool PluginWindow::containsActiveWindows()
{
    return activePluginWindows.size() > 0;
}

float PluginWindow::getDesktopScaleFactor() const
{
    return Desktop::getInstance().getGlobalScaleFactor();
}

//==============================================================================
class ProcessorProgramPropertyComp : public PropertyComponent, private AudioProcessorListener
{
public:
    ProcessorProgramPropertyComp(const String& name, AudioProcessor& p, int index_)
        : PropertyComponent(name),
          owner(p),
          index(index_)
    {
        owner.addListener(this);
    }

    ~ProcessorProgramPropertyComp()
    {
        owner.removeListener(this);
    }

    void refresh() override { }
    void audioProcessorChanged(AudioProcessor*) override { }
    void audioProcessorParameterChanged(AudioProcessor* processor, int, float) override { }

private:
    AudioProcessor& owner;
    const int index;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProcessorProgramPropertyComp)
};

class ProgramAudioProcessorEditor : public AudioProcessorEditor
{
public:
    ProgramAudioProcessorEditor(AudioProcessor* const p)
        : AudioProcessorEditor(p)
    {
        jassert(p != nullptr);
        setOpaque(true);

        addAndMakeVisible(panel);

        Array<PropertyComponent*> programs;

        const int numPrograms = p->getNumPrograms();
        int totalHeight = 0;

        for (int i = 0; i < numPrograms; ++i)
        {
            String name(p->getProgramName(i).trim());

            if (name.isEmpty())
                name = "Unnamed";

            ProcessorProgramPropertyComp* const pc = new ProcessorProgramPropertyComp(name, *p, i);
            programs.add(pc);
            totalHeight += pc->getPreferredHeight();
        }

        panel.addProperties(programs);

        setSize(400, jlimit(25, 400, totalHeight));
    }

    void paint(Graphics& g)
    {
        g.fillAll(Colours::grey);
    }

    void resized()
    {
        panel.setBounds(getLocalBounds());
    }

private:
    PropertyPanel panel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProgramAudioProcessorEditor)
};

//==============================================================================
PluginWindow* PluginWindow::getWindowFor(AudioProcessorGraph::Node* const node,
                                       WindowFormatType type)
{
    jassert(node != nullptr);

    for (int i = activePluginWindows.size(); --i >= 0;)
        if (activePluginWindows.getUnchecked(i)->owner == node
             && activePluginWindows.getUnchecked(i)->type == type)
            return activePluginWindows.getUnchecked(i);

    AudioProcessor* processor = node->getProcessor();
    AudioProcessorEditor* ui = nullptr;

    if (type == Normal)
    {
        ui = processor->createEditorIfNeeded();

        if (ui == nullptr)
            type = Generic;
    }

    if (ui == nullptr)
    {
        if (type == Generic || type == Parameters)
            ui = new GenericAudioProcessorEditor(processor);
        else if (type == Programs)
            ui = new ProgramAudioProcessorEditor(processor);
    }

    if (ui != nullptr)
    {
        if (AudioPluginInstance* const plugin = dynamic_cast<AudioPluginInstance*>(processor))
            ui->setName(plugin->getName());

        return new PluginWindow(ui, node, type);
    }

    return nullptr;
}

PluginWindow::~PluginWindow()
{
    activePluginWindows.removeFirstMatchingValue(this);
    clearContentComponent();
}

void PluginWindow::moved()
{
    owner->properties.set(getLastXProp(type), getX());
    owner->properties.set(getLastYProp(type), getY());
}

void PluginWindow::closeButtonPressed()
{
    owner->properties.set(getOpenProp(type), false);
    delete this;
}
<JUCERPROJECT id="NTe0XB0ij" name="Light Host" projectType="guiapp" version="1.2.0"...>
#include "../JuceLibraryCode/JuceHeader.h"
#include "PluginWindow.h"
class ProcessorProgramPropertyComp : public PropertyComponent,
#include "PluginWindow.h"

class PluginWindow;
static Array <PluginWindow*> activePluginWindows;

PluginWindow::PluginWindow (Component* const pluginEditor,
                            AudioProcessorGraph::Node* const o,
                            WindowFormatType t)
    : DocumentWindow (pluginEditor->getName(), Colours::lightgrey,
                      DocumentWindow::minimiseButton | DocumentWindow::closeButton),
      owner (o),
      type (t)
{
    setSize (400, 300);
    setUsingNativeTitleBar(true);
    setContentOwned (pluginEditor, true);

    #if JUCE_WINDOWS
    // Improve positioning on Windows 11
    int defaultX = Random::getSystemRandom().nextInt (500);
    int defaultY = Random::getSystemRandom().nextInt (500);
    
    // Ensure window is visible on screen (Windows 11 has multiple virtual desktops)
    Rectangle<int> screenArea = Desktop::getInstance().getDisplays().getMainDisplay().userArea;
    defaultX = jmin(screenArea.getWidth() - 400, defaultX);
    defaultY = jmin(screenArea.getHeight() - 300, defaultY);
    #else
    int defaultX = Random::getSystemRandom().nextInt (500);
    int defaultY = Random::getSystemRandom().nextInt (500);
    #endif

    setTopLeftPosition (owner->properties.getWithDefault (getLastXProp (type), defaultX),
                        owner->properties.getWithDefault (getLastYProp (type), defaultY));

    owner->properties.set (getOpenProp (type), true);

    setVisible (true);

    activePluginWindows.add (this);
    
}

void PluginWindow::closeCurrentlyOpenWindowsFor (const uint32 nodeId)
{
    for (int i = activePluginWindows.size(); --i >= 0;)
        if (activePluginWindows.getUnchecked(i)->owner->nodeId == nodeId)
            delete activePluginWindows.getUnchecked (i);
}

void PluginWindow::closeAllCurrentlyOpenWindows()
{
    if (activePluginWindows.size() > 0)
    {
        for (int i = activePluginWindows.size(); --i >= 0;)
            delete activePluginWindows.getUnchecked (i);

        Component dummyModalComp;
        dummyModalComp.enterModalState();
        MessageManager::getInstance()->runDispatchLoopUntil (50);
    }
}

bool PluginWindow::containsActiveWindows()
{
    return activePluginWindows.size() > 0;
}

//==============================================================================
class ProcessorProgramPropertyComp : public PropertyComponent,
#include "../JuceLibraryCode/JuceHeader.h"
#include "PluginWindow.h"

static Array<PluginWindow*> activePluginWindows;

// Single definition of ProcessorProgramPropertyComp
class ProcessorProgramPropertyComp : public PropertyComponent, private AudioProcessorListener
{
public:
    ProcessorProgramPropertyComp (const String& name, AudioProcessor& p, int index_)
        : PropertyComponent (name),
          owner (p),
          index (index_)
    {
        owner.addListener (this);
    }

    ~ProcessorProgramPropertyComp()
    {
        owner.removeListener (this);
    }

    void refresh() override { }
    void audioProcessorChanged (AudioProcessor*) override { }
    void audioProcessorParameterChanged (AudioProcessor* processor, int, float) override { }

private:
    AudioProcessor& owner;
    const int index;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProcessorProgramPropertyComp)
};

// Implementation of the missing method
float PluginWindow::getDesktopScaleFactor() const
{
    return Desktop::getInstance().getGlobalScaleFactor();
}
private AudioProcessorListener
delete activePluginWindows.getUnchecked (i);
float PluginWindow::getDesktopScaleFactor() const
{
    return Desktop::getInstance().getGlobalScaleFactor();
}
void refresh() override { }
void audioProcessorChanged (AudioProcessor*) override { }
void audioProcessorParameterChanged (AudioProcessor* processor, int, float) override { }
void refresh() { }
virtual void audioProcessorChanged (AudioProcessor*) { }
virtual void audioProcessorParameterChanged(AudioProcessor* processor, int, float) { }
<JUCERPROJECT id="NTe0XB0ij" name="Light Host" projectType="guiapp" version="1.2.0"...>
class ProcessorProgramPropertyComp : public PropertyComponent, private AudioProcessorListener
{
public:
    ProcessorProgramPropertyComp (const String& name, AudioProcessor& p, int index_)
        : PropertyComponent (name),
          owner (p),
          index (index_)
    {
        owner.addListener (this);
    }

    ~ProcessorProgramPropertyComp()
    {
        owner.removeListener (this);
    }

    void refresh() override { }
    void audioProcessorChanged (AudioProcessor*) override { }
    void audioProcessorParameterChanged (AudioProcessor* processor, int, float) override { }

private:
    AudioProcessor& owner;
    const int index;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProcessorProgramPropertyComp)
};
    class ProcessorProgramPropertyComp : public PropertyComponent, private AudioProcessorListener
    {
    public:
        ProcessorProgramPropertyComp (const String& name, AudioProcessor& p, int index_)
            : PropertyComponent (name),
              owner (p),
              index (index_)
        {
            owner.addListener (this);
        }

        ~ProcessorProgramPropertyComp()
        {
            owner.removeListener (this);
        }

        void refresh() override { }
        void audioProcessorChanged (AudioProcessor*) override { }
        void audioProcessorParameterChanged (AudioProcessor* processor, int, float) override { }

    private:
        AudioProcessor& owner;
        const int index;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProcessorProgramPropertyComp)
    };
    <JUCERPROJECT id="NTe0XB0ij" name="Light Host" projectType="guiapp" version="1.2.0"
                  /* ...other attributes... */>
      <!-- ... other elements ... -->
      <LIVE_SETTINGS>
        <OSX headerPath=""/>
        <WINDOWS installerType="inno" winArchitecture="x64"/>
        <LINUX/>
      </LIVE_SETTINGS>
    </JUCERPROJECT>
    <JUCERPROJECT id="NTe0XB0ij" name="Light Host" projectType="guiapp" version="1.2.0"
                  buildVST="1" buildRTAS="0" buildAU="1" 
class ProcessorProgramPropertyComp : public PropertyComponent, private AudioProcessorListener
{
public:
    ProcessorProgramPropertyComp (const String& name, AudioProcessor& p, int index_)
        : PropertyComponent (name),
          owner (p),
          index (index_)
    {
        owner.addListener (this);
    }

    ~ProcessorProgramPropertyComp()
    {
        owner.removeListener (this);
    }

    void refresh() override { }
    void audioProcessorChanged (AudioProcessor*) override { }
    void audioProcessorParameterChanged (AudioProcessor* processor, int, float) override { }

private:
    AudioProcessor& owner;
    const int index;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProcessorProgramPropertyComp)
};
                  vstFolder="./VST_SDK" vst3Folder="./VST3_SDK"
                  pluginName="Light Host" pluginDesc="VST/AU/VST3 Plugin Host"
                  /* ...other attributes... */>
    <JUCERPROJECT id="NTe0XB0ij" name="Light Host" projectType="guiapp" version="1.2.0"
                  /* ...other attributes... */
                  displaySplashScreen="0" useAppConfig="0" addUsingNamespaceToJuceHeader="0"
                  hardwareAccelerated="1" enableGNUExtensions="1" userNotes="High DPI support for Windows 11">
    <JUCERPROJECT id="NTe0XB0ij" name="Light Host" projectType="guiapp" version="1.2.0"
                  displaySplashScreen="0" splashScreenColour="Dark" reportAppUsage="0"
                  /* ...other attributes... */>
    <JUCEOPTIONS JUCE_WASAPI="enabled" JUCE_DIRECTSOUND="enabled" JUCE_ALSA="enabled"
                 JUCE_QUICKTIME="disabled" JUCE_USE_FLAC="disabled" JUCE_USE_OGGVORBIS="disabled"
                 JUCE_USE_CDBURNER="disabled" JUCE_USE_CDREADER="disabled" JUCE_USE_CAMERA="disabled"
                 JUCE_PLUGINHOST_VST="enabled" JUCE_PLUGINHOST_AU="enabled" JUCE_WEB_BROWSER="disabled"
                 JUCE_PLUGINHOST_VST3="enabled" JUCE_ASIO="enabled" JUCE_PLUGINHOST_LADSPA="enabled"
                 JUCE_PLUGINHOST_LV2="enabled" JUCE_PLUGINHOST_AAX="enabled" JUCE_PLUGINHOST_ARA="enabled"
                 JUCE_PLUGINHOST_AU_AIRMUSIC="enabled"/>
    <JUCEOPTIONS JUCE_WASAPI="enabled" JUCE_DIRECTSOUND="enabled" JUCE_ALSA="enabled"
                 JUCE_QUICKTIME="disabled" JUCE_USE_FLAC="disabled" JUCE_USE_OGGVORBIS="disabled"
                 JUCE_USE_CDBURNER="disabled" JUCE_USE_CDREADER="disabled" JUCE_USE_CAMERA="disabled"
                 JUCE_PLUGINHOST_VST="enabled" JUCE_PLUGINHOST_AU="enabled" JUCE_WEB_BROWSER="disabled"
                 JUCE_PLUGINHOST_VST3="enabled" JUCE_ASIO="enabled" JUCE_PLUGINHOST_LADSPA="enabled"
                 JUCE_PLUGINHOST_LV2="enabled" JUCE_PLUGINHOST_AAX="enabled" JUCE_PLUGINHOST_ARA="enabled"
                 JUCE_PLUGINHOST_AU_AIRMUSIC="enabled"/>
    <VS2022 targetFolder="Builds/VisualStudio2022" vstFolder="" vst3Folder=""
            smallIcon="pmwje3" bigIcon="kxxp8K">
      <CONFIGURATIONS>
        <CONFIGURATION name="Debug" winWarningLevel="4" generateManifest="1" winArchitecture="x64"
                       isDebug="1" optimisation="1" targetName="Light Host"
                       cppLanguageStandard="c++17"/>
        <CONFIGURATION name="Release" winWarningLevel="4" generateManifest="1" winArchitecture="x64"
                       isDebug="0" optimisation="3" targetName="Light Host"
                       cppLanguageStandard="c++17"/>
      </CONFIGURATIONS>
      <MODULEPATHS>
        <!-- Same module paths as VS2015 configuration -->
        <MODULEPATH id="juce_video" path="lib/juce"/>
        <MODULEPATH id="juce_opengl" path="lib/juce"/>
        <!-- ... other modules ... -->
      </MODULEPATHS>
    </VS2022>
    <!-- For VS2015 configuration -->
    <CONFIGURATION name="Release" winWarningLevel="4" generateManifest="1" winArchitecture="x64"
                   isDebug="0" optimisation="3" targetName="Light Host"
                   cppLanguageStandard="c++17" cppLibType=""/>
    <MODULEPATH id="juce_video" path="lib/juce"/>
    <MODULEPATH id="juce_opengl" path="lib/juce"/>
    <!-- ... other modules with consistent forward slashes ... -->
    <JUCERPROJECT id="NTe0XB0ij" name="Light Host" projectType="guiapp" version="1.2.0"
                  jucerVersion="7.0.9" companyName="Rolando Islas" bundleIdentifier="com.rolandoislas.lighthost"
                  companyWebsite="https://www.rolandoislas.com" companyEmail="admin@rolandoislas.com"
                  includeBinaryInAppConfig="1">
    <JUCERPROJECT id="NTe0XB0ij" name="Light Host" projectType="guiapp" version="1.2.0"
                  /* ...other attributes... */>
      <!-- ... other elements ... -->
      <LIVE_SETTINGS>
        <OSX headerPath=""/>
        <WINDOWS installerType="inno" winArchitecture="x64"/>
        <LINUX/>
      </LIVE_SETTINGS>
    </JUCERPROJECT>
    <JUCERPROJECT id="NTe0XB0ij" name="Light Host" projectType="guiapp" version="1.2.0"
                  /* ...other attributes... */>
      <!-- ... other elements ... -->
      <LIVE_SETTINGS>
        <OSX headerPath=""/>
        <WINDOWS installerType="inno" winArchitecture="x64"/>
        <LINUX/>
      </LIVE_SETTINGS>
    </JUCERPROJECT>
                                     private AudioProcessorListener
{
public:
    ProcessorProgramPropertyComp (const String& name, AudioProcessor& p, int index_)
        : PropertyComponent (name),
          owner (p),
          index (index_)
    {
        owner.addListener (this);
    }

    ~ProcessorProgramPropertyComp()
    {
        owner.removeListener (this);
    }

    void refresh() { }
    virtual void audioProcessorChanged (AudioProcessor*) { }
    virtual void audioProcessorParameterChanged(AudioProcessor* processor, int, float) { }

private:
    AudioProcessor& owner;
    const int index;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProcessorProgramPropertyComp)
};

class ProgramAudioProcessorEditor : public AudioProcessorEditor
{
public:
    ProgramAudioProcessorEditor (AudioProcessor* const p)
        : AudioProcessorEditor (p)
    {
        jassert (p != nullptr);
        setOpaque (true);

        addAndMakeVisible (panel);

        Array<PropertyComponent*> programs;

        const int numPrograms = p->getNumPrograms();
        int totalHeight = 0;

        for (int i = 0; i < numPrograms; ++i)
        {
            String name (p->getProgramName (i).trim());

            if (name.isEmpty())
                name = "Unnamed";

            ProcessorProgramPropertyComp* const pc = new ProcessorProgramPropertyComp (name, *p, i);
            programs.add (pc);
            totalHeight += pc->getPreferredHeight();
        }

        panel.addProperties (programs);

        setSize (400, jlimit (25, 400, totalHeight));
    }

    void paint (Graphics& g)
    {
        g.fillAll (Colours::grey);
    }

    void resized()
    {
        panel.setBounds (getLocalBounds());
    }

private:
    PropertyPanel panel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProgramAudioProcessorEditor)
};

//==============================================================================
PluginWindow* PluginWindow::getWindowFor (AudioProcessorGraph::Node* const node,
                                          WindowFormatType type)
{
    jassert (node != nullptr);

    for (int i = activePluginWindows.size(); --i >= 0;)
        if (activePluginWindows.getUnchecked(i)->owner == node
             && activePluginWindows.getUnchecked(i)->type == type)
            return activePluginWindows.getUnchecked(i);

    AudioProcessor* processor = node->getProcessor();
    AudioProcessorEditor* ui = nullptr;

    if (type == Normal)
    {
        ui = processor->createEditorIfNeeded();

        if (ui == nullptr)
            type = Generic;
    }

    if (ui == nullptr)
    {
        if (type == Generic || type == Parameters)
            ui = new GenericAudioProcessorEditor (processor);
        else if (type == Programs)
            ui = new ProgramAudioProcessorEditor (processor);
    }

    if (ui != nullptr)
    {
        if (AudioPluginInstance* const plugin = dynamic_cast<AudioPluginInstance*> (processor))
            ui->setName (plugin->getName());

        return new PluginWindow (ui, node, type);
    }

    return nullptr;
}

PluginWindow::~PluginWindow()
{
    activePluginWindows.removeFirstMatchingValue (this);
    clearContentComponent();
}

void PluginWindow::moved()
{
    owner->properties.set (getLastXProp (type), getX());
    owner->properties.set (getLastYProp (type), getY());
}

void PluginWindow::closeButtonPressed()
{
    owner->properties.set (getOpenProp (type), false);
    delete this;
}
