#!/usr/bin/env python3
from __future__ import annotations
import os, subprocess, sys, threading
import tkinter as tk
from tkinter import filedialog, messagebox, ttk
from ghv.version import CURRENT_GHAC, GHA_STUDIO_VERSION, PROJECT_STATUS

ROOT = os.path.dirname(os.path.abspath(__file__))


class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title(f'GHA Studio {GHA_STUDIO_VERSION} ({PROJECT_STATUS})')
        self.geometry('760x520')
        self.minsize(680, 470)
        self.infile = tk.StringVar()
        self.outfile = tk.StringVar()
        self.mode = tk.StringVar(value='hq')
        self.status = tk.StringVar(value='Ready')
        self.proc = None
        self._build()

    def _build(self):
        pad = {'padx': 10, 'pady': 6}
        top = ttk.Frame(self); top.pack(fill='x', **pad)
        ttk.Label(top, text='Input audio or video').grid(row=0, column=0, sticky='w')
        ttk.Entry(top, textvariable=self.infile).grid(row=1, column=0, sticky='ew', padx=(0, 6))
        ttk.Button(top, text='Browse…', command=self.pick_input).grid(row=1, column=1)
        ttk.Label(top, text='Output .gha').grid(row=2, column=0, sticky='w', pady=(8, 0))
        ttk.Entry(top, textvariable=self.outfile).grid(row=3, column=0, sticky='ew', padx=(0, 6))
        ttk.Button(top, text='Browse…', command=self.pick_output).grid(row=3, column=1)
        top.columnconfigure(0, weight=1)

        opt = ttk.LabelFrame(self, text=f'GHAC{CURRENT_GHAC}')
        opt.pack(fill='x', **pad)
        ttk.Label(opt, text='Mode').grid(row=0, column=0, padx=8, pady=8, sticky='w')
        ttk.Combobox(opt, textvariable=self.mode, values=['hq', 'compact'], state='readonly', width=14).grid(row=0, column=1, padx=8, pady=8, sticky='w')
        ttk.Label(opt, text='HQ is recommended for music; Compact is smaller.').grid(row=0, column=2, padx=8, sticky='w')
        opt.columnconfigure(2, weight=1)

        btn = ttk.Frame(self); btn.pack(fill='x', **pad)
        self.enc_btn = ttk.Button(btn, text='Convert to GHA', command=self.convert); self.enc_btn.pack(side='left')
        ttk.Button(btn, text='Play GHA…', command=self.play).pack(side='left', padx=8)
        ttk.Button(btn, text='Inspect…', command=self.info).pack(side='left')
        ttk.Label(btn, textvariable=self.status).pack(side='right')

        self.log = tk.Text(self, height=15, wrap='word')
        self.log.pack(fill='both', expand=True, padx=10, pady=8)
        self.log.insert('end', 'GHA 0.2 — .gha container + GHAC1 custom audio codec\n')

    def pick_input(self):
        p = filedialog.askopenfilename(title='Choose audio/video', filetypes=[('Media files', '*.wav *.mp3 *.flac *.aac *.m4a *.ogg *.opus *.mp4 *.mkv *.mov *.webm'), ('All files', '*.*')])
        if p:
            self.infile.set(p)
            self.outfile.set(os.path.splitext(p)[0] + '.gha')

    def pick_output(self):
        p = filedialog.asksaveasfilename(title='Save GHA', defaultextension='.gha', filetypes=[('GHA audio', '*.gha')])
        if p:
            self.outfile.set(p)

    def append(self, s):
        self.log.insert('end', s); self.log.see('end')

    def convert(self):
        inp, out = self.infile.get().strip(), self.outfile.get().strip()
        if not inp or not os.path.isfile(inp):
            return messagebox.showwarning('GHA', 'Choose a valid input file first.')
        if not out:
            out = os.path.splitext(inp)[0] + '.gha'; self.outfile.set(out)
        cmd = [sys.executable, os.path.join(ROOT, 'ghaenc.py'), inp, out, '--mode', self.mode.get()]
        self.status.set('Encoding…'); self.enc_btn.state(['disabled'])
        self.append('\n$ ' + subprocess.list2cmdline(cmd) + '\n')

        def worker():
            try:
                self.proc = subprocess.Popen(cmd, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding='utf-8', errors='replace')
                for line in self.proc.stdout:
                    self.after(0, self.append, line)
                rc = self.proc.wait()
                self.after(0, self.status.set, 'Done' if rc == 0 else f'Failed ({rc})')
            except Exception as e:
                self.after(0, messagebox.showerror, 'GHA', str(e))
            finally:
                self.proc = None; self.after(0, lambda: self.enc_btn.state(['!disabled']))
        threading.Thread(target=worker, daemon=True).start()

    def play(self):
        p = filedialog.askopenfilename(title='Play GHA', filetypes=[('GHA audio', '*.gha'), ('All files', '*.*')])
        if p:
            subprocess.Popen([sys.executable, os.path.join(ROOT, 'ghaplay.py'), p], cwd=ROOT)

    def info(self):
        p = filedialog.askopenfilename(title='Inspect GHA', filetypes=[('GHA audio', '*.gha'), ('All files', '*.*')])
        if not p:
            return
        try:
            text = subprocess.check_output([sys.executable, os.path.join(ROOT, 'ghainfo.py'), p], cwd=ROOT, text=True, encoding='utf-8', errors='replace')
            messagebox.showinfo('GHA info', text)
        except Exception as e:
            messagebox.showerror('GHA', str(e))


if __name__ == '__main__':
    App().mainloop()
