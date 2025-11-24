import requests
import time
import random
from PIL import Image, ImageDraw, ImageFont
import numpy as np
import queue as thread_queue


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
    # Core stipple pipeline: PIL image → dots + processed images
    # ------------------------------------------------------------------

    def _stippling_from_pil(
        self,
        img,
        dot_spacing_mm=1.0,
        invert=False,
        contrast=1.0,
        brightness=1.0,
        work_area=None,
        mode="binary",        # "binary" (old behavior) or "density"
        max_passes=3,         # max taps per cell in density mode
        jitter_factor=0.3,    # fraction of dot_spacing for random jitter
    ):
        """
        Convert a PIL image to a list of dot positions in mm.

        Returns (dots, work_area, bw_img, processed_img)
        - dots: list[(x_mm, y_mm)]
        - work_area: (min_x, max_x, min_y, max_y)
        - bw_img: dithered or density-preview image in pixel space
        - processed_img: grayscale, contrast/brightness adjusted, resized
        """
        work_area = self._get_work_area(work_area)
        min_x, max_x, min_y, max_y = work_area
        width_mm = max_x - min_x
        height_mm = max_y - min_y

        dot_spacing_mm = max(dot_spacing_mm, 0.2)

        # ------------------------------------------------------------------
        # 1) Convert to grayscale and compute aspect ratios
        # ------------------------------------------------------------------
        img = img.convert("L")
        img_w, img_h = img.size
        aspect = img_w / img_h if img_h else 1.0          # image aspect (w/h)

        area_aspect = width_mm / height_mm if height_mm else 1.0

        # ------------------------------------------------------------------
        # 2) Compute *physical* region the image will occupy (letterboxed)
        #    - maintain aspect ratio
        #    - keep dot spacing isotropic
        # ------------------------------------------------------------------
        if area_aspect > aspect:
            # Work area is wider than image: image takes full height
            phys_h = height_mm
            phys_w = height_mm * aspect
        else:
            # Work area is taller/narrower: image takes full width
            phys_w = width_mm
            phys_h = width_mm / aspect

        # Center image in work area (letterboxing)
        offset_x_mm = min_x + (width_mm - phys_w) / 2.0
        offset_y_mm = min_y + (height_mm - phys_h) / 2.0

        # Grid size based on physical image area and dot spacing
        grid_w = max(1, int(phys_w / dot_spacing_mm))
        grid_h = max(1, int(phys_h / dot_spacing_mm))

        # Resize image to grid resolution (this keeps sampling uniform)
        img = img.resize((grid_w, grid_h), Image.Resampling.LANCZOS)

        # ------------------------------------------------------------------
        # 3) Apply contrast / brightness
        # ------------------------------------------------------------------
        arr = np.array(img, dtype=np.float32)
        arr = (arr - 128.0) * float(contrast) + 128.0
        arr *= float(brightness)
        arr = np.clip(arr, 0, 255).astype(np.uint8)

        if invert:
            arr = 255 - arr

        # Keep a processed grayscale image (for preview)
        processed_img = Image.fromarray(arr, mode="L")

        dots = []

        # ------------------------------------------------------------------
        # 4) Convert tone → dots
        # ------------------------------------------------------------------
        if mode == "binary":
            # Floyd–Steinberg dither -> binary image
            bw = processed_img.convert("1", dither=Image.FLOYDSTEINBERG)
            bw_arr = np.array(bw)

            h, w = bw_arr.shape
            for j in range(h):
                for i in range(w):
                    if bw_arr[j, i] == 0:  # black pixel -> one dot
                        x_frac = (i + 0.5) / w
                        y_frac = (j + 0.5) / h
                        x_mm = offset_x_mm + x_frac * phys_w
                        y_mm = offset_y_mm + y_frac * phys_h
                        dots.append((x_mm, y_mm))

        else:
            # "density" mode: number of taps per cell based on darkness
            h, w = arr.shape
            bw = Image.new("L", (w, h), 255)
            bw_px = bw.load()

            max_passes = max(1, int(max_passes))
            jitter_factor = float(jitter_factor)

            for j in range(h):
                for i in range(w):
                    val = arr[j, i]  # 0 (black) .. 255 (white)
                    darkness = 1.0 - (val / 255.0)  # 0=white, 1=black

                    passes = int(round(darkness * max_passes))
                    if passes <= 0:
                        continue

                    # For preview: darker pixel for more passes
                    preview_val = int(255 * (1.0 - min(1.0, darkness)))
                    bw_px[i, j] = preview_val

                    # Base physical position: center of the cell
                    x_frac = (i + 0.5) / w
                    y_frac = (j + 0.5) / h
                    base_x = offset_x_mm + x_frac * phys_w
                    base_y = offset_y_mm + y_frac * phys_h

                    # Generate multiple jittered dots
                    for _ in range(passes):
                        if jitter_factor > 0:
                            jitter_mm = dot_spacing_mm * jitter_factor
                            dx = (random.random() - 0.5) * jitter_mm
                            dy = (random.random() - 0.5) * jitter_mm
                        else:
                            dx = dy = 0.0

                        x_mm = min(max(base_x + dx, min_x), max_x)
                        y_mm = min(max(base_y + dy, min_y), max_y)
                        dots.append((x_mm, y_mm))

        return dots, work_area, bw, processed_img

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
        mode="binary",
        max_passes=3,
        jitter_factor=0.3,
    ):
        orig = Image.open(image_path).convert("RGB")
        dots, area, bw, processed = self._stippling_from_pil(
            orig,
            dot_spacing_mm=dot_spacing_mm,
            invert=invert,
            contrast=contrast,
            brightness=brightness,
            work_area=work_area,
            mode=mode,
            max_passes=max_passes,
            jitter_factor=jitter_factor,
        )
        # You can return both processed and original if you want
        return dots, area, bw, processed

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
        mode="binary",
        max_passes=3,
        jitter_factor=0.3,
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

        dots, area, bw, processed = self._stippling_from_pil(
            img,
            dot_spacing_mm=dot_spacing_mm,
            invert=invert,
            contrast=contrast,
            brightness=brightness,
            work_area=work_area,
            mode=mode,
            max_passes=max_passes,
            jitter_factor=jitter_factor,
        )

        return dots, area, bw, processed

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

            # Start at home
            commands.append("H")

            serp = True
            for row_idx, (row_y, pts) in enumerate(rows):
                pts_sorted = sorted(pts, key=lambda p: p[0])
                if serp and (row_idx % 2 == 1):
                    pts_sorted.reverse()

                for x, y in pts_sorted:
                    commands.append(f"G0 X{x:.2f} Y{y:.2f}")
                    commands.append(f"D X{x:.2f} Y{y:.2f}")

            commands.append("H")

        else:
            commands.append("H")
            for x, y in dots:
                commands.append(f"G0 X{x:.2f} Y{y:.2f}")
                commands.append(f"D X{x:.2f} Y{y:.2f}")
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

        BATCH_SIZE = 60
        sent = 0

        for i in range(0, total, BATCH_SIZE):
            batch = gcode[i:min(i + BATCH_SIZE, total)]

            if not self.wait_for_queue_space(required=len(batch), max_wait=120):
                print(f"Queue did not free up in time (batch {i//BATCH_SIZE + 1})")
                return False

            result = self.send_batch(batch)
            if result:
                queued = result.get("queued", 0)
                failed = result.get("failed", 0)
                sent += queued

                if progress_callback:
                    progress_callback(sent, total)

                if failed > 0:
                    print(f"Warning: {failed} commands failed in batch")

                time.sleep(0.1)
            else:
                print(f"Failed to send batch {i//BATCH_SIZE + 1}")
                return False

        if progress_callback:
            time.sleep(0.5)
            progress_callback(sent, total)

        return True