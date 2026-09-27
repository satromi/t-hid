# da.ps1 — STM32H533 のデバッグ認証 (DA) と product state の操作
#
#   鍵・証明書・OBK は .da\ に置く (tools/da/make_da_certs.py と
#   STM32TrustedPackageCreator_CLI -obk で作る。docs/guide/u2f.md を参照)。
#
#   .\tools\da\da.ps1 discovery    状態を表示する (読み取りのみ。ボードはリセットされる)
#   .\tools\da\da.ps1 provision    Provisioning にして DA の設定 (root 公開鍵のハッシュ) を書き込む
#   .\tools\da\da.ps1 test         証明書で認証できるか確かめる (データは消えない)
#   .\tools\da\da.ps1 close        TZ-Closed にする (デバッガはつながらなくなる)
#   .\tools\da\da.ps1 open-debug   証明書で認証し、HDPL3 でデバッグを開く許可を出す
#                                  (今のファームウェアは HDPL2 までしか上げないので、
#                                   TZ-Closed ではこれでもデバッガはつながらない)
#
#   実行: powershell -ExecutionPolicy Bypass -File <このファイルのパス> <コマンド>
#   discovery / test / open-debug の後は、NUCLEO の USB (CN1) を抜き挿しして電源を
#   入れ直すこと (チップが認証の待ち受け状態に残り、ファームウェアが動かないため)。
#   .\tools\da\da.ps1 regression   Open に戻す (Flash を全消去。鍵も消える)

param([Parameter(Mandatory = $true)][string]$Command)

$ErrorActionPreference = "Stop"
$P = "$env:LOCALAPPDATA\stm32cube\bundles\programmer\2.23.0\bin\STM32_Programmer_CLI.exe"
$DA = Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) ".da"
$Key = Join-Path $DA "key_3_leaf.pem"
$Cert = Join-Path $DA "cert_leaf_chain.b64"
$Obk = Join-Path $DA "DA_Config.obk"

function Run([string[]]$CliArgs) {
    Write-Host "> STM32_Programmer_CLI $($CliArgs -join ' ')"
    & $P @CliArgs
    if ($LASTEXITCODE -ne 0) { throw "STM32_Programmer_CLI が失敗しました ($LASTEXITCODE)" }
}

function Confirm([string]$Message) {
    Write-Host $Message -ForegroundColor Yellow
    if ((Read-Host "続けるなら yes と入力") -ne "yes") { throw "中止しました" }
}

function Require([string[]]$Files) {
    foreach ($f in $Files) { if (-not (Test-Path $f)) { throw "$f がありません" } }
}

switch ($Command) {
    "discovery" {
        Run @("-c", "port=SWD", "debugauth=2")
    }
    "provision" {
        Require @($Obk, $Key, $Cert)
        Confirm ("product state を Provisioning にします。以後、Open に戻すには DA (証明書) が必要です。`n" +
                 "$DA を別の場所に控えましたか?")
        Run @("-c", "port=SWD", "mode=HOTPLUG", "-ob", "PRODUCT_STATE=0x17")
        Run @("-c", "port=SWD", "mode=HOTPLUG", "-hardRst")
        Run @("-c", "port=SWD", "mode=HOTPLUG", "-sdp", $Obk)
        Run @("-c", "port=SWD", "mode=HOTPLUG", "-hardRst")
        Write-Host "書き込みました。discovery で provisioning integrity status が VALID か確かめてください。"
    }
    { $_ -in "test", "open-debug" } {
        Require @($Key, $Cert)
        Run @("-c", "port=SWD", "per=c", "key=$Key", "cert=$Cert", "debugauth=1")
    }
    "close" {
        Require @($Key, $Cert)
        Confirm ("product state を TZ-Closed にします。セキュア側はデバッガから読めなくなり、`n" +
                 "セキュア側の書き込みには open-debug が必要になります。test は成功しましたか?")
        Run @("-c", "port=SWD", "mode=HOTPLUG", "-ob", "PRODUCT_STATE=0xC6")
    }
    "regression" {
        Require @($Key, $Cert)
        Confirm "Open に戻します。Flash はすべて消え、U2F の鍵も消えます (登録済みのサイトは使えなくなります)。"
        Run @("-c", "port=SWD", "per=a", "key=$Key", "cert=$Cert", "debugauth=1")
    }
    default { throw "不明なコマンド: $Command" }
}
