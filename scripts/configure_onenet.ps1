param(
    [string]$ProductId='4lh76atTpR',
    [string]$DeviceName='clock001',
    [ValidateRange(1,365)][int]$ValidDays=30,
    [string]$KeyFile,
    [string]$OutputFile
)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/onenet_token_lib.ps1"
$repoRoot=Split-Path -Parent $PSScriptRoot
if(!$OutputFile){$OutputFile=Join-Path $repoRoot 'app/onenet_config_local.h'}
$secretText=$null
$secretPointer=[IntPtr]::Zero
try {
    if($KeyFile){$secretText=[IO.File]::ReadAllText((Resolve-Path -LiteralPath $KeyFile).Path).Trim()}
    else {
        $secureKey=Read-Host 'Paste the DEVICE key (hidden input)' -AsSecureString
        $secretPointer=[Runtime.InteropServices.Marshal]::SecureStringToBSTR($secureKey)
        $secretText=[Runtime.InteropServices.Marshal]::PtrToStringBSTR($secretPointer).Trim()
    }
    $expires=[DateTimeOffset]::UtcNow.AddDays($ValidDays).ToUnixTimeSeconds()
    $token=New-OneNetDeviceToken -ProductId $ProductId -DeviceName $DeviceName -DeviceKey $secretText -Expires $expires
    $header=@"
#ifndef CLOCK_ONENET_CONFIG_LOCAL_H
#define CLOCK_ONENET_CONFIG_LOCAL_H
/* Generated locally. Contains a credential; ignored by Git. No raw device key. */
#define CLOCK_ONENET_PRODUCT_ID "$ProductId"
#define CLOCK_ONENET_DEVICE_NAME "$DeviceName"
#define CLOCK_ONENET_MQTT_HOST "$ProductId.mqttstls.acc.cmcconenet.cn"
#define CLOCK_ONENET_MQTT_PORT 8883U
#define CLOCK_ONENET_TOKEN "$token"
#define CLOCK_ONENET_TOKEN_EXPIRES $($expires)UL
#endif
"@
    $absoluteOutput=[IO.Path]::GetFullPath($OutputFile)
    [IO.File]::WriteAllText($absoluteOutput,$header+"`n",[Text.UTF8Encoding]::new($false))
    Write-Host "OneNET credential configured in $absoluteOutput"
    Write-Host "Valid for $ValidDays day(s). Run .\scripts\build.ps1 -Configuration Release (reconfigures CMake)."
}finally{
    $secretText=$null
    $token=$null
    if($secretPointer -ne [IntPtr]::Zero){[Runtime.InteropServices.Marshal]::ZeroFreeBSTR($secretPointer)}
    if($secureKey){$secureKey.Dispose()}
}
