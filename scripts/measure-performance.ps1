param(
    [string]$Exe=(Join-Path (Split-Path $PSScriptRoot -Parent) 'dist\Breakly.exe'),
    [ValidateRange(0,120)][int]$IdleMinutes=5,
    [ValidateRange(1,200)][int]$Cycles=20,
    [string]$DeliveryDirectory=(Join-Path (Split-Path $PSScriptRoot -Parent) 'dist'),
    [string]$OutputDirectory=(Join-Path (Split-Path $PSScriptRoot -Parent) ('measurements\'+(Get-Date -Format 'yyyyMMdd-HHmmss')))
)
$ErrorActionPreference='Stop'
$Exe=(Resolve-Path -LiteralPath $Exe).Path
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
[IO.Directory]::CreateDirectory($OutputDirectory) | Out-Null
$profilePath=Join-Path $OutputDirectory 'profile'
[IO.Directory]::CreateDirectory($profilePath) | Out-Null
# Isolated profile; no registry changes or interference with the user's running instance.
[IO.File]::WriteAllText((Join-Path $profilePath 'settings.json'),'{"version":1,"reading":true,"interval0":1440,"interval1":1440,"interval2":1440,"onboarded":true}')
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class BreaklyProbe {
    public delegate bool EnumProc(IntPtr w, IntPtr p);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb,IntPtr p);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr w,out uint pid);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr w,System.Text.StringBuilder s,int n);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr w,uint m,IntPtr a,IntPtr b);
    [DllImport("user32.dll")] public static extern uint GetGuiResources(IntPtr process,uint flag);
    [DllImport("psapi.dll",SetLastError=true)] static extern bool QueryWorkingSet(IntPtr p,IntPtr buffer,int size);
    public static IntPtr Window(int pid,string name) {
        IntPtr found=IntPtr.Zero;
        EnumWindows((w,p)=>{uint id;GetWindowThreadProcessId(w,out id);var s=new System.Text.StringBuilder(200);GetClassName(w,s,200);if(id==pid && s.ToString()==name){found=w;return false;}return true;},IntPtr.Zero);
        return found;
    }
    public static long PrivateWorkingSet(IntPtr process) {
        int size=1024*1024;
        for(int attempt=0;attempt<8;attempt++,size*=2) {
            IntPtr p=Marshal.AllocHGlobal(size);
            try {
                if(!QueryWorkingSet(process,p,size))continue;
                long count=Marshal.ReadIntPtr(p).ToInt64(),pages=0;
                if(count>(size/IntPtr.Size)-1)continue;
                for(long i=0;i<count;i++) {long flags=Marshal.ReadIntPtr(p,checked((int)((i+1)*IntPtr.Size))).ToInt64();if((flags&(1L<<8))==0)pages++;}
                return pages*Environment.SystemPageSize;
            } finally {Marshal.FreeHGlobal(p);}
        }
        throw new InvalidOperationException("QueryWorkingSet failed");
    }
}
'@
$rows=[Collections.Generic.List[object]]::new()
$startup=[Diagnostics.Stopwatch]::StartNew()
$appProcess=Start-Process -FilePath $Exe -ArgumentList @('--background','--diagnostics','--data-dir',('"'+$profilePath+'"')) -WindowStyle Hidden -PassThru
$hostWindow=[IntPtr]::Zero
while ($startup.Elapsed.TotalSeconds -lt 10) {
    $hostWindow=[BreaklyProbe]::Window($appProcess.Id,'Breakly.Host.V1')
    if ($hostWindow -ne [IntPtr]::Zero -and (Test-Path -LiteralPath (Join-Path $profilePath 'startup.json'))) { break }
    if ($appProcess.HasExited) { throw 'Breakly exited before tray readiness.' }
    Start-Sleep -Milliseconds 10
}
if ($hostWindow -eq [IntPtr]::Zero) { throw 'Breakly did not create its host window.' }
$startup.Stop()
$script:lastSnapshotTime=$appProcess.StartTime
$script:lastSnapshotCpu=0.0
function Command([int]$id) {
    [BreaklyProbe]::PostMessage($hostWindow,0x111,[IntPtr]$id,[IntPtr]::Zero) | Out-Null
}
function Snapshot([string]$stage,[double]$cpu=-1,[double]$oneCore=-1) {
    $appProcess.Refresh()
    $sampleTime=Get-Date
    $interval=($sampleTime-$script:lastSnapshotTime).TotalMilliseconds
    $processCpu=$appProcess.TotalProcessorTime.TotalMilliseconds
    if($cpu -lt 0 -and $interval -gt 0) {
        $oneCore=($processCpu-$script:lastSnapshotCpu)/$interval*100
        $cpu=$oneCore/[Environment]::ProcessorCount
    }
    $row=[pscustomobject]@{
        Stage=$stage;TimestampUtc=[DateTime]::UtcNow.ToString('o');ElapsedSec=[math]::Round(((Get-Date)-$appProcess.StartTime).TotalSeconds,3)
        PrivateWorkingSetBytes=[BreaklyProbe]::PrivateWorkingSet($appProcess.Handle)
        PrivateBytes=$appProcess.PrivateMemorySize64;WorkingSetBytes=$appProcess.WorkingSet64
        CpuPercentAllCores=$cpu;CpuPercentOneCore=$oneCore;TotalCpuMs=$processCpu
        Threads=$appProcess.Threads.Count;Gdi=[BreaklyProbe]::GetGuiResources($appProcess.Handle,0);User=[BreaklyProbe]::GetGuiResources($appProcess.Handle,1);Handles=$appProcess.HandleCount
    }
    $script:lastSnapshotTime=$sampleTime;$script:lastSnapshotCpu=$processCpu
    $rows.Add($row);$rows | Export-Csv -LiteralPath (Join-Path $OutputDirectory 'metrics.csv') -NoTypeInformation -Encoding UTF8
    $row | ConvertTo-Json -Compress | Write-Output
}
try {
    Snapshot 'cold_tray'
    $appProcess.Refresh();$initialCpu=$appProcess.TotalProcessorTime.TotalMilliseconds
    $idle=[Diagnostics.Stopwatch]::StartNew()
    $milestones=@(5,30,$IdleMinutes) | Where-Object {$_ -gt 0 -and $_ -le $IdleMinutes} | Sort-Object -Unique
    foreach($minute in $milestones) {
        while($idle.Elapsed.TotalSeconds -lt $minute*60) {Start-Sleep -Milliseconds ([int][math]::Min(5000,[math]::Max(10,$minute*60000-$idle.Elapsed.TotalMilliseconds)))}
        $appProcess.Refresh();$oneCore=($appProcess.TotalProcessorTime.TotalMilliseconds-$initialCpu)/$idle.Elapsed.TotalMilliseconds*100
        Snapshot ('idle_'+$minute+'m') ($oneCore/[Environment]::ProcessorCount) $oneCore
    }
    Command 100;Start-Sleep -Seconds 2;Snapshot 'main_open'
    Command 101;Start-Sleep -Seconds 2;Snapshot 'main_closed'
    Command 102;Start-Sleep -Seconds 2;Snapshot 'notification_open'
    Command 104;Start-Sleep -Seconds 2;Snapshot 'notification_closed'
    # Compare resource counts after cache warmup; first-use loading is not a handle leak.
    Command 100;Start-Sleep -Milliseconds 150;Command 101;Start-Sleep -Milliseconds 150
    Snapshot 'cycles_baseline'
    for($cycle=1;$cycle -le $Cycles;$cycle++) {
        Command 100;Start-Sleep -Milliseconds 150;Command 101;Start-Sleep -Milliseconds 150
        if($cycle -in @(10,20,40,60,100) -and $cycle -ne $Cycles){Start-Sleep -Seconds 1;Snapshot ('after_'+$cycle+'_cycles')}
    }
    Start-Sleep -Seconds 2;Snapshot ('after_'+$Cycles+'_cycles')
    $files=Get-ChildItem -LiteralPath $DeliveryDirectory -Recurse -File | Select-Object Name,Length
    $files | Export-Csv -LiteralPath (Join-Path $OutputDirectory 'delivery.csv') -NoTypeInformation -Encoding UTF8
    $metadata=[pscustomobject]@{OS=[Environment]::OSVersion.VersionString;LogicalProcessors=[Environment]::ProcessorCount;Exe=$Exe;ExeSha256=(Get-FileHash -LiteralPath $Exe -Algorithm SHA256).Hash;StartupProcessToReadyObservedMs=$startup.Elapsed.TotalMilliseconds;TrayInternal=(Get-Content -LiteralPath (Join-Path $profilePath 'startup.json') -Raw | ConvertFrom-Json);IdleMinutes=$IdleMinutes;Cycles=$Cycles;CpuUnavailableValue=-1}
    $metadata | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'environment.json') -Encoding UTF8
} finally {
    if (-not $appProcess.HasExited) { Command 103; if(-not $appProcess.WaitForExit(10000)){Write-Warning 'Close the test instance from its tray menu.'} }
}
