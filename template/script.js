
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

filesInput.addEventListener("change", (event) => {
    const files = Array.from(event.target.files);

    files.forEach(file => {
        if (!file.type.startsWith("audio/")) return;

        tracks.push({
            file,
            url: URL.createObjectURL(file)
        });
    });

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

        li.textContent = `${index + 1}. ${track.file.name}`;

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
    const file = track.file;

    audio.src = track.url;

    titleElement.textContent =
        file.name.replace(/\.[^/.]+$/, "");

    filenameElement.textContent = file.name;

    document.getElementById("size").textContent =
        formatSize(file.size);

    document.getElementById("format").textContent =
        file.name.split(".").pop().toUpperCase();

    audio.play().then(() => {
        playButton.textContent = "⏸";
    }).catch(error => {
        console.error("Erro na reprodução:", error);
    });

    renderPlaylist();
    updateMediaSession();
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

    const file = tracks[currentIndex].file;

    navigator.mediaSession.metadata = new MediaMetadata({
        title: file.name.replace(/\.[^/.]+$/, ""),
        artist: "Arquivo local",
        album: "Minha playlist"
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
