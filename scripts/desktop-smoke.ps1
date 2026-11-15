param([string]$Exe=(Join-Path (Split-Path $PSScriptRoot -Parent) 'dist\Breakly.exe'))
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$resultDirectory=Join-Path $projectRoot ('checks\desktop-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
[IO.Directory]::CreateDirectory($resultDirectory) | Out-Null
$testProfile=Join-Path $resultDirectory 'profile'
[IO.Directory]::CreateDirectory($testProfile) | Out-Null
[IO.File]::WriteAllText((Join-Path $testProfile 'settings.json'),'{"version":1,"theme":2,"duration0":5,"reading":true,"fullscreen":0}')
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Text;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public static class DesktopCheck {
    public delegate bool EnumProc(IntPtr w,IntPtr p);
    [StructLayout(LayoutKind.Sequential)] public struct Rect {public int Left,Top,Right,Bottom;}
    [StructLayout(LayoutKind.Sequential)] struct GuiInfo {public int Size,Flags;public IntPtr Active,Focus,Capture,MenuOwner,MoveSize,Caret;public Rect CaretRect;}
    [DllImport("user32.dll")] static extern bool GetGUIThreadInfo(uint thread,ref GuiInfo info);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc cb,IntPtr p);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr w,out uint pid);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr w,StringBuilder s,int n);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr w,uint m,IntPtr a,IntPtr b);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr w,uint m,IntPtr a,IntPtr b);
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr w,int id);
    [DllImport("user32.dll")] static extern bool PrintWindow(IntPtr w,IntPtr dc,uint flags);
    [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr w,out Rect rect);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr w);
    [DllImport("user32.dll")] static extern bool SetForegroundWindow(IntPtr w);
    [DllImport("user32.dll")] static extern bool ShowWindow(IntPtr w,int command);
    [DllImport("user32.dll")] static extern bool SetWindowPos(IntPtr w,IntPtr after,int x,int y,int cx,int cy,uint flags);
    [DllImport("user32.dll")] static extern bool GetClientRect(IntPtr w,out Rect r);
    [DllImport("user32.dll")] static extern bool ScreenToClient(IntPtr w,ref Point p);
    [DllImport("user32.dll")] static extern IntPtr SetFocus(IntPtr w);
    [DllImport("user32.dll")] static extern IntPtr GetFocus();
    [DllImport("user32.dll")] static extern bool AttachThreadInput(uint a,uint b,bool attach);
    [DllImport("kernel32.dll")] static extern uint GetCurrentThreadId();
    [DllImport("user32.dll")] static extern uint GetDpiForWindow(IntPtr w);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern bool SetWindowText(IntPtr w,string text);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr w,StringBuilder s,int n);
    [DllImport("user32.dll",EntryPoint="SendMessageW",CharSet=CharSet.Unicode)] static extern IntPtr ReadText(IntPtr w,uint msg,IntPtr n,StringBuilder text);
    [DllImport("user32.dll",EntryPoint="SendMessageW",CharSet=CharSet.Unicode)] static extern IntPtr WriteText(IntPtr w,uint msg,IntPtr n,string text);
    [DllImport("user32.dll")] static extern IntPtr WindowFromPoint(Point p);
    public static string Text(IntPtr w) {var s=new StringBuilder(1024);ReadText(w,0xD,new IntPtr(1024),s);return s.ToString();}
    public static void EditText(IntPtr w,string text) {WriteText(w,0xC,IntPtr.Zero,text);}
    public static IntPtr Focused(IntPtr w) {uint pid;uint thread=GetWindowThreadProcessId(w,out pid);var info=new GuiInfo();info.Size=Marshal.SizeOf(typeof(GuiInfo));return GetGUIThreadInfo(thread,ref info)?info.Focus:IntPtr.Zero;}
    public static void Focus(IntPtr w) {
        uint id;uint thread=GetWindowThreadProcessId(w,out id);uint mine=GetCurrentThreadId();
        AttachThreadInput(mine,thread,true);try{SetFocus(w);}finally{AttachThreadInput(mine,thread,false);}
    }
    public static bool ContainsControl(IntPtr parent,IntPtr child) {
        Rect r,c;GetWindowRect(child,out r);GetClientRect(parent,out c);
        var a=new Point(r.Left,r.Top);var b=new Point(r.Right,r.Bottom);ScreenToClient(parent,ref a);ScreenToClient(parent,ref b);
        return a.X>=0&&a.Y>=0&&b.X<=c.Right&&b.Y<=c.Bottom;
    }
    public static void Resize(IntPtr w,int width,int height) {SetWindowPos(w,IntPtr.Zero,0,0,width,height,0x16);}
    public static void DpiMessage(IntPtr w) {
        Rect r;GetWindowRect(w,out r);IntPtr p=Marshal.AllocHGlobal(Marshal.SizeOf(typeof(Rect)));
        try {Marshal.StructureToPtr(r,p,false);uint dpi=GetDpiForWindow(w);SendMessage(w,0x2E0,new IntPtr(dpi|(dpi<<16)),p);}finally{Marshal.FreeHGlobal(p);}
    }
    public static IntPtr Window(int pid,string name) {
        IntPtr found=IntPtr.Zero;
        EnumWindows((w,p)=>{uint id;GetWindowThreadProcessId(w,out id);var s=new StringBuilder(200);GetClassName(w,s,200);if(id==pid && s.ToString()==name){found=w;return false;}return true;},IntPtr.Zero);
        return found;
    }
    public static void Capture(IntPtr w,string file) {
        ShowWindow(w,9);SetWindowPos(w,new IntPtr(-1),80,80,0,0,0x41);SetForegroundWindow(w);System.Threading.Thread.Sleep(700);
        Rect r;GetWindowRect(w,out r);
        uint owner,visibleOwner;GetWindowThreadProcessId(w,out owner);GetWindowThreadProcessId(WindowFromPoint(new Point((r.Left+r.Right)/2,(r.Top+r.Bottom)/2)),out visibleOwner);
        if(!IsWindowVisible(w)||r.Right<=r.Left||r.Bottom<=r.Top)throw new Exception("Window is unavailable for capture");
        using(var bitmap=new Bitmap(r.Right-r.Left,r.Bottom-r.Top)) {
            using(var graphics=Graphics.FromImage(bitmap)) {
                if(visibleOwner==owner)graphics.CopyFromScreen(r.Left,r.Top,0,0,new Size(r.Right-r.Left,r.Bottom-r.Top));
                else {IntPtr dc=graphics.GetHdc();try {if(!PrintWindow(w,dc,2))throw new Exception("PrintWindow failed");}finally{graphics.ReleaseHdc(dc);}}
            }
            bitmap.Save(file,ImageFormat.Png);
        }
        SetWindowPos(w,new IntPtr(-2),0,0,0,0,0x13);
    }
}
'@
$results=[Collections.Generic.List[string]]::new()
function Assert([bool]$condition,[string]$message) {if(-not $condition){throw $message};$results.Add($message);Write-Output ('PASS: '+$message)}
$appProcess=Start-Process -FilePath $Exe -ArgumentList @('--data-dir',('"'+$testProfile+'"')) -WindowStyle Hidden -PassThru
try {
    Start-Sleep -Seconds 2
    $hostWindow=[DesktopCheck]::Window($appProcess.Id,'Breakly.Host.V1')
    $mainWindow=[DesktopCheck]::Window($appProcess.Id,'Breakly.Main')
    Assert ($hostWindow -ne [IntPtr]::Zero) 'Host created'
    Assert ($mainWindow -ne [IntPtr]::Zero) 'First manual launch opens main window'
    [DesktopCheck]::Capture($mainWindow,(Join-Path $resultDirectory 'main-dark.png'))
    $duplicate=Start-Process -FilePath $Exe -ArgumentList @('--background','--data-dir',('"'+$testProfile+'"')) -WindowStyle Hidden -PassThru
    Assert ($duplicate.WaitForExit(5000)) 'Second instance exits'
    Assert ($duplicate.ExitCode -eq 0) 'Second instance succeeds'
    [DesktopCheck]::PostMessage($hostWindow,0x111,[IntPtr]101,[IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 400
    Assert (-not $appProcess.HasExited) 'Closing main keeps process running'
    Assert ([DesktopCheck]::Window($appProcess.Id,'Breakly.Main') -eq [IntPtr]::Zero) 'Main HWND destroyed on close'
    [DesktopCheck]::PostMessage($hostWindow,0x111,[IntPtr]102,[IntPtr]::Zero) | Out-Null
    Start-Sleep -Seconds 1
    $reminder=[DesktopCheck]::Window($appProcess.Id,'Breakly.Reminder')
    Assert ($reminder -ne [IntPtr]::Zero) 'Custom reminder created'
    [DesktopCheck]::PostMessage($hostWindow,0x111,[IntPtr]100,[IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 300
    $mainWindow=[DesktopCheck]::Window($appProcess.Id,'Breakly.Main')
    [DesktopCheck]::SendMessage([DesktopCheck]::GetDlgItem($mainWindow,208),0xF5,[IntPtr]::Zero,[IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 200
    Assert ([DesktopCheck]::Focused($reminder) -eq [DesktopCheck]::GetDlgItem($reminder,201)) 'Explicit reminder opening focuses first action'
    [DesktopCheck]::PostMessage([DesktopCheck]::GetDlgItem($reminder,201),0x100,[IntPtr]9,[IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 200
    Assert ([DesktopCheck]::Focused($reminder) -eq [DesktopCheck]::GetDlgItem($reminder,202)) 'Tab from reminder HWND reaches next action'
    [DesktopCheck]::Focus([DesktopCheck]::GetDlgItem($reminder,201))
    [DesktopCheck]::Capture($reminder,(Join-Path $resultDirectory 'notification-dark.png'))
    [DesktopCheck]::SendMessage([DesktopCheck]::GetDlgItem($reminder,201),0xF5,[IntPtr]::Zero,[IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 250
    $activeReminder=[DesktopCheck]::Window($appProcess.Id,'Breakly.Reminder')
    Assert ([DesktopCheck]::Focused($activeReminder) -eq [DesktopCheck]::GetDlgItem($activeReminder,203)) 'Starting rest preserves keyboard access'
    [DesktopCheck]::PostMessage($hostWindow,0x111,[IntPtr]107,[IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 250
    Assert ([DesktopCheck]::Window($appProcess.Id,'Breakly.Reminder') -ne [IntPtr]::Zero) 'Pause preserves active break window'
    [DesktopCheck]::PostMessage($hostWindow,0x111,[IntPtr]110,[IntPtr]::Zero) | Out-Null
    Start-Sleep -Seconds 6
    $reminder=[DesktopCheck]::Window($appProcess.Id,'Breakly.Reminder')
    [DesktopCheck]::Capture($reminder,(Join-Path $resultDirectory 'break-complete.png'))
    [DesktopCheck]::SendMessage([DesktopCheck]::GetDlgItem($reminder,204),0xF5,[IntPtr]::Zero,[IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 500
    Assert ([DesktopCheck]::Window($appProcess.Id,'Breakly.Reminder') -eq [IntPtr]::Zero) 'Confirm closes completed break'
    # Synthetic messages validate event dispatch, not actual OS locking or sleep.
    [DesktopCheck]::SendMessage($hostWindow,0x2B1,[IntPtr]7,[IntPtr]::Zero) | Out-Null
    [DesktopCheck]::SendMessage($hostWindow,0x2B1,[IntPtr]8,[IntPtr]::Zero) | Out-Null
    [DesktopCheck]::SendMessage($hostWindow,0x218,[IntPtr]4,[IntPtr]::Zero) | Out-Null
    [DesktopCheck]::SendMessage($hostWindow,0x218,[IntPtr]18,[IntPtr]::Zero) | Out-Null
    [DesktopCheck]::PostMessage($hostWindow,0x111,[IntPtr]105,[IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 500
    $mainWindow=[DesktopCheck]::Window($appProcess.Id,'Breakly.Main')
    [DesktopCheck]::Capture($mainWindow,(Join-Path $resultDirectory 'settings-dark.png'))
    [DesktopCheck]::SendMessage([DesktopCheck]::GetDlgItem($mainWindow,802),0xF5,[IntPtr]::Zero,[IntPtr]::Zero) | Out-Null
    Assert ([DesktopCheck]::GetDlgItem($mainWindow,510) -ne [IntPtr]::Zero) 'Break settings page opens'
    [DesktopCheck]::EditText([DesktopCheck]::GetDlgItem($mainWindow,510),'27')
    Assert ([DesktopCheck]::Text([DesktopCheck]::GetDlgItem($mainWindow,510)) -eq '27') 'Unsaved field can be edited'
    [DesktopCheck]::SendMessage($hostWindow,0x1A,[IntPtr]::Zero,[IntPtr]::Zero) | Out-Null
    Assert ([DesktopCheck]::Text([DesktopCheck]::GetDlgItem($mainWindow,510)) -eq '27') 'Theme event preserves unsaved input'
    [DesktopCheck]::DpiMessage($mainWindow)
    Assert ([DesktopCheck]::Text([DesktopCheck]::GetDlgItem($mainWindow,510)) -eq '27') 'DPI event preserves unsaved input'
    [DesktopCheck]::Resize($mainWindow,600,360)
    [DesktopCheck]::Focus([DesktopCheck]::GetDlgItem($mainWindow,400))
    Start-Sleep -Milliseconds 300
    Assert ([DesktopCheck]::ContainsControl($mainWindow,[DesktopCheck]::GetDlgItem($mainWindow,400))) 'Scroll follows focus to Save in a short window'
    [DesktopCheck]::Focus([DesktopCheck]::GetDlgItem($mainWindow,515))
    Start-Sleep -Milliseconds 300
    Assert ([DesktopCheck]::ContainsControl($mainWindow,[DesktopCheck]::GetDlgItem($mainWindow,515))) 'Horizontal scroll reveals a focused field'
    [DesktopCheck]::Capture($mainWindow,(Join-Path $resultDirectory 'short-window-focus.png'))
    [DesktopCheck]::Resize($mainWindow,920,760)
    [DesktopCheck]::SendMessage([DesktopCheck]::GetDlgItem($mainWindow,801),0xF5,[IntPtr]::Zero,[IntPtr]::Zero) | Out-Null
    [DesktopCheck]::SendMessage([DesktopCheck]::GetDlgItem($mainWindow,500),0x14E,[IntPtr]1,[IntPtr]::Zero) | Out-Null
    [DesktopCheck]::SendMessage([DesktopCheck]::GetDlgItem($mainWindow,400),0xF5,[IntPtr]::Zero,[IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 500
    [DesktopCheck]::Capture($mainWindow,(Join-Path $resultDirectory 'settings-light.png'))
    [DesktopCheck]::PostMessage($hostWindow,0x111,[IntPtr]103,[IntPtr]::Zero) | Out-Null
    Assert ($appProcess.WaitForExit(5000)) 'Exit terminates process'
    $statistics=Get-Content -LiteralPath (Join-Path $testProfile 'statistics.json') -Raw | ConvertFrom-Json
    Assert ($statistics.days[0].completed -eq 1) 'Completed break persisted once'
    Assert ($statistics.days[0].started -eq 1) 'Started break persisted'
    $settings=Get-Content -LiteralPath (Join-Path $testProfile 'settings.json') -Raw | ConvertFrom-Json
    Assert ($settings.theme -eq 1) 'UI saves settings atomically'
    $results | Set-Content -LiteralPath (Join-Path $resultDirectory 'passed.txt') -Encoding UTF8
    Write-Output ('Results: '+$resultDirectory)
} finally {
    if(-not $appProcess.HasExited -and $hostWindow -ne [IntPtr]::Zero) {[DesktopCheck]::PostMessage($hostWindow,0x111,[IntPtr]103,[IntPtr]::Zero)|Out-Null;$appProcess.WaitForExit(5000)|Out-Null}
}
