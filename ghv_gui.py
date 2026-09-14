#!/usr/bin/env python3
from __future__ import annotations
import os, subprocess, sys, threading
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

ROOT = os.path.dirname(os.path.abspath(__file__))


def parse_progress(line: str):
    if not line.startswith('PROGRESS '):
        return None
    out = {}
    for token in line[len('PROGRESS '):].strip().split():
        if '=' in token:
            k, v = token.split('=', 1)
            out[k] = v
    return out


class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title('GHV Studio 0.4')
        self.geometry('860x650')
        self.minsize(760, 560)
        self.infile = tk.StringVar()
        self.outfile = tk.StringVar()
        self.preset = tk.StringVar(value='balanced')
        self.audio_mode = tk.StringVar(value='hq')
        self.status = tk.StringVar(value='Ready')
        self.speed = tk.StringVar(value='')
        self.proc = None
        self._build()

    def _build(self):
        pad = {'padx': 10, 'pady': 6}
        top = ttk.Frame(self)
        top.pack(fill='x', **pad)
        ttk.Label(top, text='Input video').grid(row=0, column=0, sticky='w')
        ttk.Entry(top, textvariable=self.infile).grid(row=1, column=0, sticky='ew', padx=(0, 6))
        ttk.Button(top, text='Browse…', command=self.pick_input).grid(row=1, column=1)
        ttk.Label(top, text='Output .ghv').grid(row=2, column=0, sticky='w', pady=(8, 0))
        ttk.Entry(top, textvariable=self.outfile).grid(row=3, column=0, sticky='ew', padx=(0, 6))
        ttk.Button(top, text='Browse…', command=self.pick_output).grid(row=3, column=1)
        top.columnconfigure(0, weight=1)

        opt = ttk.LabelFrame(self, text='GHVC3 encoding')
        opt.pack(fill='x', **pad)
        ttk.Label(opt, text='Preset').grid(row=0, column=0, sticky='w', padx=8, pady=8)
        ttk.Combobox(opt, textvariable=self.preset, values=['fast', 'balanced', 'quality'], state='readonly', width=14).grid(row=0, column=1, sticky='w', padx=8, pady=8)
        ttk.Label(opt, text='Balanced is recommended for first tests').grid(row=0, column=2, sticky='w', padx=8)
        ttk.Label(opt, text='Audio').grid(row=1, column=0, sticky='w', padx=8, pady=(0, 8))
        ttk.Combobox(opt, textvariable=self.audio_mode, values=['hq', 'compact'], state='readonly', width=14).grid(row=1, column=1, sticky='w', padx=8, pady=(0, 8))
        ttk.Label(opt, text='GHAC1 HQ recommended for music').grid(row=1, column=2, sticky='w', padx=8, pady=(0, 8))
        opt.columnconfigure(2, weight=1)

        buttons = ttk.Frame(self)
        buttons.pack(fill='x', **pad)
        self.enc_btn = ttk.Button(buttons, text='Convert to GHV', command=self.convert)
        self.enc_btn.pack(side='left')
        self.cancel_btn = ttk.Button(buttons, text='Cancel', command=self.cancel, state='disabled')
        self.cancel_btn.pack(side='left', padx=8)
        ttk.Button(buttons, text='Play GHV…', command=self.play_file).pack(side='left', padx=8)
        ttk.Button(buttons, text='Inspect…', command=self.info_file).pack(side='left')
        ttk.Button(buttons, text='Build Native Core', command=self.build_native).pack(side='right')

        self.bar = ttk.Progressbar(self, maximum=100, mode='determinate')
        self.bar.pack(fill='x', padx=12, pady=(4, 2))
        stat = ttk.Frame(self)
        stat.pack(fill='x', padx=12)
        ttk.Label(stat, textvariable=self.status).pack(side='left')
        ttk.Label(stat, textvariable=self.speed).pack(side='right')
        self.log = tk.Text(self, height=18, wrap='word')
        self.log.pack(fill='both', expand=True, padx=10, pady=8)
        self.log.insert('end', 'GHV 0.4 — new .ghv container + GHVC3 motion/bit-pack compression + GHAC1 audio\n')
        self.log.insert('end', 'Native C++ core is recommended for long videos; NumPy fallback remains available.\n')

    def pick_input(self):
        p = filedialog.askopenfilename(title='Choose input video', filetypes=[('Video files', '*.mp4 *.mov *.mkv *.avi *.webm *.ogv'), ('All files', '*.*')])
        if p:
            self.infile.set(p)
            self.outfile.set(os.path.splitext(p)[0] + '.ghv')

    def pick_output(self):
        p = filedialog.asksaveasfilename(title='Save GHV', defaultextension='.ghv', filetypes=[('GHV video', '*.ghv')])
        if p:
            self.outfile.set(p)

    def _append(self, s):
        self.log.insert('end', s)
        self.log.see('end')

    def _line(self, line):
        p = parse_progress(line)
        if p:
            try:
                pct = float(p.get('pct', 0))
                fps = float(p.get('fps', 0))
                eta = float(p.get('eta', -1))
                frame = p.get('frame', '?')
                total = p.get('total', '?')
                size = p.get('size_mib', '?')
                repeats = p.get('repeats')
                self.bar['value'] = pct
                eta_text = '?' if eta < 0 else f'{eta:.0f}s'
                self.status.set(f'Encoding {pct:.1f}%  ({frame}/{total})')
                extra = f'   repeat {repeats}' if repeats is not None else ''
                self.speed.set(f'{fps:.1f} FPS   ETA {eta_text}   {size} MiB{extra}')
            except Exception:
                self._append(line)
        else:
            self._append(line)

    def convert(self):
        inp, out = self.infile.get().strip(), self.outfile.get().strip()
        if not inp or not os.path.isfile(inp):
            return messagebox.showwarning('GHV', 'Choose a valid input video first.')
        if not out:
            out = os.path.splitext(inp)[0] + '.ghv'
            self.outfile.set(out)
        cmd = [sys.executable, os.path.join(ROOT, 'ghvenc.py'), inp, out,
               '--preset', self.preset.get(), '--audio-quality', self.audio_mode.get()]
        self.enc_btn.state(['disabled'])
        self.cancel_btn.state(['!disabled'])
        self.bar['value'] = 0
        self.status.set('Starting…')
        self.speed.set('')
        self._append('\n$ ' + subprocess.list2cmdline(cmd) + '\n')

        def worker():
            try:
                self.proc = subprocess.Popen(cmd, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                             text=True, encoding='utf-8', errors='replace', bufsize=1)
                for line in self.proc.stdout:
                    self.after(0, self._line, line)
                rc = self.proc.wait()
                final = 'Conversion complete' if rc == 0 else ('Cancelled' if rc in (-15, 1) else f'Failed (exit {rc})')
                self.after(0, self.status.set, final)
                if rc == 0:
                    self.after(0, self.bar.configure, {'value': 100})
            except Exception as e:
                self.after(0, messagebox.showerror, 'GHV', str(e))
            finally:
                self.proc = None
                self.after(0, lambda: self.enc_btn.state(['!disabled']))
                self.after(0, lambda: self.cancel_btn.state(['disabled']))

        threading.Thread(target=worker, daemon=True).start()

    def cancel(self):
        if self.proc and self.proc.poll() is None:
            try:
                self.proc.terminate()
                self.status.set('Cancelling…')
            except Exception:
                pass

    def play_file(self):
        p = filedialog.askopenfilename(title='Play GHV', filetypes=[('GHV video', '*.ghv'), ('All files', '*.*')])
        if p:
            subprocess.Popen([sys.executable, os.path.join(ROOT, 'ghvplay.py'), p], cwd=ROOT)

    def info_file(self):
        p = filedialog.askopenfilename(title='Inspect GHV', filetypes=[('GHV video', '*.ghv'), ('All files', '*.*')])
        if not p:
            return
        try:
            text = subprocess.check_output([sys.executable, os.path.join(ROOT, 'ghvinfo.py'), p], cwd=ROOT, text=True, encoding='utf-8', errors='replace')
            messagebox.showinfo('GHV info', text)
        except Exception as e:
            messagebox.showerror('GHV', str(e))

    def build_native(self):
        if os.name != 'nt':
            return messagebox.showinfo('GHV', 'On Linux/macOS, run native/build_linux.sh (or compile ghvcore.cpp with any C++17 compiler).')
        subprocess.Popen(['cmd', '/c', os.path.join(ROOT, 'build_native_windows.bat')], cwd=ROOT)


if __name__ == '__main__':
    App().mainloop()
