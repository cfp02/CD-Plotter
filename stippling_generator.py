#!/usr/bin/env python3
"""
Stippling Generator for ESP32 Plotter
Converts images or text to stippling patterns and sends G-code to ESP32 via WiFi
"""

import requests
import time
import json
from PIL import Image, ImageDraw, ImageFont
import numpy as np
import tkinter as tk
from tkinter import filedialog, messagebox, ttk
import threading
import queue as thread_queue

class StipplingGenerator:
    def __init__(self, esp32_ip="192.168.1.100"):
        self.esp32_ip = esp32_ip
        self.base_url = f"http://{esp32_ip}"
        self.command_queue = thread_queue.Queue()
        self.sending = False
        
    def get_queue_status(self):
        """Get current queue status from ESP32"""
        try:
            response = requests.get(f"{self.base_url}/queuestatus", timeout=2)
            if response.status_code == 200:
                return response.json()
        except Exception as e:
            print(f"Error getting queue status: {e}")
        return None
    
    def send_command(self, cmd):
        """Send single G-code command to ESP32"""
        try:
            response = requests.get(f"{self.base_url}/gcode", params={"cmd": cmd}, timeout=2)
            if response.status_code == 200:
                result = response.text.strip()
                if result == "ok":
                    return True
                elif result.startswith("error:"):
                    print(f"Error: {result}")
                    return False
        except Exception as e:
            print(f"Error sending command: {e}")
        return False
    
    def send_batch(self, gcode_lines):
        """Send batch of G-code commands"""
        gcode_content = "\n".join(gcode_lines)
        try:
            response = requests.post(
                f"{self.base_url}/uploadgcode",
                data=gcode_content,
                headers={"Content-Type": "text/plain"},
                timeout=10
            )
            if response.status_code == 200:
                result = response.json()
                return result
        except Exception as e:
            print(f"Error sending batch: {e}")
        return None
    
    def wait_for_queue_space(self, required=1, max_wait=60):
        """Wait for queue to have space"""
        start_time = time.time()
        while time.time() - start_time < max_wait:
            status = self.get_queue_status()
            if status and status.get("free", 0) >= required:
                return True
            time.sleep(0.5)
        return False
    
    def image_to_stippling(self, image_path, dot_density=0.5, invert=False, work_area=None):
        """
        Convert image to stippling pattern
        dot_density: 0.0-1.0, higher = more dots
        invert: True for dark dots on light background
        work_area: (min_x, max_x, min_y, max_y) in mm
        """
        # Load and process image
        img = Image.open(image_path).convert("L")  # Grayscale
        img_width, img_height = img.size
        
        # Get work area from ESP32 if not provided
        if work_area is None:
            try:
                response = requests.get(f"{self.base_url}/getworkarea", timeout=2)
                if response.status_code == 200:
                    area = response.json()
                    work_area = (area["minX"], area["maxX"], area["minY"], area["maxY"])
                else:
                    work_area = (0, 200, 0, 200)  # Default
            except:
                work_area = (0, 200, 0, 200)  # Default
        
        min_x, max_x, min_y, max_y = work_area
        width_mm = max_x - min_x
        height_mm = max_y - min_y
        
        # Resize image to fit work area (maintain aspect ratio)
        aspect_ratio = img_width / img_height
        if width_mm / height_mm > aspect_ratio:
            # Image is taller, fit to height
            target_height = int(height_mm * 10)  # 10 pixels per mm
            target_width = int(target_height * aspect_ratio)
        else:
            # Image is wider, fit to width
            target_width = int(width_mm * 10)
            target_height = int(target_width / aspect_ratio)
        
        img = img.resize((target_width, target_height), Image.Resampling.LANCZOS)
        img_array = np.array(img)
        
        if invert:
            img_array = 255 - img_array
        
        # Generate stippling pattern using Floyd-Steinberg dithering approach
        # Higher values = more dots
        threshold = int(255 * (1 - dot_density))
        dots = []
        
        # Sample points based on image intensity
        step = max(1, int(5 / dot_density))  # Adjust step size based on density
        
        for y in range(0, target_height, step):
            for x in range(0, target_width, step):
                # Get average intensity in region
                region = img_array[y:min(y+step, target_height), x:min(x+step, target_width)]
                avg_intensity = np.mean(region)
                
                # Convert to probability of dot
                prob = (255 - avg_intensity) / 255.0
                if np.random.random() < prob * dot_density:
                    # Convert pixel coordinates to mm
                    mm_x = min_x + (x / target_width) * width_mm
                    mm_y = min_y + (y / target_height) * height_mm
                    dots.append((mm_x, mm_y))
        
        return dots, work_area
    
    def text_to_stippling(self, text, font_size=20, work_area=None):
        """Convert text to stippling pattern"""
        # Get work area if not provided
        if work_area is None:
            try:
                response = requests.get(f"{self.base_url}/getworkarea", timeout=2)
                if response.status_code == 200:
                    area = response.json()
                    work_area = (area["minX"], area["maxX"], area["minY"], area["maxY"])
                else:
                    work_area = (0, 200, 0, 200)
            except:
                work_area = (0, 200, 0, 200)
        
        min_x, max_x, min_y, max_y = work_area
        width_mm = max_x - min_x
        height_mm = max_y - min_y
        
        # Create image with text
        img = Image.new("L", (int(width_mm * 10), int(height_mm * 10)), 255)
        draw = ImageDraw.Draw(img)
        
        try:
            font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", font_size)
        except:
            try:
                font = ImageFont.truetype("arial.ttf", font_size)
            except:
                font = ImageFont.load_default()
        
        # Get text bounding box
        bbox = draw.textbbox((0, 0), text, font=font)
        text_width = bbox[2] - bbox[0]
        text_height = bbox[3] - bbox[1]
        
        # Center text
        x = (img.width - text_width) // 2
        y = (img.height - text_height) // 2
        
        draw.text((x, y), text, fill=0, font=font)
        
        # Convert to stippling
        return self.image_to_stippling_from_array(np.array(img), work_area)
    
    def image_to_stippling_from_array(self, img_array, work_area):
        """Convert image array to stippling"""
        min_x, max_x, min_y, max_y = work_area
        width_mm = max_x - min_x
        height_mm = max_y - min_y
        height, width = img_array.shape
        
        dots = []
        step = 3  # Sample every 3 pixels
        
        for y in range(0, height, step):
            for x in range(0, width, step):
                if img_array[y, x] < 128:  # Dark pixel
                    mm_x = min_x + (x / width) * width_mm
                    mm_y = min_y + (y / height) * height_mm
                    dots.append((mm_x, mm_y))
        
        return dots, work_area
    
    def generate_gcode(self, dots, pen_up_between=True):
        """Generate G-code commands from dot list"""
        commands = []
        commands.append("G0 X0 Y0")  # Start at home
        commands.append("P0")  # Pen up
        
        for x, y in dots:
            if pen_up_between:
                commands.append(f"G0 X{x:.2f} Y{y:.2f}")  # Rapid move
            else:
                commands.append(f"G1 X{x:.2f} Y{y:.2f}")  # Linear move
            commands.append(f"D X{x:.2f} Y{y:.2f}")  # Dot command
        
        commands.append("G0 X0 Y0")  # Return home
        commands.append("P0")  # Pen up
        
        return commands
    
    def send_pattern(self, dots, progress_callback=None):
        """Send stippling pattern to ESP32"""
        gcode = self.generate_gcode(dots)
        total = len(gcode)
        
        # Try batch upload first
        result = self.send_batch(gcode)
        if result and result.get("failed", 0) == 0:
            if progress_callback:
                progress_callback(total, total)
            return True
        
        # Fall back to streaming
        sent = 0
        for cmd in gcode:
            if not self.wait_for_queue_space(required=1):
                print("Queue full, waiting...")
                return False
            
            if self.send_command(cmd):
                sent += 1
                if progress_callback:
                    progress_callback(sent, total)
            else:
                print(f"Failed to send command: {cmd}")
                return False
            
            # Small delay to prevent overwhelming
            time.sleep(0.01)
        
        return True


