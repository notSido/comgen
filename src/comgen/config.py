"""Configuration management for comgen."""

import os
from dataclasses import dataclass, field
from pathlib import Path


@dataclass
class Config:
    """Configuration settings for comgen."""

    # API settings
    api_key: str = field(default_factory=lambda: os.environ.get("ANTHROPIC_API_KEY", ""))
    model: str = "claude-sonnet-4-20250514"

    # Behavior settings
    auto_execute: bool = False  # If True, execute commands without confirmation
    show_thinking: bool = False  # If True, show LLM reasoning
    history_file: Path = field(
        default_factory=lambda: Path.home() / ".comgen_history"
    )
    max_history: int = 1000

    # Shell settings
    shell: str = field(default_factory=lambda: os.environ.get("SHELL", "/bin/bash"))
    working_dir: Path = field(default_factory=Path.cwd)

    def validate(self) -> list[str]:
        """Validate configuration and return list of errors."""
        errors = []
        if not self.api_key:
            errors.append(
                "ANTHROPIC_API_KEY environment variable is not set. "
                "Please set it to your Anthropic API key."
            )
        return errors


def load_config() -> Config:
    """Load configuration from environment and defaults."""
    return Config()
