from functools import lru_cache

from pydantic import Field
from pydantic_settings import BaseSettings, SettingsConfigDict


class Settings(BaseSettings):
    dashscope_api_key: str = Field(default="", alias="DASHSCOPE_API_KEY")
    qwen_model: str = Field(default="qwen3.7-plus", alias="QWEN_MODEL")
    api_token: str = Field(default="", alias="FJKER_API_TOKEN")
    server_host: str = Field(default="0.0.0.0", alias="SERVER_HOST")
    server_port: int = Field(default=8080, alias="SERVER_PORT")
    max_pages: int = Field(default=20, alias="MAX_PAGES")

    model_config = SettingsConfigDict(
        env_file=".env",
        env_file_encoding="utf-8",
        extra="ignore",
    )


@lru_cache
def get_settings() -> Settings:
    return Settings()
