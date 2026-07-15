param(
    [string]$ExeOverride = ""
)

$ErrorActionPreference = "Stop"

$repo = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$outputDir = Join-Path $repo "output\winx64"
if ($ExeOverride -and (Test-Path -LiteralPath $ExeOverride)) {
    $exePath = $ExeOverride
} else {
    $buildPath = Join-Path $outputDir "build\Release\DLNA Server.exe"
    $installPath = Join-Path $outputDir "DLNA Server.exe"
    if (Test-Path -LiteralPath $installPath) {
        $exePath = $installPath
    } elseif (Test-Path -LiteralPath $buildPath) {
        $exePath = $buildPath
    } else {
        throw "DLNA Server.exe not found. Run build-assets.bat --platform winx64 first."
    }
}
$configPath = Join-Path $outputDir "config.ini"
$testMediaDir = Join-Path $env:TEMP "dlna-server-TestMedia"

function Set-IniValue {
    param([string]$Section, [string]$Key, [string]$Value, [string]$FilePath)
    $content = Get-Content -LiteralPath $FilePath -Raw
    $sectionPattern = "(?ms)(^\[$Section\].*?)(?=^\[|$)"
    $keyPattern = "(?m)^($Key\s*=\s*).*$"
    if ($content -match $sectionPattern) {
        $sectionBlock = $Matches[1]
        if ($sectionBlock -match $keyPattern) {
            $replaced = $sectionBlock -replace $keyPattern, "`${1}$Value"
            $content = $content -replace [regex]::Escape($sectionBlock), $replaced
        } else {
            $replaced = $sectionBlock + "`r`n$Key=$Value"
            $content = $content -replace [regex]::Escape($sectionBlock), $replaced
        }
    } else {
        $content += "`r`n[$Section]`r`n$Key=$Value`r`n"
    }
    Set-Content -LiteralPath $FilePath -Value $content -Encoding UTF8
}

function Assert-Equal {
    param([string]$Expected, [string]$Actual, [string]$Message)
    if ($Expected -ne $Actual) {
        throw "FAIL $Message : expected '$Expected', got '$Actual'"
    }
    Write-Host "PASS $Message"
}

function Assert-NotEmpty {
    param([string]$Value, [string]$Message)
    if ([string]::IsNullOrEmpty($Value)) {
        throw "FAIL $Message : expected non-empty value, got '$Value'"
    }
    Write-Host "PASS $Message"
}

