# Shared by Tools\build_editor.ps1 and Tools\editor_setup.ps1 (dot-sourced): engine location, the
# CrossguardEditor build with compact error output, the .uproject plugin check.
# The engine lookup is the same as Tools\run_editor_tests.ps1's (kept separate there so that script
# stays self-contained).

$script:CgRepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$script:CgProjectFile = Join-Path $script:CgRepoRoot "Crossguard.uproject"

function Get-CgEngineAssociation {
    try {
        $project = [System.IO.File]::ReadAllText($script:CgProjectFile) | ConvertFrom-Json
        return [string]$project.EngineAssociation
    } catch {
        return ""
    }
}

# Normalizes a folder to the engine root (the folder that contains Engine\). "" if it is not one.
function Resolve-CgEngineRoot([string]$Dir) {
    if (-not $Dir) { return "" }
    $Dir = $Dir.Trim().Trim('"').TrimEnd('\', '/')
    if (Test-Path (Join-Path $Dir "Engine\Build\BatchFiles\Build.bat")) { return $Dir }
    if ((Split-Path $Dir -Leaf) -eq "Engine" -and (Test-Path (Join-Path $Dir "Build\BatchFiles\Build.bat"))) {
        return (Split-Path $Dir -Parent)
    }
    return ""
}

# -EngineDir, else $env:UE_ENGINE_DIR, else the .uproject's EngineAssociation (registry, launcher
# list), else C:\Program Files\Epic Games\UE_<association>. Throws with what was tried.
function Find-CgEngineDir([string]$EngineDir) {
    $tried = New-Object System.Collections.Generic.List[string]

    if ($EngineDir) {
        $root = Resolve-CgEngineRoot $EngineDir
        if ($root) { return $root }
        throw "-EngineDir '$EngineDir' is not an engine folder (no Engine\Build\BatchFiles\Build.bat)."
    }
    if ($env:UE_ENGINE_DIR) {
        $root = Resolve-CgEngineRoot $env:UE_ENGINE_DIR
        if ($root) { return $root }
        $tried.Add("UE_ENGINE_DIR=$($env:UE_ENGINE_DIR)")
    }

    $association = Get-CgEngineAssociation
    if (-not $association) { $association = "5.8" }

    try {
        $builds = Get-ItemProperty -Path "HKCU:\SOFTWARE\Epic Games\Unreal Engine\Builds" -ErrorAction Stop
        $value = $builds.PSObject.Properties | Where-Object { $_.Name -eq $association } | Select-Object -First 1
        if ($value) {
            $root = Resolve-CgEngineRoot ([string]$value.Value)
            if ($root) { return $root }
            $tried.Add("HKCU Builds\$association=$($value.Value)")
        }
    } catch { }

    foreach ($key in @("HKLM:\SOFTWARE\EpicGames\Unreal Engine\$association", "HKLM:\SOFTWARE\WOW6432Node\EpicGames\Unreal Engine\$association")) {
        try {
            $installed = (Get-ItemProperty -Path $key -ErrorAction Stop).InstalledDirectory
            $root = Resolve-CgEngineRoot $installed
            if ($root) { return $root }
            $tried.Add("$key=$installed")
        } catch { }
    }

    $launcherDat = Join-Path $env:ProgramData "Epic\UnrealEngineLauncher\LauncherInstalled.dat"
    if (Test-Path $launcherDat) {
        try {
            $list = [System.IO.File]::ReadAllText($launcherDat) | ConvertFrom-Json
            foreach ($entry in $list.InstallationList) {
                if ($entry.AppName -eq "UE_$association") {
                    $root = Resolve-CgEngineRoot ([string]$entry.InstallLocation)
                    if ($root) { return $root }
                    $tried.Add("LauncherInstalled.dat UE_$association=$($entry.InstallLocation)")
                }
            }
        } catch { }
    }

    $default = "C:\Program Files\Epic Games\UE_$association"
    $root = Resolve-CgEngineRoot $default
    if ($root) { return $root }
    $tried.Add($default)

    throw ("Unreal Engine $association not found. Tried: " + ($tried -join "; ") +
        ". Pass -EngineDir 'C:\path\to\UE_5.8' or set the UE_ENGINE_DIR environment variable.")
}

# Compiler / linker / UBT error lines from a build log, compact: "Source/X.cpp:12: error C2065: ...".
function Get-CgBuildErrors([string[]]$Lines, [switch]$Warnings) {
    $root = $script:CgRepoRoot.TrimEnd('\') + '\'
    $pattern = if ($Warnings) { ': warning [A-Z]+\d+' } else { ': (fatal )?error |error C\d{4}|error LNK\d+|^\s*ERROR:|^Error:|Unable to build' }
    $result = New-Object System.Collections.Generic.List[string]
    $seen = @{}
    foreach ($line in $Lines) {
        if ($line -notmatch $pattern) { continue }
        $text = $line.Trim()
        # MSVC "C:\path\File.cpp(12): error C1234: msg" / "(12,5)" -> "Source/File.cpp:12: error C1234: msg"
        $m = [regex]::Match($text, '^(?<file>[A-Za-z]:\\[^(]+|[^(:]+)\((?<line>\d+)(,\d+)?\)\s*:\s*(?<rest>.*)$')
        if ($m.Success) {
            $file = $m.Groups['file'].Value
            if ($file.StartsWith($root, [System.StringComparison]::OrdinalIgnoreCase)) { $file = $file.Substring($root.Length) }
            $text = ($file -replace '\\', '/') + ":" + $m.Groups['line'].Value + ": " + $m.Groups['rest'].Value
        } elseif ($text.StartsWith($root, [System.StringComparison]::OrdinalIgnoreCase)) {
            $text = $text.Substring($root.Length)
        }
        if (-not $seen.ContainsKey($text)) { $seen[$text] = $true; $result.Add($text) }
    }
    return ,$result
}

# Builds CrossguardEditor Win64 Development with the engine's Build.bat. Returns a hashtable:
# ExitCode, Seconds, Errors (compact list), Warnings (count), LiveCoding (bool).
function Invoke-CgEditorBuild([string]$EngineRoot, [string]$LogFile) {
    $buildBat = Join-Path $EngineRoot "Engine\Build\BatchFiles\Build.bat"
    $errLog = "$LogFile.stderr"
    New-Item -ItemType Directory -Force -Path (Split-Path $LogFile -Parent) | Out-Null
    Remove-Item -Path $LogFile, $errLog -Force -ErrorAction SilentlyContinue
    $start = Get-Date
    # cmd /c ""bat" args": cmd strips the outer quotes, so paths with spaces stay quoted.
    $buildArgs = "/c `"`"$buildBat`" CrossguardEditor Win64 Development `"-Project=$($script:CgProjectFile)`" -WaitMutex -NoHotReloadFromIDE`""
    $proc = Start-Process -FilePath "cmd.exe" -ArgumentList $buildArgs -NoNewWindow -PassThru `
        -RedirectStandardOutput $LogFile -RedirectStandardError $errLog
    $null = $proc.Handle   # keeps ExitCode readable after exit (Windows PowerShell quirk)
    $proc.WaitForExit()
    if ((Test-Path $errLog) -and (Get-Item $errLog).Length -gt 0) {
        Add-Content -Path $LogFile -Value (Get-Content -Path $errLog)
    }
    Remove-Item -Path $errLog -Force -ErrorAction SilentlyContinue
    $text = @(Get-Content -Path $LogFile -ErrorAction SilentlyContinue)
    return @{
        ExitCode   = $proc.ExitCode
        Seconds    = [int]((Get-Date) - $start).TotalSeconds
        Errors     = (Get-CgBuildErrors $text)
        Warnings   = (Get-CgBuildErrors $text -Warnings).Count
        LiveCoding = [bool]($text -match "Live Coding")
    }
}

# True if an UnrealEditor process has this project open (builds then fail or hot-reload).
function Test-CgEditorRunning {
    $procs = @(Get-Process -Name "UnrealEditor", "UnrealEditor-Cmd" -ErrorAction SilentlyContinue)
    return $procs.Count -gt 0
}

# Makes sure Crossguard.uproject enables the given plugins. Inserts missing entries as text after
# '"Plugins": [' so the file's tab formatting is kept. Returns the names it added.
function Enable-CgProjectPlugins([string[]]$Names) {
    $text = [System.IO.File]::ReadAllText($script:CgProjectFile)
    $project = $text | ConvertFrom-Json
    $present = @{}
    if ($project.PSObject.Properties.Name -contains "Plugins") {
        foreach ($p in $project.Plugins) { $present[[string]$p.Name] = [bool]$p.Enabled }
    }
    $added = New-Object System.Collections.Generic.List[string]
    foreach ($name in $Names) {
        if ($present.ContainsKey($name) -and $present[$name]) { continue }
        if ($present.ContainsKey($name)) {
            # Listed but disabled: flip it.
            $pattern = '("Name"\s*:\s*"' + [regex]::Escape($name) + '"\s*,\s*"Enabled"\s*:\s*)false'
            $text = [regex]::Replace($text, $pattern, '${1}true')
        } else {
            $entry = "`n`t`t{`n`t`t`t`"Name`": `"$name`",`n`t`t`t`"Enabled`": true`n`t`t},"
            if ($text -match '"Plugins"\s*:\s*\[') {
                $text = [regex]::Replace($text, '("Plugins"\s*:\s*\[)', '${1}' + $entry.Replace('$', '$$'), 1)
                # A list that was empty ends up with a trailing comma before ']'.
                $text = [regex]::Replace($text, ',(\s*\])', '${1}')
            } else {
                throw "Crossguard.uproject has no Plugins list; add $name by hand."
            }
        }
        $added.Add($name)
    }
    if ($added.Count -gt 0) {
        $null = $text | ConvertFrom-Json   # still valid JSON, or throw before writing
        [System.IO.File]::WriteAllText($script:CgProjectFile, $text, (New-Object System.Text.UTF8Encoding($false)))
    }
    return ,$added
}
