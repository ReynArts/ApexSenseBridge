using System;
using System.Collections.Generic;
using System.IO;

namespace ApexSenseBridgeTray.Services
{
    internal static class PlatformClientProcessFilter
    {
        private static readonly HashSet<string> ExcludedExecutables =
            new HashSet<string>(StringComparer.OrdinalIgnoreCase)
            {
                // Steam
                "steam.exe",
                "steamservice.exe",
                "steamwebhelper.exe",
                "steamerrorreporter.exe",
                "steamerrorreporter64.exe",
                "steam_monitor.exe",
                "gameoverlayui.exe",

                // Battle.net
                "battle.net.exe",
                "battle.net launcher.exe",
                "battle.net helper.exe",
                "agent.exe",
                "blizzardbrowser.exe",
                "blizzarderror.exe",

                // Epic Games
                "epicgameslauncher.exe",
                "epicgamesupdater.exe",
                "epicwebhelper.exe",
                "eosoverlayrenderer-win32-shipping.exe",
                "eosoverlayrenderer-win64-shipping.exe",

                // EA / Origin
                "eadesktop.exe",
                "ealauncher.exe",
                "eabackgroundservice.exe",
                "ealocalhostsvc.exe",
                "eacefsubprocess.exe",
                "link2ea.exe",
                "origin.exe",
                "originwebhelperservice.exe",

                // Ubisoft Connect
                "ubisoftconnect.exe",
                "ubisoftconnectwebcore.exe",
                "ubisoftextension.exe",
                "ubisoftgamelauncher.exe",
                "uplay.exe",
                "uplaywebcore.exe",
                "upc.exe",

                // GOG Galaxy
                "galaxyclient.exe",
                "galaxyclient helper.exe",
                "galaxycommunication.exe",

                // Rockstar Games Launcher
                "rockstargameslauncher.exe",
                "launcherpatcher.exe",
                "socialclubhelper.exe",

                // Xbox app / Microsoft Gaming Services
                "xboxpcapp.exe",
                "gamingservices.exe",
                "gamingservicesnet.exe",

                // Other library clients
                "riotclientservices.exe",
                "riotclientux.exe",
                "riotclientuxrender.exe",
                "amazon games.exe",
                "itch.exe",
                "playnite.desktopapp.exe",
                "playnite.fullscreenapp.exe"
            };

        public static bool IsExcluded(string fileNameOrPath)
        {
            if (string.IsNullOrWhiteSpace(fileNameOrPath)) return false;

            string fileName;
            try
            {
                fileName = Path.GetFileName(fileNameOrPath.Trim());
            }
            catch
            {
                fileName = fileNameOrPath.Trim();
            }

            return !string.IsNullOrWhiteSpace(fileName) && ExcludedExecutables.Contains(fileName);
        }
    }
}
