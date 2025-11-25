#!/usr/bin/env python3
import io
import base64
import threading

from flask import Flask, render_template, request, jsonify

from PIL import Image, ImageDraw, ImageFont, ImageOps

from stippling_engine import StipplingGenerator  # adjust if needed

app = Flask(__name__)

# ---------------------------------------------------------------------
# Global state for "last generated" pattern
# ---------------------------------------------------------------------
CURRENT_DOTS = None
CURRENT_WORK_AREA = None
CURRENT_DOT_SPACING = None
CURRENT_PEN_SIZE = None

# For safety if multiple requests come in at once
STATE_LOCK = threading.Lock()

# ---------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------

def pil_to_base64_png(img):
    """Convert a PIL image to base64-encoded PNG string."""
    buf = io.BytesIO()
    img.save(buf, format="PNG")
    return base64.b64encode(buf.getvalue()).decode("ascii")


def render_text_to_image(text, width_px=1200, height_px=800, font_size=120):
    """
    Render text into a grayscale PIL image centered in the frame.
    Similar to your text_to_stippling logic but local to the web app.
    """
    img = Image.new("L", (width_px, height_px), 255)
    draw = ImageDraw.Draw(img)

    # Try a couple of fonts, then default
    font = None
    for path in [
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "arial.ttf",
    ]:
        try:
            font = ImageFont.truetype(path, int(font_size))
            break
        except Exception:
            continue
    if font is None:
        font = ImageFont.load_default()

    bbox = draw.textbbox((0, 0), text, font=font)
    tw = bbox[2] - bbox[0]
    th = bbox[3] - bbox[1]

    x = (width_px - tw) // 2
    y = (height_px - th) // 2

    draw.text((x, y), text, font=font, fill=0)

    return img


def build_stipple_preview(dots, work_area, pen_size_mm, img_size=(800, 800)):
    """
    Create a preview image with dots drawn at the correct relative scale,
    similar to the Tkinter preview.
    """
    if not dots or not work_area:
        return Image.new("RGB", img_size, "white")

    w_px, h_px = img_size
    min_x, max_x, min_y, max_y = work_area
    width_mm = max_x - min_x
    height_mm = max_y - min_y

    if width_mm <= 0 or height_mm <= 0:
        return Image.new("RGB", img_size, "white")

    # Leave small margins
    scale_x = (w_px - 20) / width_mm
    scale_y = (h_px - 20) / height_mm
    scale = min(scale_x, scale_y)

    offset_x = 10
    offset_y = 10

    # Convert pen size in mm to pixel radius
    radius_px = (float(pen_size_mm) * scale) / 2.0
    radius_px = max(radius_px, 0.5)

    out = Image.new("RGB", (w_px, h_px), "white")
    draw = ImageDraw.Draw(out)

    for x_mm, y_mm in dots:
        cx = offset_x + (x_mm - min_x) * scale
        cy = offset_y + (y_mm - min_y) * scale
        bbox = [
            cx - radius_px,
            cy - radius_px,
            cx + radius_px,
            cy + radius_px,
        ]
        draw.ellipse(bbox, fill="black", outline="black")

    return out

# ---------------------------------------------------------------------
# Routes
# ---------------------------------------------------------------------

@app.route("/")
def index():
    return render_template("index.html")


