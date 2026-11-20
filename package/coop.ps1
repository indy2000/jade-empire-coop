# Jade Empire Co-op 0.9.0 - instalador e atalhos.
# Uso: chamado pelos arquivos .bat desta pasta (instalar, hospedar, entrar, mostrar-codigo, desinstalar).
# Nunca altera a instalacao original do jogo: trabalha numa copia em %USERPROFILE%\JadeEmpireCoop\jogo.
param([Parameter(Mandatory = $true)][ValidateSet('install', 'host', 'join', 'code', 'uninstall')][string]$Mode)

$ErrorActionPreference = 'Stop'
$Here = Split-Path -Parent $MyInvocation.MyCommand.Path
$Root = Join-Path $env:USERPROFILE 'JadeEmpireCoop'
$Dest = Join-Path $Root 'jogo'
$ExeHash = '0D25F18C028A7DFFFF9DD110B7BE677F7E9B63E1CAD86C1EB8E4E8E189C54FE5'   # JadeEmpire.exe v1.00 build 1510 (Steam)
$Pins = [ordered]@{
    'xinput9_1_0.dll' = '0B703DAF4DD206682E094640E3FFDDFE256C622ABAB4019450CED9264CA47454'   # jebridge 0.9.0
    'steam_api.dll'   = 'FDAFB7C65EC7D4182B42BF92CC8671C68E4DC547CC6E613899D2618A47EB65DA'   # Steamworks.NET Standalone 2025.164.1, x86
}
$Template = 'j00h_abbot0'

