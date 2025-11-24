#!/usr/bin/env python3
from flask import Flask, render_template, request, jsonify
from PIL import Image
from io import BytesIO
import json

from stippling_engine import StipplingGenerator

app = Flask(__name__)


# ---------------------------------------------------------------------
# Routes
# ---------------------------------------------------------------------

@app.route("/")
def index():
    # Just serves the HTML UI
    return render_template("index.html")


@app.route("/generate", methods=["POST"])
def generate():
    """
    Takes:
      - esp32_ip
      - mode = "image" or "text"
      - image file (if mode=image)
      - text (if mode=text)
      - spacing, invert, contrast, brightness, text_size
    Returns:
      - dots: [{x, y}, ...]
      - work_area: [min_x, max_x, min_y, max_y]
    """
    esp32_ip = request.form.get("esp32_ip", "192.168.1.185")
    gen = StipplingGenerator(esp32_ip)

    mode = request.form.get("mode", "image")
    spacing = float(request.form.get("spacing", 1.0))
    invert = request.form.get("invert", "false") in ("true", "on", "1")
    contrast = float(request.form.get("contrast", 1.0))
    brightness = float(request.form.get("brightness", 1.0))
    text_size = float(request.form.get("text_size", 40.0))

    if mode == "text":
        text = request.form.get("text", "")
        dots, work_area, bw, orig_img = gen.text_to_stippling(
            text=text,
            font_size=text_size,
            dot_spacing_mm=spacing,
            invert=invert,
            contrast=contrast,
            brightness=brightness,
        )
    else:
        # image mode
        file = request.files.get("image")
        if not file:
            return jsonify({"error": "No image uploaded"}), 400

        img = Image.open(file.stream).convert("RGB")
        dots, work_area, bw, orig_img = gen._stippling_from_pil(
            img,
            dot_spacing_mm=spacing,
            invert=invert,
            contrast=contrast,
            brightness=brightness,
            work_area=None,
        )

    return jsonify(
        {
            "dots": [{"x": float(x), "y": float(y)} for x, y in dots],
            "work_area": list(work_area),
        }
    )


@app.route("/send", methods=["POST"])
def send():
    """
    Takes:
      - esp32_ip
      - dots (JSON string of [{x, y}, ...])
      - row_height_mm (usually same as spacing)
    """
    esp32_ip = request.form.get("esp32_ip", "192.168.1.185")
    gen = StipplingGenerator(esp32_ip)

    dots_json = request.form.get("dots")
    if not dots_json:
        return jsonify({"error": "No dots"}), 400

    try:
        dots_data = json.loads(dots_json)
    except Exception as e:
        return jsonify({"error": f"Invalid JSON: {e}"}), 400

    dots = [(float(d["x"]), float(d["y"])) for d in dots_data]
    row_height_mm = float(request.form.get("row_height_mm", 1.0))

    ok = gen.send_pattern(
        dots,
        progress_callback=None,  # you can add logging here if you want
        row_height_mm=row_height_mm,
        serpentine=True,
    )

    return jsonify({"ok": bool(ok)})


@app.route("/home", methods=["POST"])
def home():
    esp32_ip = request.form.get("esp32_ip", "192.168.1.185")
    gen = StipplingGenerator(esp32_ip)
    gen.send_command("H")
    return jsonify({"ok": True})


if __name__ == "__main__":
    # dev server
    app.run(host="0.0.0.0", port=5000, debug=True)