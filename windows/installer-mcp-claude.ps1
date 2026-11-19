# Ajoute le serveur MCP de Drapeo à Claude Desktop (claude_desktop_config.json).
# drapeo-mcp.exe doit être dans le même dossier que ce script.
$ErrorActionPreference = 'Stop'
$exe = Join-Path $PSScriptRoot 'drapeo-mcp.exe'
if (-not (Test-Path $exe)) { Write-Host "drapeo-mcp.exe introuvable dans $PSScriptRoot" -ForegroundColor Red; exit 1 }
$projDir = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'Drapeo'
New-Item -ItemType Directory -Force -Path $projDir | Out-Null
$projet = Join-Path $projDir 'mcp.drapeo'

# Configuration classique, et celle de la version Microsoft Store si elle existe.
$dirs = @(Join-Path $env:APPDATA 'Claude')
Get-ChildItem (Join-Path $env:LOCALAPPDATA 'Packages') -Directory -Filter 'Claude_*' -ErrorAction SilentlyContinue | ForEach-Object {
    $d = Join-Path $_.FullName 'LocalCache\Roaming\Claude'
    if (Test-Path $d) { $dirs += $d }
}
foreach ($dir in $dirs) {
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    $file = Join-Path $dir 'claude_desktop_config.json'
    $cfg = $null
    if (Test-Path $file) {
        $text = [IO.File]::ReadAllText($file)
        Copy-Item $file "$file.avant-drapeo" -Force
        if ($text.Trim()) { $cfg = $text | ConvertFrom-Json }
    }
    if (-not $cfg) { $cfg = [pscustomobject]@{} }
    if (-not $cfg.PSObject.Properties['mcpServers']) { $cfg | Add-Member -NotePropertyName mcpServers -NotePropertyValue ([pscustomobject]@{}) }
    $server = [pscustomobject]@{ command = $exe; args = @('--projet', $projet) }
    if ($cfg.mcpServers.PSObject.Properties['drapeo']) { $cfg.mcpServers.drapeo = $server }
    else { $cfg.mcpServers | Add-Member -NotePropertyName drapeo -NotePropertyValue $server }
    [IO.File]::WriteAllText($file, ($cfg | ConvertTo-Json -Depth 50), (New-Object Text.UTF8Encoding($false)))
    Write-Host "Drapeo ajouté à $file" -ForegroundColor Green
}
Write-Host "Projet de l'assistant : $projet"
Write-Host "Quittez complètement Claude Desktop (icône près de l'horloge > Quitter), puis relancez-le."
