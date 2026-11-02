using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Reflection;
using System.Security.Cryptography;
using System.Security.Principal;
using System.Text;
using System.Text.RegularExpressions;
using Microsoft.Win32;

namespace NolaSetup
{
    class Options
    {
        public string Verb = "";
        public string Role = "";
        public string Peer = "";
        public string Net = "";
        public string Game = "";
        public string Bridge = "";
        public string Steam = "";
        public string FwName = "";
        public bool NoFw;
        public bool Yes;
    }

    class Journal
    {
        readonly string dir;
        readonly string state;
        readonly Dictionary<string, bool> seen = new Dictionary<string, bool>(StringComparer.OrdinalIgnoreCase);
        int counter;

        public Journal(string backupDir)
        {
            dir = backupDir;
            state = Path.Combine(backupDir, "state.txt");
        }

        public string StatePath { get { return state; } }

        public void Begin()
        {
            Directory.CreateDirectory(Path.Combine(dir, "files"));
            if (!File.Exists(state)) File.WriteAllText(state, "NOLA-JOURNAL 1\n", Encoding.ASCII);
        }

        void Add(string line)
        {
            File.AppendAllText(state, line + "\n", Encoding.UTF8);
        }

        public void Meta(string key, string value)
        {
            Add("M\t" + key + "\t" + value);
        }

        public void Touch(string path)
        {
            if (seen.ContainsKey(path)) return;
            seen[path] = true;
            if (File.Exists(path))
            {
                counter++;
                string name = counter.ToString("D5");
                File.Copy(path, Path.Combine(dir, "files", name), true);
                Add("F\t" + path + "\t" + name);
            }
            else Add("F\t" + path + "\t-");
        }

        public void MadeDir(string path)
        {
            Add("D\t" + path);
        }

        public void Rule(string name)
        {
            Add("W\t" + name);
        }

        public static List<string[]> Read(string statePath)
        {
            List<string[]> rows = new List<string[]>();
            foreach (string l in File.ReadAllLines(statePath, Encoding.UTF8))
            {
                if (l.Length == 0 || l.StartsWith("NOLA-JOURNAL") || l.StartsWith("S2COOP-JOURNAL")) continue;
                rows.Add(l.Split('\t'));
            }
            return rows;
        }
    }

    static class Program
    {
        const string GameFolderName = "S.T.A.L.K.E.R. 2 Heart of Chornobyl";
        const string ExeName = "Stalker2-Win64-Shipping.exe";
        const int HostPort = 7779;
        const int ClientPort = 7778;

        static Options opt = new Options();
        static ZipArchive payload;
        static Dictionary<string, string> sums = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        static Journal journal;
        static string version = "";

        static void Say(string s)
        {
            Console.WriteLine(s);
        }

        static void Fail(string s)
        {
            Console.WriteLine();
            Console.WriteLine("ERROR: " + s);
            throw new InvalidOperationException(s);
        }

        static string Ask(string prompt)
        {
            Console.Write(prompt);
            string r = Console.ReadLine();
            return r == null ? "" : r.Trim();
        }

        static string Md5(byte[] b)
        {
            using (MD5 m = MD5.Create())
            {
                StringBuilder sb = new StringBuilder();
                foreach (byte x in m.ComputeHash(b)) sb.Append(x.ToString("x2"));
                return sb.ToString();
            }
        }

        static string ResText(string name)
        {
            using (Stream s = Assembly.GetExecutingAssembly().GetManifestResourceStream(name))
            {
                if (s == null) return "";
                using (StreamReader r = new StreamReader(s, Encoding.UTF8)) return r.ReadToEnd();
            }
        }

        static void LoadPayload()
        {
            Stream s = Assembly.GetExecutingAssembly().GetManifestResourceStream("payload.zip");
            if (s == null) Fail("this installer has no payload (damaged download)");
            payload = new ZipArchive(s, ZipArchiveMode.Read);
            foreach (string l in ResText("manifest.txt").Split('\n'))
            {
                string t = l.Trim();
                if (t.Length == 0) continue;
                string[] p = t.Split(new char[] { ' ' }, 3);
                if (p[0] == "version") { version = t.Substring(8); continue; }
                sums[p[2]] = p[0];
            }
        }

        static byte[] Payload(string name)
        {
            ZipArchiveEntry e = payload.GetEntry(name);
            if (e == null) Fail("payload file missing: " + name);
            using (Stream s = e.Open())
            using (MemoryStream ms = new MemoryStream())
            {
                s.CopyTo(ms);
                byte[] b = ms.ToArray();
                string want;
                if (sums.TryGetValue(name, out want) && Md5(b) != want) Fail("payload file damaged: " + name);
                return b;
            }
        }

