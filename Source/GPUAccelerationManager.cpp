//
// GPUAccelerationManager.cpp
// Nova Host
//
// Created for NovaHost April 19, 2025
//

#include "GPUAccelerationManager.h"

JUCE_IMPLEMENT_SINGLETON(GPUAccelerationManager)

// GPUContext implementation
GPUAccelerationManager::GPUContext::GPUContext()
{
    // Set rendering with vsync by default
    setRenderer(this);
    setComponentPaintingEnabled(true);
    setContinuousRepainting(false);
    setOpenGLVersionRequired(juce::OpenGLContext::OpenGLVersion::openGL3_2);
}

GPUAccelerationManager::GPUContext::~GPUContext()
{
    shutdownOpenGL();
}

void GPUAccelerationManager::GPUContext::initialise()
{
    if (initialized)
        return;
    
    // Attempt to initialize OpenGL
    auto initResult = [this]() -> bool {
        try {
            juce::Component tempComponent;
            attachTo(tempComponent);
            
            // Wait for initialization to complete
            int attempts = 0;
            while (!isAttached() && attempts++ < 10)
                juce::Thread::sleep(50);
                
            if (!isAttached())
                return false;
            
            // Get OpenGL information
            if (isAttached())
            {
                execute([this]() {
                    gpuVendor = juce::String(reinterpret_cast<const char*>(glGetString(GL_VENDOR)));
                    gpuRenderer = juce::String(reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
                    glVersion = juce::String(reinterpret_cast<const char*>(glGetString(GL_VERSION)));
                }, true);
            }
            
            // Detach from temporary component
            detach();
            return true;
        }
        catch (const std::exception& e) {
            juce::Logger::writeToLog("OpenGL initialization error: " + juce::String(e.what()));
            return false;
        }
    }();
    
    initialized = initResult;
    supported = initResult && gpuRenderer.isNotEmpty();
}

void GPUAccelerationManager::GPUContext::onRender()
{
    // This is called during each render cycle
    // Can be used for global OpenGL state management or performance monitoring
    
    // Measure render time
    static juce::uint32 lastRenderTimestamp = 0;
    
    juce::uint32 currentTime = juce::Time::getMillisecondCounter();
    if (lastRenderTimestamp > 0)
    {
        lastFrameTimeMs = static_cast<float>(currentTime - lastRenderTimestamp);
    }
    
    lastRenderTimestamp = currentTime;
}

// GPUAccelerationManager implementation
GPUAccelerationManager::GPUAccelerationManager()
    : context(std::make_unique<GPUContext>()),
      enabled(false)
{
    metrics.frameTimeMs = 0.0f;
    metrics.gpuLoadPercent = 0.0f;
    metrics.frameCount = 0;
    metrics.lastFrameTimestamp = 0;
}

GPUAccelerationManager::~GPUAccelerationManager()
{
    // Make sure all components are properly detached from the OpenGL context
    for (const auto& [component, data] : acceleratedComponents)
    {
        if (component != nullptr && context != nullptr)
        {
            if (context->isAttached())
                context->detach();
        }
    }
    
    acceleratedComponents.clear();
    
    // Clean up context
    context.reset();
    
    clearSingletonInstance();
}

bool GPUAccelerationManager::isGPUAccelerationAvailable()
{
    initializeIfNeeded();
    return context != nullptr && context->isSupported();
}

bool GPUAccelerationManager::isGPUAccelerationEnabled()
{
    return enabled && isGPUAccelerationAvailable();
}

void GPUAccelerationManager::setGPUAccelerationEnabled(bool shouldBeEnabled)
{
    if (shouldBeEnabled && !isGPUAccelerationAvailable())
    {
        juce::Logger::writeToLog("Cannot enable GPU acceleration: not available on this system");
        return;
    }
    
    if (enabled == shouldBeEnabled)
        return;
    
    enabled = shouldBeEnabled;
    
    // Apply change to all registered components
    for (const auto& [component, data] : acceleratedComponents)
    {
        if (component != nullptr)
        {
            if (enabled)
            {
                context->attachTo(*component);
                if (data.continuousRepaint)
                    context->setContinuousRepainting(true);
            }
            else
            {
                context->detach();
            }
        }
    }
}

juce::OpenGLContext* GPUAccelerationManager::getSharedContext()
{
    initializeIfNeeded();
    return context.get();
}

void GPUAccelerationManager::applyToComponent(juce::Component* component, bool continuousRepaint)
{
    if (component == nullptr)
        return;
        
    initializeIfNeeded();
    
    // Store component data
    ComponentData data;
    data.component = component;
    data.continuousRepaint = continuousRepaint;
    acceleratedComponents[component] = data;
    
    // Apply acceleration if enabled
    if (enabled && context != nullptr)
    {
        context->attachTo(*component);
        if (continuousRepaint)
            context->setContinuousRepainting(true);
    }
    
    // Make sure component cleans up properly when destroyed
    class ComponentCleanupHelper : public juce::ComponentListener
    {
    public:
        ComponentCleanupHelper(GPUAccelerationManager* manager, juce::Component* comp)
            : owner(manager), component(comp)
        {
            component->addComponentListener(this);
        }

        ~ComponentCleanupHelper() override
        {
            if (component != nullptr)
                component->removeComponentListener(this);
        }

        void componentBeingDeleted(juce::Component& comp) override
        {
            if (owner != nullptr && &comp == component)
                owner->removeFromComponent(component);
        }

    private:
        GPUAccelerationManager* owner;
        juce::Component* component;
    };

    data.cleanupHelper = std::make_shared<ComponentCleanupHelper>(this, component);
}

void GPUAccelerationManager::removeFromComponent(juce::Component* component)
{
    if (component == nullptr)
        return;
        
    auto it = acceleratedComponents.find(component);
    if (it != acceleratedComponents.end())
    {
        if (context != nullptr)
            context->detach();
            
        acceleratedComponents.erase(it);
    }
}

juce::String GPUAccelerationManager::getGPUInfo()
{
    initializeIfNeeded();
    
    if (context != nullptr)
    {
        return "Vendor: " + context->getGPUVendor() + 
               "\nRenderer: " + context->getGPURenderer() + 
               "\nOpenGL Version: " + context->getGLVersion();
    }
    
    return "GPU information not available";
}

juce::String GPUAccelerationManager::getOpenGLVersionString()
{
    initializeIfNeeded();
    return context != nullptr ? context->getGLVersion() : juce::String();
}

bool GPUAccelerationManager::isFeatureSupported(const juce::String& featureName)
{
    initializeIfNeeded();
    
    if (!context->isAttached())
        return false;
        
    bool supported = false;
    
    // Execute on the OpenGL thread to check for feature support
    context->execute([this, &featureName, &supported]() {
        // Common OpenGL feature checks
        if (featureName == "GLSL")
        {
            const char* glVersion = reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION));
            supported = (glVersion != nullptr && strlen(glVersion) > 0);
        }
        else if (featureName == "FBO")
        {
            GLint maxAttach = 0;
            glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &maxAttach);
            supported = (maxAttach > 0);
        }
        else if (featureName == "MSAA")
        {
            GLint maxSamples = 0;
            glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
            supported = (maxSamples > 1);
        }
        else if (featureName == "Compute")
        {
            const char* glVersion = reinterpret_cast<const char*>(glGetString(GL_VERSION));
            // Rough check for compute shader support (OpenGL 4.3+)
            supported = (glVersion != nullptr && strstr(glVersion, "4.") != nullptr &&
                        (glVersion[2] >= '3' || glVersion[2] == '1' && glVersion[3] >= '0'));
        }
    }, true);
    
    return supported;
}

