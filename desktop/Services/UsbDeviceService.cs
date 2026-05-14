using System;
using System.Collections.Generic;
using System.Linq;
using System.Management;
using System.Text.RegularExpressions;

namespace PortableCncApp.Services;

public sealed record UsbCdcPortPair(string DisplayName, string GrblPort, string AppPort, string InstanceId)
{
    public override string ToString() => DisplayName;
}

public static class UsbDeviceService
{
    private const string RpiVid = "VID_2E8A";
    private const string TeensyVid = "VID_16C0";
    private const string TeensyDualSerialPid = "PID_048B";

    public static List<string> GetControllerPorts()
    {
        var controller = new List<string>();
        var other = new List<string>();

        if (OperatingSystem.IsWindows())
        {
            try
            {
                using var searcher = new ManagementObjectSearcher(
                    "SELECT Name, DeviceID FROM Win32_PnPEntity WHERE Name LIKE '%(COM%)'");

                foreach (ManagementObject obj in searcher.Get())
                {
                    var name = obj["Name"]?.ToString() ?? "";
                    var deviceId = obj["DeviceID"]?.ToString() ?? "";
                    var match = Regex.Match(name, @"COM\d+");
                    if (!match.Success) continue;

                    var port = match.Value;
                    if (deviceId.Contains(RpiVid, StringComparison.OrdinalIgnoreCase))
                        controller.Add(port);
                    else
                        other.Add(port);
                }
            }
            catch
            {
                other.AddRange(System.IO.Ports.SerialPort.GetPortNames());
            }
        }
        else
        {
            other.AddRange(System.IO.Ports.SerialPort.GetPortNames());
        }

        return controller.Count > 0 ? controller : other;
    }

    public static List<UsbCdcPortPair> GetPortableCncPortPairs()
    {
        if (!OperatingSystem.IsWindows())
            return PairFallbackPorts(System.IO.Ports.SerialPort.GetPortNames());

        try
        {
            var ports = new List<UsbComPortInfo>();
            using var searcher = new ManagementObjectSearcher(
                "SELECT Name, DeviceID, PNPDeviceID FROM Win32_PnPEntity WHERE Name LIKE '%(COM%)'");

            foreach (ManagementObject obj in searcher.Get())
            {
                var name = obj["Name"]?.ToString() ?? "";
                var deviceId = obj["PNPDeviceID"]?.ToString()
                            ?? obj["DeviceID"]?.ToString()
                            ?? "";
                var portMatch = Regex.Match(name, @"COM\d+", RegexOptions.IgnoreCase);
                if (!portMatch.Success)
                    continue;

                if (!deviceId.Contains(TeensyVid, StringComparison.OrdinalIgnoreCase) ||
                    !deviceId.Contains(TeensyDualSerialPid, StringComparison.OrdinalIgnoreCase))
                    continue;

                var miMatch = Regex.Match(deviceId, @"MI_([0-9A-F]{2})", RegexOptions.IgnoreCase);
                if (!miMatch.Success)
                    continue;

                ports.Add(new UsbComPortInfo(
                    PortName: portMatch.Value,
                    DeviceId: deviceId,
                    InterfaceId: miMatch.Groups[1].Value.ToUpperInvariant(),
                    InstanceKey: BuildInstanceKey(deviceId)));
            }

            var pairs = new List<UsbCdcPortPair>();
            foreach (var group in ports.GroupBy(p => p.InstanceKey))
            {
                var grbl = group.FirstOrDefault(p => p.InterfaceId == "00");
                var app = group.FirstOrDefault(p => p.InterfaceId == "02");
                if (grbl.PortName is null || app.PortName is null)
                    continue;

                pairs.Add(new UsbCdcPortPair(
                    DisplayName: $"Portable CNC ({app.PortName} app, {grbl.PortName} GRBL)",
                    GrblPort: grbl.PortName,
                    AppPort: app.PortName,
                    InstanceId: group.Key));
            }

            if (pairs.Count > 0)
                return pairs.OrderBy(p => p.AppPort, StringComparer.OrdinalIgnoreCase).ToList();
        }
        catch
        {
            // Fall through to manual pairing.
        }

        return PairFallbackPorts(System.IO.Ports.SerialPort.GetPortNames());
    }

    public static UsbCdcPortPair? FindPortableCncPairByAppPort(string appPort)
        => GetPortableCncPortPairs()
            .FirstOrDefault(p => string.Equals(p.AppPort, appPort, StringComparison.OrdinalIgnoreCase));

    private static string BuildInstanceKey(string deviceId)
    {
        var withoutMi = Regex.Replace(deviceId, @"&MI_[0-9A-F]{2}", "", RegexOptions.IgnoreCase);
        return withoutMi.ToUpperInvariant();
    }

    private static List<UsbCdcPortPair> PairFallbackPorts(IEnumerable<string> ports)
    {
        var ordered = ports
            .Distinct(StringComparer.OrdinalIgnoreCase)
            .OrderBy(PortNumber)
            .ThenBy(p => p, StringComparer.OrdinalIgnoreCase)
            .ToList();

        if (ordered.Count < 2)
            return new List<UsbCdcPortPair>();

        return new List<UsbCdcPortPair>
        {
            new(
                DisplayName: $"Portable CNC ({ordered[1]} app, {ordered[0]} GRBL)",
                GrblPort: ordered[0],
                AppPort: ordered[1],
                InstanceId: "fallback")
        };
    }

    private static int PortNumber(string port)
    {
        var match = Regex.Match(port, @"\d+");
        return match.Success && int.TryParse(match.Value, out var number) ? number : int.MaxValue;
    }

    private readonly record struct UsbComPortInfo(
        string PortName,
        string DeviceId,
        string InterfaceId,
        string InstanceKey);
}
