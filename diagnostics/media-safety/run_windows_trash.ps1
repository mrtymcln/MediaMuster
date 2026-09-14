param(
    [Parameter(Mandatory=$true)][string]$Probe,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [int]$TimeoutSeconds = 45,
    [switch]$DialogFollowupOnly
)
$ErrorActionPreference = 'Stop'
$Probe = (Resolve-Path -LiteralPath $Probe).Path
[void](New-Item -ItemType Directory -Force -Path $OutputDirectory)
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
$runId = [guid]::NewGuid().ToString('N')
$summary = [ordered]@{
    runId=$runId; machine=[Environment]::OSVersion.VersionString;
    user=[Security.Principal.WindowsIdentity]::GetCurrent().Name;
    probe=$Probe; timeoutSeconds=$TimeoutSeconds; dialogFollowupOnly=[bool]$DialogFollowupOnly;
    limitation='Hosted Windows Server; not interactive Windows 10/11 or real Avid/shared-storage validation.';
    cases=@(); setup='not-started'
}
$settings = @{}
$virtualDisk = Join-Path $OutputDirectory "disposable-$runId.vhd"
$mounted = $false
$driveRoot = $null

function Save-Summary {
    $summary | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'windows-trash-summary.json') -Encoding utf8
}
function Run-ProcessBounded([string]$Executable, [string[]]$Arguments, [string]$Prefix, [int]$Seconds,
    [string]$DialogAction='None',[string]$ProbeEvidence='') {
    $stdout = Join-Path $OutputDirectory "$Prefix.stdout.txt"
    $stderr = Join-Path $OutputDirectory "$Prefix.stderr.txt"
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName=$Executable
    $start.UseShellExecute=$false
    $start.RedirectStandardOutput=$true
    $start.RedirectStandardError=$true
    foreach ($argument in $Arguments) { [void]$start.ArgumentList.Add($argument) }
    $process=[Diagnostics.Process]::new()
    $process.StartInfo=$start
    [void]$process.Start()
    $outTask=$process.StandardOutput.ReadToEndAsync()
    $errTask=$process.StandardError.ReadToEndAsync()
    $clock=[Diagnostics.Stopwatch]::StartNew()
    $inspection=$null
    if($DialogAction -ne 'None') {
        $finished=$process.WaitForExit(3000)
        if(!$finished -and (Test-Path -LiteralPath $ProbeEvidence)) {
            $inspectionJson=Join-Path $OutputDirectory "$Prefix-dialog.json"
            $inspector=Join-Path $PSScriptRoot 'inspect_probe_dialog.ps1'
            $inspection=Run-ProcessBounded 'powershell.exe' @('-NoProfile','-NonInteractive','-MTA','-ExecutionPolicy','Bypass',
                '-File',$inspector,'-TargetProcessId',"$($process.Id)",'-Evidence',$ProbeEvidence,'-Output',$inspectionJson,'-Action',$DialogAction) "$Prefix-inspector" 15
            $inspection['evidence']=$inspectionJson
        }
        $finished=$process.WaitForExit([math]::Max(1,$Seconds*1000-[int]$clock.ElapsedMilliseconds))
    } else { $finished=$process.WaitForExit($Seconds*1000) }
    if (!$finished) {
        # Only this diagnostic child and its descendants are terminated.
        $process.Kill($true)
        [void]$process.WaitForExit(10000)
    }
    $outTask.GetAwaiter().GetResult() | Set-Content -LiteralPath $stdout -Encoding utf8
    $errTask.GetAwaiter().GetResult() | Set-Content -LiteralPath $stderr -Encoding utf8
    return [ordered]@{ timedOut=(!$finished); exitCode=$process.ExitCode; stdout=$stdout; stderr=$stderr; dialogInspection=$inspection }
}
function Run-Case([string]$Name, [long]$Bytes, [string]$Root, [bool]$ScanVolume, [string]$ExpectedControl='not-run',[string]$DialogAction='None') {
    $case=[ordered]@{name=$Name; bytes=$Bytes; expectedControl=$ExpectedControl;dialogAction=$DialogAction}
    $modes=if($ScanVolume){@('control','production')}else{@('production')}
    foreach($mode in $modes) {
        $json=Join-Path $OutputDirectory "$Name-$mode.json"
        $caseRoot=Join-Path $Root "$Name-$mode"
        $arguments=@('--case',$Name,'--root',$caseRoot,'--output',$json,'--bytes',"$Bytes",'--mode',$mode)
        if($ScanVolume){$arguments+=@('--scan-volume')}
        Write-Host "Running $Name / $mode (generated disposable bytes only)"
        $action=if($mode -eq 'production'){$DialogAction}else{'None'}
        $run=Run-ProcessBounded $Probe $arguments "$Name-$mode" $TimeoutSeconds $action $json
        $run['evidence']=$json
        if(Test-Path -LiteralPath $json) {
            $data=Get-Content -LiteralPath $json -Raw | ConvertFrom-Json
            $run['stage']=$data.stage
            $run['classification']=$data.classification
            $run['expectedSha256']=$data.expectedSha256
            $run['source']=$data.source
            $run['scanLimitReached']=$data.scanLimitReached
            $run['unreadableScanDirectories']=$data.unreadableScanDirectories
            if($data.source -and (Test-Path -LiteralPath $data.source -PathType Leaf)) {
                $run['sourceSha256AfterChild']=(Get-FileHash -LiteralPath $data.source -Algorithm SHA256).Hash.ToLowerInvariant()
            }
        }
        if($run.timedOut) { $run['classification']='inconclusive-child-timeout-possible-shell-dialog' }
        $case[$mode]=$run
    }
    if($ScanVolume) {
        # NTFS System Volume Information is not a Shell recycle location. Explicitly
        # exclude only that exact volume-root directory; retain all other scan limits.
        $blocking=@($case.control.unreadableScanDirectories | Where-Object {$_.path -notmatch '^[A-Za-z]:[/\\]System Volume Information$'})
        $case['ignoredScanDirectories']=@($case.control.unreadableScanDirectories | Where-Object {$_.path -match '^[A-Za-z]:[/\\]System Volume Information$'})
        $case['configurationDemonstrated']=(!$case.control.timedOut -and !$case.control.scanLimitReached -and
            $blocking.Count -eq 0 -and $case.control.classification -eq $ExpectedControl)
    }
    $summary.cases+=@($case)
    Save-Summary
}
function Remember-Setting([string]$Path,[string]$Name) {
    $id="$Path|$Name"
    if($settings.ContainsKey($id)){return}
    $key=Get-Item -LiteralPath $Path -ErrorAction SilentlyContinue
    $exists=($null -ne $key) -and ($key.GetValueNames() -contains $Name)
    $settings[$id]=@{path=$Path;name=$Name;existed=$exists;keyExisted=($null -ne $key)}
    if($exists) {
        $settings[$id]['value']=$key.GetValue($Name)
        $settings[$id]['kind']=$key.GetValueKind($Name).ToString()
    }
    if($key){$key.Close()}
}
function Set-DiagnosticSetting([string]$Path,[string]$Name,[int]$Value) {
    Remember-Setting $Path $Name
    [void](New-Item -Path $Path -Force)
    [void](New-ItemProperty -LiteralPath $Path -Name $Name -Value $Value -PropertyType DWord -Force)
}
function Configure-Bin([int]$Nuke,[int]$CapacityMb,[int]$PolicyNuke) {
    foreach($key in $script:binKeys) {
        Set-DiagnosticSetting $key 'NukeOnDelete' $Nuke
        Set-DiagnosticSetting $key 'MaxCapacity' $CapacityMb
    }
    Set-DiagnosticSetting 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Policies\Explorer' 'NoRecycleFiles' $PolicyNuke
    $config=[ordered]@{nukeOnDelete=$Nuke;capacityMiB=$CapacityMb;noRecycleFiles=$PolicyNuke;keys=@()}
    foreach($key in $script:binKeys) {
        $row=Get-ItemProperty -LiteralPath $key
        $config.keys+=@(@{path=$key;NukeOnDelete=$row.NukeOnDelete;MaxCapacity=$row.MaxCapacity})
    }
    return $config
}

