# Windows-Setup (Aufgabe 1.0)

Vorhanden: CLion 2025.1.4 mit GCC 13.1 (C++20), Ninja und CMake 3.31.6.
Das Skript verwendet diese zusammengehoerigen Werkzeuge und einen separaten
Build-Ordner `build/windows-debug`. Der bestehende CLion-Build bleibt erhalten.

## Einmalig: Vulkan SDK installieren

Das vollstaendige **Windows x64 Vulkan SDK** von
https://vulkan.lunarg.com/sdk/home/ installieren (mindestens 1.3).
Header, Shader-Werkzeuge und Validation Layers werden benoetigt.
Standardpfad: `C:\VulkanSDK\<Version>`.

Der automatische Download war in der Codex-Ausfuehrungsumgebung durch
Netzwerkberechtigungen gesperrt. Das SDK wurde deshalb noch nicht installiert.
Ein vorhandenes `vulkan-1.dll` allein ersetzt das SDK nicht.

## Bauen und starten

PowerShell im Ordner `a01\crat3r` oeffnen:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\windows.ps1 Setup
powershell -NoProfile -ExecutionPolicy Bypass -File .\windows.ps1 Run
```

Bei abweichendem SDK-Pfad bei jedem Aufruf `-VulkanSdk 'D:\VulkanSDK\<Version>'`
angeben. `-ClionHome` erlaubt einen abweichenden CLion-Pfad.
`Build` baut erneut; `Run` baut ebenfalls vor dem Start. Debug ist der Standard,
damit die Vulkan Validation Layers aktiv sind. Die Prozessumgebung wird nur fuer
den Skriptaufruf angepasst; systemweite PATH-Aenderungen sind nicht erforderlich.

Erwartetes Ergebnis laut Aufgabe 1.0: ein Fenster mit gruenem Hintergrund und UI.
Die GPU und ihr Treiber muessen Vulkan 1.3 unterstuetzen. Das laesst sich nach der
SDK-Installation mit `C:\VulkanSDK\<Version>\Bin\vulkaninfo.exe --summary` pruefen.
GPU-Unterstuetzung, Kompilierung und Fensterstart sind hier noch nicht verifiziert.

## CLion

Den Projektordner `a01\crat3r` oeffnen. Unter Settings > Build, Execution,
Deployment > CMake fuer das Debug-Profil die Environment-Variable
`VULKAN_SDK=C:\VulkanSDK\<Version>` setzen. Die gebuendelte MinGW-Toolchain
verwenden, CMake neu laden und Target `a01` bauen/starten.

Die CMake-Konfiguration prueft nun das SDK und `glslangValidator` direkt. Obwohl
Aufgabenblatt und README `glslc` nennen, ruft der mitgelieferte Shader-Build
tatsaechlich `glslangValidator` auf. Das vollstaendige SDK enthaelt beide.
Das Target `a01` baut jetzt auch die Shader automatisch mit.

Die Implementierungsaufgaben 1.1 bis 1.8 in `src/a01/main.cpp` und
`shaders/a01/particles.comp` sind nicht Teil dieses Setups und bleiben offen.
