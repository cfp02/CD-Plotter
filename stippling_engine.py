import requests
import time
from PIL import Image, ImageDraw, ImageFont, ImageOps
import numpy as np
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

        return dots, work_area, bw, img

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
            # Start at home (H command moves to home position with pen up)
            commands.append("H")

            serp = True
            for row_idx, (row_y, pts) in enumerate(rows):
                pts_sorted = sorted(pts, key=lambda p: p[0])
                if serp and (row_idx % 2 == 1):
                    pts_sorted.reverse()

                for x, y in pts_sorted:
                    # Move to position with pen up (G0 automatically ensures pen is up)
                    commands.append(f"G0 X{x:.2f} Y{y:.2f}")
                    # Make dot (D command handles pen down, dwell, pen up automatically)
                    commands.append(f"D X{x:.2f} Y{y:.2f}")

            # Return home (H command moves to home position with pen up)
            commands.append("H")

        else:
            # Simple in-order path
            # Start at home (H command moves to home position with pen up)
            commands.append("H")
            for x, y in dots:
                # Move to position with pen up (G0 automatically ensures pen is up)
                commands.append(f"G0 X{x:.2f} Y{y:.2f}")
                # Make dot (D command handles pen down, dwell, pen up automatically)
                commands.append(f"D X{x:.2f} Y{y:.2f}")
            # Return home (H command moves to home position with pen up)
            commands.append("H")

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
