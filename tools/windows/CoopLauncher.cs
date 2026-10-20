using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using System.Windows.Forms;

internal static class LaunchPlan
{
    internal static bool ValidAddress(string value)
    {
        string[] parts = value.Split('.');
        if (parts.Length != 4) return false;
        foreach (string part in parts)
        {
            byte number;
            if (part.Length == 0 || (part.Length > 1 && part[0] == '0')) return false;
            foreach (char c in part) if (c < '0' || c > '9') return false;
            if (!Byte.TryParse(part, out number)) return false;
        }
        IPAddress address;
        return IPAddress.TryParse(value, out address) &&
            address.AddressFamily == AddressFamily.InterNetwork &&
            !address.Equals(IPAddress.Any) && !address.Equals(IPAddress.Broadcast) &&
            address.GetAddressBytes()[0] < 224;
    }

    // Windows argv quoting: double backslashes before a quote or the closing quote.
    internal static string Quote(string value)
    {
        StringBuilder result = new StringBuilder("\"");
        int slashes = 0;
        foreach (char c in value)
        {
            if (c == '\\') { slashes++; continue; }
            result.Append('\\', c == '"' ? slashes * 2 + 1 : slashes);
            result.Append(c);
            slashes = 0;
        }
        result.Append('\\', slashes * 2);
        return result.Append('"').ToString();
    }

    internal static string Home(string appData, int game, bool host)
    {
        return Path.Combine(appData, "underworld-exhumed-coop", "uw" + game,
            host ? "host" : "client");
    }

    internal static string Arguments(bool host, string address, int port, string home, string data)
    {
        if (port < 1 || port > 65535) throw new ArgumentException("Use a port from 1 to 65535.");
        if (!host && !ValidAddress(address))
            throw new ArgumentException("Enter the host's IPv4 address, for example 192.168.1.20.");
        string args = "--home " + Quote(home) + (host ? " --coop-host " : " --coop-join ") +
            Quote(host ? port.ToString() : address + ":" + port);
        args += " --coop-shared";
        if (!String.IsNullOrWhiteSpace(data)) args += " --data " + Quote(data);
        return args;
    }
}

internal sealed class LauncherForm : Form
{
    private readonly bool host;
    private readonly string appData;
    private readonly string bundle;
    private readonly Dictionary<string, string> saved = new Dictionary<string, string>();
    private readonly ComboBox game = new ComboBox();
    private readonly TextBox address = new TextBox();
    private readonly ComboBox hostAddresses = new ComboBox();
    private readonly NumericUpDown port = new NumericUpDown();
    private readonly TextBox data = new TextBox();
    private readonly Label status = new Label();
    private readonly Button start = new Button();
    private bool running;
    private int previousGame;