void GPUAccelerationManager::configureOptimalSettings()
{
    initializeIfNeeded();
    
    if (!isGPUAccelerationAvailable())
        return;
    
    // Configure based on GPU vendor
    juce::String vendor = context->getGPUVendor().toLowerCase();
    
    if (vendor.contains("nvidia"))
    {
        // NVIDIA optimizations
        context->setSwapInterval(1); // V-sync on
        // Additional NVIDIA-specific settings could be applied here
    }
    else if (vendor.contains("amd") || vendor.contains("ati"))
    {
        // AMD optimizations
        context->setSwapInterval(1); // V-sync on
    }
    else if (vendor.contains("intel"))
    {
        // Intel integrated GPU optimizations
        context->setSwapInterval(1); // V-sync on
        // Lower quality settings might be appropriate for Intel GPUs
    }
    else
    {
        // Default settings for unknown GPUs
        context->setSwapInterval(1);
    }
}

bool GPUAccelerationManager::selectGPU(const juce::String& gpuName)
{
    // GPU selection is platform-dependent and may not be supported
    
    #if JUCE_WINDOWS
    // On Windows, attempt to select GPU using NVIDIA or AMD APIs
    // This is just a placeholder - actual implementation would require vendor SDKs
    
    // Implementation would typically use NvAPI or AMD GPU Services (AGS) Library
    
    // Return true only if selection is confirmed successful
    return false;
    
    #elif JUCE_MAC || JUCE_IOS
    // macOS/iOS GPU selection is handled by the OS
    // Can't directly choose GPU on Apple platforms
    return false;
    
    #else
    // For other platforms (Linux, etc.)
    return false;
    #endif
}