class StipplingGUI:
    def __init__(self):
        self.root = tk.Tk()
        self.root.title("ESP32 Plotter - Stippling Generator")
        self.root.geometry("800x700")
        
        self.generator = None
        self.dots = []
        self.work_area = None
        
        self.setup_ui()
        
    def setup_ui(self):
        # Connection settings
        conn_frame = ttk.LabelFrame(self.root, text="ESP32 Connection", padding=10)
        conn_frame.pack(fill=tk.X, padx=10, pady=5)
        
        ttk.Label(conn_frame, text="IP Address:").grid(row=0, column=0, sticky=tk.W)
        self.ip_entry = ttk.Entry(conn_frame, width=20)
        self.ip_entry.insert(0, "192.168.1.100")
        self.ip_entry.grid(row=0, column=1, padx=5)
        
        ttk.Button(conn_frame, text="Connect", command=self.connect).grid(row=0, column=2, padx=5)
        
        self.status_label = ttk.Label(conn_frame, text="Not connected", foreground="red")
        self.status_label.grid(row=1, column=0, columnspan=3, sticky=tk.W, pady=5)
        
        # Input selection
        input_frame = ttk.LabelFrame(self.root, text="Input", padding=10)
        input_frame.pack(fill=tk.X, padx=10, pady=5)
        
        ttk.Button(input_frame, text="Load Image", command=self.load_image).pack(side=tk.LEFT, padx=5)
        ttk.Button(input_frame, text="Text Input", command=self.text_input).pack(side=tk.LEFT, padx=5)
        
        # Settings
        settings_frame = ttk.LabelFrame(self.root, text="Stippling Settings", padding=10)
        settings_frame.pack(fill=tk.X, padx=10, pady=5)
        
        ttk.Label(settings_frame, text="Dot Density:").grid(row=0, column=0, sticky=tk.W)
        self.density_var = tk.DoubleVar(value=0.5)
        density_scale = ttk.Scale(settings_frame, from_=0.1, to=1.0, variable=self.density_var, orient=tk.HORIZONTAL)
        density_scale.grid(row=0, column=1, sticky=tk.EW, padx=5)
        self.density_label = ttk.Label(settings_frame, text="0.5")
        self.density_label.grid(row=0, column=2, padx=5)
        density_scale.configure(command=lambda v: self.density_label.config(text=f"{float(v):.2f}"))
        
        self.invert_var = tk.BooleanVar()
        ttk.Checkbutton(settings_frame, text="Invert (dark on light)", variable=self.invert_var).grid(row=1, column=0, columnspan=3, sticky=tk.W, pady=5)
        
        # Preview
        preview_frame = ttk.LabelFrame(self.root, text="Preview", padding=10)
        preview_frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=5)
        
        self.preview_canvas = tk.Canvas(preview_frame, bg="white", width=400, height=300)
        self.preview_canvas.pack(fill=tk.BOTH, expand=True)
        
        # Progress
        self.progress_var = tk.StringVar(value="Ready")
        ttk.Label(self.root, textvariable=self.progress_var).pack(pady=5)
        
        self.progress_bar = ttk.Progressbar(self.root, mode='determinate')
        self.progress_bar.pack(fill=tk.X, padx=10, pady=5)
        
        # Actions
        action_frame = ttk.Frame(self.root)
        action_frame.pack(fill=tk.X, padx=10, pady=5)
        
        ttk.Button(action_frame, text="Generate Pattern", command=self.generate_pattern).pack(side=tk.LEFT, padx=5)
        ttk.Button(action_frame, text="Send to Plotter", command=self.send_to_plotter).pack(side=tk.LEFT, padx=5)
        ttk.Button(action_frame, text="Home Plotter", command=self.home_plotter).pack(side=tk.LEFT, padx=5)
        
    def connect(self):
        ip = self.ip_entry.get()
        self.generator = StipplingGenerator(ip)
        status = self.generator.get_queue_status()
        if status:
            self.status_label.config(text=f"Connected - Queue: {status['used']}/{status['size']}", foreground="green")
        else:
            self.status_label.config(text="Connection failed", foreground="red")
    
    def load_image(self):
        filename = filedialog.askopenfilename(
            title="Select Image",
            filetypes=[("Image files", "*.png *.jpg *.jpeg *.bmp *.gif")]
        )
        if filename:
            self.image_path = filename
            self.progress_var.set(f"Image loaded: {filename}")
    
    def text_input(self):
        dialog = tk.Toplevel(self.root)
        dialog.title("Enter Text")
        dialog.geometry("300x100")
        
        ttk.Label(dialog, text="Text:").pack(pady=5)
        text_entry = ttk.Entry(dialog, width=30)
        text_entry.pack(pady=5)
        text_entry.focus()
        
        def ok():
            self.text = text_entry.get()
            dialog.destroy()
            self.progress_var.set(f"Text: {self.text}")
        
        ttk.Button(dialog, text="OK", command=ok).pack(pady=5)
    
    def generate_pattern(self):
        if not self.generator:
            messagebox.showerror("Error", "Please connect to ESP32 first")
            return
        
        try:
            if hasattr(self, 'image_path'):
                self.dots, self.work_area = self.generator.image_to_stippling(
                    self.image_path,
                    dot_density=self.density_var.get(),
                    invert=self.invert_var.get()
                )
            elif hasattr(self, 'text'):
                self.dots, self.work_area = self.generator.text_to_stippling(
                    self.text,
                    font_size=40
                )
            else:
                messagebox.showerror("Error", "Please load an image or enter text first")
                return
            
            self.progress_var.set(f"Generated {len(self.dots)} dots")
            self.update_preview()
        except Exception as e:
            messagebox.showerror("Error", f"Failed to generate pattern: {e}")
    
    def update_preview(self):
        self.preview_canvas.delete("all")
        if not self.dots:
            return
        
        # Get canvas size
        width = self.preview_canvas.winfo_width()
        height = self.preview_canvas.winfo_height()
        
        if width <= 1 or height <= 1:
            return
        
        # Scale dots to canvas
        min_x = min(d[0] for d in self.dots)
        max_x = max(d[0] for d in self.dots)
        min_y = min(d[1] for d in self.dots)
        max_y = max(d[1] for d in self.dots)
        
        scale_x = (width - 20) / (max_x - min_x) if max_x > min_x else 1
        scale_y = (height - 20) / (max_y - min_y) if max_y > min_y else 1
        scale = min(scale_x, scale_y)
        
        offset_x = 10 - min_x * scale
        offset_y = 10 - min_y * scale
        
        # Draw dots
        for x, y in self.dots:
            canvas_x = x * scale + offset_x
            canvas_y = y * scale + offset_y
            self.preview_canvas.create_oval(canvas_x - 1, canvas_y - 1, canvas_x + 1, canvas_y + 1, fill="black")
    
    def send_to_plotter(self):
        if not self.dots:
            messagebox.showerror("Error", "Please generate a pattern first")
            return
        
        if not self.generator:
            messagebox.showerror("Error", "Please connect to ESP32 first")
            return
        
        def progress(sent, total):
            self.progress_var.set(f"Sending: {sent}/{total}")
            self.progress_bar['maximum'] = total
            self.progress_bar['value'] = sent
        
        def send_thread():
            success = self.generator.send_pattern(self.dots, progress_callback=progress)
            if success:
                self.progress_var.set(f"Sent {len(self.dots)} dots successfully!")
            else:
                self.progress_var.set("Failed to send pattern")
        
        threading.Thread(target=send_thread, daemon=True).start()
    
    def home_plotter(self):
        if self.generator:
            self.generator.send_command("H")
    
    def run(self):
        self.root.mainloop()


if __name__ == "__main__":
    app = StipplingGUI()
    app.run()

