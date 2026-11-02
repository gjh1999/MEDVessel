$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$dataRoot = [IO.Path]::GetFullPath((Join-Path $repositoryRoot 'datasets'))
$vmtkBase = 'https://raw.githubusercontent.com/vmtk/vmtk-test-data/7f5abc07a33e1037a106fa4e0ac6c320adb1baa1/input/'
$thinBase = 'https://raw.githubusercontent.com/yzhong52/ThinVesselSegmentation/master/data/'
$assets = @(
    @{ Path='thin-vessel/data15.data'; Uri=$thinBase+'data15.data'; Hash='f50f771c585f3761b5880009a77ad9d1795bd9082420a51b58dbb4a04934e4f2' },
    @{ Path='thin-vessel/data15.data.readme.txt'; Uri=$thinBase+'data15.data.readme.txt'; Hash='9238bfe76be8c6c13c44b946534fe15f009d8dbf1bad2691fa6b82439006cf3b' },
    @{ Path='vmtk/aorta.mha'; Uri=$vmtkBase+'aorta.mha'; Hash='435c368b6ce04011b94bceb9f9a62110d49934417a96b73aafec57aba41cfca0' },
    @{ Path='vmtk/aorta-centerline.vtp'; Uri=$vmtkBase+'aorta-centerline.vtp'; Hash='6c540671dbef5575561fc5a9b4447ccff29e99261a8ec33d16aa213b6d24d2ca' },
    @{ Path='vmtk/aorta-final-levelset.mha'; Uri=$vmtkBase+'aorta-final-levelset.mha'; Hash='bf376aa5b4f72b68630e14266fdd060cfdb148ff1532f056fd11fadea42d1961' }
)
foreach ($asset in $assets) {
    $destination = [IO.Path]::GetFullPath((Join-Path $dataRoot $asset.Path))
    if (-not $destination.StartsWith($dataRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Reference data path escaped its dataset directory'
    }
    if (Test-Path -LiteralPath $destination) {
        if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -ne $asset.Hash) {
            throw "Existing reference file has an unexpected checksum: $destination"
        }
        Write-Output "Verified $($asset.Path)"
        continue
    }
    New-Item -ItemType Directory -Force (Split-Path -Parent $destination) | Out-Null
    $temporary = $destination + '.download'
    Invoke-WebRequest -UseBasicParsing -Uri $asset.Uri -OutFile $temporary -TimeoutSec 120
    if ((Get-FileHash -LiteralPath $temporary -Algorithm SHA256).Hash -ne $asset.Hash) {
        throw "Downloaded checksum mismatch: $($asset.Path)"
    }
    Move-Item -LiteralPath $temporary -Destination $destination
    Write-Output "Downloaded $($asset.Path)"
}
