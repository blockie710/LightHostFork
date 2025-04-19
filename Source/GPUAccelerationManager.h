//
// GPUAccelerationManager.h
// Nova Host
//
// Created for NovaHost April 19, 2025
//

#pragma once

#include "../JuceLibraryCode/JuceHeader.h"
#include <memory>
#include <vector>
#include <map>

/**
 * Central manager for GPU acceleration throughout the application
 * Handles OpenGL context sharing, shader compilation, and GPU capability detection
 */
class GPUAccelerationManager : public juce::DeletedAtShutdown
{
public:
    /** Returns the singleton instance of the GPU acceleration manager */
    static GPUAccelerationManager& getInstance();
    
    /** Destructor - cleans up GPU resources */
    ~GPUAccelerationManager();
    
    /** Check if GPU acceleration is available on this system */
    bool isGPUAccelerationAvailable();
    
    /** Returns true if GPU acceleration is currently enabled */
    bool isGPUAccelerationEnabled();
    
    /** Enable or disable GPU acceleration */
    void setGPUAccelerationEnabled(bool shouldBeEnabled);
    
    /** Get standard shared OpenGL context for use across the application */
    juce::OpenGLContext* getSharedContext();
    
    /** Apply GPU acceleration to a component if enabled */
    void applyToComponent(juce::Component* component, bool continuousRepaint = false);
    
    /** Remove GPU acceleration from a component */
    void removeFromComponent(juce::Component* component);
    
    /** Get information about the current GPU */
    juce::String getGPUInfo();
    
    /** Get the current OpenGL version string */
    juce::String getOpenGLVersionString();
    
    /** Check if a specific OpenGL feature is supported */
    bool isFeatureSupported(const juce::String& featureName);
    
    /** Configure optimal settings based on detected GPU capabilities */
    void configureOptimalSettings();
    
    /** Switch to a different GPU on multi-GPU systems (if supported) */
    bool selectGPU(const juce::String& gpuName);
    
    /** Get available GPU names on this system */
    juce::StringArray getAvailableGPUs();
    
    /** Monitor GPU performance metrics */
    struct GPUMetrics {
        float frameTimeMs;
        float gpuLoadPercent;
        int frameCount;
        int64 lastFrameTimestamp;
    };
    
    /** Get current GPU performance metrics */
    GPUMetrics getCurrentMetrics();
    
private:
    GPUAccelerationManager();
    
    class GPUContext : public juce::OpenGLContext
    {
    public:
        GPUContext();
        ~GPUContext() override;
        
        void initialise();
        bool isSupported() const { return initialized && supported; }
        juce::String getGPUVendor() const { return gpuVendor; }
        juce::String getGPURenderer() const { return gpuRenderer; }
        juce::String getGLVersion() const { return glVersion; }
        
        float getLastFrameTime() const { return lastFrameTimeMs; }
        void updateFrameTime(float newTimeMs) { lastFrameTimeMs = newTimeMs; }
        
    private:
        bool initialized = false;
        bool supported = false;
        juce::String gpuVendor;
        juce::String gpuRenderer;
        juce::String glVersion;
        float lastFrameTimeMs = 0.0f;
        
        void onRender() override;
        
        friend class GPUAccelerationManager;
    };
    
    std::unique_ptr<GPUContext> context;
    bool enabled = false;
    
    struct ComponentData {
        juce::Component* component = nullptr;
        bool continuousRepaint = false;
    };
    
    std::map<juce::Component*, ComponentData> acceleratedComponents;
    GPUMetrics metrics;
    
    juce::String detectGPUVendor();
    juce::StringArray detectAvailableGPUs();
    void initializeIfNeeded();
    void updateMetrics();
    
    // Prevent copying
    GPUAccelerationManager(const GPUAccelerationManager&) = delete;
    GPUAccelerationManager& operator= (const GPUAccelerationManager&) = delete;
    
    JUCE_DECLARE_SINGLETON(GPUAccelerationManager, true)
};