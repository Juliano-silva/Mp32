from flask import Flask, render_template

from static.python.api import MusicAPI
from static.python.db import DB


app = Flask(
    __name__,
    template_folder="template",
    static_folder="static",
)

app.register_blueprint(DB)
app.register_blueprint(MusicAPI)


@app.route("/")
def index():
    return render_template("index.html")


if __name__ == "__main__":
    app.run(
        host="0.0.0.0",
        port=5000,
        debug=True,
    )
