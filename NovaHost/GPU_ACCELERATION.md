# GPU Acceleration in NovaHost

NovaHost now includes comprehensive GPU acceleration support for improved performance and visual quality, especially when hosting complex plugin UIs.

## Features

- **Hardware-Accelerated Rendering**: Plugin UIs are rendered using modern OpenGL for smoother displays
- **Optimized Performance**: Reduced CPU usage during UI rendering, leaving more processing power for audio
- **Automatic GPU Detection**: NovaHost detects the best GPU on multi-GPU systems
- **Per-Plugin Settings**: Enable/disable GPU acceleration per plugin
- **Smart Resource Management**: Properly manages GPU resources to prevent memory leaks

## Requirements

- **Windows**: DirectX 11 compatible GPU, Windows 10 or later
- **macOS**: Metal-compatible GPU, macOS 10.13 or later
- **Linux**: OpenGL 3.2+ compatible GPU with updated drivers

## Common Questions

### How do I enable GPU acceleration?

GPU acceleration is enabled by default if your system supports it. You can toggle this in the global settings.

### Can I disable GPU acceleration for specific plugins?

Yes. Right-click on a plugin UI window's title bar and select "Disable GPU Acceleration" from the context menu if a specific plugin has visual issues with acceleration enabled.

### Will GPU acceleration improve audio performance?

Indirectly, yes. By offloading UI rendering to the GPU, your CPU has more resources available for audio processing, potentially allowing for lower latency settings and better performance.

### Does GPU acceleration increase memory usage?

There is a slight increase in memory usage when GPU acceleration is enabled, but NovaHost intelligently manages GPU resources to minimize this overhead.

## Performance Monitoring

NovaHost includes a GPU performance monitoring tool that can be accessed from Settings → Performance → GPU Monitor. This displays:

- Current GPU usage
- Frame rendering time
- GPU memory consumption

## Troubleshooting

If you experience issues with GPU acceleration:

1. Update your graphics drivers to the latest version
2. Try disabling GPU acceleration for problematic plugins
3. Check if your GPU meets the minimum requirements
4. Consult the detailed logs in the NovaHost log directory

## Technical Details

The GPU acceleration implementation uses OpenGL with a shared context model to efficiently render plugin UIs. It automatically configures optimal settings based on your specific GPU model and vendor.
