#!/usr/bin/env python3
"""
Stippling Generator for ESP32 Plotter
Converts images or text to stippling patterns and sends G-code to ESP32 via WiFi.
Includes full preview, contrast/brightness controls, dot spacing, and dithering.
"""

import requests
import time
import json
from PIL import Image, ImageDraw, ImageFont, ImageOps, ImageTk
import numpy as np
import tkinter as tk
from tkinter import filedialog, messagebox, ttk
import threading
import queue as thread_queue


# =====================================================================
# =======================  STIPPLING ENGINE  ==========================
# =====================================================================

class StipplingGenerator:
    def __init__(self, esp32_ip="192.168.1.100"):
        self.esp32_ip = esp32_ip
        self.base_url = f"http://{esp32_ip}"
        self.command_queue = thread_queue.Queue()

    # ------------------------------------------------------------------
    # Core networking helpers
    # ------------------------------------------------------------------

    def get_queue_status(self):
        try:
            r = requests.get(f"{self.base_url}/queuestatus", timeout=2)
            if r.status_code == 200:
                return r.json()
        except Exception:
            pass
        return None

    def send_command(self, cmd):
        try:
            r = requests.get(f"{self.base_url}/gcode", params={"cmd": cmd}, timeout=2)
            if r.status_code == 200:
                t = r.text.strip()
                return t == "ok"
        except Exception:
            pass
        return False

    def send_batch(self, gcode_lines):
        body = "\n".join(gcode_lines)
        try:
            r = requests.post(
                f"{self.base_url}/uploadgcode",
                data=body,
                headers={"Content-Type": "text/plain"},
                timeout=10
            )
            if r.status_code == 200:
                return r.json()
        except Exception:
            pass
        return None

    def wait_for_queue_space(self, required=1, max_wait=60):
        start = time.time()
        while time.time() - start < max_wait:
            status = self.get_queue_status()
            if status and status.get("free", 0) >= required:
                return True
            time.sleep(0.5)
        return False

    # ------------------------------------------------------------------
    # Work area resolver
    # ------------------------------------------------------------------

    def _get_work_area(self, work_area):
        if work_area is not None:
            return work_area

        try:
            r = requests.get(f"{self.base_url}/getworkarea", timeout=2)
            if r.status_code == 200:
                a = r.json()
                return (a["minX"], a["maxX"], a["minY"], a["maxY"])
        except Exception:
            pass

        return (0, 200, 0, 200)

    # ------------------------------------------------------------------
    # Core stipple pipeline: PIL image → dots + preview image
    # ------------------------------------------------------------------

    def _stippling_from_pil(
        self,
        img,
        dot_spacing_mm=1.0,
        invert=False,
        contrast=1.0,
        brightness=1.0,
        work_area=None,
    ):
        work_area = self._get_work_area(work_area)
        min_x, max_x, min_y, max_y = work_area
        width_mm = max_x - min_x
        height_mm = max_y - min_y

        dot_spacing_mm = max(dot_spacing_mm, 0.2)

        img = img.convert("L")
        img_w, img_h = img.size
        aspect = img_w / img_h if img_h else 1.0

        grid_w = max(1, int(width_mm / dot_spacing_mm))
        grid_h = max(1, int(height_mm / dot_spacing_mm))

        if grid_w / grid_h > aspect:
            target_h = grid_h
            target_w = int(target_h * aspect)
        else:
            target_w = grid_w
            target_h = int(target_w / aspect)

        target_w = max(1, target_w)
        target_h = max(1, target_h)
        img = img.resize((target_w, target_h), Image.Resampling.LANCZOS)

        arr = np.array(img, dtype=np.float32)
        arr = (arr - 128) * float(contrast) + 128
        arr *= float(brightness)
        arr = np.clip(arr, 0, 255).astype(np.uint8)

        if invert:
            arr = 255 - arr

        proc = Image.fromarray(arr, mode="L")

        bw = proc.convert("1", dither=Image.FLOYDSTEINBERG)
        bw_arr = np.array(bw)

        h, w = bw_arr.shape
        dots = []
        for y in range(h):
            for x in range(w):
                if bw_arr[y, x] == 0:
                    mm_x = min_x + (x / w) * width_mm
                    mm_y = min_y + (y / h) * height_mm
                    dots.append((mm_x, mm_y))

        return dots, work_area, bw

    # ------------------------------------------------------------------
    # Image file → stipple
    # ------------------------------------------------------------------

    def image_to_stippling(
        self,
        image_path,
        dot_spacing_mm=1.0,
        invert=False,
        contrast=1.0,
        brightness=1.0,
        work_area=None,
    ):
        orig = Image.open(image_path).convert("RGB")
        dots, area, bw = self._stippling_from_pil(
            orig,
            dot_spacing_mm=dot_spacing_mm,
            invert=invert,
            contrast=contrast,
            brightness=brightness,
            work_area=work_area,
        )
        return dots, area, bw, orig

    # ------------------------------------------------------------------
    # Text → stipple (render text to image first)
    # ------------------------------------------------------------------

    def text_to_stippling(
        self,
        text,
        font_size=40,
        dot_spacing_mm=1.0,
        invert=False,
        contrast=1.0,
        brightness=1.0,
        work_area=None,
    ):
        work_area = self._get_work_area(work_area)
        min_x, max_x, min_y, max_y = work_area
        width_mm = max_x - min_x
        height_mm = max_y - min_y

        img_w = max(1, int(width_mm * 10))
        img_h = max(1, int(height_mm * 10))
        img = Image.new("L", (img_w, img_h), 255)
        draw = ImageDraw.Draw(img)

        try:
            font = ImageFont.truetype(
                "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                font_size
            )
        except:
            try:
                font = ImageFont.truetype("arial.ttf", font_size)
            except:
                font = ImageFont.load_default()

        bbox = draw.textbbox((0, 0), text, font=font)
        tw = bbox[2] - bbox[0]
        th = bbox[3] - bbox[1]
        x = (img_w - tw) // 2
        y = (img_h - th) // 2
        draw.text((x, y), text, font=font, fill=0)

        dots, area, bw = self._stippling_from_pil(
            img,
            dot_spacing_mm=dot_spacing_mm,
            invert=invert,
            contrast=contrast,
            brightness=brightness,
            work_area=work_area,
        )

        return dots, area, bw, img.convert("RGB")

    # ------------------------------------------------------------------
    # Convert dots → G-code
    # ------------------------------------------------------------------

    def generate_gcode(self, dots):
        commands = []
        commands.append("G0 X0 Y0")
        commands.append("P0")  # pen up

        for x, y in dots:
            commands.append(f"G0 X{x:.2f} Y{y:.2f}")
            commands.append(f"D X{x:.2f} Y{y:.2f}")

        commands.append("P0")
        commands.append("G0 X0 Y0")
        return commands

    # ------------------------------------------------------------------
    # Upload pattern to ESP32
    # ------------------------------------------------------------------

    def send_pattern(self, dots, progress=None):
        gcode = self.generate_gcode(dots)

        result = self.send_batch(gcode)
        if result and result.get("failed", 0) == 0:
            if progress:
                progress(len(gcode), len(gcode))
            return True

        sent = 0
        for cmd in gcode:
            if not self.wait_for_queue_space(1):
                return False
            if self.send_command(cmd):
                sent += 1
                if progress:
                    progress(sent, len(gcode))
            else:
                return False
        return True


