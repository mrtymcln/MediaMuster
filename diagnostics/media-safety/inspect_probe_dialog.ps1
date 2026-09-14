param(
    [Parameter(Mandatory=$true)][int]$TargetProcessId,
    [Parameter(Mandatory=$true)][string]$Evidence,
    [Parameter(Mandatory=$true)][string]$Output,
    [ValidateSet('Record','No','Yes')][string]$Action='Record'
)
$ErrorActionPreference='Stop'
$result=[ordered]@{targetProcessId=$TargetProcessId;action=$Action;windows=@();clicked=$false}
function Save-Result { $result | ConvertTo-Json -Depth 15 | Set-Content -LiteralPath $Output -Encoding utf8 }
function Fixture-Hash([string]$Path) {
    $stream=[System.IO.File]::OpenRead($Path)
    $sha=[System.Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-','').ToLowerInvariant() }
    finally { $sha.Dispose(); $stream.Dispose() }
}
Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public class MMWindow {
 public long handle; public long owner; public uint processId; public string text; public string className;
 public int id; public bool enabled; public bool visible; public List<MMWindow> children=new List<MMWindow>();
}
public static class MMDiagWindows {
 delegate bool EnumProc(IntPtr window,IntPtr param);
 [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc proc,IntPtr param);
 [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr parent,EnumProc proc,IntPtr param);
 [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window,out uint process);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr window,StringBuilder text,int length);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr window,StringBuilder text,int length);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern IntPtr SendMessageTimeout(IntPtr window,uint message,UIntPtr wParam,StringBuilder lParam,uint flags,uint timeout,out UIntPtr result);
 [DllImport("user32.dll")] static extern bool PostMessage(IntPtr window,uint message,IntPtr wParam,IntPtr lParam);
 [DllImport("user32.dll")] static extern IntPtr GetWindow(IntPtr window,uint command);
 [DllImport("user32.dll")] static extern int GetDlgCtrlID(IntPtr window);
 [DllImport("user32.dll")] static extern bool IsWindowEnabled(IntPtr window);
 [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr window);
 [DllImport("user32.dll")] static extern bool IsChild(IntPtr parent,IntPtr child);
 public static uint ProcessId(long window) { uint id;GetWindowThreadProcessId(new IntPtr(window),out id);return id; }
 static MMWindow Read(IntPtr window) {
   uint id;GetWindowThreadProcessId(window,out id);
   var text=new StringBuilder(4096);GetWindowText(window,text,text.Capacity);
   if(text.Length==0){UIntPtr ignored;SendMessageTimeout(window,0x000D,new UIntPtr(4096),text,2,100,out ignored);}
   var cls=new StringBuilder(256);GetClassName(window,cls,cls.Capacity);
   return new MMWindow{handle=window.ToInt64(),owner=GetWindow(window,4).ToInt64(),processId=id,text=text.ToString(),className=cls.ToString(),id=GetDlgCtrlID(window),enabled=IsWindowEnabled(window),visible=IsWindowVisible(window)};
 }
 public static List<MMWindow> Snapshot(int processId) {
   var all=new List<MMWindow>();
   EnumWindows((window,param)=>{uint id;GetWindowThreadProcessId(window,out id);if(id!=(uint)processId)return true;
     var item=Read(window);int count=0;EnumChildWindows(window,(child,p)=>{if(count++>=256)return false;item.children.Add(Read(child));return true;},IntPtr.Zero);all.Add(item);return all.Count<16;
   },IntPtr.Zero);return all;
 }
 public static bool Click(long parent,long button,int processId) {
   if(ProcessId(parent)!=(uint)processId||ProcessId(button)!=(uint)processId||!IsChild(new IntPtr(parent),new IntPtr(button)))return false;
   return PostMessage(new IntPtr(button),0x00F5,IntPtr.Zero,IntPtr.Zero);
 }
}
'@
try {
    $process=Get-Process -Id $TargetProcessId -ErrorAction Stop
    $data=Get-Content -LiteralPath $Evidence -Raw | ConvertFrom-Json
    $safeFixture=$data.mode -eq 'production' -and $data.stage -eq 'production-delete-entered' -and
        $data.volumeLabel -eq 'MMTRASH_DIAG' -and $data.source -match '/_mediamuster-trash-diagnostic/[0-9a-f]+/[^/]+/production\.bin$' -and
        (Test-Path -LiteralPath $data.source -PathType Leaf)
    if(!$safeFixture){throw 'Probe evidence does not identify a pending generated production fixture.'}
    $result['fixture']=$data.source
    $result['expectedSha256']=$data.expectedSha256
    $actual=Fixture-Hash $data.source
    if(!$data.expectedSha256 -or $actual -ne $data.expectedSha256){throw 'Generated fixture hash did not match.'}
    $windows=@([MMDiagWindows]::Snapshot($TargetProcessId))
    $result.windows=$windows
    Save-Result
    $uiaAvailable=$false
    try { Add-Type -AssemblyName UIAutomationClient;Add-Type -AssemblyName UIAutomationTypes;$uiaAvailable=$true }
    catch { $result['uiaLoadError']=$_.Exception.Message }
    $observations=@()
    foreach($window in $windows) {
        if(!$window.visible){continue}
        $observation=[ordered]@{handle=$window.handle;text=$window.text;nativeChildren=$window.children;uia=@();eligible=$false}
        $texts=@($window.text)+@($window.children | ForEach-Object {$_.text})
        $uiaButtons=@()
        if($uiaAvailable) {
            try {
                $element=[System.Windows.Automation.AutomationElement]::FromHandle([IntPtr]$window.handle)
                if($element.Current.ProcessId -ne $TargetProcessId){throw 'UIA root has another process id.'}
                $elements=$element.FindAll([System.Windows.Automation.TreeScope]::Descendants,[System.Windows.Automation.Condition]::TrueCondition)
                for($i=0;$i-lt[math]::Min($elements.Count,256);$i++) {
                    $child=$elements.Item($i);$current=$child.Current
                    $observation.uia+=@(@{name=$current.Name;automationId=$current.AutomationId;controlType=$current.ControlType.ProgrammaticName;processId=$current.ProcessId;enabled=$current.IsEnabled;handle=$current.NativeWindowHandle})
                    $texts+=@($current.Name)
                    if($current.ControlType -eq [System.Windows.Automation.ControlType]::Button -and $current.IsEnabled -and
                        $current.ProcessId -eq $TargetProcessId -and $current.Name.Replace('&','').Trim() -eq $Action) {$uiaButtons+=@($child)}
                }
            } catch {$observation['uiaError']=$_.Exception.Message}
        }
        $joined=$texts -join "`n"
        $observation['combinedText']=$joined
        $eligible=$joined -match 'production\.bin' -and $joined -match '(?i)permanent' -and $joined -match '(?i)delet' -and
            [MMDiagWindows]::ProcessId($window.handle) -eq $TargetProcessId
        $observation.eligible=$eligible
        $observations+=@($observation)
        $result['observations']=$observations
        Save-Result
        if($Action -eq 'Record' -or !$eligible -or $result.clicked){continue}
        # Recheck the target is alive and our generated file is intact immediately before the scoped click.
        if(!(Get-Process -Id $TargetProcessId -ErrorAction SilentlyContinue) -or
            (Fixture-Hash $data.source) -ne $data.expectedSha256){throw 'Fixture or process changed before click.'}
        $buttonId=if($Action -eq 'Yes'){6}else{7}
        $buttons=@($window.children | Where-Object {$_.processId -eq $TargetProcessId -and $_.className -eq 'Button' -and
            $_.id -eq $buttonId -and $_.enabled -and $_.text.Replace('&','').Trim() -eq $Action})
        if($buttons.Count -eq 1) {
            $result['clickMechanism']='BM_CLICK to same-PID descendant with matching fixed ID and label'
            $result['clicked']=[MMDiagWindows]::Click($window.handle,$buttons[0].handle,$TargetProcessId)
        } elseif($uiaButtons.Count -eq 1) {
            $pattern=$uiaButtons[0].GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern)
            $result['clickMechanism']='InvokePattern on uniquely named same-PID descendant of verified dialog'
            Save-Result
            $pattern.Invoke()
            $result['clicked']=$true
        } else { $result['clickRefused']='No unique matching button in the verified dialog.' }
        Save-Result
    }
} catch {$result['error']=$_.Exception.Message}
Save-Result