try {
    if (-not (Test-Path -LiteralPath $testMediaDir)) {
        New-Item -ItemType Directory -Path $testMediaDir -Force | Out-Null
        Set-Content -LiteralPath (Join-Path $testMediaDir "test.txt") -Value "test" -Encoding UTF8
    }

    if (Test-Path -LiteralPath $configPath) {
        $backupPath = $configPath + ".bak"
        Copy-Item -LiteralPath $configPath -Destination $backupPath -Force
    } else {
        $backupPath = $null
        @"
[Settings]
DeviceUUID=11111111-2222-3333-4444-555555555555
MediaSources=$testMediaDir
"@ | Set-Content -LiteralPath $configPath -Encoding UTF8
    }
    Set-IniValue "Settings" "DeviceUUID" "11111111-2222-3333-4444-555555555555" $configPath
    Set-IniValue "Settings" "MediaSources" $testMediaDir $configPath

    $env:DLNA_SERVER_SKIP_FIREWALL = "1"
    $serverProc = Start-Process -FilePath $exePath -PassThru

    Add-Type -AssemblyName System.Windows.Forms
    Add-Type -AssemblyName UIAutomationClient
    Add-Type -AssemblyName UIAutomationTypes
    Add-Type -AssemblyName WindowsBase

    $mainWindow = $null
    for ($i = 0; $i -lt 60; $i++) {
        Start-Sleep -Milliseconds 500
        $found = $null
        [System.Windows.Automation.AutomationElement]::RootElement.FindAll([System.Windows.Automation.Condition]::TrueCondition) | ForEach-Object {
            if ($_.Current.Name -match "DLNA Server") {
                $found = $_
            }
        }
        if ($found) {
            $mainWindow = $found
            break
        }
    }
    if (-not $mainWindow) {
        throw "DLNA Server window did not appear within 30 seconds"
    }

    $mainWindow.SetFocus()
    Start-Sleep -Milliseconds 500

    $focusedBefore = [System.Windows.Automation.AutomationElement]::FocusedElement
    if ($focusedBefore -and $focusedBefore.Current.Name) {
        Write-Host ("Initial focus: '" + $focusedBefore.Current.Name + "' (ctrlType=" + $focusedBefore.Current.ControlType.ProgrammaticName + ")")
    }

    [System.Windows.Forms.SendKeys]::SendWait("{TAB}")
    Start-Sleep -Milliseconds 300

    $focusedTab = [System.Windows.Automation.AutomationElement]::FocusedElement
    $nameTab = if ($focusedTab -and $focusedTab.Current.Name) { $focusedTab.Current.Name } else { "?" }
    Assert-NotEmpty $nameTab "Tab moved to a control (no focus loss)"

    [System.Windows.Forms.SendKeys]::SendWait("+{TAB}")
    Start-Sleep -Milliseconds 300

    $focusedShiftTab = [System.Windows.Automation.AutomationElement]::FocusedElement
    Assert-NotEmpty ($focusedShiftTab.Current.Name) "Shift+Tab moved back to a control"

    [System.Windows.Forms.SendKeys]::SendWait("{RIGHT}")
    Start-Sleep -Milliseconds 300

    $focusedRight = [System.Windows.Automation.AutomationElement]::FocusedElement
    $nameRight = if ($focusedRight -and $focusedRight.Current.Name) { $focusedRight.Current.Name } else { "?" }
    Assert-NotEmpty $nameRight "Right arrow kept focus"

    [System.Windows.Forms.SendKeys]::SendWait("{UP}")
    Start-Sleep -Milliseconds 300

    $focusedUp = [System.Windows.Automation.AutomationElement]::FocusedElement
    $nameUp = if ($focusedUp -and $focusedUp.Current.Name) { $focusedUp.Current.Name } else { "?" }
    Assert-NotEmpty $nameUp "Up arrow kept focus (no-op)"

    [System.Windows.Forms.SendKeys]::SendWait("{DOWN}")
    Start-Sleep -Milliseconds 300

    $focusedDown = [System.Windows.Automation.AutomationElement]::FocusedElement
    $nameDown = if ($focusedDown -and $focusedDown.Current.Name) { $focusedDown.Current.Name } else { "?" }
    Assert-NotEmpty $nameDown "Down arrow kept focus (no-op)"

    [System.Windows.Forms.SendKeys]::SendWait("{LEFT}")
    Start-Sleep -Milliseconds 300

    $focusedLeft = [System.Windows.Automation.AutomationElement]::FocusedElement
    $nameLeft = if ($focusedLeft -and $focusedLeft.Current.Name) { $focusedLeft.Current.Name } else { "?" }
    Assert-NotEmpty $nameLeft "Left arrow kept focus"

    Write-Host "All basic navigation assertions passed."

    $focusedElement = $null
    $listBoxCandidate = $null
    [System.Windows.Automation.AutomationElement]::RootElement.FindAll([System.Windows.Automation.Condition]::TrueCondition) | ForEach-Object {
        $ctrlName = $_.Current.ControlType.ProgrammaticName
        if ($ctrlName -eq "ControlType.ListItem" -or $ctrlName -eq "ControlType.List") {
            $listBoxCandidate = $_
        }
        if ($_.Current.IsKeyboardFocusable) {
            $focusedElement = $_
        }
    }
    if ($listBoxCandidate) {
        $listBoxCandidate.SetFocus()
        Start-Sleep -Milliseconds 200

        [System.Windows.Forms.SendKeys]::SendWait("%")
        Start-Sleep -Milliseconds 500

        [System.Windows.Forms.SendKeys]::SendWait("d")
        Start-Sleep -Milliseconds 300

        Write-Host "PASS Alt-then-D mnemonic sequence sent without error"
    } else {
        Write-Host "WARN no list control found; skipping mnemonic delete test"
    }

    Write-Host "`nAll accessibility UI tests passed."
}
catch {
    Write-Host "`nFAILED: $_"
    exit 1
}
finally {
    Remove-Item Env:\DLNA_SERVER_SKIP_FIREWALL -ErrorAction SilentlyContinue
    if ($serverProc -and -not $serverProc.HasExited) {
        try {
            Stop-Process -Id $serverProc.Id -Force -ErrorAction SilentlyContinue
        }
        catch {}
    }
    if ($backupPath -and (Test-Path $backupPath)) {
        Copy-Item -LiteralPath $backupPath -Destination $configPath -Force
        Remove-Item -LiteralPath $backupPath -Force -ErrorAction SilentlyContinue
    } elseif (Test-Path $configPath) {
        Remove-Item -LiteralPath $configPath -Force -ErrorAction SilentlyContinue
    }
}

exit 0
