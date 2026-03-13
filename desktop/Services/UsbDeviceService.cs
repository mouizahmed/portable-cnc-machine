using System;
using System.Collections.Generic;
using System.Management;
using System.Text.RegularExpressions;

namespace PortableCncApp.Services;

public static class UsbDeviceService
{
    // Raspberry Pi VID — covers all Pico variants (Pico, Pico W, Pico 2, Pico 2W)
    private const string RPI_VID = "VID_2E8A";

    /// <summary>
    /// Returns COM ports whose USB VID matches the Raspberry Pi Pico family.
    /// Falls back to all available COM ports if WMI is unavailable.
    /// </summary>
    public static List<string> GetPicoPorts()
    {
        var pico  = new List<string>();
        var other = new List<string>();

        try
        {
            using var searcher = new ManagementObjectSearcher(
                "SELECT Name, DeviceID FROM Win32_PnPEntity WHERE Name LIKE '%(COM%)'");

            foreach (ManagementObject obj in searcher.Get())
            {
                var name     = obj["Name"]?.ToString() ?? "";
                var deviceId = obj["DeviceID"]?.ToString() ?? "";
                var match    = Regex.Match(name, @"COM\d+");
                if (!match.Success) continue;

                var port = match.Value;
                if (deviceId.Contains(RPI_VID, StringComparison.OrdinalIgnoreCase))
                    pico.Add(port);
                else
                    other.Add(port);
            }
        }
        catch
        {
            // WMI unavailable — fall back to plain port enumeration
            other.AddRange(System.IO.Ports.SerialPort.GetPortNames());
        }

        // Return Pico ports first; if none found return all ports so the user
        // can still connect manually
        return pico.Count > 0 ? pico : other;
    }

}
