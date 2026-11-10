import tkinter as tk
from tkinter import scrolledtext, filedialog, simpledialog
import subprocess
import os
import threading
import sys
import json
import urllib.request

class VLCMaster:
    def __init__(self, root):
        self.root = root
        self.root.title("VLC Master by Popov ©2026")
        self.root.configure(bg="#f4f4f4")
        
        # Avant-plan forcé par défaut
        self.root.attributes('-topmost', True)
        
        self.script_dir = os.path.dirname(os.path.abspath(sys.argv[0]))
        self.config_file = os.path.join(self.script_dir, "vlc_config.json")

        # --- VARIABLES ---
        self.vlc_path = tk.StringVar(value=r"C:\Program Files\VideoLAN\VLC\vlc.exe")
        self.stream_url = tk.StringVar(value="http://192.168.1.90/lineup.m3u")
        self.vlc_password = "pass"
        self.full_screen_mode = tk.BooleanVar(value=True)
        self.view_mode = 0  
        self.current_idx = 1
        self.last_volume = "256" # son a 100%
        self.is_muted = False
        self.channel_names = {str(i+1): f"CH {i+1}" for i in range(28)}
        
        self.load_config()
        self.setup_ui()
        self.setup_shortcuts()
        self.apply_view()
        self.log("### SYSTÈME PRÊT ###")

    def setup_shortcuts(self):
        self.root.bind("<Left>", lambda e: self.change_channel(-1))
        self.root.bind("<Right>", lambda e: self.change_channel(1))

    def setup_ui(self):
        # --- 1. BARRE CONFIG (Mode Normal uniquement) ---
        self.top_frame = tk.Frame(self.root, bg="#f4f4f4")
        tk.Button(self.top_frame, text="📁 VLC", command=self.browse_vlc, width=5).pack(side=tk.LEFT)
        tk.Entry(self.top_frame, textvariable=self.stream_url, width=30).pack(side=tk.LEFT, padx=5)
        tk.Button(self.top_frame, text="🚀 LANCER", command=self.start_vlc, bg="#ccffcc", width=10).pack(side=tk.LEFT, padx=5)

        # --- 2. BARRE COMMANDE (Présente dans TOUS les modes) ---
        self.nav_frame = tk.Frame(self.root, bg="#f4f4f4")
        tk.Button(self.nav_frame, text="◀ CH -", command=lambda: self.change_channel(-1), bg="#ffcccc", width=7).pack(side=tk.LEFT, padx=2)
        tk.Button(self.nav_frame, text="CH + ▶", command=lambda: self.change_channel(1), bg="#ccffcc", width=7).pack(side=tk.LEFT, padx=2)
        
        tk.Button(self.nav_frame, text="PAUSE", command=self.remote_pause, bg="#eeeeee", width=6).pack(side=tk.LEFT, padx=5)
        self.btn_fs = tk.Button(self.nav_frame, text="FullScreen", command=self.remote_fullscreen, width=8)
        self.btn_fs.pack(side=tk.LEFT, padx=2)
        
        tk.Button(self.nav_frame, text="VOL -", command=lambda: self.set_volume("-10"), width=5).pack(side=tk.LEFT, padx=5)
        self.btn_mute = tk.Button(self.nav_frame, text="MUTE", command=self.toggle_mute, bg="#f8d7da", width=5)
        self.btn_mute.pack(side=tk.LEFT, padx=2)
        tk.Button(self.nav_frame, text="VOL +", command=lambda: self.set_volume("+10"), width=5).pack(side=tk.LEFT, padx=2)
        
        # Bouton + Jaune (Switch intelligent)
        tk.Button(self.nav_frame, text="-", command=self.smart_view_switch, bg="#ffffaa", width=3, font=("Arial", 9, "bold")).pack(side=tk.LEFT, padx=10)

        # --- 3. GRILLE DES CHAINES (Normal, Moyen, Geant) ---
        self.grid_container = tk.Frame(self.root, bg="#cccccc", bd=1, relief="sunken")
        self.btns = {}
        for i in range(1, 29):
            btn = tk.Button(self.grid_container, text=self.channel_names.get(str(i)), width=12, height=2)
            btn.grid(row=(i-1)//7, column=(i-1)%7, padx=2, pady=2, sticky="nsew")
            btn.bind("<Button-1>", lambda e, idx=i: self.goto_channel(idx))
            btn.bind("<Button-3>", lambda e, idx=i: self.rename_channel(idx))
            self.btns[str(i)] = btn

        # --- 4. CONSOLE MONITORING (Normal, Geant) ---
        self.console_label = tk.Label(self.root, text="MONITORING :", bg="#f4f4f4", font=("Arial", 9, "bold"))
        self.console = scrolledtext.ScrolledText(self.root, width=110, height=15, bg="black", fg="#00ff00", font=("Consolas", 10))
        self.console.config(state='disabled')
        
        self.update_fs_ui()

    def smart_view_switch(self):
        """ Logique : Normal (0) -> Moyen (1) -> Nano (2) -> Geant (3) -> Normal """
        self.view_mode = (self.view_mode + 1) % 4
        self.apply_view()

    def apply_view(self):
        # On nettoie l'affichage
        self.top_frame.pack_forget()
        self.nav_frame.pack_forget()
        self.grid_container.pack_forget()
        self.console_label.pack_forget()
        self.console.pack_forget()
        
        if self.view_mode == 0: # NORMAL : Tout
            self.root.geometry("725x550")
            self.top_frame.pack(pady=10)
            self.nav_frame.pack(pady=5)
            self.grid_container.pack(pady=5, padx=20, fill="x")
            self.console_label.pack(anchor="w", padx=20); self.console.pack(pady=10, padx=20)
            
        elif self.view_mode == 1: # MOYEN : Chaines + Commandes
            self.root.geometry("725x260")
            self.nav_frame.pack(pady=10)
            self.grid_container.pack(pady=5, padx=20, fill="x")
            
        elif self.view_mode == 2: # NANO : Commandes uniquement
            self.root.geometry("480x40")
            self.nav_frame.pack(pady=10)
            
        elif self.view_mode == 3: # GEANT : Chaines + Commandes + Monitoring
            self.root.geometry("725x550")
            self.nav_frame.pack(pady=10)
            self.grid_container.pack(pady=5, padx=20, fill="x")
            self.console_label.pack(anchor="w", padx=20); self.console.pack(pady=10, padx=20)

    def set_volume(self, val):
        self.is_muted = False
        self.btn_mute.config(bg="#f8d7da", text="MUTE", fg="black")
        url = f"http://127.0.0.1:8080/requests/status.xml?command=volume&val={val}"
        threading.Thread(target=self.send_vlc_req, args=(url, f"Volume {val}"), daemon=True).start()

    def toggle_mute(self):
        if not self.is_muted:
            self.is_muted = True
            self.btn_mute.config(bg="#ff0000", text="UNMUTE", fg="white")
            url = "http://127.0.0.1:8080/requests/status.xml?command=volume&val=0"
        else:
            self.is_muted = False
            self.btn_mute.config(bg="#f8d7da", text="MUTE", fg="black")
            url = f"http://127.0.0.1:8080/requests/status.xml?command=volume&val={self.last_volume}"
        threading.Thread(target=self.send_vlc_req, args=(url, "MUTE TOGGLE"), daemon=True).start()

    def remote_pause(self):
        url = "http://127.0.0.1:8080/requests/status.xml?command=pl_pause"
        threading.Thread(target=self.send_vlc_req, args=(url, "PAUSE/PLAY"), daemon=True).start()

    def remote_fullscreen(self):
        new_state = not self.full_screen_mode.get()
        self.full_screen_mode.set(new_state)
        self.update_fs_ui()
        self.save_config()
        url = "http://127.0.0.1:8080/requests/status.xml?command=fullscreen"
        threading.Thread(target=self.send_vlc_req, args=(url, f"FS {new_state}"), daemon=True).start()

    def send_vlc_req(self, url, desc):
        try:
            passman = urllib.request.HTTPPasswordMgrWithDefaultRealm()
            passman.add_password(None, url, "", self.vlc_password)
            opener = urllib.request.build_opener(urllib.request.HTTPBasicAuthHandler(passman))
            opener.open(url, timeout=2)
            self.log(f"✅ OK : {desc}")
        except: self.log(f"❌ Erreur : {desc}")

    def change_channel(self, step):
        self.current_idx += step
        if self.current_idx > 28: self.current_idx = 1
        if self.current_idx < 1: self.current_idx = 28
        self.goto_channel(self.current_idx)

    def goto_channel(self, idx):
        self.current_idx = idx
        url = f"http://127.0.0.1:8080/requests/status.xml?command=pl_play&id={idx + 3}"
        threading.Thread(target=self.send_vlc_req, args=(url, self.channel_names.get(str(idx), idx)), daemon=True).start()

    def start_vlc(self):
        self.save_config()
        vlc_exe = self.vlc_path.get()
        if not os.path.exists(vlc_exe): return
        cmd = [vlc_exe, self.stream_url.get(), "--one-instance", "--extraintf", "http", "--http-password", self.vlc_password]
        if self.full_screen_mode.get(): cmd.extend(["--fullscreen", "--no-video-title-show"])
        subprocess.Popen(cmd, cwd=os.path.dirname(vlc_exe))

    def log(self, text):
        self.console.config(state='normal'); self.console.insert(tk.END, f"> {text}\n"); self.console.see(tk.END); self.console.config(state='disabled'); self.root.update_idletasks()

    def update_fs_ui(self):
        self.btn_fs.config(bg="#add8e6" if self.full_screen_mode.get() else "#e1e1e1")

    def save_config(self):
        try:
            data = {"vlc_path": self.vlc_path.get(), "stream_url": self.stream_url.get(), "names": self.channel_names, "fullscreen": self.full_screen_mode.get()}
            with open(self.config_file, "w") as f: json.dump(data, f, indent=4)
        except: pass

    def load_config(self):
        if os.path.exists(self.config_file):
            try:
                with open(self.config_file, "r") as f:
                    data = json.load(f)
                    self.vlc_path.set(data.get("vlc_path", self.vlc_path.get()))
                    self.stream_url.set(data.get("stream_url", self.stream_url.get()))
                    self.full_screen_mode.set(data.get("fullscreen", True))
                    self.channel_names.update(data.get("names", {}))
            except: pass

    def browse_vlc(self):
        self.root.attributes('-topmost', False) # On désactive l'avant-plan
        p = filedialog.askopenfilename()
        self.root.attributes('-topmost', True) # On remet l'avant-plan
        if p: self.vlc_path.set(p); self.save_config()

    def rename_channel(self, idx):
        self.root.attributes('-topmost', False) # Indispensable pour voir le popup
        n = simpledialog.askstring("Editer", "Nom :", initialvalue=self.channel_names.get(str(idx), ""))
        self.root.attributes('-topmost', True) # On remet l'avant-plan
        if n: self.channel_names[str(idx)] = n; self.btns[str(idx)].config(text=n); self.save_config()

if __name__ == "__main__":
    root = tk.Tk(); app = VLCMaster(root); root.mainloop()