"""LLM integration for generating bash commands."""

import os
import platform
from dataclasses import dataclass

import anthropic

from .config import Config


SYSTEM_PROMPT = """\
You are a command-line assistant that converts natural language requests into bash commands.

Your task is to generate a single bash command (or a pipeline of commands) that accomplishes the user's request.

Guidelines:
- Generate ONLY the command, no explanations or markdown formatting
- Use common Unix utilities that are typically available on most systems
- Prefer simple, readable commands over complex one-liners when possible
- If the request is ambiguous, generate the most likely intended command
- For dangerous operations (rm -rf, dd, etc.), include appropriate safeguards
- Use proper quoting to handle filenames with spaces
- If you absolutely cannot generate a command for the request, respond with: ERROR: <reason>

Environment information:
- Operating System: {os_info}
- Shell: {shell}
- Current Directory: {cwd}
"""


@dataclass
class CommandResult:
    """Result of command generation."""

    command: str | None
    error: str | None = None
    thinking: str | None = None

    @property
    def is_error(self) -> bool:
        return self.error is not None or (
            self.command is not None and self.command.startswith("ERROR:")
        )


class CommandGenerator:
    """Generates bash commands from natural language using Claude."""

    def __init__(self, config: Config):
        self.config = config
        self.client = anthropic.Anthropic(api_key=config.api_key)
        self.conversation_history: list[dict] = []

    def _get_system_prompt(self) -> str:
        """Build system prompt with environment context."""
        return SYSTEM_PROMPT.format(
            os_info=f"{platform.system()} {platform.release()}",
            shell=self.config.shell,
            cwd=self.config.working_dir,
        )

    def generate(self, prompt: str) -> CommandResult:
        """Generate a bash command from a natural language prompt."""
        self.conversation_history.append({"role": "user", "content": prompt})

        try:
            response = self.client.messages.create(
                model=self.config.model,
                max_tokens=1024,
                system=self._get_system_prompt(),
                messages=self.conversation_history,
            )

            command = response.content[0].text.strip()

            # Check if LLM returned an error
            if command.startswith("ERROR:"):
                error_msg = command[6:].strip()
                self.conversation_history.pop()  # Remove failed request
                return CommandResult(command=None, error=error_msg)

            # Add assistant response to history for context
            self.conversation_history.append({"role": "assistant", "content": command})

            return CommandResult(command=command)

        except anthropic.APIError as e:
            self.conversation_history.pop()  # Remove failed request
            return CommandResult(command=None, error=f"API error: {e}")
        except Exception as e:
            self.conversation_history.pop()
            return CommandResult(command=None, error=f"Unexpected error: {e}")

    def add_execution_result(self, command: str, output: str, success: bool):
        """Add command execution result to conversation history for context."""
        result_msg = f"Command executed: {command}\n"
        result_msg += f"Status: {'success' if success else 'failed'}\n"
        if output:
            # Truncate very long outputs
            if len(output) > 2000:
                output = output[:2000] + "\n... (truncated)"
            result_msg += f"Output:\n{output}"

        self.conversation_history.append({"role": "user", "content": result_msg})

    def clear_history(self):
        """Clear conversation history."""
        self.conversation_history = []
