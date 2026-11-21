import os
import json
import time
import urllib.parse

from flask import Flask, render_template, request, jsonify, Response, abort
import requests
import yt_dlp

try:
    import serial
    from serial.tools import list_ports
except Exception:
    serial = None
    list_ports = None

try:
    import spotipy
    from spotipy.oauth2 import SpotifyClientCredentials
except Exception:
    spotipy = None

app = Flask(
    __name__,
    template_folder="template",
    static_folder="static"
)


def proxied(url):
    if not url:
        return None
    return "/api/stream?url=" + urllib.parse.quote(url, safe="")


def best_audio_stream(info):
    if info.get("url") and info.get("acodec") not in (None, "none"):
        return info.get("url")
    formats = info.get("formats") or []
    audio = [
        f for f in formats
        if f.get("acodec") not in (None, "none")
        and f.get("vcodec") in (None, "none")
    ]
    if not audio:
        audio = [f for f in formats if f.get("acodec") not in (None, "none")]
    if not audio:
        return info.get("url")
    audio.sort(key=lambda f: (f.get("abr") or 0, f.get("tbr") or 0), reverse=True)
    return audio[0].get("url")


def fetch_youtube(url):
    opts = {"quiet": True, "no_warnings": True, "format": "bestaudio/best"}
    with yt_dlp.YoutubeDL(opts) as ydl:
        info = ydl.extract_info(url, download=False)
    if isinstance(info, dict) and info.get("_type") == "playlist" and info.get("entries"):
        info = info["entries"][0]
    title = info.get("title") or "YouTube"
    artist = info.get("uploader") or info.get("channel") or info.get("creator") or "YouTube"
    thumbnail = info.get("thumbnail")
    if not thumbnail and info.get("thumbnails"):
        thumbnail = info["thumbnails"][-1].get("url")
    stream_url = best_audio_stream(info)
    return {
        "source": "youtube",
        "title": title,
        "artist": artist,
        "album": info.get("album") or "",
        "duration": info.get("duration"),
        "thumbnail": thumbnail,
        "webpage_url": info.get("webpage_url") or url,
        "stream_url": proxied(stream_url or url)
    }


def search_youtube_audio(query):
    if not query:
        return None
    opts = {"quiet": True, "no_warnings": True, "format": "bestaudio/best", "default_search": "ytsearch1"}
    with yt_dlp.YoutubeDL(opts) as ydl:
        info = ydl.extract_info(f"ytsearch1:{query}", download=False)
    entries = info.get("entries") if isinstance(info, dict) else None
    if entries:
        info = entries[0]
    if not isinstance(info, dict):
        return None
    title = info.get("title") or query
    artist = info.get("uploader") or info.get("channel") or "YouTube"
    thumbnail = info.get("thumbnail")
    if not thumbnail and info.get("thumbnails"):
        thumbnail = info["thumbnails"][-1].get("url")
    stream_url = best_audio_stream(info)
    return {
        "title": title,
        "artist": artist,
        "duration": info.get("duration"),
        "thumbnail": thumbnail,
        "stream_url": stream_url or info.get("webpage_url") or query
    }


def fetch_spotify(url):
    title = None
    artist = None
    album = None
    duration = None
    thumbnail = None

    track_id = None
    for part in url.split("?")[0].rstrip("/").split("/"):
        if len(part) == 22 and part.isalnum():
            track_id = part
    if "spotify:track:" in url:
        track_id = url.split("spotify:track:")[-1].split(":")[0]

    client_id = os.environ.get("SPOTIFY_CLIENT_ID")
    client_secret = os.environ.get("SPOTIFY_CLIENT_SECRET")
    if track_id and client_id and client_secret and spotipy is not None:
        try:
            sp = spotipy.Spotify(auth_manager=SpotifyClientCredentials(
                client_id=client_id,
                client_secret=client_secret
            ))
            track = sp.track(track_id)
            title = track.get("name")
            artists = track.get("artists") or []
            artist = artists[0].get("name") if artists else None
            album_info = track.get("album") or {}
            album = album_info.get("name")
            duration = (track.get("duration_ms") or 0) / 1000.0
            images = album_info.get("images") or []
            if images:
                thumbnail = images[0].get("url")
        except Exception:
            pass

    if not title:
        try:
            r = requests.get(
                "https://open.spotify.com/oembed",
                params={"url": url},
                timeout=10
            )
            if r.ok:
                data = r.json()
                title = data.get("title")
                thumbnail = data.get("thumbnail_url") or thumbnail
        except Exception:
            pass

    if not artist and title:
        try:
            embed_url = url.replace("open.spotify.com/track", "open.spotify.com/embed/track").split("?")[0]
            r = requests.get(embed_url, timeout=10, headers={"User-Agent": "Mozilla/5.0"})
            if r.ok:
                import re
                m = re.search(r"<title[^>]*>(.*?)</title>", r.text, re.I | re.S)
                if m:
                    page_title = m.group(1).strip()
                    if " - " in page_title:
                        maybe_artist = page_title.split(" - ", 1)[-1].split("|")[0].strip()
                        if maybe_artist and maybe_artist.lower() != "spotify":
                            artist = maybe_artist
        except Exception:
            pass

    query = " ".join(part for part in [artist, title] if part).strip()
    yt = search_youtube_audio(query) if query else None
    if yt:
        title = title or yt.get("title")
        artist = artist or yt.get("artist")
        duration = duration or yt.get("duration")
        thumbnail = thumbnail or yt.get("thumbnail")
        stream_url = yt.get("stream_url")
    else:
        stream_url = None

    return {
        "source": "spotify",
        "title": title or "Spotify",
        "artist": artist or "Desconhecido",
        "album": album or "",
        "duration": duration,
        "thumbnail": thumbnail,
        "webpage_url": url,
        "stream_url": proxied(stream_url) if stream_url else None
    }


