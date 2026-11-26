import os
import json
import time
import urllib.parse
import sqlite3
import io
import mimetypes

from flask import Flask, render_template, request, jsonify, Response, abort, send_file, redirect
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

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
DB_PATH = os.path.join(BASE_DIR, "music.db")


def get_db():
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    return conn


def init_db():
    conn = get_db()
    conn.execute("""
        CREATE TABLE IF NOT EXISTS tracks (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            source TEXT,
            title TEXT,
            artist TEXT,
            album TEXT,
            duration REAL,
            thumbnail TEXT,
            filename TEXT,
            format TEXT,
            size INTEGER,
            url TEXT,
            webpage_url TEXT,
            data BLOB,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        )
    """)
    conn.commit()
    try:
        conn.execute("ALTER TABLE tracks ADD COLUMN webpage_url TEXT")
        conn.commit()
    except Exception:
        pass
    conn.close()


def track_from_row(row):
    return {
        "id": row["id"],
        "source": row["source"],
        "name": row["filename"] or row["title"],
        "title": row["title"],
        "artist": row["artist"],
        "album": row["album"],
        "duration": row["duration"],
        "thumbnail": row["thumbnail"],
        "url": "/api/tracks/{}/stream".format(row["id"]),
        "webpage_url": row["webpage_url"] if "webpage_url" in row.keys() else None,
        "size": row["size"],
        "format": row["format"],
    }


def insert_track(source=None, title=None, artist=None, album=None, duration=None,
                thumbnail=None, filename=None, format=None, size=None, url=None, data=None,
                webpage_url=None):
    conn = get_db()
    cur = conn.execute(
        """INSERT INTO tracks (source, title, artist, album, duration, thumbnail, filename, format, size, url, webpage_url, data)
           VALUES (?,?,?,?,?,?,?,?,?,?,?,?)""",
        (source, title, artist, album, duration, thumbnail, filename, format, size, url, webpage_url, data),
    )
    conn.commit()
    row = conn.execute("SELECT * FROM tracks WHERE id=?", (cur.lastrowid,)).fetchone()
    conn.close()
    return track_from_row(row)


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
    return jsonify({"ok": ok, "message": message}), 200


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


def unproxied(url):
    if not url:
        return None
    if url.startswith("/api/stream?url="):
        return urllib.parse.unquote(url.split("url=", 1)[-1])
    return url


def proxy_request(target):
    if not target:
        return None
    headers = {"User-Agent": "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"}
    if "Range" in request.headers:
        headers["Range"] = request.headers["Range"]
    try:
        upstream = requests.get(target, stream=True, headers=headers, timeout=30)
    except Exception:
        return None
    if upstream.status_code >= 400:
        try:
            upstream.close()
        except Exception:
            pass
        return None
    def generate():
        for chunk in upstream.iter_content(chunk_size=8192):
            if chunk:
                yield chunk
    response = Response(generate(), status=upstream.status_code)
    for header in ("Content-Type", "Content-Length", "Content-Range", "Accept-Ranges"):
        if header in upstream.headers:
            response.headers[header] = upstream.headers[header]
    return response


@app.route("/api/stream")
def api_stream():
    target = request.args.get("url")
    if not target:
        abort(400)
    target = urllib.parse.unquote(target)
    resp = proxy_request(target)
    if resp is None:
        abort(502)
    return resp


@app.route("/api/tracks", methods=["GET"])
def api_tracks_list():
    conn = get_db()
    rows = conn.execute("SELECT * FROM tracks ORDER BY id").fetchall()
    conn.close()
    return jsonify([track_from_row(r) for r in rows])


@app.route("/api/tracks", methods=["POST"])
def api_tracks_create():
    data = request.get_json(silent=True) or {}
    try:
        duration = float(data.get("duration")) if data.get("duration") is not None else None
    except (TypeError, ValueError):
        duration = None
    track = insert_track(
        source=data.get("source") or "link",
        title=data.get("title") or "Sem título",
        artist=data.get("artist") or "Desconhecido",
        album=data.get("album") or "",
        duration=duration,
        thumbnail=data.get("thumbnail"),
        filename=data.get("name") or data.get("title"),
        format=data.get("format"),
        size=data.get("size"),
        url=data.get("url"),
        webpage_url=data.get("webpage_url"),
        data=None,
    )
    return jsonify(track), 201