        static List<string> PayloadFiles(string prefix)
        {
            List<string> r = new List<string>();
            foreach (ZipArchiveEntry e in payload.Entries)
                if (e.FullName.StartsWith(prefix, StringComparison.Ordinal) && !e.FullName.EndsWith("/")) r.Add(e.FullName);
            return r;
        }

        static bool IsAdmin()
        {
            using (WindowsIdentity id = WindowsIdentity.GetCurrent())
                return new WindowsPrincipal(id).IsInRole(WindowsBuiltInRole.Administrator);
        }

        static List<string> SteamRoots()
        {
            List<string> roots = new List<string>();
            if (opt.Steam.Length > 0) { roots.Add(opt.Steam); return roots; }
            try
            {
                using (RegistryKey k = Registry.CurrentUser.OpenSubKey(@"Software\Valve\Steam"))
                    if (k != null && k.GetValue("SteamPath") != null) roots.Add(((string)k.GetValue("SteamPath")).Replace('/', '\\'));
            }
            catch (Exception) { }
            try
            {
                using (RegistryKey k = Registry.LocalMachine.OpenSubKey(@"SOFTWARE\WOW6432Node\Valve\Steam"))
                    if (k != null && k.GetValue("InstallPath") != null) roots.Add((string)k.GetValue("InstallPath"));
            }
            catch (Exception) { }
            roots.Add(@"C:\Program Files (x86)\Steam");
            return roots;
        }