def fetch_generic(url):
    name = urllib.parse.unquote(url.split("/")[-1].split("?")[0]) or "Link"
    return {
        "source": "link",
        "title": name,
        "artist": "Desconhecido",
        "album": "",
        "duration": None,
        "thumbnail": None,
        "webpage_url": url,
        "stream_url": proxied(url)
    }


_serial_connection = None


def find_serial_port():
    env_port = os.environ.get("ESP32_PORT")
    if env_port:
        return env_port
    if list_ports is None:
        return None
    candidates = []
    for port in list_ports.comports():
        description = (port.description or "").lower()
        device = port.device or ""
        if any(key in description for key in ("usb", "acm", "cp210", "ch340", "uart")) or "ttyusb" in device.lower() or "ttyacm" in device.lower():
            candidates.append(device)
    if candidates:
        return candidates[0]
    ports = list(list_ports.comports())
    return ports[0].device if ports else None


def get_serial_connection():
    global _serial_connection
    if serial is None:
        return None
    if _serial_connection is not None and getattr(_serial_connection, "is_open", False):
        return _serial_connection
    port = find_serial_port()
    if not port:
        return None
    try:
        _serial_connection = serial.Serial(port, 115200, timeout=1)
        time.sleep(2)
        return _serial_connection
    except Exception:
        _serial_connection = None
        return None


def send_metadata_to_esp32(title, artist, duration_ms):
    connection = get_serial_connection()
    if connection is None:
        return False, "Porta serial do ESP32 nao encontrada"
    try:
        line = "META|{}|{}|{}\n".format(title or "", artist or "", int(duration_ms or 0))
        connection.write(line.encode("utf-8", "replace"))
        connection.flush()
        return True, "Metadados enviados ao ESP32"
    except Exception as exc:
        global _serial_connection
        try:
            connection.close()
        except Exception:
            pass
        _serial_connection = None
        return False, str(exc)


@app.route("/api/notify", methods=["POST"])
def api_notify():
    data = request.get_json(silent=True) or {}
    title = data.get("title")
    artist = data.get("artist")
    duration = data.get("duration")
    duration_ms = int((duration or 0) * 1000) if duration else 0
    ok, message = send_metadata_to_esp32(title, artist, duration_ms)
    status = 200 if ok else 503
    return jsonify({"ok": ok, "message": message}), status


@app.route("/")
def index():
    return render_template("index.html")


@app.route("/api/link", methods=["POST"])
def api_link():
    data = request.get_json(silent=True) or {}
    url = (data.get("url") or "").strip()
    if not url:
        return jsonify({"error": "URL obrigatória"}), 400
    try:
        lowered = url.lower()
        if "youtube.com" in lowered or "youtu.be" in lowered:
            meta = fetch_youtube(url)
        elif "spotify.com" in lowered or lowered.startswith("spotify:"):
            meta = fetch_spotify(url)
        else:
            meta = fetch_generic(url)
    except Exception as exc:
        return jsonify({"error": str(exc)}), 500
    return jsonify(meta)


@app.route("/api/stream")
def api_stream():
    target = request.args.get("url")
    if not target:
        abort(400)
    target = urllib.parse.unquote(target)
    headers = {}
    if "Range" in request.headers:
        headers["Range"] = request.headers["Range"]
    try:
        upstream = requests.get(target, stream=True, headers=headers, timeout=30)
    except Exception:
        abort(502)
    def generate():
        for chunk in upstream.iter_content(chunk_size=8192):
            if chunk:
                yield chunk
    response = Response(generate(), status=upstream.status_code)
    for header in ("Content-Type", "Content-Length", "Content-Range", "Accept-Ranges"):
        if header in upstream.headers:
            response.headers[header] = upstream.headers[header]
    return response


if __name__ == "__main__":
    app.run(
        host="0.0.0.0",
        port=5000,
        debug=True
    )