@app.route("/api/tracks/upload", methods=["POST"])
def api_tracks_upload():
    file = request.files.get("file")
    if not file or not file.filename:
        return jsonify({"error": "Arquivo obrigatório"}), 400
    data = file.read()
    filename = file.filename
    title = request.form.get("title") or filename
    artist = request.form.get("artist") or "Arquivo local"
    album = request.form.get("album") or ""
    try:
        duration = float(request.form.get("duration")) if request.form.get("duration") else None
    except (TypeError, ValueError):
        duration = None
    thumbnail = request.form.get("thumbnail")
    fmt = filename.rsplit(".", 1)[-1].upper() if "." in filename else ""
    track = insert_track(
        source="local",
        title=title,
        artist=artist,
        album=album,
        duration=duration,
        thumbnail=thumbnail,
        filename=filename,
        format=fmt,
        size=len(data),
        url=None,
        data=data,
    )
    return jsonify(track), 201


@app.route("/api/tracks/<int:track_id>", methods=["DELETE"])
def api_tracks_delete(track_id):
    conn = get_db()
    conn.execute("DELETE FROM tracks WHERE id=?", (track_id,))
    conn.commit()
    conn.close()
    return jsonify({"ok": True})


def resolve_fresh_stream(row):
    webpage_url = row["webpage_url"] if "webpage_url" in row.keys() else None
    stored = row["url"]
    if not webpage_url and stored and ("youtube.com" in stored or "youtu.be" in stored or "spotify.com" in stored):
        webpage_url = unproxied(stored)
    if webpage_url and ("youtube.com" in webpage_url or "youtu.be" in webpage_url or "music.youtube.com" in webpage_url):
        try:
            meta = fetch_youtube(webpage_url)
            url = unproxied(meta.get("stream_url"))
            if url:
                return url
        except Exception:
            pass
    if webpage_url and "spotify.com" in webpage_url:
        try:
            meta = fetch_spotify(webpage_url)
            url = unproxied(meta.get("stream_url"))
            if url:
                return url
        except Exception:
            pass
    # Fallback: search YouTube by title for tracks without an original URL
    title = row["title"] if "title" in row.keys() else None
    artist = row["artist"] if "artist" in row.keys() else None
    query = " ".join(part for part in [artist, title] if part).strip()
    if query and (row["source"] in (None, "youtube", "link")):
        try:
            yt = search_youtube_audio(query)
            if yt:
                url = unproxied(yt.get("stream_url"))
                if url:
                    return url
        except Exception:
            pass
    target = unproxied(stored)
    if target and target.startswith("http"):
        return target
    return None


@app.route("/api/tracks/<int:track_id>/stream")
def api_track_stream(track_id):
    conn = get_db()
    row = conn.execute("SELECT data, url, filename, webpage_url, source, title, artist FROM tracks WHERE id=?", (track_id,)).fetchone()
    conn.close()
    if not row:
        abort(404)
    if row["data"] is not None:
        mimetype = mimetypes.guess_type(row["filename"] or "")[0] or "application/octet-stream"
        return send_file(io.BytesIO(row["data"]), mimetype=mimetype, download_name=row["filename"], conditional=True)
    stored = row["url"]
    target = unproxied(stored)
    resp = proxy_request(target) if target else None
    if resp is None:
        try:
            new_target = resolve_fresh_stream(row)
        except Exception:
            new_target = None
        if new_target:
            try:
                proxied_url = "/api/stream?url=" + urllib.parse.quote(new_target, safe="")
                conn = get_db()
                conn.execute("UPDATE tracks SET url=? WHERE id=?", (proxied_url, track_id))
                conn.commit()
                conn.close()
            except Exception:
                pass
            resp = proxy_request(new_target)
    if resp is None:
        abort(404)
    return resp


init_db()

if __name__ == "__main__":
    app.run(
        host="0.0.0.0",
        port=5000,
        debug=True
    )
