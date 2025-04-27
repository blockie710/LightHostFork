# NovaHost Memory Usage Monitor
# This script monitors the memory usage of NovaHost over time
# and saves the results to a CSV file for analysis

param (
    [Parameter(Mandatory=$false)]
    [string]$ProcessName = "Nova Host",
    
    [Parameter(Mandatory=$false)]
    [int]$Duration = 3600,  # Default monitoring duration in seconds (1 hour)
    
    [Parameter(Mandatory=$false)]
    [int]$Interval = 5,     # Interval between measurements in seconds
    
    [Parameter(Mandatory=$false)]
    [string]$OutputFile = "memory_usage_$(Get-Date -Format 'yyyyMMdd_HHmmss').csv"
)

# Check if output directory exists, create if not
$outputDir = ".\logs"
if (-not (Test-Path -Path $outputDir)) {
    New-Item -ItemType Directory -Path $outputDir | Out-Null
}

$outputPath = Join-Path -Path $outputDir -ChildPath $OutputFile

# Display startup information
Write-Host "NovaHost Memory Usage Monitor"
Write-Host "============================="
Write-Host "Process to monitor: $ProcessName"
Write-Host "Duration: $Duration seconds"
Write-Host "Sampling interval: $Interval seconds"
Write-Host "Output file: $outputPath"
Write-Host ""

# Create CSV file with headers
"Timestamp,ProcessID,WorkingSet_MB,PrivateBytes_MB,VirtualMem_MB,CPU_Percent,Threads,Handles" | Out-File -FilePath $outputPath

# Function to get memory stats
function Get-ProcessMemoryStats {
    param (
        [Parameter(Mandatory=$true)]
        [string]$ProcessName
    )
    
    try {
        $processes = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue
        
        if (-not $processes) {
            return $null
        }
        
        foreach ($process in $processes) {
            # Get CPU counter if available
            $cpuPercent = 0
            try {
                $cpuCounter = (Get-Counter -Counter "\Process($($process.Name))\% Processor Time" -ErrorAction SilentlyContinue).CounterSamples.CookedValue
                if ($cpuCounter) {
                    $cpuPercent = [math]::Round($cpuCounter / [Environment]::ProcessorCount, 2)
                }
            } catch {
                # Counter not available
            }
            
            # Create custom object with process stats
            [PSCustomObject]@{
                Timestamp = (Get-Date -Format "yyyy-MM-dd HH:mm:ss")
                ProcessID = $process.Id
                WorkingSet_MB = [math]::Round($process.WorkingSet64 / 1MB, 2)
                PrivateBytes_MB = [math]::Round($process.PrivateMemorySize64 / 1MB, 2)
                VirtualMem_MB = [math]::Round($process.VirtualMemorySize64 / 1MB, 2)
                CPU_Percent = $cpuPercent
                Threads = $process.Threads.Count
                Handles = $process.HandleCount
            }
        }
    } catch {
        Write-Host "Error getting process stats: $_"
        return $null
    }
}

# Main monitoring loop
$startTime = Get-Date
$endTime = $startTime.AddSeconds($Duration)
$iteration = 0

Write-Host "Starting monitoring at $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
Write-Host "Press Ctrl+C to stop monitoring early"
Write-Host ""

try {
    while ((Get-Date) -lt $endTime) {
        $iteration++
        
        # Get process stats
        $stats = Get-ProcessMemoryStats -ProcessName $ProcessName
        
        if ($stats) {
            foreach ($stat in $stats) {
                # Write to console (updates)
                if ($iteration % 5 -eq 0) {
                    Write-Host "$(Get-Date -Format 'HH:mm:ss') - PID: $($stat.ProcessID), Memory: $($stat.WorkingSet_MB) MB, CPU: $($stat.CPU_Percent)%"
                }
                
                # Write to CSV
                "$($stat.Timestamp),$($stat.ProcessID),$($stat.WorkingSet_MB),$($stat.PrivateBytes_MB),$($stat.VirtualMem_MB),$($stat.CPU_Percent),$($stat.Threads),$($stat.Handles)" | 
                Out-File -FilePath $outputPath -Append
            }
        } else {
            Write-Host "Warning: Process '$ProcessName' not found at $(Get-Date -Format 'HH:mm:ss')"
        }
        
        # Sleep until next interval
        Start-Sleep -Seconds $Interval
    }
} catch {
    Write-Host "`nMonitoring interrupted: $_"
} finally {
    Write-Host "`nMonitoring completed at $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
    Write-Host "Results saved to $outputPath"
    Write-Host ""
    Write-Host "To analyze the results, you can import the CSV into Excel or use PowerShell:"
    Write-Host "    `$data = Import-Csv '$outputPath'"
    Write-Host "    `$data | Measure-Object -Property WorkingSet_MB -Maximum -Minimum -Average | Select-Object Count,Maximum,Minimum,Average"
}