@app.route("/generate", methods=["POST"])
def generate():
    """
    Generate stippling pattern from an uploaded image or text,
    using the enhanced density-based stippling if requested.
    Returns:
      - dot count
      - original preview
      - stipple preview
    """
    # ESP32 IP (not strictly needed for generation, but we accept it)
    esp32_ip = request.form.get("esp32_ip", "192.168.1.185").strip()
    gen = StipplingGenerator(esp32_ip=esp32_ip)

    # Parameters
    try:
        dot_spacing_mm = float(request.form.get("dot_spacing_mm", "1.0"))
    except ValueError:
        dot_spacing_mm = 1.0

    try:
        contrast = float(request.form.get("contrast", "1.0"))
    except ValueError:
        contrast = 1.0

    try:
        brightness = float(request.form.get("brightness", "1.0"))
    except ValueError:
        brightness = 1.0

    invert = request.form.get("invert", "false") == "true"

    try:
        text_size = float(request.form.get("text_size", "40.0"))
    except ValueError:
        text_size = 40.0

    try:
        pen_size_mm = float(request.form.get("pen_size_mm", "0.7"))
    except ValueError:
        pen_size_mm = 0.7

    # New knobs
    mode = request.form.get("mode", "binary")  # "binary" or "density"
    try:
        max_passes = int(request.form.get("max_passes", "3"))
    except ValueError:
        max_passes = 3

    try:
        jitter_factor = float(request.form.get("jitter", "0.3"))
    except ValueError:
        jitter_factor = 0.3

    # Input: either image upload or text
    img = None
    orig_img_for_preview = None

    upload = request.files.get("image")
    text = request.form.get("text", "").strip()

    if upload and upload.filename:
        img = Image.open(upload.stream).convert("RGB")
        orig_img_for_preview = img.copy()
    elif text:
        # Render text into a virtual canvas, then stipple that
        # (Size here is just a pixel canvas; physical scale happens in _stippling_from_pil)
        text_img = render_text_to_image(
            text=text,
            width_px=1600,
            height_px=1000,
            font_size=text_size,
        )
        img = text_img.convert("RGB")
        orig_img_for_preview = img.convert("RGB")
    else:
        return jsonify({
            "success": False,
            "error": "No image or text provided."
        }), 400

    try:
        # Call your enhanced stippling engine
        dots, work_area, bw_img, processed_img = gen._stippling_from_pil(
            img,
            dot_spacing_mm=dot_spacing_mm,
            invert=invert,
            contrast=contrast,
            brightness=brightness,
            work_area=None,           # or a fixed tuple if you want
            mode=mode,
            max_passes=max_passes,
            jitter_factor=jitter_factor,
        )
    except TypeError as e:
        # This usually means _stippling_from_pil doesn't have the new args yet
        return jsonify({
            "success": False,
            "error": f"_stippling_from_pil signature mismatch: {e}"
        }), 500

    # Build previews
    # 1) Original preview (scaled to fit)
    if orig_img_for_preview is None:
        orig_img_for_preview = processed_img.convert("RGB")

    orig_preview = ImageOps.contain(orig_img_for_preview, (600, 400))
    orig_b64 = pil_to_base64_png(orig_preview)

    # 2) Stipple preview with physically scaled dots
    stipple_preview = build_stipple_preview(
        dots,
        work_area,
        pen_size_mm=pen_size_mm,
        img_size=(600, 400),
    )
    stipple_b64 = pil_to_base64_png(stipple_preview)

    with STATE_LOCK:
        global CURRENT_DOTS, CURRENT_WORK_AREA, CURRENT_DOT_SPACING, CURRENT_PEN_SIZE
        CURRENT_DOTS = dots
        CURRENT_WORK_AREA = work_area
        CURRENT_DOT_SPACING = dot_spacing_mm
        CURRENT_PEN_SIZE = pen_size_mm

    return jsonify({
        "success": True,
        "dots_count": len(dots),
        "preview_original": orig_b64,
        "preview_stipple": stipple_b64,
    })


@app.route("/send", methods=["POST"])
def send_to_plotter():
    """
    Send the last generated pattern to the ESP32.
    Expects JSON: { "esp32_ip": "...", "dot_spacing_mm": "..." }
    """
    data = request.get_json(force=True, silent=True) or {}

    esp32_ip = data.get("esp32_ip", "192.168.1.185").strip()
    try:
        dot_spacing_mm = float(data.get("dot_spacing_mm", CURRENT_DOT_SPACING or 1.0))
    except ValueError:
        dot_spacing_mm = CURRENT_DOT_SPACING or 1.0

    with STATE_LOCK:
        dots = CURRENT_DOTS

    if not dots:
        return jsonify({
            "success": False,
            "error": "No pattern generated yet.",
        }), 400

    gen = StipplingGenerator(esp32_ip=esp32_ip)

    ok = gen.send_pattern(
        dots,
        progress_callback=None,  # you could add streaming progress later
        row_height_mm=dot_spacing_mm,
        serpentine=True,
    )

    if ok:
        return jsonify({"success": True, "message": "Pattern sent to plotter."})
    else:
        return jsonify({"success": False, "error": "Failed to send pattern."}), 500


@app.route("/home", methods=["POST"])
def home_plotter():
    """
    Home the plotter. Expects JSON: { "esp32_ip": "..." }
    """
    data = request.get_json(force=True, silent=True) or {}
    esp32_ip = data.get("esp32_ip", "192.168.1.185").strip()

    gen = StipplingGenerator(esp32_ip=esp32_ip)
    ok = gen.send_command("H")

    if ok:
        return jsonify({"success": True, "message": "Homing command sent."})
    else:
        return jsonify({"success": False, "error": "Failed to home plotter."}), 500


if __name__ == "__main__":
    # For dev only
    app.run(host="0.0.0.0", port=5000, debug=True)