juce::StringArray GPUAccelerationManager::getAvailableGPUs()
{
    return detectAvailableGPUs();
}

GPUAccelerationManager::GPUMetrics GPUAccelerationManager::getCurrentMetrics()
{
    if (isGPUAccelerationEnabled())
    {
        updateMetrics();
    }
    
    return metrics;
}

juce::String GPUAccelerationManager::detectGPUVendor()
{
    initializeIfNeeded();
    return context != nullptr ? context->getGPUVendor() : juce::String();
}

juce::StringArray GPUAccelerationManager::detectAvailableGPUs()
{
    juce::StringArray result;
    
    // Add the primary GPU
    initializeIfNeeded();
    if (context != nullptr && context->isSupported())
    {
        result.add(context->getGPURenderer());
    }
    
    #if JUCE_WINDOWS
    // On Windows, attempt to enumerate GPUs
    // This would be implemented using platform-specific APIs
    
    // Just a naive implementation example:
    if (juce::SystemStats::getEnvironmentVariable("PCI_DEVICES", {}).contains("VGA"))
    {
        // Parse output to find additional GPUs
    }
    
    #elif JUCE_MAC || JUCE_IOS
    // macOS/iOS GPU detection
    // This would use IOKit to enumerate GPUs
    
    #elif JUCE_LINUX
    // Linux GPU detection using glxinfo or similar
    juce::ChildProcess glxinfo;
    if (glxinfo.start("glxinfo -B", juce::ChildProcess::wantStdOut))
    {
        juce::String output = glxinfo.readAllProcessOutput();
        
        if (output.isEmpty())
        {
            juce::Logger::writeToLog("glxinfo output is empty. Unable to detect GPUs.");
            return result;
        }

        juce::StringArray lines;
        lines.addLines(output);
        
        for (const auto& line : lines)
        {
            if (line.contains("Device:"))
            {
                juce::String deviceName = line.fromFirstOccurrenceOf("Device:", false, false).trim();
                
                if (deviceName.isEmpty())
                {
                    juce::Logger::writeToLog("Found a 'Device:' line, but the device name is empty.");
                    continue;
                }
                
                if (!result.contains(deviceName))
                {
                    result.add(deviceName);
                }
            }
        }

        if (result.isEmpty())
        {
            juce::Logger::writeToLog("No valid GPUs detected from glxinfo output.");
        }
    }
    else
    {
        juce::Logger::writeToLog("Failed to execute glxinfo. Ensure it is installed and accessible.");
    }
    #endif
    
    return result;
}

void GPUAccelerationManager::initializeIfNeeded()
{
    if (context == nullptr)
        context = std::make_unique<GPUContext>();
        
    if (context != nullptr && !context->isSupported())
        context->initialise();
}

void GPUAccelerationManager::updateMetrics()
{
    if (context != nullptr && context->isAttached())
    {
        metrics.frameTimeMs = context->getLastFrameTime();
        
        int64 currentTime = juce::Time::currentTimeMillis();
        
        if (metrics.lastFrameTimestamp > 0)
        {
            int64 elapsedMs = currentTime - metrics.lastFrameTimestamp;
            if (elapsedMs > 0)
            {
                // Calculate framerate and estimate load
                float fps = 1000.0f / (float)elapsedMs;
                
                // Rough estimate of GPU load based on frame time
                // This is a very simplistic approach - real GPU load would require vendor-specific APIs
                metrics.gpuLoadPercent = juce::jlimit(0.0f, 100.0f, metrics.frameTimeMs * fps);
                
                metrics.frameCount++;
            }
        }
        
        metrics.lastFrameTimestamp = currentTime;
    }
}