try {
    # Ordinary test needs no policy changes and does not inspect the system volume.
    $ordinaryRoot=Join-Path $env:RUNNER_TEMP "_mediamuster-trash-diagnostic/$runId"
    if(!$DialogFollowupOnly){Run-Case 'ordinary-runner-volume' 65536 $ordinaryRoot $false}
    $letters=@('Z','Y','X','W','V','U','T','S','R')
    $letter=$letters | Where-Object { !(Test-Path "${_}:\") -and !(Get-PSDrive -Name $_ -ErrorAction SilentlyContinue) } | Select-Object -First 1
    if(!$letter){throw 'No unused diagnostic drive letter available.'}
    if($virtualDisk.IndexOfAny([char[]]"`r`n`"") -ge 0 -or (Test-Path -LiteralPath $virtualDisk)){throw 'Unsafe or occupied generated VHD pathname.'}
    $create=Join-Path $OutputDirectory 'create-disposable-vhd.txt'
    @"
create vdisk file="$virtualDisk" maximum=256 type=expandable
select vdisk file="$virtualDisk"
attach vdisk
create partition primary
format fs=ntfs label=MMTRASH_DIAG quick
assign letter=$letter
exit
"@ | Set-Content -LiteralPath $create -Encoding ascii
    $setup=Run-ProcessBounded 'diskpart.exe' @('/s',$create) 'create-vhd' 60
    $summary['diskpartCreate']=$setup
    $driveRoot="${letter}:\"
    $volume=Get-Volume -DriveLetter $letter -ErrorAction SilentlyContinue
    if($setup.timedOut -or !$volume -or $volume.FileSystemLabel -ne 'MMTRASH_DIAG') {
        throw 'Disposable VHD was not confirmed; bin edge cases were not run.'
    }
    $mounted=$true
    $summary.setup='disposable-vhd-mounted'
    $summary['virtualDisk']=$virtualDisk
    $summary['volume']=@{driveRoot=$driveRoot;uniqueId=$volume.UniqueId;fileSystem=$volume.FileSystem;label=$volume.FileSystemLabel}
    $guidMatch=[regex]::Match([string]$volume.UniqueId,'\{[0-9a-fA-F-]+\}')
    if(!$guidMatch.Success){throw 'No volume GUID found for the disposable VHD.'}
    $guid=$guidMatch.Value
    $base='HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\BitBucket'
    # Explorer's per-volume persistence is undocumented. Configure observed variants,
    # record them, and require the independently destructive disposable legacy control
    # to demonstrate each requested condition before drawing a conclusion.
    $script:binKeys=@("$base\Volume\$guid","$base\Volume$guid","$base\Volume\Volume$guid")
    if(Test-Path -LiteralPath $base) {
        $script:binKeys+=@(Get-ChildItem -LiteralPath $base -Recurse -ErrorAction SilentlyContinue |
            Where-Object {$_.Name -like "*$guid*"} | ForEach-Object {'HKCU:' + $_.Name.Substring('HKEY_CURRENT_USER'.Length)})
    }
    $script:binKeys=@($script:binKeys | Select-Object -Unique)
    $root=Join-Path $driveRoot "_mediamuster-trash-diagnostic/$runId"
    $matrix=@(
        @{name='vhd-normal';bytes=65536;nuke=0;capacity=64;policy=0;control='control-recycled'},
        @{name='vhd-bin-disabled';bytes=65536;nuke=1;capacity=64;policy=0;control='control-original-gone-no-matching-content-found'},
        @{name='vhd-oversize';bytes=4194304;nuke=0;capacity=1;policy=0;control='control-original-gone-no-matching-content-found'},
        @{name='vhd-policy-disabled';bytes=65536;nuke=0;capacity=64;policy=1;control='control-original-gone-no-matching-content-found'},
        @{name='vhd-normal-reset';bytes=65536;nuke=0;capacity=64;policy=0;control='control-recycled'}
    )
    if($DialogFollowupOnly) {
        $matrix=@(
            @{name='vhd-oversize-inspect';bytes=4194304;nuke=0;capacity=1;policy=0;control='control-original-gone-no-matching-content-found';dialog='Record'},
            @{name='vhd-oversize-no';bytes=4194304;nuke=0;capacity=1;policy=0;control='control-original-gone-no-matching-content-found';dialog='No'},
            @{name='vhd-oversize-yes';bytes=4194304;nuke=0;capacity=1;policy=0;control='control-original-gone-no-matching-content-found';dialog='Yes'}
        )
    }
    foreach($row in $matrix) {
        $config=Configure-Bin $row.nuke $row.capacity $row.policy
        $config | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $OutputDirectory "$($row.name)-configuration.json") -Encoding utf8
        $dialog=if($row.ContainsKey('dialog')){$row.dialog}else{'None'}
        Run-Case $row.name $row.bytes $root $true $row.control $dialog
    }
    $summary.setup='matrix-completed'
} catch {
    $summary['harnessError']=$_.Exception.Message
    Write-Host "Windows diagnostic limitation: $($_.Exception.Message)"
} finally {
    foreach($saved in $settings.Values) {
        try {
            if($saved.existed) {
                [void](New-ItemProperty -LiteralPath $saved.path -Name $saved.name -Value $saved.value -PropertyType $saved.kind -Force)
            } else {
                Remove-ItemProperty -LiteralPath $saved.path -Name $saved.name -ErrorAction SilentlyContinue
            }
        } catch { $summary['settingsRestoreError']=$_.Exception.Message }
    }
    $summary['registryValuesRestored']=!$summary.Contains('settingsRestoreError')
    # Preserve generated evidence and VHD contents; detach only this exact generated file.
    if(Test-Path -LiteralPath $virtualDisk) {
        $detach=Join-Path $OutputDirectory 'detach-disposable-vhd.txt'
        "select vdisk file=`"$virtualDisk`"`r`ndetach vdisk`r`nexit" | Set-Content -LiteralPath $detach -Encoding ascii
        try { $summary['diskpartDetach']=Run-ProcessBounded 'diskpart.exe' @('/s',$detach) 'detach-vhd' 45 }
        catch { $summary['detachError']=$_.Exception.Message }
    }
    Save-Summary
}
Write-Host ($summary | ConvertTo-Json -Depth 15)
if($summary.Contains('harnessError')) { exit 2 }
if(@($summary.cases | Where-Object {$_.production.classification -eq 'unresolved-no-matching-content-found'}).Count -gt 0) { exit 3 }
exit 0