        static string FindGame()
        {
            List<string> libs = new List<string>();
            foreach (string r in SteamRoots())
            {
                libs.Add(r);
                string vdf = Path.Combine(r, @"steamapps\libraryfolders.vdf");
                if (!File.Exists(vdf)) continue;
                foreach (Match m in Regex.Matches(File.ReadAllText(vdf), "\"path\"\\s+\"([^\"]+)\""))
                    libs.Add(m.Groups[1].Value.Replace(@"\\", @"\"));
            }
            foreach (string l in libs)
            {
                string g = Path.Combine(l, @"steamapps\common\" + GameFolderName);
                if (File.Exists(Path.Combine(g, @"Stalker2\Binaries\Win64\" + ExeName))) return g;
            }
            return "";
        }

        static string Win64(string game)
        {
            return Path.Combine(game, @"Stalker2\Binaries\Win64");
        }

        static string BackupDir(string game)
        {
            return Path.Combine(Win64(game), "nola_backup");
        }

        static string OldBackupDir(string game)
        {
            return Path.Combine(Win64(game), "s2coop_backup");
        }

        static void EnsureDir(string dir)
        {
            if (Directory.Exists(dir)) return;
            string parent = Path.GetDirectoryName(dir);
            if (!string.IsNullOrEmpty(parent)) EnsureDir(parent);
            Directory.CreateDirectory(dir);
            journal.MadeDir(dir);
        }

        static void Put(string path, byte[] data)
        {
            EnsureDir(Path.GetDirectoryName(path));
            journal.Touch(path);
            File.WriteAllBytes(path, data);
            if (Md5(File.ReadAllBytes(path)) != Md5(data)) Fail("write verification failed: " + path);
        }

        static void PutPayload(string name, string path)
        {
            Put(path, Payload(name));
        }

        static void Remove(string path)
        {
            if (!File.Exists(path)) return;
            journal.Touch(path);
            File.Delete(path);
        }

        static void PruneEmpty(string dir)
        {
            foreach (string d in Directory.GetDirectories(dir)) PruneEmpty(d);
            if (Directory.GetFileSystemEntries(dir).Length == 0) Directory.Delete(dir);
        }

        static void RemoveTree(string dir)
        {
            if (!Directory.Exists(dir)) return;
            foreach (string f in Directory.GetFiles(dir, "*", SearchOption.AllDirectories)) Remove(f);
            PruneEmpty(dir);
        }

        static string RunNetsh(string args, out int code)
        {
            ProcessStartInfo psi = new ProcessStartInfo("netsh.exe", args);
            psi.UseShellExecute = false;
            psi.RedirectStandardOutput = true;
            psi.RedirectStandardError = true;
            psi.CreateNoWindow = true;
            using (Process p = Process.Start(psi))
            {
                string o = p.StandardOutput.ReadToEnd() + p.StandardError.ReadToEnd();
                p.WaitForExit();
                code = p.ExitCode;
                return o.Trim();
            }
        }

        static string RuleName(string role, string port)
        {
            if (opt.FwName.Length > 0) return opt.FwName;
            return "NOLA " + role + " UDP " + port;
        }

        static void FirewallAdd(string gameExe, string role, string port)
        {
            string name = RuleName(role, port);
            string add = "advfirewall firewall add rule name=\"" + name + "\" dir=in action=allow protocol=UDP localport=" + port +
                         " program=\"" + gameExe + "\" profile=private enable=yes";
            if (opt.NoFw)
            {
                Say("[--] firewall skipped (--nofw). Rule it would add: netsh " + add);
                return;
            }
            if (!IsAdmin())
            {
                Say("WARNING: not running as administrator, firewall rule not added. Run this as administrator, or run once in an administrator prompt:");
                Say("  netsh " + add);
                return;
            }
            int code;
            RunNetsh("advfirewall firewall delete rule name=\"S2Coop relay\"", out code);
            RunNetsh("advfirewall firewall delete rule name=\"S2Coop co-op UDP " + HostPort + "," + ClientPort + "\"", out code);
            RunNetsh("advfirewall firewall delete rule name=\"" + name + "\"", out code);
            string o = RunNetsh(add, out code);
            if (code != 0) Fail("firewall rule not added: " + o);
            journal.Rule(name);
            Say("[ok] firewall: inbound UDP " + port + " allowed for " + ExeName + " (Private profile), rule '" + name + "'");
        }

        static void FirewallRemove(string name)
        {
            if (opt.NoFw) { Say("[--] firewall rule '" + name + "' left (--nofw)"); return; }
            if (!IsAdmin()) { Say("WARNING: not administrator, firewall rule '" + name + "' not removed"); return; }
            int code;
            string o = RunNetsh("advfirewall firewall delete rule name=\"" + name + "\"", out code);
            Say(code == 0 ? "[ok] firewall rule '" + name + "' removed" : "[--] firewall rule '" + name + "' not present");
        }

        static string[] ReadLinesOrEmpty(string path)
        {
            return File.Exists(path) ? File.ReadAllLines(path) : new string[0];
        }

        static void PatchIni(string iniPath)
        {
            string[] lines = File.ReadAllLines(iniPath);
            bool changed = false;
            bool sawTick = false;
            string sec = "";
            bool sawMaj = false, sawMin = false;
            for (int i = 0; i < lines.Length; i++)
            {
                Match sm = Regex.Match(lines[i], @"^\s*\[(.+?)\]");
                if (sm.Success) sec = sm.Groups[1].Value.Trim().ToLowerInvariant();
                Match vm = sec == "engineversionoverride" ? Regex.Match(lines[i], @"^\s*(MajorVersion|MinorVersion)\s*=") : Match.Empty;
                if (vm.Success)
                {
                    string k = vm.Groups[1].Value;
                    if (k == "MajorVersion") sawMaj = true; else sawMin = true;
                    if (!Regex.IsMatch(lines[i], @"^\s*" + k + @"\s*=\s*5\s*$")) { lines[i] = k + " = 5"; changed = true; }
                }
                else if (Regex.IsMatch(lines[i], @"^\s*HookEngineTick\s*="))
                {
                    sawTick = true;
                    if (!Regex.IsMatch(lines[i], @"^\s*HookEngineTick\s*=\s*1\s*$")) { lines[i] = "HookEngineTick = 1"; changed = true; }
                }
                else if (Regex.IsMatch(lines[i], @"^\s*ModsFolderPath\s*=\s*\S"))
                {
                    lines[i] = "ModsFolderPath =";
                    changed = true;
                }
            }
            if (!sawTick) Say("[--] UE4SS-settings.ini has no HookEngineTick line (UE4SS default is 1)");
            if (!sawMaj || !sawMin)
            {
                var l = new System.Collections.Generic.List<string>(lines);
                int at = l.FindIndex(x => x.Trim().Equals("[EngineVersionOverride]", StringComparison.OrdinalIgnoreCase));
                if (at < 0) { l.Add("[EngineVersionOverride]"); at = l.Count - 1; }
                if (!sawMin) { l.Insert(at + 1, "MinorVersion = 5"); Say("[ok] UE4SS-settings.ini had no MinorVersion line: added MinorVersion = 5"); }
                if (!sawMaj) { l.Insert(at + 1, "MajorVersion = 5"); Say("[ok] UE4SS-settings.ini had no MajorVersion line: added MajorVersion = 5"); }
                lines = l.ToArray();
                changed = true;
            }
            if (changed)
            {
                journal.Touch(iniPath);
                File.WriteAllLines(iniPath, lines, new UTF8Encoding(false));
                Say("[ok] UE4SS-settings.ini: HookEngineTick = 1, ModsFolderPath empty, EngineVersionOverride 5.5 (original kept in the backup)");
            }
            else Say("[ok] UE4SS-settings.ini already fine, left as it is");
        }

        static void MergeModsTxt(string path)
        {
            if (!File.Exists(path))
            {
                PutPayload("ue4ss/Mods/mods.txt", path);
                return;
            }
            List<string> keep = new List<string>();
            bool had = false;
            foreach (string l in File.ReadAllLines(path))
            {
                if (Regex.IsMatch(l, @"^\s*(S2CoopAlife|S2CoopNative|NolaAlife)\s*:")) continue;
                if (Regex.IsMatch(l, @"^\s*NolaNative\s*:")) { if (!had) keep.Add("NolaNative : 1"); had = true; continue; }
                keep.Add(l);
            }
            if (!had) keep.Add("NolaNative : 1");
            journal.Touch(path);
            File.WriteAllLines(path, keep.ToArray(), Encoding.ASCII);
        }

        static void WriteFlags(string path, string net, string peer)
        {
            string text = Encoding.ASCII.GetString(Payload("flags/flags.txt"));
            StringBuilder sb = new StringBuilder();
            foreach (string l in text.Split('\n'))
            {
                string t = l.TrimEnd('\r');
                if (t.Length == 0) continue;
                if (Regex.IsMatch(t, @"^\s*(net|netpeer)\s*=")) continue;
                sb.Append(t).Append("\r\n");
            }
            sb.Append("net=").Append(net).Append("\r\n");
            if (net == "ip") sb.Append("netpeer=").Append(peer).Append("\r\n");
            Put(path, Encoding.ASCII.GetBytes(sb.ToString()));
        }

        static void CheckRunning(string game)
        {
            string want = Path.GetFullPath(Path.Combine(Win64(game), ExeName));
            foreach (Process p in Process.GetProcessesByName("Stalker2-Win64-Shipping"))
            {
                string path = "";
                try { path = p.MainModule.FileName; }
                catch (Exception) { Fail("S.T.A.L.K.E.R. 2 is running and its folder cannot be checked. Close the game and run this again."); }
                if (string.Equals(Path.GetFullPath(path), want, StringComparison.OrdinalIgnoreCase))
                    Fail("S.T.A.L.K.E.R. 2 is running. Close the game and run this again.");
            }
        }

        static string PickNet()
        {
            string n = opt.Net.ToLowerInvariant();
            if (n.Length > 0 && n != "steam" && n != "ip") Fail("--net must be steam or ip");
            if (n.Length == 0 && opt.Yes) n = "steam";
            while (n != "steam" && n != "ip")
            {
                Say("How do the PCs connect? NOLA takes up to five players: the host plus four friends.");
                Say("1) Steam friends (default): the host invites friends in game; no IP, VPN or firewall rule");
                Say("2) IP address: LAN or VPN; you type the other PC's IP");
                string c = Ask("Choose 1 or 2 (Enter = 1): ").ToLowerInvariant();
                if (c.Length == 0 || c == "1" || c == "steam") n = "steam";
                else if (c == "2" || c == "ip") n = "ip";
            }
            if (n == "steam" && opt.Peer.Length > 0) Fail("--peer is only used with --net ip");
            return n;
        }

        static string PickPeer()
        {
            string p = opt.Peer;
            while (!Regex.IsMatch(p, @"^[0-9A-Za-z\.\-:]+$"))
            {
                p = Ask("IP address of the OTHER PC (LAN or VPN, for example 26.x.x.x for Radmin): ");
            }
            return p;
        }

        static string PickGame()
        {
            string g = opt.Game;
            if (g.Length == 0) g = FindGame();
            if (g.Length > 0) Say("Game folder: " + g);
            while (!File.Exists(Path.Combine(g, @"Stalker2\Binaries\Win64\" + ExeName)))
            {
                if (g.Length > 0) Say("Not a " + GameFolderName + " folder: " + g);
                g = Ask("Paste the full path of the '" + GameFolderName + "' folder: ").Trim('"');
                if (g.Length == 0) Fail("no game folder");
            }
            return g;
        }

        static int Install()
        {
            string game = PickGame();
            CheckRunning(game);
            if (opt.Role.Length > 0) Say("Note: --role is no longer used. Every PC gets the same install; pick Host co-op or Join co-op in the game's main menu.");
            string net = PickNet();
            string peer = net == "ip" ? PickPeer() : "";
            string bridge = opt.Bridge.Length > 0 ? opt.Bridge : @"C:\nola_bridge";
            string w64 = Win64(game);
            string ue = Path.Combine(w64, "ue4ss");
            string mods = Path.Combine(game, @"Stalker2\Content\Paks\~mods");

            if (File.Exists(Path.Combine(OldBackupDir(game), "state.txt")))
            {
                Say("An install from before the NOLA rename was found: restoring the original files first.");
                Uninstall(game, OldBackupDir(game));
            }
            if (File.Exists(Path.Combine(BackupDir(game), "state.txt")))
            {
                Say("An earlier NOLA install was found: restoring the original files first.");
                Uninstall(game, BackupDir(game));
            }

            Say("");
            if (net == "steam") Say("Connection: Steam friends");
            else Say("Connection: IP   other PC: " + peer + "   listening on UDP " + HostPort + " (when hosting) and " + ClientPort + " (when joining)");
            if (!opt.Yes && Ask("Install now? (y/n): ").ToLowerInvariant() != "y") { Say("Cancelled, nothing changed."); return 1; }

            journal = new Journal(BackupDir(game));
            journal.Begin();
            journal.Meta("version", version);
            journal.Meta("net", net);
            if (net == "ip") journal.Meta("peer", peer);
            journal.Meta("game", game);
            journal.Meta("bridge", bridge);

            PutPayload("dwmapi.dll", Path.Combine(w64, "dwmapi.dll"));
            PutPayload("ue4ss/UE4SS.dll", Path.Combine(ue, "UE4SS.dll"));
            PutPayload("ue4ss/LICENSE", Path.Combine(ue, "LICENSE"));
            PutPayload("ue4ss/VTableLayout.ini", Path.Combine(ue, "VTableLayout.ini"));
            foreach (string f in PayloadFiles("ue4ss/UE4SS_Signatures/"))
                PutPayload(f, Path.Combine(ue, f.Substring(6).Replace('/', '\\')));
            string ini = Path.Combine(ue, "UE4SS-settings.ini");
            if (File.Exists(ini)) PatchIni(ini);
            else { PutPayload("ue4ss/UE4SS-settings.ini", ini); Say("[ok] UE4SS-settings.ini (stock) installed"); }
            Say("[ok] UE4SS loader installed");

            foreach (string oldMod in new string[] { "S2CoopAlife", "S2CoopNative", "NolaAlife" })
            {
                string oldDir = Path.Combine(ue, @"Mods\" + oldMod);
                if (Directory.Exists(oldDir)) { RemoveTree(oldDir); Say("[ok] old " + oldMod + " mod removed (kept in the backup)"); }
            }
            PutPayload("ue4ss/Mods/NolaNative/dlls/main.dll", Path.Combine(ue, @"Mods\NolaNative\dlls\main.dll"));
            PutPayload("ue4ss/Mods/NolaNative/assets/nola_title.png", Path.Combine(ue, @"Mods\NolaNative\assets\nola_title.png"));
            MergeModsTxt(Path.Combine(ue, @"Mods\mods.txt"));
            Say("[ok] NolaNative installed and enabled in mods.txt");

            int old = 0;
            if (Directory.Exists(mods))
                foreach (string pat in new string[] { "zzzzzz_S2Coop*", "zzzzzz_Nola*" })
                    foreach (string f in Directory.GetFiles(mods, pat)) { Remove(f); old++; }
            PutPayload("paks/zzzzzz_Nola_P.pak", Path.Combine(mods, "zzzzzz_Nola_P.pak"));
            Say("[ok] pak installed (" + old + " old NOLA paks removed, kept in the backup)");

            string oldRole = Path.Combine(bridge, "role.txt");
            if (File.Exists(oldRole)) { Remove(oldRole); Say("[ok] old role.txt removed (the role is picked in the main menu now)"); }
            WriteFlags(Path.Combine(bridge, "flags.txt"), net, peer);
            Say("[ok] settings: " + bridge + "\\flags.txt net=" + net + (net == "ip" ? ", netpeer=" + peer : ""));

            if (net == "ip") FirewallAdd(Path.Combine(w64, ExeName), "co-op", HostPort + "," + ClientPort);
            else Say("[ok] firewall: no rule needed for Steam friends, none added");

            Say("");
            Say("Done. Start the game. In the main menu pick Host co-op (then load your world) or Join co-op (the host's world follows).");
            if (net == "steam")
                Say("Steam friends: the host presses Home in game to invite up to four friends; each friend accepts the invite or uses Join Game in the Steam friends list.");
            Say("To remove everything and restore your files: run this installer again and choose Uninstall.");
            return 0;
        }

        static int Uninstall(string game, string bdir)
        {
            string state = Path.Combine(bdir, "state.txt");
            if (!File.Exists(state)) return 1;
            List<string[]> rows = Journal.Read(state);
            int restored = 0, removed = 0;
            for (int i = rows.Count - 1; i >= 0; i--)
            {
                string[] r = rows[i];
                if (r[0] == "F")
                {
                    string target = r[1];
                    if (File.Exists(target)) { File.SetAttributes(target, FileAttributes.Normal); File.Delete(target); removed++; }
                    if (r[2] != "-")
                    {
                        EnsureDirPlain(Path.GetDirectoryName(target));
                        File.Copy(Path.Combine(bdir, "files", r[2]), target, true);
                        restored++;
                    }
                }
                else if (r[0] == "D")
                {
                    if (Directory.Exists(r[1]) && Directory.GetFileSystemEntries(r[1]).Length == 0) Directory.Delete(r[1]);
                }
                else if (r[0] == "W")
                {
                    FirewallRemove(r[1]);
                }
            }
            Directory.Delete(bdir, true);
            Say("[ok] uninstall done: " + restored + " files restored, " + removed + " files removed");
            return 0;
        }

        static void EnsureDirPlain(string dir)
        {
            if (!Directory.Exists(dir)) Directory.CreateDirectory(dir);
        }

        static void Parse(string[] a)
        {
            for (int i = 0; i < a.Length; i++)
            {
                string s = a[i];
                string next = i + 1 < a.Length ? a[i + 1] : "";
                switch (s.ToLowerInvariant())
                {
                    case "install": case "uninstall": case "find": opt.Verb = s.ToLowerInvariant(); break;
                    case "--role": opt.Role = next; i++; break;
                    case "--peer": opt.Peer = next; i++; break;
                    case "--net": opt.Net = next; i++; break;
                    case "--game": opt.Game = next.TrimEnd('\\'); i++; break;
                    case "--bridge": opt.Bridge = next.TrimEnd('\\'); i++; break;
                    case "--steam": opt.Steam = next.TrimEnd('\\'); i++; break;
                    case "--fwname": opt.FwName = next; i++; break;
                    case "--nofw": opt.NoFw = true; break;
                    case "--yes": opt.Yes = true; break;
                    default: Fail("unknown argument: " + s); break;
                }
            }
        }

        static int Run(string[] args)
        {
            Parse(args);
            LoadPayload();
            Say("=== NOLA setup " + version + " ===");
            if (opt.Verb == "find")
            {
                string g = FindGame();
                Say(g.Length > 0 ? "Game folder: " + g : "Game folder not found");
                return g.Length > 0 ? 0 : 1;
            }
            if (opt.Verb.Length == 0)
            {
                Say("1) Install");
                Say("2) Uninstall (restore the original files)");
                string c = Ask("Choose 1 or 2: ");
                opt.Verb = c == "2" ? "uninstall" : "install";
            }
            if (opt.Verb == "uninstall")
            {
                string game = PickGame();
                CheckRunning(game);
                int rc1 = File.Exists(Path.Combine(OldBackupDir(game), "state.txt")) ? Uninstall(game, OldBackupDir(game)) : 1;
                int rc2 = File.Exists(Path.Combine(BackupDir(game), "state.txt")) ? Uninstall(game, BackupDir(game)) : 1;
                if (rc1 != 0 && rc2 != 0) { Say("No NOLA install journal found in " + Win64(game) + ": nothing to uninstall."); return 1; }
                return 0;
            }
            return Install();
        }

        static int Main(string[] args)
        {
            int rc = 1;
            try { rc = Run(args); }
            catch (InvalidOperationException) { rc = 2; }
            catch (Exception e) { Console.WriteLine("ERROR: " + e.Message); rc = 3; }
            bool interactive = args.Length == 0;
            if (interactive) { Console.WriteLine(); Console.Write("Press Enter to close"); Console.ReadLine(); }
            return rc;
        }
    }
}