    internal LauncherForm(bool isHost, string baseFolder, string settingsRoot)
    {
        host = isHost; bundle = baseFolder; appData = settingsRoot;
        Text = "Underworld Co-op - " + (host ? "Host" : "Client");
        Font = new Font("Segoe UI", 10);
        AutoScaleMode = AutoScaleMode.Dpi;
        ClientSize = new Size(570, 450);
        FormBorderStyle = FormBorderStyle.FixedDialog;
        MaximizeBox = false;
        StartPosition = FormStartPosition.CenterScreen;
        string executable = Path.Combine(bundle, host ? "Host.exe" : "Client.exe");
        if (File.Exists(executable)) Icon = Icon.ExtractAssociatedIcon(executable);
        ReadSettings();

        Label heading = LabelAt(host ? "Start a co-op host" : "Join a co-op host", 24, 20, 520, 32);
        heading.Font = new Font(Font.FontFamily, 17, FontStyle.Bold);
        LabelAt("Game", 24, 74, 120, 26);
        game.SetBounds(158, 70, 385, 30);
        game.DropDownStyle = ComboBoxStyle.DropDownList;
        game.Items.AddRange(new object[] { "Ultima Underworld", "Ultima Underworld II" });
        game.SelectedIndex = Get("game", "1") == "2" ? 1 : 0;
        previousGame = game.SelectedIndex + 1;
        Controls.Add(game);

        LabelAt(host ? "Your LAN address" : "Host IP address", 24, 121, 132, 26);
        address.SetBounds(158, 117, 300, 28);
        address.Text = Get("address", "127.0.0.1");
        if (host)
        {
            hostAddresses.SetBounds(158, 117, 300, 28);
            hostAddresses.DropDownStyle = ComboBoxStyle.DropDownList;
            hostAddresses.Items.AddRange(LanAddresses()); hostAddresses.SelectedIndex = 0;
            Controls.Add(hostAddresses);
            Button copy = new Button(); copy.Text = "Copy"; copy.SetBounds(468, 116, 75, 30);
            copy.Click += delegate { Clipboard.SetText(hostAddresses.Text); };
            Controls.Add(copy);
        }
        else Controls.Add(address);
        LabelAt(host ? "Give a LAN address above to the other player."
            : "Use 127.0.0.1 only when the host runs on this PC.", 158, 149, 385, 32);

        LabelAt("Port", 24, 190, 120, 26);
        port.SetBounds(158, 186, 108, 28); port.Minimum = 1; port.Maximum = 65535;
        int storedPort;
        port.Value = Int32.TryParse(Get("port", "27860"), out storedPort) &&
            storedPort >= 1 && storedPort <= 65535 ? storedPort : 27860;
        Controls.Add(port);
        LabelAt("Use the same game and port on both PCs.", 280, 190, 270, 30);

        LabelAt("Game folder", 24, 237, 120, 26);
        data.SetBounds(158, 233, 300, 28); data.Text = host ? Get("data" + previousGame, "") : "Supplied by host";
        data.Enabled = host;
        Controls.Add(data);
        Button browse = new Button(); browse.Text = "Browse"; browse.SetBounds(468, 232, 75, 30);
        browse.Enabled = host;
        browse.Click += delegate
        {
            using (FolderBrowserDialog dialog = new FolderBrowserDialog())
            {
                dialog.Description = "Choose your original GOG game folder";
                dialog.ShowNewFolderButton = false;
                if (Directory.Exists(data.Text)) dialog.SelectedPath = data.Text;
                if (dialog.ShowDialog(this) == DialogResult.OK) data.Text = dialog.SelectedPath;
            }
        };
        Controls.Add(browse);
        LabelAt(host ? "Optional: leave blank to find or choose your installed game."
            : "The host supplies the game view and sound.", 158, 265, 390, 38);
        game.SelectedIndexChanged += delegate
        {
            saved["data" + previousGame] = data.Text;
            previousGame = game.SelectedIndex + 1;
            data.Text = host ? Get("data" + previousGame, "") : "Supplied by host";
        };

        Label note = LabelAt("UW1: separate characters, inventories and stats.\nQuests/world are shared. UW2 still uses one party character.",
            24, 311, 520, 48);
        note.ForeColor = Color.FromArgb(110, 75, 15);
        start.Text = host ? "Start host" : "Join game";
        start.SetBounds(383, 376, 160, 40); start.Click += delegate { Launch(); };
        Controls.Add(start); AcceptButton = start;
        status.SetBounds(24, 377, 345, 56); status.Text = host
            ? "Allow the game through Windows Firewall on your private network."
            : "Your view and actions use the host's shared world.";
        Controls.Add(status);
        // Keep redirected output alive until the game closes, even if the hidden form is closed.
        FormClosing += delegate(object sender, FormClosingEventArgs e)
        {
            if (running) { e.Cancel = true; Hide(); }
        };
    }

    private Label LabelAt(string text, int x, int y, int width, int height)
    {
        Label label = new Label(); label.Text = text; label.SetBounds(x, y, width, height);
        Controls.Add(label); return label;
    }

    private string SettingsFile
    {
        get { return Path.Combine(appData, "underworld-exhumed-coop", host ? "host.ini" : "client.ini"); }
    }

    private string Get(string name, string fallback)
    {
        string value; return saved.TryGetValue(name, out value) ? value : fallback;
    }

    private void ReadSettings()
    {
        try
        {
            if (!File.Exists(SettingsFile)) return;
            foreach (string line in File.ReadAllLines(SettingsFile))
            {
                int equals = line.IndexOf('=');
                if (equals > 0) saved[line.Substring(0, equals)] = line.Substring(equals + 1);
            }
        }
        catch (IOException) { }
        catch (UnauthorizedAccessException) { }
    }

    private void SaveSettings()
    {
        saved["game"] = (game.SelectedIndex + 1).ToString();
        saved["port"] = port.Value.ToString();
        if (host) saved["data" + (game.SelectedIndex + 1)] = data.Text.Trim();
        if (!host) saved["address"] = address.Text.Trim();
        Directory.CreateDirectory(Path.GetDirectoryName(SettingsFile));
        List<string> lines = new List<string>();
        foreach (KeyValuePair<string, string> item in saved) lines.Add(item.Key + "=" + item.Value);
        File.WriteAllLines(SettingsFile, lines.ToArray());
    }

