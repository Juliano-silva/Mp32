
import io
import mimetypes
import sqlite3

from flask import Blueprint, abort, jsonify, redirect, request, send_file

from .config import DB_PATH

DB = Blueprint('DB', __name__) 


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
            data BLOB,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        )
    """)
    conn.commit()
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
        "size": row["size"],
        "format": row["format"],
    }


def insert_track(source=None, title=None, artist=None, album=None, duration=None,
                thumbnail=None, filename=None, format=None, size=None, url=None, data=None):
    conn = get_db()
    cur = conn.execute(
        """INSERT INTO tracks (source, title, artist, album, duration, thumbnail, filename, format, size, url, data)
           VALUES (?,?,?,?,?,?,?,?,?,?,?)""",
        (source, title, artist, album, duration, thumbnail, filename, format, size, url, data),
    )
    conn.commit()
    row = conn.execute("SELECT * FROM tracks WHERE id=?", (cur.lastrowid,)).fetchone()
    conn.close()
    return track_from_row(row)


@DB.route("/api/tracks", methods=["GET"])
def api_tracks_list():
    conn = get_db()
    rows = conn.execute("SELECT * FROM tracks ORDER BY id").fetchall()
    conn.close()
    return jsonify([track_from_row(r) for r in rows])


@DB.route("/api/tracks", methods=["POST"])
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
        data=None,
    )
    return jsonify(track), 201


@DB.route("/api/tracks/upload", methods=["POST"])
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


@DB.route("/api/tracks/<int:track_id>", methods=["DELETE"])
def api_tracks_delete(track_id):
    conn = get_db()
    conn.execute("DELETE FROM tracks WHERE id=?", (track_id,))
    conn.commit()
    conn.close()
    return jsonify({"ok": True})


@DB.route("/api/tracks/<int:track_id>/stream")
def api_track_stream(track_id):
    conn = get_db()
    row = conn.execute("SELECT data, url, filename FROM tracks WHERE id=?", (track_id,)).fetchone()
    conn.close()
    if not row:
        abort(404)
    if row["data"] is not None:
        mimetype = mimetypes.guess_type(row["filename"] or "")[0] or "application/octet-stream"
        return send_file(io.BytesIO(row["data"]), mimetype=mimetype, download_name=row["filename"], conditional=True)
    if row["url"]:
        return redirect(row["url"], code=302)
    abort(404)

