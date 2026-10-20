using System;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Text;
using System.Threading;
using System.Windows.Forms;

internal static class LauncherTests
{
    private static void Check(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
    }

    [STAThread]
    private static int Main(string[] args)
    {
        if (Path.GetFileName(Application.ExecutablePath).StartsWith("uw"))
        {
            int homeIndex = Array.IndexOf(args, "--home");
            File.WriteAllLines(Path.Combine(args[homeIndex + 1], "test-arguments.txt"), args);
            Console.WriteLine("test game stdout"); Console.Error.WriteLine("test game stderr");
            return 0;
        }
        if (args.Length > 0 && args[0] == "--echo")
        {
            for (int i = 1; i < args.Length; i++)
                Console.WriteLine(Convert.ToBase64String(Encoding.UTF8.GetBytes(args[i])));
            return 0;
        }
        try
        {
            string[] values = { "", "a b", @"C:\a b\", "a\"b", @"C:\save\host", "--coop-join" };
            StringBuilder command = new StringBuilder("--echo");
            foreach (string value in values) command.Append(" " + LaunchPlan.Quote(value));
            ProcessStartInfo info = new ProcessStartInfo(Application.ExecutablePath, command.ToString());
            info.UseShellExecute = false; info.CreateNoWindow = true; info.RedirectStandardOutput = true;
            using (Process probe = Process.Start(info))
            {
                foreach (string expected in values)
                    Check(probe.StandardOutput.ReadLine() == Convert.ToBase64String(Encoding.UTF8.GetBytes(expected)),
                        "Windows argument round trip failed: " + expected);
                probe.WaitForExit(); Check(probe.ExitCode == 0, "Argument probe failed");
            }
            foreach (string invalid in new[] { "", "localhost", "127.1", "0.0.0.0", "255.255.255.255",
                "256.1.1.1", "192.168.1.1:27860", "01.2.3.4", "224.0.0.1", "1.2.3.+4" })
                Check(!LaunchPlan.ValidAddress(invalid), "Accepted invalid address: " + invalid);
            Check(LaunchPlan.ValidAddress("127.0.0.1"), "Loopback rejected");
            Check(LaunchPlan.ValidAddress("192.168.1.20"), "LAN address rejected");
            string folder = Path.Combine(Path.GetDirectoryName(Application.ExecutablePath), "test saves");
            string host = LaunchPlan.Home(folder, 1, true), client = LaunchPlan.Home(folder, 1, false);
            Check(host != client && host != LaunchPlan.Home(folder, 2, true), "Save folders collide");
            Check(LaunchPlan.Arguments(true, "", 27860, host, "").Contains("--coop-host \"27860\""), "Host mode missing");
            Check(LaunchPlan.Arguments(false, "127.0.0.1", 27860, client, folder).Contains(
                "--coop-join \"127.0.0.1:27860\""), "Client mode missing");
            foreach (int invalid in new[] { 0, 65536 })
            {
                bool rejected = false;
                try { LaunchPlan.Arguments(true, "", invalid, host, ""); }
                catch (ArgumentException) { rejected = true; }
                Check(rejected, "Invalid port accepted");
            }
            Application.EnableVisualStyles(); Application.SetCompatibleTextRenderingDefault(false);
            foreach (bool isHost in new[] { true, false })
            {
                using (LauncherForm form = new LauncherForm(isHost, args[0], folder))
                {
                    form.ShowInTaskbar = false; form.StartPosition = FormStartPosition.Manual;
                    form.Location = new Point(-32000, -32000); form.Show(); Application.DoEvents();
                    using (Bitmap bitmap = new Bitmap(form.Width, form.Height))
                    {
                        form.DrawToBitmap(bitmap, new Rectangle(0, 0, bitmap.Width, bitmap.Height));
                        bitmap.Save(Path.Combine(Path.GetDirectoryName(Application.ExecutablePath),
                            isHost ? "host.png" : "client.png"));
                    }
                    form.Hide();
                }
            }
            // Exercise the actual button, process spawn, saved settings and captured output.
            string fakeBundle = Path.Combine(folder, "test bundle");
            for (int selected = 1; selected <= 2; selected++)
            {
                string bin = Path.Combine(fakeBundle, "bin", "uw" + selected);
                Directory.CreateDirectory(bin);
                File.Copy(Application.ExecutablePath, Path.Combine(bin, "uw" + selected + "port.exe"), true);
                foreach (bool isHost in new[] { true, false })
                {
                    using (LauncherForm form = new LauncherForm(isHost, fakeBundle, folder))
                    {
                        form.ShowInTaskbar = false; form.StartPosition = FormStartPosition.Manual;
                        form.Location = new Point(-32000, -32000); form.Show();
                        foreach (Control control in form.Controls)
                        {
                            ComboBox choices = control as ComboBox;
                            if (choices != null && choices.Items[0].ToString() == "Ultima Underworld")
                                choices.SelectedIndex = selected - 1;
                        }
                        foreach (Control control in form.Controls)
                        {
                            Button button = control as Button;
                            if (button != null && button.Text == (isHost ? "Start host" : "Join game"))
                                button.PerformClick();
                        }
                        Stopwatch timer = Stopwatch.StartNew();
                        while (!form.IsDisposed && timer.ElapsedMilliseconds < 5000)
                        { Application.DoEvents(); Thread.Sleep(10); }
                        Check(form.IsDisposed, "Launcher did not finish after the test game exited");
                    }
                    string home = LaunchPlan.Home(folder, selected, isHost);
                    string[] captured = File.ReadAllLines(Path.Combine(home, "test-arguments.txt"));
                    Check(captured[0] == "--home" && captured[1] == home, "Save path was not passed correctly");
                    Check(captured[2] == (isHost ? "--coop-host" : "--coop-join"), "Wrong role passed to game");
                    Check(captured[3] == (isHost ? "27860" : "127.0.0.1:27860"), "Wrong endpoint passed to game");
                    Check(captured[4] == "--coop-shared", "The launcher did not enable the shared world");
                    bool stdout = false, stderr = false;
                    foreach (string log in Directory.GetFiles(home, "launcher-*.log"))
                    {
                        string contents = File.ReadAllText(log);
                        stdout |= contents.Contains("test game stdout"); stderr |= contents.Contains("test game stderr");
                    }
                    Check(stdout && stderr, "Game output was not captured in startup logs");
                }
            }
            Console.WriteLine("Launcher Windows arguments, validation, save isolation, UI, process startup and logs: PASS");
            return 0;
        }
        catch (Exception ex) { Console.Error.WriteLine(ex); return 1; }
    }
}
