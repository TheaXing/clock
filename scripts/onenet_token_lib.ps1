function New-OneNetDeviceToken {
    param([string]$ProductId,[string]$DeviceName,[string]$DeviceKey,[long]$Expires)
    if($ProductId -notmatch '^[A-Za-z0-9]{8,32}$'){throw 'Invalid product ID'}
    if($DeviceName -notmatch '^[A-Za-z0-9_-]{1,64}$'){throw 'Invalid device name'}
    if($Expires -le [DateTimeOffset]::UtcNow.ToUnixTimeSeconds() -or $Expires -gt 4102444800){throw 'Token expiry must be a future Unix time before 2100'}
    if([string]::IsNullOrWhiteSpace($DeviceKey)){throw 'Device key is empty'}
    try{$keyBytes=[Convert]::FromBase64String($DeviceKey)}catch{throw 'Device key must be valid Base64'}
    if($keyBytes.Length -eq 0){throw 'Device key is empty'}
    $hmac=[Security.Cryptography.HMACSHA256]::new()
    try {
        $hmac.Key=$keyBytes
        $resource="products/$ProductId/devices/$DeviceName"
        $expiryText=$Expires.ToString([Globalization.CultureInfo]::InvariantCulture)
        $stringToSign=$expiryText+"`nsha256`n"+$resource+"`n2018-10-31"
        $signature=[Convert]::ToBase64String($hmac.ComputeHash([Text.Encoding]::UTF8.GetBytes($stringToSign)))
        return 'version=2018-10-31&res='+[Uri]::EscapeDataString($resource)+'&et='+$expiryText+'&method=sha256&sign='+[Uri]::EscapeDataString($signature)
    }finally{
        $hmac.Dispose()
        [Array]::Clear($keyBytes,0,$keyBytes.Length)
    }
}
