from app import app

if __name__ == "__main__":
    import os

    app.run(
        host=os.environ.get("JF_DASHBOARD_HOST", "127.0.0.1"),
        port=int(os.environ.get("JF_DASHBOARD_PORT", "5000")),
        debug=os.environ.get("FLASK_DEBUG") == "1",
    )
