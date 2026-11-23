const audio = document.getElementById("audio");

const filesInput = document.getElementById("files");
const playlistElement = document.getElementById("playlist");

const titleElement = document.getElementById("title");
const filenameElement = document.getElementById("filename");

const playButton = document.getElementById("play");
const progress = document.getElementById("progress");

let tracks = [];
let currentIndex = -1;

function formatTime(seconds) {
    if (!Number.isFinite(seconds)) return "00:00";

    const min = Math.floor(seconds / 60);
    const sec = Math.floor(seconds % 60);

    return `${String(min).padStart(2, "0")}:${String(sec).padStart(2, "0")}`;
}

function formatSize(bytes) {
    return (bytes / (1024 * 1024)).toFixed(2) + " MB";
}

async function loadTracksFromDb() {
    try {
        const response = await fetch("/api/tracks");
        const data = await response.json();
        tracks = Array.isArray(data) ? data : [];
        renderPlaylist();
        if (currentIndex === -1 && tracks.length) {
            loadTrack(0);
        }
    } catch (error) {
        console.error(error);
    }
}

filesInput.addEventListener("change", async (event) => {
    const files = Array.from(event.target.files);

    for (const file of files) {
        if (!file.type.startsWith("audio/")) return;

        const formData = new FormData();
        formData.append("file", file);
        formData.append("title", file.name.replace(/\.[^/.]+$/, ""));
        formData.append("artist", "Arquivo local");

        try {
            const response = await fetch("/api/tracks/upload", {
                method: "POST",
                body: formData
            });
            const data = await response.json();
            if (!response.ok) {
                throw new Error(data.error || "Falha no upload.");
            }
            tracks.push(data);
        } catch (error) {
            console.error(error);
        }
    }

    renderPlaylist();

    if (currentIndex === -1 && tracks.length) {
        loadTrack(0);
    }

    filesInput.value = "";
});

function renderPlaylist() {
    playlistElement.innerHTML = "";

    tracks.forEach((track, index) => {
        const li = document.createElement("li");
        li.textContent = `${index + 1}. ${track.title || track.name}`;

        if (index === currentIndex) {
            li.classList.add("active");
        }


        li.addEventListener("click", () => loadTrack(index));

        playlistElement.appendChild(li);
    });
}

function loadTrack(index) {
    if (index < 0 || index >= tracks.length) return;

    currentIndex = index;

    const track = tracks[index];

    audio.src = track.url;

    titleElement.textContent =
        track.title || track.name || "Sem título";

    filenameElement.textContent =
        track.name || track.url || "-";

    document.getElementById("size").textContent =
        track.size ? formatSize(track.size) : "-";

    document.getElementById("format").textContent =
        track.format || (track.source ? track.source.toUpperCase() : "-");

    updateMediaSession();
    notifyEsp32();

    audio.play().then(() => {
        playButton.textContent = "⏸";
    }).catch(error => {
        if (error && error.name !== "AbortError" && error.name !== "NotAllowedError") {
            console.error("Erro na reprodução:", error);
        }
    });

    renderPlaylist();
}

playButton.addEventListener("click", () => {
    if (!audio.src) return;

    if (audio.paused) {
        audio.play();
    } else {
        audio.pause();
    }
});

audio.addEventListener("play", () => {
    playButton.textContent = "⏸";
});

audio.addEventListener("pause", () => {
    playButton.textContent = "▶";
});

audio.addEventListener("loadedmetadata", () => {
    document.getElementById("duration").textContent =
        formatTime(audio.duration);

    document.getElementById("durationInfo").textContent =
        formatTime(audio.duration);

    progress.max = audio.duration || 0;

    updateMediaSession();
    notifyEsp32();
});

audio.addEventListener("timeupdate", () => {
    progress.value = audio.currentTime;

    document.getElementById("currentTime").textContent =
        formatTime(audio.currentTime);

    updatePosition();
});

progress.addEventListener("input", () => {
    audio.currentTime = Number(progress.value);
});

