param(
    [Parameter(Mandatory=$true)][ValidateSet('ApolloService','SunshineSvc')][string]$Name,
    [Parameter(Mandatory=$true)][ValidateSet('Inspect','Stop','Remove')][string]$Action,
    [Parameter(Mandatory=$true)][string]$ExpectedPath
)
# Guard Apollo's existing service lifecycle; never mutate another installation.
$ErrorActionPreference = 'Stop'
try {
    $service = Get-CimInstance Win32_Service -Filter "Name='$Name'"
    if (!$service) { exit 0 }
    $actual = $service.PathName.Trim()
    if ($actual.StartsWith('"') -and $actual.EndsWith('"')) { $actual = $actual.Substring(1,$actual.Length-2) }
    $actual = [IO.Path]::GetFullPath($actual)
    $expected = [IO.Path]::GetFullPath($ExpectedPath)
    if (![String]::Equals($actual,$expected,[StringComparison]::OrdinalIgnoreCase)) {
        if ($Name -eq 'SunshineSvc') { Write-Host 'Preserving Sunshine service belonging to another installation.'; exit 0 }
        throw "ApolloService belongs to another installation: $actual"
    }
    if ($Action -eq 'Inspect') { exit 0 }
    $controller = Get-Service -Name $Name
    try {
        if ($controller.Status -ne [ServiceProcess.ServiceControllerStatus]::Stopped) {
            Stop-Service -InputObject $controller
        }
        $controller.WaitForStatus([ServiceProcess.ServiceControllerStatus]::Stopped,[TimeSpan]::FromSeconds(30))
    } finally {
        # An open ServiceController handle can keep DeleteService pending.
        $controller.Dispose()
    }
    if ($Action -eq 'Remove') {
        $result = Invoke-CimMethod -InputObject $service -MethodName Delete
        if ($result.ReturnValue -ne 0) { throw "Service deletion failed: $($result.ReturnValue)" }
        $deadline = [DateTime]::UtcNow.AddSeconds(30)
        while (Get-CimInstance Win32_Service -Filter "Name='$Name'") {
            if ([DateTime]::UtcNow -ge $deadline) { throw 'Service remains marked for deletion; close service management tools and retry.' }
            Start-Sleep -Milliseconds 100
        }
    }
    exit 0
} catch {
    Write-Host "Service $Action failed: $_"
    exit 1
}
