<#
.SYNOPSIS
    Starts a local "cluster" on one PC: a dedicated hub server, a dedicated match (raid) server and N
    game clients that connect to them. -Stop closes everything it started.

.DESCRIPTION
    Two modes:
      -Mode Editor (default): every process is the editor binary, uncooked (UnrealEditor.exe
        Crossguard.uproject <map> -server / <address> -game). Works with the launcher engine too: a
        dedicated server run from the editor binary needs no Server target. Build CrossguardEditor
        first (Tools\build_editor.bat).
      -Mode Packaged: the servers are the packaged CrossguardServer.exe (Tools\build_server.bat
        -Package, source engine only); clients are the packaged client (-ClientBuildDir, from
        build_server -Package -WithClient) or, without it, the editor binary of -EngineDir, which must
        then be the same source engine (a client from another engine build is refused as an
        incompatible version).

    Hub server:   <HubMap>?Hub -server -port=<HubPort> -AllowDevPlayerIds -RaidServer=127.0.0.1:<RaidPort>
    Match server: <RaidMap> -server -port=<RaidPort> -AllowDevPlayerIds -HubServer=127.0.0.1:<HubPort> -KeepMatchServer
    Client i:     127.0.0.1:<port>?DevPlayerId=<name i> -game -windowed (tiled) -CrossguardPlayerId=<name i>

    With -Backend (the local Nakama from Tools\run_local_backend.bat must be running): both servers get
    -SaveBackend=Nakama -NakamaUrl=<url> -ServerId=local-hub / local-match -PublicAddress=127.0.0.1:<port>
    and the HTTP key through the CROSSGUARD_NAKAMA_HTTP_KEY environment variable (read from Backend\.env;
    not on the command line), no -RaidServer= (Nakama's matchmaking) and no -AllowDevPlayerIds (players
    must log in; match servers take only match tickets). The servers register with the backend; the
    clients open on the main menu: Log in, then Play (the backend names the hub). docs/backend-setup.md.

    Each server is started first and waited for ("listening on port" in its log) before the clients.
    Logs: Saved\LocalCluster\<name>.log; process ids: Saved\LocalCluster\pids.json (for -Stop).
    The development player ids (dev-<name>) need a development build and -AllowDevPlayerIds on the
    servers (docs/persistence.md, "Player id"); hub and match server share the save folder only when
    they run from the same project folder / build (Editor mode: the project's Saved\Crossguard).

    Exit codes: 0 started (or stopped); 1 a server did not start listening in time; 4 bad set-up.

.PARAMETER Mode
    Editor (default) or Packaged.

.PARAMETER EngineDir
    Engine root (the folder holding Engine\). Default: $env:UE_ENGINE_DIR, else the .uproject's
    EngineAssociation (launcher list / registry). Packaged mode with -ClientBuildDir does not need it.

.PARAMETER ServerBuildDir
    Packaged mode: the archive folder of build_server -Package (holds WindowsServer\). Default
    Saved\Packages\Server.

.PARAMETER ClientBuildDir
    Packaged mode: the archive folder holding the packaged client (Windows\Crossguard.exe). Optional.

.PARAMETER Clients
    Number of game clients. Default 2. 0 starts only the servers.

.PARAMETER ClientsTo
    Hub (default) or Raid: which server the clients connect to.

.PARAMETER NoHub
    Do not start the hub server (clients must use -ClientsTo Raid).

.PARAMETER NoRaid
    Do not start the match server.

.PARAMETER Names
    Development player names for the clients, comma-separated. Default alice,bob,carol,dave,...

.PARAMETER ServerWaitSeconds
    How long to wait for each server to listen. Default 180 (the editor binary loads slowly the
    first time).

.PARAMETER ExtraServerArgs
    Appended to both servers' command lines (e.g. "-RaidMap=/Game/Maps/L_Raid").

.PARAMETER Backend
    Run against the local Nakama backend (see above). Start it first: Tools\run_local_backend.bat.

.PARAMETER NakamaUrl
    With -Backend: the Nakama API. Default http://127.0.0.1:7350.

.PARAMETER Stop
    Close every process the last run started (from pids.json) and exit.

.EXAMPLE
    Tools\run_local_cluster.bat
.EXAMPLE
    Tools\run_local_cluster.bat -Clients 3 -ClientsTo Raid -NoHub
.EXAMPLE
    Tools\run_local_cluster.bat -Mode Packaged -ClientBuildDir Saved\Packages\Server
.EXAMPLE
    Tools\run_local_cluster.bat -Backend
.EXAMPLE
    Tools\run_local_cluster.bat -Stop
#>
[CmdletBinding()]
param(
    [ValidateSet("Editor", "Packaged")]
    [string]$Mode = "Editor",
    [string]$EngineDir = "",
    [string]$ServerBuildDir = "",
    [string]$ClientBuildDir = "",
    [int]$Clients = 2,
    [ValidateSet("Hub", "Raid")]
    [string]$ClientsTo = "Hub",
    [switch]$NoHub,
    [switch]$NoRaid,
    [string]$Names = "alice,bob,carol,dave,erin,frank,grace,heidi",
    [int]$HubPort = 7777,
    [int]$RaidPort = 7778,
    [string]$HubMap = "/Game/Maps/L_Hub",
    [string]$RaidMap = "/Game/Maps/Dev/L_DevSandbox",
    [int]$ResX = 960,
    [int]$ResY = 540,
    [int]$ServerWaitSeconds = 180,
    [string]$ExtraServerArgs = "",
    [switch]$Backend,
    [string]$NakamaUrl = "http://127.0.0.1:7350",
    [switch]$Stop
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "ue_common.ps1")

$OutDir = Join-Path $CgRepoRoot "Saved\LocalCluster"
$PidFile = Join-Path $OutDir "pids.json"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

function Write-SetupError([string]$Message) {
    Write-Host "SETUP ERROR: $Message" -ForegroundColor Red
    exit 4
}

function Stop-CgCluster {
    if (-not (Test-Path $PidFile)) {
        Write-Host "Nothing to stop ($PidFile not found)."
        return
    }
    # Not @(... | ConvertFrom-Json): Windows PowerShell 5.1 would nest the array.
    $entries = [System.IO.File]::ReadAllText($PidFile) | ConvertFrom-Json
    foreach ($entry in $entries) {
        $proc = Get-Process -Id ([int]$entry.Pid) -ErrorAction SilentlyContinue
        # Only a process with the recorded start time: a reused pid is someone else's.
        if ($proc -ne $null -and $proc.StartTime.ToString("o") -eq [string]$entry.Started) {
            Write-Host ("Stopping {0} (pid {1})" -f $entry.Name, $entry.Pid)
            Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
        }
    }
    Remove-Item -Path $PidFile -Force -ErrorAction SilentlyContinue
}

if ($Stop) {
    Stop-CgCluster
    exit 0
}

if ($NoHub -and $ClientsTo -eq "Hub" -and $Clients -gt 0 -and -not $Backend) { Write-SetupError "-NoHub with clients needs -ClientsTo Raid." }

# --- Backend (-Backend) --------------------------------------------------------------------------------

$BackendArgs = ""
if ($Backend) {
    $envFile = Join-Path $CgRepoRoot "Backend\.env"
    if (-not (Test-Path $envFile)) { Write-SetupError "Backend\.env not found: start the backend first (Tools\run_local_backend.bat)." }
    $httpKey = ""
    foreach ($line in [System.IO.File]::ReadAllLines($envFile)) {
        if ($line -match '^\s*NAKAMA_HTTP_KEY\s*=\s*(.+)$') { $httpKey = $Matches[1].Trim() }
    }
    if (-not $httpKey) { Write-SetupError "NAKAMA_HTTP_KEY missing in Backend\.env." }
    try {
        $null = Invoke-WebRequest -Uri "$NakamaUrl/healthcheck" -UseBasicParsing -TimeoutSec 5
    } catch {
        Write-SetupError "Nakama does not answer at $NakamaUrl. Start it first: Tools\run_local_backend.bat"
    }
    # Inherited by the server processes; not on their command lines (other processes can read those).
    $env:CROSSGUARD_NAKAMA_HTTP_KEY = $httpKey
    $BackendArgs = "-SaveBackend=Nakama -NakamaUrl=$NakamaUrl"
    Write-Host "Backend: Nakama at $NakamaUrl (saves, leases, parties, matchmaking, tickets)." -ForegroundColor Cyan
}
if ($NoRaid -and $ClientsTo -eq "Raid" -and $Clients -gt 0) { Write-SetupError "-NoRaid with clients needs -ClientsTo Hub." }
if (Test-Path $PidFile) {
    Write-Host "A previous cluster is recorded; stopping it first." -ForegroundColor Yellow
    Stop-CgCluster
}

# --- Executables -------------------------------------------------------------------------------------

$EngineRoot = ""
$needEngine = ($Mode -eq "Editor") -or ($Clients -gt 0 -and -not $ClientBuildDir)
if ($needEngine) {
    try {
        $EngineRoot = Find-CgEngineDir $EngineDir
    } catch {
        Write-SetupError $_.Exception.Message
    }
    Write-Host ("Engine: {0} ({1}{2})" -f $EngineRoot, (Get-CgEngineVersion $EngineRoot),
        $(if (Test-CgInstalledEngine $EngineRoot) { ", launcher" } else { ", source" }))
}
$EditorExe = if ($EngineRoot) { Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor.exe" } else { "" }
if ($EditorExe -and -not (Test-Path $EditorExe)) { Write-SetupError "$EditorExe not found." }
if (-not (Test-Path $CgProjectFile)) { Write-SetupError "$CgProjectFile not found." }

if ($Mode -eq "Packaged") {
    if (-not $ServerBuildDir) { $ServerBuildDir = Join-Path $CgRepoRoot "Saved\Packages\Server" }
    $ServerExe = Join-Path $ServerBuildDir "WindowsServer\CrossguardServer.exe"
    if (-not (Test-Path $ServerExe)) {
        Write-SetupError "$ServerExe not found. Package it first: Tools\build_server.bat -Package (source engine)."
    }
    if ($EngineRoot -and $Clients -gt 0 -and (Test-CgInstalledEngine $EngineRoot)) {
        Write-Host ("WARNING: clients from the launcher editor will likely be refused by a server packaged with the " +
            "source engine (version mismatch). Pass -ClientBuildDir (build_server -Package -WithClient).") -ForegroundColor Yellow
    }
}
$ClientExe = ""
if ($ClientBuildDir) {
    $ClientExe = Join-Path $ClientBuildDir "Windows\Crossguard.exe"
    if (-not (Test-Path $ClientExe)) { Write-SetupError "$ClientExe not found." }
}

$started = New-Object System.Collections.Generic.List[object]

function Start-CgProcess([string]$Name, [string]$Exe, [string]$Arguments) {
    $log = Join-Path $OutDir "$Name.log"
    Remove-Item -Path $log -Force -ErrorAction SilentlyContinue
    $fullArgs = "$Arguments `"-abslog=$log`""
    Write-Host ("Starting {0}: {1} {2}" -f $Name, $Exe, $fullArgs)
    $proc = Start-Process -FilePath $Exe -ArgumentList $fullArgs -PassThru
    $script:started.Add([pscustomobject]@{ Name = $Name; Pid = $proc.Id; Started = $proc.StartTime.ToString("o"); Log = $log })
    # Written after every start: a failed wait still leaves -Stop something to close.
    [System.IO.File]::WriteAllText($PidFile, (ConvertTo-Json @($script:started.ToArray())), (New-Object System.Text.UTF8Encoding($false)))
    return $proc
}

function Wait-CgListening([string]$Name, $Proc, [int]$Port) {
    $log = Join-Path $OutDir "$Name.log"
    $deadline = (Get-Date).AddSeconds($ServerWaitSeconds)
    while ((Get-Date) -lt $deadline) {
        if ($Proc.HasExited) {
            Write-Host "$Name exited (code $($Proc.ExitCode)) before listening. Log: $log" -ForegroundColor Red
            return $false
        }
        if ((Test-Path $log) -and (Select-String -Path $log -Pattern "listening on port $Port" -SimpleMatch -Quiet)) {
            Write-Host "$Name is listening on port $Port." -ForegroundColor Green
            return $true
        }
        Start-Sleep -Seconds 2
    }
    Write-Host "$Name did not listen on port $Port within $ServerWaitSeconds s. Log: $log" -ForegroundColor Red
    return $false
}

function Get-CgServerCommand([string]$MapUrl, [int]$Port, [string]$Extra) {
    # With the backend players log in (no development ids) and match servers take only tickets.
    $devIds = if ($Backend) { "" } else { "-AllowDevPlayerIds" }
    $common = "-server -log -unattended -port=$Port $devIds $Extra $ExtraServerArgs".Trim() -replace "\s+", " "
    if ($Mode -eq "Packaged") {
        return @($ServerExe, "$MapUrl $common")
    }
    return @($EditorExe, "`"$CgProjectFile`" $MapUrl $common")
}

# --- Servers -----------------------------------------------------------------------------------------

$ok = $true
if (-not $NoHub) {
    # Assumed (placeholder): the hub sends parties to the one local match server (-RaidServer=), the
    # development stand-in for matchmaking.
    $raidArg = if ($NoRaid -or $Backend) { "" } else { "-RaidServer=127.0.0.1:$RaidPort" }
    if ($Backend) { $raidArg = "$BackendArgs -ServerId=local-hub -PublicAddress=127.0.0.1:$HubPort" }
    $cmd = Get-CgServerCommand "${HubMap}?Hub" $HubPort $raidArg
    $hub = Start-CgProcess "hub" $cmd[0] $cmd[1]
    $ok = $ok -and (Wait-CgListening "hub" $hub $HubPort)
}
if ($ok -and -not $NoRaid) {
    # Teams go back to the hub server (-HubServer=). Not -DevListenHub: a dedicated match server never
    # travels to the hub map as a whole (ACrossguardGameMode::GetHubDestination).
    $hubArg = if ($NoHub) { "" } else { "-HubServer=127.0.0.1:$HubPort" }
    # A dedicated match server exits when its match is over (one match per process); keep it up locally
    # so several raids can be tried in a row.
    if ($Backend) { $hubArg = "$hubArg $BackendArgs -ServerId=local-match -PublicAddress=127.0.0.1:$RaidPort" }
    $cmd = Get-CgServerCommand $RaidMap $RaidPort "$hubArg -KeepMatchServer".Trim()
    $raid = Start-CgProcess "match" $cmd[0] $cmd[1]
    $ok = $ok -and (Wait-CgListening "match" $raid $RaidPort)
}
if (-not $ok) {
    Write-Host "A server did not start; stopping what was started. Logs: $OutDir" -ForegroundColor Red
    Stop-CgCluster
    exit 1
}

# --- Clients -----------------------------------------------------------------------------------------

$nameList = @($Names.Split(",") | ForEach-Object { $_.Trim() } | Where-Object { $_ })
$port = if ($ClientsTo -eq "Raid") { $RaidPort } else { $HubPort }
$columns = 2
for ($i = 0; $i -lt $Clients; $i++) {
    $name = if ($i -lt $nameList.Count) { $nameList[$i] } else { "player$($i + 1)" }
    $x = ($i % $columns) * $ResX
    $y = [Math]::Floor($i / $columns) * ($ResY + 40)
    $window = "-windowed -ResX=$ResX -ResY=$ResY -WinX=$x -WinY=$y"
    $url = "127.0.0.1:${port}?DevPlayerId=$name"
    # With the backend: the main menu (Log in, then Play); the device id is crossguard-dev-<name>.
    if ($Backend) { $url = "" }
    $clientArgs = "$url -game -log $window -CrossguardPlayerId=$name".Trim()
    if ($ClientExe) {
        $null = Start-CgProcess "client-$name" $ClientExe $clientArgs
    } else {
        $null = Start-CgProcess "client-$name" $EditorExe "`"$CgProjectFile`" $clientArgs"
    }
}

Write-Host ""
Write-Host ("Cluster up: {0}{1}{2} client(s) to the {3}. Logs: {4}" -f
    $(if ($NoHub) { "" } else { "hub :$HubPort, " }),
    $(if ($NoRaid) { "" } else { "match :$RaidPort, " }),
    $Clients, $ClientsTo.ToLower(), $OutDir) -ForegroundColor Green
if ($Backend) {
    Write-Host "Each client: Log in, then Play (the backend picks the hub). A raid starts with 2 teams queued or after"
    Write-Host "CROSSGUARD_MATCH_MAX_WAIT_SEC (Backend\.env; 30 s by default, 5 is handy here)."
}
Write-Host "Stop everything: Tools\run_local_cluster.bat -Stop"
exit 0