function Say($t) { Write-Host $t }
function Fail($t) { Write-Host ''; Write-Host "ERRO: $t" -ForegroundColor Red; exit 1 }
function Sha($p) { (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToUpper() }

function Get-SteamPath {
    $k = Get-ItemProperty -Path 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue
    if ($k -and $k.SteamPath) { return ($k.SteamPath -replace '/', '\') }
    return $null
}

function Find-Game($steam) {
    $libs = New-Object System.Collections.Generic.List[string]
    if ($steam) {
        $libs.Add($steam)
        $vdf = Join-Path $steam 'steamapps\libraryfolders.vdf'
        if (Test-Path -LiteralPath $vdf) {
            foreach ($m in [regex]::Matches((Get-Content -LiteralPath $vdf -Raw), '"path"\s+"([^"]+)"')) {
                $libs.Add(($m.Groups[1].Value -replace '\\\\', '\'))
            }
        }
    }
    foreach ($l in ($libs | Select-Object -Unique)) {
        $g = Join-Path $l 'steamapps\common\Jade Empire'
        if (Test-Path -LiteralPath $g) { return $g }
    }
    return $null
}

# The game exe is the one with the known hash (some guides rename JadeEmpire.exe to JadeEmpireLauncher.exe).
function Find-GameExe($dir) {
    foreach ($f in (Get-ChildItem -LiteralPath $dir -Filter '*.exe' -File)) {
        if ($f.Length -gt 1MB -and (Sha $f.FullName) -eq $ExeHash) { return $f }
    }
    return $null
}

function Write-Ascii($path, $text) { [System.IO.File]::WriteAllText($path, $text, [System.Text.Encoding]::ASCII) }

function Assert-Installed {
    if (-not (Test-Path -LiteralPath (Join-Path $Dest 'xinput_orig.dll'))) { Fail 'O co-op ainda nao foi instalado. Rode instalar.bat primeiro.' }
    foreach ($n in $Pins.Keys) {
        $p = Join-Path $Dest $n
        if (-not (Test-Path -LiteralPath $p) -or (Sha $p) -ne $Pins[$n]) { Fail "Arquivo $n da copia nao confere. Rode instalar.bat de novo." }
    }
}

function Start-Game {
    if (-not (Get-Process -Name 'steam' -ErrorAction SilentlyContinue)) { Fail 'O Steam nao esta aberto. Abra o Steam, entre na sua conta e tente de novo.' }
    $exe = Find-GameExe $Dest
    if (-not $exe) { Fail 'Nao achei o executavel do jogo na copia.' }
    Start-Process -FilePath $exe.FullName -WorkingDirectory $Dest
    Say "Jogo iniciado: $($exe.Name)"
}

switch ($Mode) {
    'install' {
        Say '== Jade Empire Co-op 0.9.0: instalacao =='
        foreach ($n in $Pins.Keys) {
            $p = Join-Path $Here "arquivos\$n"
            if (-not (Test-Path -LiteralPath $p)) { Fail "Falta arquivos\$n no pacote." }
            if ((Sha $p) -ne $Pins[$n]) { Fail "arquivos\$n esta diferente do esperado (pacote corrompido ou alterado)." }
        }
        Say 'Arquivos do pacote conferidos.'

        $steam = Get-SteamPath
        if (-not $steam) { Fail 'Steam nao encontrado neste PC.' }
        $game = Find-Game $steam
        if (-not $game) {
            $game = Read-Host 'Nao achei o Jade Empire. Cole o caminho da pasta do jogo'
            $game = $game.Trim('"', ' ')
            if (-not (Test-Path -LiteralPath $game)) { Fail 'Pasta nao encontrada.' }
        }
        Say "Jogo encontrado em: $game"
        $exe = Find-GameExe $game
        if (-not $exe) { Fail 'Esta nao e a versao suportada (Jade Empire Special Edition da Steam, build 1510). Verifique os arquivos do jogo na Steam.' }
        Say "Versao confere ($($exe.Name))."

        $steamDll = Join-Path $steam 'Steam.dll'
        if (-not (Test-Path -LiteralPath $steamDll)) { Fail "Nao achei Steam.dll em $steam." }

        if (Test-Path -LiteralPath (Join-Path $Dest 'xinput_orig.dll')) {
            Say "Copia ja existe em $Dest; atualizando so os arquivos do co-op."
        } else {
            $size = (Get-ChildItem -LiteralPath $game -Recurse -File | Measure-Object -Property Length -Sum).Sum
            $drive = Get-PSDrive -Name ($env:USERPROFILE.Substring(0, 1))
            if ($drive.Free -lt ($size + 1GB)) { Fail ("Espaco insuficiente: a copia precisa de {0:N1} GB." -f (($size + 1GB) / 1GB)) }
            Say ("Copiando o jogo ({0:N1} GB) para {1}. Pode levar alguns minutos..." -f ($size / 1GB), $Dest)
            New-Item -ItemType Directory -Force -Path $Dest | Out-Null
            & robocopy.exe $game $Dest /E /R:1 /W:1 /NFL /NDL /NJH /NJS /NP | Out-Null
            if ($LASTEXITCODE -ge 8) { Fail "A copia falhou (robocopy codigo $LASTEXITCODE)." }
            $orig = Join-Path $Dest 'xinput9_1_0.dll'
            if (-not (Test-Path -LiteralPath $orig)) { Fail 'A copia nao tem xinput9_1_0.dll; instalacao do jogo incompleta?' }
            Move-Item -LiteralPath $orig -Destination (Join-Path $Dest 'xinput_orig.dll')
        }
        Copy-Item -LiteralPath $steamDll -Destination (Join-Path $Dest 'Steam.dll') -Force
        foreach ($n in $Pins.Keys) { Copy-Item -LiteralPath (Join-Path $Here "arquivos\$n") -Destination (Join-Path $Dest $n) -Force }
        Write-Ascii (Join-Path $Dest 'steam_appid.txt') '480'
        New-Item -ItemType Directory -Force -Path (Join-Path $Dest 'bridge') | Out-Null
        Assert-Installed
        Say ''
        Write-Host "Pronto! Co-op instalado em $Dest" -ForegroundColor Green
        Say 'Seu jogo original nao foi alterado. Seus saves foram copiados junto.'
    }
    'host' {
        Assert-Installed
        Write-Ascii (Join-Path $Dest 'bridge\net.txt') "transport steam`r`nrole host`r`ntemplate $Template`r`n"
        Remove-Item -LiteralPath (Join-Path $Dest 'bridge\lobby.txt') -ErrorAction SilentlyContinue
        Start-Game
        Say ''
        Say 'Carregue o seu save, e com o personagem na tela aperte Ctrl+Shift+N (vai tocar um bipe).'
        Say 'Depois rode mostrar-codigo.bat e mande o codigo para o seu amigo.'
        Say 'Quando ele entrar, o jogo faz um salvamento automatico e envia para ele (fique no jogo).'
    }
    'join' {
        Assert-Installed
        $code = (Read-Host 'Cole o codigo do lobby que o host te mandou').Trim()
        if ($code -notmatch '^\d{15,20}$') { Fail 'Codigo invalido: deve ter so numeros (uns 18 digitos).' }
        Write-Ascii (Join-Path $Dest 'bridge\net.txt') "transport steam`r`nrole join`r`nlobby $code`r`ntemplate $Template`r`ngetsave 1`r`n"
        Remove-Item -LiteralPath (Join-Path $Dest 'bridge\save_received.txt') -ErrorAction SilentlyContinue
        Start-Game
        Say ''
        Say 'No MENU PRINCIPAL do jogo (sem carregar nada) aperte Ctrl+Shift+N: toca um bipe.'
        Say 'Espere dois bipes seguidos (o save do host chegou) e carregue o save "Co-op - host".'
    }
    'code' {
        $f = Join-Path $Dest 'bridge\lobby.txt'
        if (-not (Test-Path -LiteralPath $f)) { Fail 'Ainda nao ha lobby. No jogo, com o personagem na tela, aperte Ctrl+Shift+N e espere uns 5 segundos.' }
        $code = (Get-Content -LiteralPath $f -Raw).Trim()
        Say "Codigo do lobby: $code"
        try { Set-Clipboard -Value $code; Say '(copiado para a area de transferencia)' } catch { }
    }
    'uninstall' {
        if (-not (Test-Path -LiteralPath $Dest)) { Say 'Nada para remover.'; break }
        Say "Isto apaga a copia do jogo em $Dest, inclusive os saves feitos nela."
        Say 'O jogo original da Steam nao e afetado.'
        $a = Read-Host 'Digite APAGAR para confirmar'
        if ($a -ne 'APAGAR') { Say 'Cancelado.'; break }
        Remove-Item -LiteralPath $Dest -Recurse -Force
        Say 'Copia removida.'
    }
}