document.getElementById("volume").addEventListener("input", event => {
    audio.volume = Number(event.target.value);
});

document.getElementById("next").addEventListener("click", () => {
    if (tracks.length) {
        loadTrack((currentIndex + 1) % tracks.length);
    }
});

document.getElementById("previous").addEventListener("click", () => {
    if (tracks.length) {
        loadTrack((currentIndex - 1 + tracks.length) % tracks.length);
    }
});

audio.addEventListener("ended", () => {
    if (tracks.length) {
        loadTrack((currentIndex + 1) % tracks.length);
    }
});

// Media Session API: publica metadados ao sistema operacional.

function updateMediaSession() {
    if (!("mediaSession" in navigator) || currentIndex < 0) return;

    const track = tracks[currentIndex];

    navigator.mediaSession.metadata = new MediaMetadata({
        title: track.title || track.name || "Sem título",
        artist: track.artist || "Desconhecido",
        album: track.album || "Minha playlist",
        artwork: track.thumbnail ? [{ src: track.thumbnail }] : []
    });


    navigator.mediaSession.setActionHandler("play", () => audio.play());
    navigator.mediaSession.setActionHandler("pause", () => audio.pause());

    navigator.mediaSession.setActionHandler("nexttrack", () => {
        if (tracks.length) loadTrack((currentIndex + 1) % tracks.length);
    });

    navigator.mediaSession.setActionHandler("previoustrack", () => {
        if (tracks.length) loadTrack((currentIndex - 1 + tracks.length) % tracks.length);
    });
}

function updatePosition() {
    if (!("mediaSession" in navigator)) return;
    if (!Number.isFinite(audio.duration) || audio.duration <= 0) return;

    navigator.mediaSession.setPositionState({
        duration: audio.duration,
        playbackRate: audio.playbackRate,
        position: audio.currentTime
    });
}

function notifyEsp32() {
    if (currentIndex < 0) return;
    const track = tracks[currentIndex];
    fetch("/api/notify", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
            title: track.title || track.name || "Sem título",
            artist: track.artist || "Desconhecido",
            duration: track.duration || audio.duration || 0
        })
    }).catch(() => {});
}

const linkInput = document.getElementById("linkInput");
const addLinkButton = document.getElementById("addLink");
const linkStatus = document.getElementById("linkStatus");

if (addLinkButton) {
    addLinkButton.addEventListener("click", async () => {
        const url = (linkInput.value || "").trim();
        if (!url) {
            linkStatus.textContent = "Informe um link.";
            return;
        }

        linkStatus.textContent = "Buscando dados...";
        addLinkButton.disabled = true;

        try {
            const response = await fetch("/api/link", {
                method: "POST",
                headers: { "Content-Type": "application/json" },
                body: JSON.stringify({ url })
            });

            const data = await response.json();
            if (!response.ok) {
                throw new Error(data.error || "Falha ao buscar link.");
            }

            const saveResponse = await fetch("/api/tracks", {
                method: "POST",
                headers: { "Content-Type": "application/json" },
                body: JSON.stringify({
                    source: data.source || "link",
                    title: data.title || "Sem título",
                    artist: data.artist || "Desconhecido",
                    album: data.album || "",
                    duration: data.duration || null,
                    thumbnail: data.thumbnail || null,
                    name: data.title || url,
                    format: data.source ? data.source.toUpperCase() : "LINK",
                    size: null,
                    url: data.stream_url || data.webpage_url || url
                })
            });

            const saved = await saveResponse.json();
            if (!saveResponse.ok) {
                throw new Error(saved.error || "Falha ao salvar link.");
            }

            tracks.push(saved);

            renderPlaylist();

            if (currentIndex === -1 && tracks.length) {
                loadTrack(tracks.length - 1);
            }

            linkStatus.textContent = "Adicionado.";
            linkInput.value = "";
        } catch (error) {
            linkStatus.textContent = error.message || "Erro ao adicionar link.";
            console.error(error);
        } finally {
            addLinkButton.disabled = false;
        }
    });
}

loadTracksFromDb();
