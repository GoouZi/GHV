#!/usr/bin/env python3
from __future__ import annotations
import os, subprocess, sys, threading
import tkinter as tk
from tkinter import filedialog, messagebox, ttk
from ghv.version import CURRENT_GHVC, GHV_STUDIO_VERSION, PROJECT_STATUS

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
        self.title(f'GHV Studio {GHV_STUDIO_VERSION} ({PROJECT_STATUS})')
        self.geometry('860x650')
        self.minsize(760, 560)
        self.infile = tk.StringVar()
        self.outfile = tk.StringVar()
        self.preset = tk.StringVar(value='balanced')
        self.audio_mode = tk.StringVar(value='hq')
        self.threads = tk.StringVar(value='Auto')
        self.play_buffer = tk.StringVar(value='Auto')
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

        opt = ttk.LabelFrame(self, text=f'GHVC{CURRENT_GHVC} encoding')
        opt.pack(fill='x', **pad)
        ttk.Label(opt, text='Preset').grid(row=0, column=0, sticky='w', padx=8, pady=8)
        ttk.Combobox(opt, textvariable=self.preset, values=['veryfast', 'fast', 'compact', 'balanced', 'quality'], state='readonly', width=14).grid(row=0, column=1, sticky='w', padx=8, pady=8)
        ttk.Label(opt, text='Balanced: default. Compact: smaller files. Very Fast: quickest tests.').grid(row=0, column=2, sticky='w', padx=8)
        ttk.Label(opt, text='Audio').grid(row=1, column=0, sticky='w', padx=8, pady=(0, 8))
        ttk.Combobox(opt, textvariable=self.audio_mode, values=['hq', 'compact'], state='readonly', width=14).grid(row=1, column=1, sticky='w', padx=8, pady=(0, 8))
        ttk.Label(opt, text='GHAC1 HQ recommended for music').grid(row=1, column=2, sticky='w', padx=8, pady=(0, 8))
        ttk.Label(opt, text='Native threads').grid(row=2, column=0, sticky='w', padx=8, pady=(0, 8))
        ttk.Combobox(opt, textvariable=self.threads, values=['Auto','2','4','6','8','12','16'], state='readonly', width=14).grid(row=2, column=1, sticky='w', padx=8, pady=(0, 8))
        ttk.Label(opt, text='Auto normally uses all OpenMP threads available.').grid(row=2, column=2, sticky='w', padx=8, pady=(0, 8))
        ttk.Label(opt, text='Playback buffer').grid(row=3, column=0, sticky='w', padx=8, pady=(0, 8))
        ttk.Combobox(opt, textvariable=self.play_buffer, values=['Auto','8','12','16','24','32'], state='readonly', width=14).grid(row=3, column=1, sticky='w', padx=8, pady=(0, 8))
        ttk.Label(opt, text='Auto targets ~64 MiB decoded cushion; 1080p usually gets ~20 frames.').grid(row=3, column=2, sticky='w', padx=8, pady=(0, 8))
        opt.columnconfigure(2, weight=1)

        buttons = ttk.Frame(self)
        buttons.pack(fill='x', **pad)
        self.enc_btn = ttk.Button(buttons, text='Convert to GHV', command=self.convert)
        self.enc_btn.pack(side='left')
        self.cancel_btn = ttk.Button(buttons, text='Cancel', command=self.cancel, state='disabled')
        self.cancel_btn.pack(side='left', padx=8)
        ttk.Button(buttons, text='Play GHV…', command=self.play_file).pack(side='left', padx=8)
        ttk.Button(buttons, text='Inspect…', command=self.info_file).pack(side='left')
        ttk.Button(buttons, text='Verify…', command=self.verify_file).pack(side='left', padx=8)
        ttk.Button(buttons, text='Diagnose…', command=self.diagnose_file).pack(side='left')
        ttk.Button(buttons, text='Repair Index…', command=self.repair_file).pack(side='left', padx=8)
        ttk.Button(buttons, text='Build Native Core', command=self.build_native).pack(side='right')

        self.bar = ttk.Progressbar(self, maximum=100, mode='determinate')
        self.bar.pack(fill='x', padx=12, pady=(4, 2))
        stat = ttk.Frame(self)
        stat.pack(fill='x', padx=12)
        ttk.Label(stat, textvariable=self.status).pack(side='left')
        ttk.Label(stat, textvariable=self.speed).pack(side='right')
        self.log = tk.Text(self, height=18, wrap='word')
        self.log.pack(fill='both', expand=True, padx=10, pady=8)
        self.log.insert('end', f'GHV Studio {GHV_STUDIO_VERSION} — GHVC{CURRENT_GHVC} beta encoder\n')
        self.log.insert('end', 'Native C++ encoder/decoder is strongly recommended; Python remains a compatibility path.\n')

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
        if self.threads.get() != 'Auto':
            cmd += ['--threads', self.threads.get()]
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
            cmd = [sys.executable, os.path.join(ROOT, 'ghvplay.py'), p]
            if self.play_buffer.get() != 'Auto':
                cmd += ['--buffer', self.play_buffer.get()]
            subprocess.Popen(cmd, cwd=ROOT)

    def info_file(self):
        p = filedialog.askopenfilename(title='Inspect GHV', filetypes=[('GHV video', '*.ghv'), ('All files', '*.*')])
        if not p:
            return
        try:
            text = subprocess.check_output([sys.executable, os.path.join(ROOT, 'ghvinfo.py'), p], cwd=ROOT, text=True, encoding='utf-8', errors='replace')
            messagebox.showinfo('GHV info', text)
        except Exception as e:
            messagebox.showerror('GHV', str(e))


    def verify_file(self):
        p = filedialog.askopenfilename(title='Verify GHV', filetypes=[('GHV video', '*.ghv'), ('All files', '*.*')])
        if not p:
            return
        self.status.set('Verifying…')
        def worker():
            try:
                text = subprocess.check_output([sys.executable, os.path.join(ROOT, 'ghvverify.py'), p], cwd=ROOT, text=True, encoding='utf-8', errors='replace', stderr=subprocess.STDOUT)
                self.after(0, messagebox.showinfo, 'GHV Verify', text.strip())
                self.after(0, self.status.set, 'Verification passed')
            except subprocess.CalledProcessError as e:
                self.after(0, messagebox.showerror, 'GHV Verify', e.output or str(e))
                self.after(0, self.status.set, 'Verification failed')
            except Exception as e:
                self.after(0, messagebox.showerror, 'GHV Verify', str(e))
        threading.Thread(target=worker, daemon=True).start()

    def diagnose_file(self):
        p = filedialog.askopenfilename(title='Diagnose GHV playback', filetypes=[('GHV video', '*.ghv'), ('All files', '*.*')])
        if not p:
            return
        self.status.set('Diagnosing playback…')
        def worker():
            try:
                text = subprocess.check_output([sys.executable, os.path.join(ROOT, 'ghvdoctor.py'), p], cwd=ROOT, text=True, encoding='utf-8', errors='replace', stderr=subprocess.STDOUT)
                self.after(0, messagebox.showinfo, 'GHV Playback Diagnosis', text.strip())
                self.after(0, self.status.set, 'Diagnosis complete')
            except subprocess.CalledProcessError as e:
                self.after(0, messagebox.showerror, 'GHV Playback Diagnosis', e.output or str(e))
                self.after(0, self.status.set, 'Diagnosis failed')
            except Exception as e:
                self.after(0, messagebox.showerror, 'GHV Playback Diagnosis', str(e))
        threading.Thread(target=worker, daemon=True).start()

    def repair_file(self):
        p = filedialog.askopenfilename(title='Repair GHV index', filetypes=[('GHV video', '*.ghv'), ('All files', '*.*')])
        if not p:
            return
        out = os.path.splitext(p)[0] + '_repaired.ghv'
        self.status.set('Repairing index…')
        def worker():
            try:
                text = subprocess.check_output([sys.executable, os.path.join(ROOT, 'ghvrepair.py'), p, out],
                                               cwd=ROOT, text=True, encoding='utf-8', errors='replace',
                                               stderr=subprocess.STDOUT)
                self.after(0, messagebox.showinfo, 'GHV Repair', text.strip())
                self.after(0, self.status.set, 'Repair complete')
            except subprocess.CalledProcessError as e:
                self.after(0, messagebox.showerror, 'GHV Repair', e.output or str(e))
                self.after(0, self.status.set, 'Repair failed')
            except Exception as e:
                self.after(0, messagebox.showerror, 'GHV Repair', str(e))
        threading.Thread(target=worker, daemon=True).start()

    def build_native(self):
        if os.name != 'nt':
            return messagebox.showinfo('GHV', 'On Linux/macOS, run native/build_linux.sh / build_macos.sh. This builds ghvcore + ghvdecode.')
        subprocess.Popen(['cmd', '/c', os.path.join(ROOT, 'native', 'build_windows.bat')], cwd=ROOT)


if __name__ == '__main__':
    App().mainloop()
