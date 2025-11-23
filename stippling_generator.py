#!/usr/bin/env python3
"""
Stippling Generator for ESP32 Plotter
Converts images or text to stippling patterns and sends G-code to ESP32 via WiFi.
Includes:
- Dot spacing (mm)
- Pen dot size (mm) with accurate dot preview
- Contrast / brightness / invert controls
- Text size slider
- Serpentine (back-and-forth) raster path
"""

import requests
import time
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
    def __init__(self, esp32_ip="192.168.1.185"):
        self.esp32_ip = esp32_ip
        self.base_url = f"http://{esp32_ip}"
        self.command_queue = thread_queue.Queue()

    # ------------------------------------------------------------------
    # Core networking helpers
    # ------------------------------------------------------------------

    def get_queue_status(self):
        """Query ESP32 for current G-code queue status."""
        try:
            r = requests.get(f"{self.base_url}/queuestatus", timeout=2)
            if r.status_code == 200:
                return r.json()
        except Exception as e:
            print(f"Error getting queue status: {e}")
        return None

    def send_command(self, cmd):
        """Send a single G-code command."""
        try:
            r = requests.get(f"{self.base_url}/gcode", params={"cmd": cmd}, timeout=2)
            if r.status_code == 200:
                t = r.text.strip()
                if t == "ok":
                    return True
                if t.startswith("error:"):
                    print(t)
        except Exception as e:
            print(f"Error sending command '{cmd}': {e}")
        return False

    def wait_for_queue_space(self, required=1, max_wait=60):
        """Wait until the ESP32 queue reports enough free slots."""
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
        """Fetch work area (minX, maxX, minY, maxY) in mm, or use default."""
        if work_area is not None:
            return work_area

        try:
            r = requests.get(f"{self.base_url}/getworkarea", timeout=2)
            if r.status_code == 200:
                a = r.json()
                return (a["minX"], a["maxX"], a["minY"], a["maxY"])
        except Exception as e:
            print(f"Error getting work area: {e}")

        # Fallback default
        return (0, 200, 0, 200)

    # ------------------------------------------------------------------
    # Core stipple pipeline: PIL image → dots + (optional) processed img
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
        """
        Convert a PIL image to a list of dot positions in mm.

        Returns (dots, work_area, bw_img)
        - dots: list[(x_mm, y_mm)]
        - work_area: (min_x, max_x, min_y, max_y)
        - bw_img: dithered black & white PIL image (mainly for debugging)
        """
        work_area = self._get_work_area(work_area)
        min_x, max_x, min_y, max_y = work_area
        width_mm = max_x - min_x
        height_mm = max_y - min_y

        dot_spacing_mm = max(dot_spacing_mm, 0.2)

        # Convert to grayscale
        img = img.convert("L")
        img_w, img_h = img.size
        aspect = img_w / img_h if img_h else 1.0

        # Grid size = physical size / dot spacing
        grid_w = max(1, int(width_mm / dot_spacing_mm))
        grid_h = max(1, int(height_mm / dot_spacing_mm))

        # Fit to grid while preserving aspect ratio
        if grid_w / grid_h > aspect:
            target_h = grid_h
            target_w = int(target_h * aspect)
        else:
            target_w = grid_w
            target_h = int(target_w / aspect)

        target_w = max(1, target_w)
        target_h = max(1, target_h)
        img = img.resize((target_w, target_h), Image.Resampling.LANCZOS)

        # Apply contrast / brightness
        arr = np.array(img, dtype=np.float32)
        arr = (arr - 128.0) * float(contrast) + 128.0
        arr *= float(brightness)
        arr = np.clip(arr, 0, 255).astype(np.uint8)

        if invert:
            arr = 255 - arr

        proc = Image.fromarray(arr, mode="L")

        # Floyd–Steinberg dithering -> binary (0/255)
        bw = proc.convert("1", dither=Image.FLOYDSTEINBERG)
        bw_arr = np.array(bw)

        # Generate dots at cell centers where pixel is black
        h, w = bw_arr.shape
        dots = []
        for j in range(h):
            for i in range(w):
                if bw_arr[j, i] == 0:  # black
                    x_frac = (i + 0.5) / w
                    y_frac = (j + 0.5) / h
                    x_mm = min_x + x_frac * width_mm
                    y_mm = min_y + y_frac * height_mm
                    dots.append((x_mm, y_mm))

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

        # Render text into a large "virtual" pixel canvas (10 px/mm)
        img_w = max(1, int(width_mm * 10))
        img_h = max(1, int(height_mm * 10))
        img = Image.new("L", (img_w, img_h), 255)
        draw = ImageDraw.Draw(img)

        try:
            font = ImageFont.truetype(
                "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                int(font_size),
            )
        except Exception:
            try:
                font = ImageFont.truetype("arial.ttf", int(font_size))
            except Exception:
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
    # Serpentine G-code generator
    # ------------------------------------------------------------------

    def generate_gcode(self, dots, row_height_mm=None, serpentine=True):
        """
        Generate G-code with optional serpentine rastering.
        - dots: list[(x_mm, y_mm)]
        - row_height_mm: approximate spacing between rows (for clustering)
        - serpentine: if True, alternate left→right / right→left each row
        """
        commands = []
        if not dots:
            return commands

        # Serpentine path: group dots by Y rows, then order X forward/back
        if serpentine and row_height_mm is not None:
            eps = max(row_height_mm * 0.5, 0.1)

            # Sort by Y (then X)
            dots_sorted = sorted(dots, key=lambda p: (p[1], p[0]))
            rows = []
            current_row = []
            current_y = None

            for x, y in dots_sorted:
                if current_y is None:
                    current_y = y
                    current_row = [(x, y)]
                elif abs(y - current_y) <= eps:
                    current_row.append((x, y))
                else:
                    rows.append((current_y, current_row))
                    current_y = y
                    current_row = [(x, y)]

            if current_row:
                rows.append((current_y, current_row))

            # Build commands
            commands.append("G0 X0 Y0")
            commands.append("P0")  # pen up

            serp = True
            for row_idx, (row_y, pts) in enumerate(rows):
                pts_sorted = sorted(pts, key=lambda p: p[0])
                if serp and (row_idx % 2 == 1):
                    pts_sorted.reverse()

                for x, y in pts_sorted:
                    commands.append(f"G0 X{x:.2f} Y{y:.2f}")
                    commands.append(f"D X{x:.2f} Y{y:.2f}")

            commands.append("P0")
            commands.append("G0 X0 Y0")

        else:
            # Simple in-order path
            commands.append("G0 X0 Y0")
            commands.append("P0")
            for x, y in dots:
                commands.append(f"G0 X{x:.2f} Y{y:.2f}")
                commands.append(f"D X{x:.2f} Y{y:.2f}")
            commands.append("P0")
            commands.append("G0 X0 Y0")

        return commands

    # ------------------------------------------------------------------
    # Upload pattern to ESP32 (batch upload for efficiency)
    # ------------------------------------------------------------------

    def send_batch(self, gcode_lines):
        """Send a batch of G-code commands via POST to /uploadgcode endpoint."""
        gcode_content = "\n".join(gcode_lines)
        try:
            r = requests.post(
                f"{self.base_url}/uploadgcode",
                data=gcode_content,
                headers={"Content-Type": "text/plain"},
                timeout=10
            )
            if r.status_code == 200:
                return r.json()
        except Exception as e:
            print(f"Error sending batch: {e}")
        return None

    def send_pattern(self, dots, progress_callback=None, row_height_mm=None, serpentine=True):
        """
        Send stippling pattern using efficient batch uploads.
        Splits G-code into chunks and sends via /uploadgcode endpoint.
        """
        gcode = self.generate_gcode(dots, row_height_mm=row_height_mm, serpentine=serpentine)
        total = len(gcode)
        
        if total == 0:
            return True
        
        # Batch size: send 60 commands at a time (leave room in 100-size queue)
        BATCH_SIZE = 60
        sent = 0
        
        # Split into batches
        for i in range(0, total, BATCH_SIZE):
            batch = gcode[i:min(i + BATCH_SIZE, total)]
            
            # Wait for queue space (need at least BATCH_SIZE free slots)
            if not self.wait_for_queue_space(required=len(batch), max_wait=120):
                print(f"Queue did not free up in time (batch {i//BATCH_SIZE + 1})")
                return False
            
            # Send batch
            result = self.send_batch(batch)
            if result:
                queued = result.get("queued", 0)
                failed = result.get("failed", 0)
                sent += queued
                
                if progress_callback:
                    progress_callback(sent, total)
                
                if failed > 0:
                    print(f"Warning: {failed} commands failed in batch")
                    # Continue anyway - might be queue full, will retry next batch
                
                # Small delay between batches to let ESP32 process
                time.sleep(0.1)
            else:
                print(f"Failed to send batch {i//BATCH_SIZE + 1}")
                return False
        
        # Wait for final commands to complete (optional - for progress tracking)
        if progress_callback:
            # Give it a moment, then update final count
            time.sleep(0.5)
            progress_callback(sent, total)
        
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

        self.orig_img = None  # original (image or rendered text)
        self.dither_img = None  # raw black/white dither (debug, optional)
        self.orig_photo = None

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

        row = 0

        # Dot spacing
        ttk.Label(settings, text="Dot Spacing (mm):").grid(row=row, column=0, sticky=tk.W)
        self.spacing_var = tk.DoubleVar(value=1.0)
        spacing_scale = ttk.Scale(settings, from_=0.5, to=3.0,
                                  variable=self.spacing_var, orient=tk.HORIZONTAL,
                                  command=lambda v: self._update_label(self.spacing_label, v))
        spacing_scale.grid(row=row, column=1, sticky=tk.EW, padx=5)
        self.spacing_label = ttk.Label(settings, text="1.00")
        self.spacing_label.grid(row=row, column=2)
        row += 1

        # Pen dot size
        ttk.Label(settings, text="Pen Dot Size (mm):").grid(row=row, column=0, sticky=tk.W)
        self.pen_size_var = tk.DoubleVar(value=0.7)
        def pen_cb(v):
            self._update_label(self.pen_size_label, v)
            self.update_preview_images()
        pen_scale = ttk.Scale(settings, from_=0.2, to=2.0,
                              variable=self.pen_size_var, orient=tk.HORIZONTAL,
                              command=pen_cb)
        pen_scale.grid(row=row, column=1, sticky=tk.EW, padx=5)
        self.pen_size_label = ttk.Label(settings, text="0.70")
        self.pen_size_label.grid(row=row, column=2)
        row += 1

        # Contrast
        ttk.Label(settings, text="Contrast:").grid(row=row, column=0, sticky=tk.W)
        self.contrast_var = tk.DoubleVar(value=1.0)
        contrast_scale = ttk.Scale(settings, from_=0.5, to=2.0,
                                   variable=self.contrast_var, orient=tk.HORIZONTAL,
                                   command=lambda v: self._update_label(self.contrast_label, v))
        contrast_scale.grid(row=row, column=1, sticky=tk.EW, padx=5)
        self.contrast_label = ttk.Label(settings, text="1.00")
        self.contrast_label.grid(row=row, column=2)
        row += 1

        # Brightness
        ttk.Label(settings, text="Brightness:").grid(row=row, column=0, sticky=tk.W)
        self.brightness_var = tk.DoubleVar(value=1.0)
        brightness_scale = ttk.Scale(settings, from_=0.5, to=1.5,
                                     variable=self.brightness_var, orient=tk.HORIZONTAL,
                                     command=lambda v: self._update_label(self.brightness_label, v))
        brightness_scale.grid(row=row, column=1, sticky=tk.EW, padx=5)
        self.brightness_label = ttk.Label(settings, text="1.00")
        self.brightness_label.grid(row=row, column=2)
        row += 1

        # Invert
        self.invert_var = tk.BooleanVar()
        ttk.Checkbutton(settings, text="Invert (dark on light)",
                        variable=self.invert_var).grid(row=row, column=0, columnspan=3,
                                                       sticky=tk.W, pady=4)
        row += 1

        # Text size
        ttk.Label(settings, text="Text Size:").grid(row=row, column=0, sticky=tk.W)
        self.text_size_var = tk.DoubleVar(value=40.0)
        text_size_scale = ttk.Scale(settings, from_=20.0, to=100.0,
                                    variable=self.text_size_var, orient=tk.HORIZONTAL,
                                    command=lambda v: self._update_label(self.text_size_label, v))
        text_size_scale.grid(row=row, column=1, sticky=tk.EW, padx=5)
        self.text_size_label = ttk.Label(settings, text="40")
        self.text_size_label.grid(row=row, column=2)
        row += 1

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

    def _update_label(self, label, value):
        try:
            label.config(text=f"{float(value):.2f}")
        except Exception:
            label.config(text=str(value))

    # ------------------------------------------------------------------
    # Interaction handlers
    # ------------------------------------------------------------------

    def connect(self):
        ip = self.ip_entry.get().strip()
        self.generator = StipplingGenerator(ip)
        s = self.generator.get_queue_status()
        if s:
            self.status_label.config(
                text=f"Connected - Queue {s['used']}/{s['size']}",
                foreground="green",
            )
        else:
            self.status_label.config(text="Connection failed", foreground="red")

    def load_image(self):
        path = filedialog.askopenfilename(
            title="Select Image",
            filetypes=[("Images", "*.png *.jpg *.jpeg *.bmp *.gif")],
        )
        if path:
            self.image_path = path
            self.text = None  # clear text mode
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
            self.image_path = None  # clear image mode
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

        if not hasattr(self, "image_path") and not hasattr(self, "text"):
            messagebox.showerror("Error", "Load an image or enter text first.")
            return

        try:
            spacing = float(self.spacing_var.get())
            invert = bool(self.invert_var.get())
            contrast = float(self.contrast_var.get())
            brightness = float(self.brightness_var.get())
            text_size = float(self.text_size_var.get())

            if getattr(self, "image_path", None):
                self.dots, self.work_area, self.dither_img, self.orig_img = (
                    self.generator.image_to_stippling(
                        self.image_path,
                        dot_spacing_mm=spacing,
                        invert=invert,
                        contrast=contrast,
                        brightness=brightness,
                    )
                )
            elif getattr(self, "text", None):
                self.dots, self.work_area, self.dither_img, self.orig_img = (
                    self.generator.text_to_stippling(
                        self.text,
                        font_size=text_size,
                        dot_spacing_mm=spacing,
                        invert=invert,
                        contrast=contrast,
                        brightness=brightness,
                    )
                )
            else:
                messagebox.showerror("Error", "No input selected.")
                return

            self.progress_var.set(f"Generated {len(self.dots)} dots")
            self.update_preview_images()

        except Exception as e:
            messagebox.showerror("Error", f"Generation failed:\n{e}")

    # ------------------------------------------------------------------
    # Preview render (with physically sized dots)
    # ------------------------------------------------------------------

    def update_preview_images(self):
        # Original preview
        if self.orig_img is not None:
            w = self.orig_canvas.winfo_width()
            h = self.orig_canvas.winfo_height()
            if w <= 1 or h <= 1:
                # Canvas not laid out yet; try again soon
                self.root.after(100, self.update_preview_images)
                return
            img = ImageOps.contain(self.orig_img, (w - 10, h - 10))
            self.orig_photo = ImageTk.PhotoImage(img)
            self.orig_canvas.delete("all")
            self.orig_canvas.create_image(w // 2, h // 2, image=self.orig_photo)

        # Stipple preview: draw dots at correct relative size
        self.stipple_canvas.delete("all")
        if not self.dots or not self.work_area:
            return

        w = self.stipple_canvas.winfo_width()
        h = self.stipple_canvas.winfo_height()
        if w <= 1 or h <= 1:
            self.root.after(100, self.update_preview_images)
            return

        min_x, max_x, min_y, max_y = self.work_area
        width_mm = max_x - min_x
        height_mm = max_y - min_y

        if width_mm <= 0 or height_mm <= 0:
            return

        scale_x = (w - 20) / width_mm
        scale_y = (h - 20) / height_mm
        scale = min(scale_x, scale_y)

        offset_x = 10
        offset_y = 10

        dot_size_mm = float(self.pen_size_var.get())
        radius_px = (dot_size_mm * scale) / 2.0
        radius_px = max(radius_px, 0.5)

        for x_mm, y_mm in self.dots:
            cx = offset_x + (x_mm - min_x) * scale
            cy = offset_y + (y_mm - min_y) * scale
            self.stipple_canvas.create_oval(
                cx - radius_px,
                cy - radius_px,
                cx + radius_px,
                cy + radius_px,
                fill="black",
                outline="black",
            )

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

        row_height_mm = float(self.spacing_var.get())

        def progress(done, total):
            self.progress_var.set(f"Sending {done}/{total}")
            self.progress_bar["maximum"] = total
            self.progress_bar["value"] = done

        def worker():
            ok = self.generator.send_pattern(
                self.dots,
                progress_callback=progress,
                row_height_mm=row_height_mm,
                serpentine=True,
            )
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