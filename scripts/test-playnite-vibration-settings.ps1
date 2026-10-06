param(
    [string]$PluginAssembly = (Join-Path (Split-Path -Parent $PSScriptRoot) "playnite\ApexSenseBridge\bin\Release\ApexSenseBridge.dll"),
    [string]$SdkAssembly = "G:\Program Files\Playnite\Playnite.SDK.dll"
)

$ErrorActionPreference = "Stop"
if ([Threading.Thread]::CurrentThread.ApartmentState -ne "STA") { throw "Run with Windows PowerShell -STA." }
Add-Type -AssemblyName PresentationFramework, PresentationCore, WindowsBase, System.Web.Extensions
[void][Reflection.Assembly]::LoadFrom((Resolve-Path -LiteralPath $SdkAssembly).Path)
$plugin = [Reflection.Assembly]::LoadFrom((Resolve-Path -LiteralPath $PluginAssembly).Path)
$serializer = New-Object System.Web.Script.Serialization.JavaScriptSerializer
$settingsType = $plugin.GetType("ApexSenseBridge.ApexSenseBridgeSettings")
$settings = [Activator]::CreateInstance($settingsType)
if ($settings.VibrationStrengthPercent -ne 100) { throw "New settings lost the neutral default." }
$deserialize = $serializer.GetType().GetMethod("Deserialize", [type[]]@([string], [type]))
$legacy = $deserialize.Invoke($serializer, @("{}", $settingsType))
if ($legacy.VibrationStrengthPercent -ne 100) { throw "Legacy settings lost the neutral default." }
$settings.VibrationStrengthPercent = 175
$restored = $deserialize.Invoke($serializer, @($serializer.Serialize($settings), $settingsType))
if ($restored.VibrationStrengthPercent -ne 175) { throw "Amplification did not survive persistence." }

# No plugin startup, API connection, settings save or game launch.
$vmType = $plugin.GetType("ApexSenseBridge.ApexSenseBridgeSettingsViewModel")
$vm = [Runtime.Serialization.FormatterServices]::GetUninitializedObject($vmType)
$flags = [Reflection.BindingFlags]"Instance,NonPublic"
$vmType.GetField("settings", $flags).SetValue($vm, $settings)
$view = [Activator]::CreateInstance($plugin.GetType("ApexSenseBridge.ApexSenseBridgeSettingsView"))
$view.DataContext = $vm
$view.Measure((New-Object Windows.Size 760, 900))
$view.Arrange((New-Object Windows.Rect 0, 0, 760, 900))
$view.UpdateLayout()
$box = $view.FindName("TxtVibrationStrength")
if ($box.Text -ne "175") { throw "Playnite did not display the saved gain." }
$box.Text = "150"
$box.GetBindingExpression([Windows.Controls.TextBox]::TextProperty).UpdateSource()
if ($settings.VibrationStrengthPercent -ne 150) { throw "Playnite did not update the gain setting." }
$builder = $plugin.GetType("ApexSenseBridge.Common.BridgeArguments").GetMethod("Build")
$arguments = $builder.Invoke($null, @("none", $true, [int]12, $false, [int]0, [int]100, [int]$settings.VibrationStrengthPercent, [int]100, [int]100))
if (-not $arguments.Contains("--vibration-strength 150")) { throw "Playnite arguments lost the gain." }
$enginePluginType = $plugin.GetType("ApexSenseBridge.ApexSenseBridge")
$enginePlugin = [Runtime.Serialization.FormatterServices]::GetUninitializedObject($enginePluginType)
$enginePluginType.GetField("settings", $flags).SetValue($enginePlugin, $vm)
$profile = [Activator]::CreateInstance($plugin.GetType("ApexSenseBridge.GameBridgeProfile"))
$launchArguments = $enginePluginType.GetMethod("BuildBridgeArguments", $flags).Invoke($enginePlugin, @($profile))
if (-not $launchArguments.Contains("--vibration-strength 150")) { throw "Actual Playnite launch lost the gain." }
Write-Output "PASS: Playnite gain defaults, legacy migration, persistence, WPF binding and engine arguments."
