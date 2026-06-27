from app.settings import Settings


def test_settings_ignores_removed_env_fields(tmp_path, monkeypatch):
    env_path = tmp_path / ".env"
    env_path.write_text(
        "\n".join(
            [
                "SERVER_PORT=8080",
                "MAX_PAGES=20",
                "MAX_UPLOAD_BYTES=6000000",
            ]
        ),
        encoding="utf-8",
    )
    monkeypatch.chdir(tmp_path)

    settings = Settings()

    assert settings.server_port == 8080
    assert settings.max_pages == 20