    private static string[] LanAddresses()
    {
        List<string> addresses = new List<string>(), secondary = new List<string>();
        try
        {
            foreach (NetworkInterface nic in NetworkInterface.GetAllNetworkInterfaces())
            {
                if (nic.OperationalStatus != OperationalStatus.Up ||
                    nic.NetworkInterfaceType == NetworkInterfaceType.Loopback) continue;
                IPInterfaceProperties properties = nic.GetIPProperties();
                bool hasGateway = properties.GatewayAddresses.Count > 0;
                foreach (UnicastIPAddressInformation info in properties.UnicastAddresses)
                {
                    string ip = info.Address.ToString();
                    if (info.Address.AddressFamily == AddressFamily.InterNetwork &&
                        !ip.StartsWith("169.254.") && !addresses.Contains(ip) && !secondary.Contains(ip))
                        (hasGateway ? addresses : secondary).Add(ip);
                }
            }
        }
        catch (NetworkInformationException) { }
        addresses.AddRange(secondary);
        return addresses.Count == 0 ? new[] { "127.0.0.1" } : addresses.ToArray();
    }

    private void Launch()
    {
        StreamWriter log = null;
        Process process = null;
        try
        {
            int selected = game.SelectedIndex + 1;
            string exe = Path.Combine(bundle, "bin", "uw" + selected, "uw" + selected + "port.exe");
            if (!File.Exists(exe)) throw new FileNotFoundException("Keep the bin folder beside this launcher.", exe);
            string home = LaunchPlan.Home(appData, selected, host);
            string folder = host ? data.Text.Trim() : "";
            string args = LaunchPlan.Arguments(host, address.Text.Trim(), (int)port.Value, home, folder);
            if (folder.Length > 0 && !Directory.Exists(folder))
                throw new DirectoryNotFoundException("The game folder does not exist. Browse to it or leave it blank.");
            Directory.CreateDirectory(home);
            SaveSettings();
            string logPath = Path.Combine(home, "launcher-" + DateTime.Now.ToString("yyyyMMdd-HHmmss-fff") + ".log");
            log = new StreamWriter(logPath, false, Encoding.UTF8); log.AutoFlush = true;
            log.WriteLine(exe + " " + args);
            process = new Process();
            process.StartInfo = new ProcessStartInfo(exe, args);
            process.StartInfo.WorkingDirectory = bundle;
            process.StartInfo.UseShellExecute = false;
            process.StartInfo.CreateNoWindow = true;
            process.StartInfo.RedirectStandardOutput = true;
            process.StartInfo.RedirectStandardError = true;
            StreamWriter output = log;
            DataReceivedEventHandler write = delegate(object sender, DataReceivedEventArgs e)
            {
                if (e.Data != null) lock (output) output.WriteLine(e.Data);
            };
            process.OutputDataReceived += write; process.ErrorDataReceived += write;
            process.Start();
            process.BeginOutputReadLine(); process.BeginErrorReadLine();
            running = true; start.Enabled = false; Hide();
            Process child = process;
            Thread wait = new Thread(delegate()
            {
                child.WaitForExit(); int code = child.ExitCode;
                child.Dispose(); lock (output) output.Dispose();
                BeginInvoke((MethodInvoker)delegate
                {
                    running = false;
                    if (code == 0) { Close(); return; }
                    start.Enabled = true; Show(); Activate();
                    status.Text = "The game stopped (code " + code + "). Try choosing its game folder.";
                    MessageBox.Show(this, "The game could not start or stopped with an error.\n\nLog: " + logPath,
                        Text, MessageBoxButtons.OK, MessageBoxIcon.Error);
                });
            });
            wait.IsBackground = true; wait.Start();
        }
        catch (Exception ex)
        {
            if (process != null) process.Dispose();
            if (log != null) log.Dispose();
            MessageBox.Show(this, ex.Message, Text, MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
    }
}

internal static class Program
{
    [STAThread]
    private static void Main()
    {
        Application.EnableVisualStyles(); Application.SetCompatibleTextRenderingDefault(false);
#if HOST
        const bool host = true;
#else
        const bool host = false;
#endif
        Application.Run(new LauncherForm(host, AppDomain.CurrentDomain.BaseDirectory,
            Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData)));
    }
}