# =====================================================================
# ============================  GUI  ==================================
# =====================================================================

class StipplingGUI:
    def __init__(self):
        self.root = tk.Tk()
        self.root.title("ESP32 Plotter - Stippling Generator")
        self.root.geometry("1000x750")

        self.generator = None
        self.dots = []
        self.work_area = None

        self.orig_img = None
        self.stipple_img = None
        self.orig_photo = None
        self.stipple_photo = None

        self.setup_ui()

    # ------------------------------------------------------------------
    # UI Layout
    # ------------------------------------------------------------------

    def setup_ui(self):
        # Connection
        conn = ttk.LabelFrame(self.root, text="ESP32 Connection", padding=10)
        conn.pack(fill=tk.X, padx=10, pady=5)

        ttk.Label(conn, text="IP Address:").grid(row=0, column=0, sticky=tk.W)
        self.ip_entry = ttk.Entry(conn, width=20)
        self.ip_entry.insert(0, "192.168.1.100")
        self.ip_entry.grid(row=0, column=1, padx=5)
        ttk.Button(conn, text="Connect", command=self.connect).grid(row=0, column=2, padx=5)

        self.status_label = ttk.Label(conn, text="Not connected", foreground="red")
        self.status_label.grid(row=1, column=0, columnspan=3, sticky=tk.W)

        # Input buttons
        inp = ttk.LabelFrame(self.root, text="Input", padding=10)
        inp.pack(fill=tk.X, padx=10, pady=5)

        ttk.Button(inp, text="Load Image", command=self.load_image).pack(side=tk.LEFT, padx=5)
        ttk.Button(inp, text="Enter Text", command=self.text_input).pack(side=tk.LEFT, padx=5)

        # Settings
        settings = ttk.LabelFrame(self.root, text="Stippling Settings", padding=10)
        settings.pack(fill=tk.X, padx=10, pady=5)
        settings.columnconfigure(1, weight=1)

        # Dot spacing
        ttk.Label(settings, text="Dot Spacing (mm):").grid(row=0, column=0, sticky=tk.W)
        self.spacing_var = tk.DoubleVar(value=1.0)
        spacing_scale = ttk.Scale(settings, from_=0.5, to=3.0, variable=self.spacing_var, orient=tk.HORIZONTAL)
        spacing_scale.grid(row=0, column=1, sticky=tk.EW, padx=5)
        self.spacing_label = ttk.Label(settings, text="1.00")
        self.spacing_label.grid(row=0, column=2)
        spacing_scale.configure(command=lambda v: self.spacing_label.config(text=f"{float(v):.2f}"))

        # Contrast
        ttk.Label(settings, text="Contrast:").grid(row=1, column=0, sticky=tk.W)
        self.contrast_var = tk.DoubleVar(value=1.0)
        contrast = ttk.Scale(settings, from_=0.5, to=2.0, variable=self.contrast_var, orient=tk.HORIZONTAL)
        contrast.grid(row=1, column=1, sticky=tk.EW, padx=5)
        self.contrast_label = ttk.Label(settings, text="1.00")
        self.contrast_label.grid(row=1, column=2)
        contrast.configure(command=lambda v: self.contrast_label.config(text=f"{float(v):.2f}"))

        # Brightness
        ttk.Label(settings, text="Brightness:").grid(row=2, column=0, sticky=tk.W)
        self.brightness_var = tk.DoubleVar(value=1.0)
        bright = ttk.Scale(settings, from_=0.5, to=1.5, variable=self.brightness_var, orient=tk.HORIZONTAL)
        bright.grid(row=2, column=1, sticky=tk.EW, padx=5)
        self.brightness_label = ttk.Label(settings, text="1.00")
        self.brightness_label.grid(row=2, column=2)
        bright.configure(command=lambda v: self.brightness_label.config(text=f"{float(v):.2f}"))

        self.invert_var = tk.BooleanVar()
        ttk.Checkbutton(settings, text="Invert (dark on light)", variable=self.invert_var).grid(row=3, column=0, columnspan=3, sticky=tk.W)

        # Preview area
        prev = ttk.LabelFrame(self.root, text="Preview", padding=10)
        prev.pack(fill=tk.BOTH, expand=True, padx=10, pady=5)

        self.orig_canvas = tk.Canvas(prev, bg="white")
        self.orig_canvas.pack(side=tk.LEFT, fill=tk.BOTH, expand=True, padx=5)

        self.stipple_canvas = tk.Canvas(prev, bg="white")
        self.stipple_canvas.pack(side=tk.LEFT, fill=tk.BOTH, expand=True, padx=5)

        # Progress
        self.progress_var = tk.StringVar(value="Ready")
        ttk.Label(self.root, textvariable=self.progress_var).pack()
        self.progress_bar = ttk.Progressbar(self.root, mode="determinate")
        self.progress_bar.pack(fill=tk.X, padx=10, pady=5)

        # Buttons
        actions = ttk.Frame(self.root)
        actions.pack(fill=tk.X, padx=10, pady=5)

        ttk.Button(actions, text="Generate Pattern", command=self.generate_pattern).pack(side=tk.LEFT, padx=5)
        ttk.Button(actions, text="Send to Plotter", command=self.send_to_plotter).pack(side=tk.LEFT, padx=5)
        ttk.Button(actions, text="Home Plotter", command=self.home_plotter).pack(side=tk.LEFT, padx=5)

    # ------------------------------------------------------------------
    # Interaction handlers
    # ------------------------------------------------------------------

    def connect(self):
        ip = self.ip_entry.get()
        self.generator = StipplingGenerator(ip)
        s = self.generator.get_queue_status()
        if s:
            self.status_label.config(text=f"Connected - Queue {s['used']}/{s['size']}", foreground="green")
        else:
            self.status_label.config(text="Connection failed", foreground="red")

    def load_image(self):
        path = filedialog.askopenfilename(title="Select Image",
                                          filetypes=[("Images", "*.png *.jpg *.jpeg *.bmp *.gif")])
        if path:
            self.image_path = path
            self.progress_var.set(f"Loaded: {path}")

    def text_input(self):
        win = tk.Toplevel(self.root)
        win.title("Enter Text")
        win.geometry("300x120")

        ttk.Label(win, text="Text:").pack(pady=5)
        entry = ttk.Entry(win, width=30)
        entry.pack(pady=5)
        entry.focus()

        def ok():
            self.text = entry.get()
            win.destroy()
            self.progress_var.set(f"Text: {self.text}")

        ttk.Button(win, text="OK", command=ok).pack(pady=5)

    # ------------------------------------------------------------------
    # Generate stipple pattern
    # ------------------------------------------------------------------

    def generate_pattern(self):
        if not self.generator:
            messagebox.showerror("Error", "Connect to ESP32 first.")
            return

        try:
            spacing = self.spacing_var.get()
            invert = self.invert_var.get()
            contrast = self.contrast_var.get()
            brightness = self.brightness_var.get()

            if hasattr(self, "image_path"):
                (self.dots, self.work_area, self.stipple_img, self.orig_img) = (
                    self.generator.image_to_stippling(
                        self.image_path,
                        dot_spacing_mm=spacing,
                        invert=invert,
                        contrast=contrast,
                        brightness=brightness,
                    )
                )
            elif hasattr(self, "text"):
                (self.dots, self.work_area, self.stipple_img, self.orig_img) = (
                    self.generator.text_to_stippling(
                        self.text,
                        font_size=40,
                        dot_spacing_mm=spacing,
                        invert=invert,
                        contrast=contrast,
                        brightness=brightness,
                    )
                )
            else:
                messagebox.showerror("Error", "Load an image or enter text first.")
                return

            self.progress_var.set(f"Generated {len(self.dots)} dots")
            self.update_preview_images()

        except Exception as e:
            messagebox.showerror("Error", f"Generation failed:\n{e}")

    # ------------------------------------------------------------------
    # Preview render
    # ------------------------------------------------------------------

    def update_preview_images(self):
        # Original
        if self.orig_img is not None:
            w = self.orig_canvas.winfo_width()
            h = self.orig_canvas.winfo_height()
            if w <= 1 or h <= 1:
                self.root.after(100, self.update_preview_images)
                return
            img = ImageOps.contain(self.orig_img, (w - 10, h - 10))
            self.orig_photo = ImageTk.PhotoImage(img)
            self.orig_canvas.delete("all")
            self.orig_canvas.create_image(w // 2, h // 2, image=self.orig_photo)

        # Stippled
        if self.stipple_img is not None:
            w = self.stipple_canvas.winfo_width()
            h = self.stipple_canvas.winfo_height()
            if w <= 1 or h <= 1:
                self.root.after(100, self.update_preview_images)
                return
            img = ImageOps.contain(self.stipple_img.convert("RGB"), (w - 10, h - 10))
            self.stipple_photo = ImageTk.PhotoImage(img)
            self.stipple_canvas.delete("all")
            self.stipple_canvas.create_image(w // 2, h // 2, image=self.stipple_photo)

    # ------------------------------------------------------------------
    # Sending to plotter
    # ------------------------------------------------------------------

    def send_to_plotter(self):
        if not self.dots:
            messagebox.showerror("Error", "Generate a pattern first.")
            return

        if not self.generator:
            messagebox.showerror("Error", "Connect to ESP32 first.")
            return

        def progress(done, total):
            self.progress_var.set(f"Sending {done}/{total}")
            self.progress_bar["maximum"] = total
            self.progress_bar["value"] = done

        def worker():
            ok = self.generator.send_pattern(self.dots, progress)
            if ok:
                self.progress_var.set("Send complete!")
            else:
                self.progress_var.set("Send failed.")

        threading.Thread(target=worker, daemon=True).start()

    def home_plotter(self):
        if self.generator:
            self.generator.send_command("H")

    # ------------------------------------------------------------------

    def run(self):
        self.root.mainloop()


# Run GUI
if __name__ == "__main__":
    StipplingGUI().run()